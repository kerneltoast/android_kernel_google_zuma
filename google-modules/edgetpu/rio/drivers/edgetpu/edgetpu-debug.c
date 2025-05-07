// SPDX-License-Identifier: GPL-2.0
/*
 * Implements methods common to the family of EdgeTPUs for mobile devices to retrieve host side
 * debug dump segments and report them to SSCD.
 *
 * Copyright (C) 2020-2022, 2024 Google LLC
 */

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/platform_data/sscoredump.h>
#include <linux/platform_device.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <gcip/gcip-alloc-helper.h>

#include "edgetpu-config.h"
#include "edgetpu-debug.h"
#include "edgetpu-device-group.h"
#include "edgetpu-dump-info.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-wakelock.h"

#define EXTERNAL_DEBUG_NS_INVASIVE_MASK (BIT(2) - 1)
#define EXTERNAL_DEBUG_NS_INVASIVE_ENABLE (BIT(0) | BIT(1))

#define EXTERNAL_DEBUG_LOCK_KEY 0
#define EXTERNAL_DEBUG_UNLOCK_KEY 0xC5ACCE55
#define EXTERNAL_DEBUG_LOCK_SLK BIT(1)

#define EXTERNAL_DEBUG_OS_LOCK_KEY 1
#define EXTERNAL_DEBUG_OS_UNLOCK_KEY 0
#define EXTERNAL_DEBUG_OS_LOCK_UP BIT(0)
#define EXTERNAL_DEBUG_OS_LOCK_R BIT(2)
#define EXTERNAL_DEBUG_OS_LOCK_OSLK BIT(5)
#define EXTERNAL_DEBUG_OS_LOCK_DLK BIT(6)

#if EDGETPU_HAS_FW_DEBUG
/* Handle FW response data available. */
void edgetpu_fw_debug_resp_ready(struct edgetpu_dev *etdev, u32 data_len)
{
	if (data_len > FW_DEBUG_BUFFER_SIZE)
		data_len = FW_DEBUG_BUFFER_SIZE;
	etdev->fw_debug_mem.data_len = data_len;
	dma_sync_sgtable_for_cpu(etdev->dev, etdev->fw_debug_mem.sgt, DMA_BIDIRECTIONAL);
	etdev->fw_debug_mem.async_resp_pending = false;
	etdev->fw_debug_mem.resp_data_ready = true;
	complete_all(&etdev->fw_debug_mem.rd_data_ready);
}

/* Read response data from firmware debug service. */
static ssize_t fw_debug_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct edgetpu_dev *etdev = file->private_data;

	if (*ppos > etdev->fw_debug_mem.data_len)
		return 0;
	/* If no response data ready to be consumed wait for notification of KCI/RKCI response. */
	if (wait_for_completion_interruptible(&etdev->fw_debug_mem.rd_data_ready))
		return -EINTR;
	if (etdev->fw_debug_mem.data_len - *ppos < count)
		count = etdev->fw_debug_mem.data_len - *ppos;
	if (copy_to_user(buf, etdev->fw_debug_mem.vaddr + *ppos, count))
		return -EFAULT;
	*ppos += count;
	if (*ppos >= etdev->fw_debug_mem.data_len) {
		etdev->fw_debug_mem.data_len = 0;
		reinit_completion(&etdev->fw_debug_mem.rd_data_ready);
		etdev->fw_debug_mem.resp_data_ready = false;
	}
	return count;
}

/* Write/append command data to firmware debug service buffer. */
static ssize_t fw_debug_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct edgetpu_dev *etdev = file->private_data;
	int ret;

	/* If waiting for RKCI send reset w/ FW handshake.  Races handled by FW. */
	if (etdev->fw_debug_mem.async_resp_pending) {
		ret = edgetpu_kci_fw_debug_reset(etdev);
		if (ret)
			etdev_warn_ratelimited(etdev, "fw debug reset error %d", ret);
		etdev->fw_debug_mem.async_resp_pending = false;
	}

	/* If unread response data waiting, discard it. */
	if (etdev->fw_debug_mem.resp_data_ready) {
		etdev->fw_debug_mem.data_len = 0;
		reinit_completion(&etdev->fw_debug_mem.rd_data_ready);
		etdev->fw_debug_mem.resp_data_ready = false;
	}

	if (etdev->fw_debug_mem.data_len + count > FW_DEBUG_BUFFER_SIZE)
		count = FW_DEBUG_BUFFER_SIZE - etdev->fw_debug_mem.data_len;
	if (count) {
		if (copy_from_user(etdev->fw_debug_mem.vaddr + etdev->fw_debug_mem.data_len, buf,
				   count))
			return -EFAULT;
		etdev->fw_debug_mem.data_len += count;
	}
	return count;
}

/* Send command buffer to firmware if closing writeable fd. */
static void fw_debug_release_flush(struct edgetpu_dev *etdev, struct file *file)
{
	size_t data_len = etdev->fw_debug_mem.data_len;
	int ret;

	if (!(file->f_mode & FMODE_WRITE))
		return;
	if (!etdev->fw_debug_mem.data_len)
		return;

	dma_sync_sgtable_for_device(etdev->dev, etdev->fw_debug_mem.sgt, DMA_BIDIRECTIONAL);
	etdev->fw_debug_mem.data_len = 0;
	ret = edgetpu_kci_fw_debug_cmd(etdev, FW_DEBUG_BUFFER_IOVA, data_len);
	if (ret == GCIP_KCI_ERROR_UNAVAILABLE)
		etdev->fw_debug_mem.async_resp_pending = true;
	else if (ret != GCIP_KCI_ERROR_OK)
		etdev_warn_ratelimited(etdev, "fw debug command error %d", ret);
}

static int fw_debug_release(struct inode *inode, struct file *file)
{
	struct edgetpu_dev *etdev = file->private_data;

	fw_debug_release_flush(etdev, file);
	edgetpu_pm_put(etdev);
	return 0;
}

/* Open firmware debug service debugfs interface. */
static int fw_debug_open(struct inode *inode, struct file *file)
{
	struct edgetpu_dev *etdev = inode->i_private;
	int ret;

	file->private_data = etdev;

	ret = edgetpu_pm_get(etdev);
	if (ret) {
		etdev_err_ratelimited(etdev, "fw debug error powering TPU: %d", ret);
		return ret;
	}

	/* Allocate command/response buffer and map to TPU if not already. */
	if (etdev->fw_debug_mem.sgt)
		return 0;

	etdev->fw_debug_mem.sgt =
		gcip_alloc_noncontiguous(etdev->dev, FW_DEBUG_BUFFER_SIZE, GFP_KERNEL);
	if (!etdev->fw_debug_mem.sgt) {
		edgetpu_pm_put(etdev);
		return -ENOMEM;
	}
	etdev->fw_debug_mem.vaddr = gcip_noncontiguous_sgt_to_mem(etdev->fw_debug_mem.sgt);
	ret = edgetpu_mmu_map_iova_sgt(etdev, FW_DEBUG_BUFFER_IOVA, etdev->fw_debug_mem.sgt,
				       DMA_BIDIRECTIONAL, 0, edgetpu_mmu_default_domain(etdev));
	if (ret) {
		gcip_free_noncontiguous(etdev->fw_debug_mem.sgt);
		etdev->fw_debug_mem.sgt = NULL;
		edgetpu_pm_put(etdev);
		return ret;
	}

	return 0;
}

static const struct file_operations fops_fw_debug = {
	.open = fw_debug_open,
	.read = fw_debug_read,
	.write = fw_debug_write,
	.owner = THIS_MODULE,
	.release = fw_debug_release,
};

/* Init firmware debug interface. */
static void edgetpu_fw_debug_init(struct edgetpu_dev *etdev)
{
	debugfs_create_file("fw_debug", 0660, etdev->d_entry, etdev, &fops_fw_debug);
	init_completion(&etdev->fw_debug_mem.rd_data_ready);
}

/* De-init firmware debug interface. */
static void edgetpu_fw_debug_exit(struct edgetpu_dev *etdev)
{
	/* All debugfs files are deleted by other code, not necessary to remove here. */

	if (!etdev->fw_debug_mem.sgt)
		return;

	edgetpu_mmu_unmap_iova_sgt(etdev, FW_DEBUG_BUFFER_IOVA, etdev->fw_debug_mem.sgt,
				   DMA_BIDIRECTIONAL, edgetpu_mmu_default_domain(etdev));
	gcip_free_noncontiguous(etdev->fw_debug_mem.sgt);
}

#else /* EDGETPU_HAS_FW_DEBUG */
static void edgetpu_fw_debug_init(struct edgetpu_dev *etdev)
{
}

static void edgetpu_fw_debug_exit(struct edgetpu_dev *etdev)
{
}
#endif /* EDGETPU_HAS_FW_DEBUG */

void edgetpu_debug_dump_cpu_regs(struct edgetpu_dev *etdev)
{
	u32 val;

	/* Acquires the PM count to ensure the TPU block and control cluster are powered. */
	if (edgetpu_pm_get_if_powered(etdev, false)) {
		dev_info(etdev->dev, "Device off. Skip CPU registers dump.");
		return;
	}

	/* Non-secure invasive debug is disabled on fused devices. */
	val = edgetpu_dev_read_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_AUTHSTATUS);
	if ((val & EXTERNAL_DEBUG_NS_INVASIVE_MASK) != EXTERNAL_DEBUG_NS_INVASIVE_ENABLE) {
		dev_info(etdev->dev, "Fused device. Skip CPU registers dump.");
		goto err_pm_put;
	}

	/* Unlocks external debug lock. */
	edgetpu_dev_write_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_LOCK_ACCESS,
				  EXTERNAL_DEBUG_UNLOCK_KEY);
	val = edgetpu_dev_read_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_LOCK_STATUS);
	if (val & EXTERNAL_DEBUG_LOCK_SLK) {
		dev_err(etdev->dev, "Fail to unlock external debug lock.");
		goto err_pm_put;
	}

	/*
	 * Checks if:
	 *   1. external debug processor is in a low-power or powerdown state where the debug
	 *      registers cannot be accessed.
	 *   2. OS is double locked.
	 *   3. external debug processor is in reset state.
	 */
	val = edgetpu_dev_read_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_PROCESSOR_STATUS);
	if (!(val & EXTERNAL_DEBUG_OS_LOCK_UP) || (val & EXTERNAL_DEBUG_OS_LOCK_DLK) ||
	    (val & EXTERNAL_DEBUG_OS_LOCK_R)) {
		dev_err(etdev->dev, "External debug OS lock status unknown. Processor status: %#x",
			val);
		goto err_external_debug_lock;
	}

	/* Unlocks OS lock. */
	edgetpu_dev_write_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_OS_LOCK_ACCESS,
				  EXTERNAL_DEBUG_OS_UNLOCK_KEY);
	val = edgetpu_dev_read_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_PROCESSOR_STATUS);
	if (val & EXTERNAL_DEBUG_OS_LOCK_OSLK) {
		dev_err(etdev->dev, "Fail to unlock external debug OS lock.");
		goto err_external_debug_lock;
	}

	/* Reads external debug registers. */
	val = edgetpu_dev_read_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_PROGRAM_COUNTER);
	dev_info(etdev->dev, "External debug program counter: %#x", val);

	/* Locks OS lock. */
	edgetpu_dev_write_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_OS_LOCK_ACCESS,
				  EXTERNAL_DEBUG_OS_LOCK_KEY);
err_external_debug_lock:
	/* Locks external debug lock. */
	edgetpu_dev_write_32_sync(etdev, EDGETPU_REG_EXTERNAL_DEBUG_LOCK_ACCESS,
				  EXTERNAL_DEBUG_LOCK_KEY);
err_pm_put:
	edgetpu_pm_put(etdev);
}

#if IS_ENABLED(CONFIG_SUBSYSTEM_COREDUMP) || IS_ENABLED(CONFIG_EDGETPU_TEST)

/*
 * The minimum wait time in millisecond to be enforced between two successive calls to the SSCD
 * module to prevent the overwrite of the previous generated core dump files. SSCD module generates
 * the files whose name are at second precision i.e.
 * crashinfo_<SUBSYSTEM_NAME>_<%Y-%m-%d_%H-%M-%S>.txt and
 * coredump_<SUBSYSTEM_NAME>_<%Y-%m-%d_%H-%M-%S>.bin.
 */
#define SSCD_REPORT_WAIT_TIME (1000ULL)

#define SET_FIELD(info, obj, __field) ((info)->__field = (obj)->__field)

static int edgetpu_get_debug_dump_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;
	int ret = edgetpu_pm_get(etdev);

	if (ret)
		return ret;
	edgetpu_debug_dump(etdev, DUMP_REASON_REQ_BY_USER);
	edgetpu_pm_put(etdev);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_get_debug_dump, NULL, edgetpu_get_debug_dump_set, "%llu\n");

/*
 * Creates debugFS entries for interacting with debug dump functions.
 *
 * This is expected to be called by edgetpu_debug_dump_init().
 */
static inline void edgetpu_setup_debug_dump_fs(struct edgetpu_dev *etdev)
{
	/* forwards write requests to edgetpu_debug_dump() */
	debugfs_create_file("get_debug_dump", 0220, etdev->d_entry, etdev, &fops_get_debug_dump);
}

/* Helper structure to hold the segments to be reported to SSCD. */
struct sscd_segments_context {
	size_t n_segs; /* current number of recorded segments */
	size_t capacity; /* number of segments allocated */
	struct sscd_segment *segs;
	/*
	 * Array with the same length as @segs, indicates whether segs[i].addr should be freed on
	 * context releasing.
	 */
	bool *free_on_release;
	struct mobile_sscd_info *sscd_info;
};

static int sscd_ctx_init(struct sscd_segments_context *ctx, struct mobile_sscd_info *sscd_info)
{
	struct sscd_platform_data *pdata = sscd_info->pdata;

	if (!pdata->sscd_report)
		return -ENOENT;
	ctx->n_segs = 0;
	ctx->capacity = 0;
	ctx->segs = NULL;
	ctx->free_on_release = NULL;
	ctx->sscd_info = sscd_info;
	return 0;
}

static void sscd_ctx_release(struct sscd_segments_context *ctx)
{
	int i;

	for (i = 0; i < ctx->n_segs; i++)
		if (ctx->free_on_release[i])
			kfree(ctx->segs[i].addr);
	kfree(ctx->segs);
	kfree(ctx->free_on_release);
}

/*
 * Pushes the segment.
 *
 * If @free_on_release is true, kfree(@seg->addr) is called when releasing @ctx.
 *
 * Returns 0 on success.
 */
static int sscd_ctx_push_segment(struct sscd_segments_context *ctx, struct sscd_segment *seg,
				 bool free_on_release)
{
	void *ptr1, *ptr2;
	size_t new_cap;

	if (ctx->n_segs >= ctx->capacity) {
		new_cap = ctx->capacity << 1;
		if (!new_cap)
			new_cap = 1;
		ptr1 = krealloc(ctx->segs, new_cap * sizeof(*ctx->segs), GFP_KERNEL);
		if (!ptr1)
			return -ENOMEM;
		ptr2 = krealloc(ctx->free_on_release, new_cap * sizeof(*ctx->free_on_release),
				GFP_KERNEL);
		if (!ptr2) {
			kfree(ptr1);
			return -ENOMEM;
		}
		ctx->segs = ptr1;
		ctx->free_on_release = ptr2;
		ctx->capacity = new_cap;
	}

	ctx->segs[ctx->n_segs] = *seg;
	ctx->free_on_release[ctx->n_segs] = free_on_release;
	ctx->n_segs++;
	return 0;
}

/*
 * Passes dump data to SSCD daemon and releases @ctx.
 *
 * Returns what sscd_report returned. Note that @ctx is always released no matter what is returned.
 */
static int sscd_ctx_report_and_release(struct sscd_segments_context *ctx, const char *crash_info)
{
	struct sscd_platform_data *pdata = ctx->sscd_info->pdata;
	struct platform_device *sscd_dev = ctx->sscd_info->dev;
	static ktime_t prev_sscd_report_time;
	uint64_t diff_ms;
	int ret;

	diff_ms = ktime_to_ms(ktime_sub(ktime_get(), prev_sscd_report_time));
	if (diff_ms < SSCD_REPORT_WAIT_TIME)
		msleep(SSCD_REPORT_WAIT_TIME - diff_ms);

	ret = pdata->sscd_report(sscd_dev, ctx->segs, ctx->n_segs, SSCD_FLAGS_ELFARM64HDR,
				 crash_info);

	prev_sscd_report_time = ktime_get();
	sscd_ctx_release(ctx);
	return ret;
}

static void sscd_release(struct device *dev)
{
	pr_debug(DRIVER_NAME " release\n");
}

static int mobile_sscd_collect_etdev_info(struct edgetpu_dev *etdev,
					  struct sscd_segments_context *ctx)
{
	struct edgetpu_dump_segment *seg_hdr;
	struct edgetpu_dev_info *info;
	const size_t seg_size = sizeof(*seg_hdr) + sizeof(*info);
	void *buffer;
	struct sscd_segment seg = {
		.size = seg_size,
	};

	buffer = kzalloc(seg_size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	seg.addr = buffer;
	seg_hdr = buffer;
	seg_hdr->type = BIT_ULL(DUMP_TYPE_KERNEL_ETDEV_BIT);
	seg_hdr->size = seg_size - sizeof(*seg_hdr);
	info = (typeof(info))(seg_hdr + 1);
	SET_FIELD(info, etdev, state);
	SET_FIELD(info, etdev, vcid_pool);
	info->job_count = atomic_read(&etdev->job_count);
	SET_FIELD(info, etdev, firmware_crash_count);
	SET_FIELD(info, etdev, watchdog_timeout_count);
	return sscd_ctx_push_segment(ctx, &seg, true);
}

static int mobile_collect_device_info(struct edgetpu_dev *etdev, struct sscd_segments_context *ctx)
{
	return mobile_sscd_collect_etdev_info(etdev, ctx);
}

/* Generates general dump, including telemetry logs and device info. */
static int mobile_sscd_generate_dump(struct edgetpu_dev *etdev)
{
	struct edgetpu_mobile_platform_dev *pdev;
	struct sscd_segments_context sscd_ctx;
	static const char crash_info[] = "[edgetpu dump]";
	int i, ret;

	pdev = to_mobile_dev(etdev);
	ret = sscd_ctx_init(&sscd_ctx, &pdev->sscd_info);
	if (ret)
		goto err;

	/* Populate sscd segments */
	for (i = 0; i < etdev->num_cores; i++) {
		struct edgetpu_coherent_mem *log_mem = &etdev->telemetry[i].log_mem;
		struct sscd_segment seg = {
			.addr = log_mem->vaddr,
			.size = log_mem->size,
		};

		ret = sscd_ctx_push_segment(&sscd_ctx, &seg, false);
		if (ret)
			goto err_release;
	}

	ret = mobile_collect_device_info(etdev, &sscd_ctx);
	if (ret)
		goto err_release;

	ret = sscd_ctx_report_and_release(&sscd_ctx, crash_info);
	if (ret)
		goto err;

	return 0;

err_release:
	sscd_ctx_release(&sscd_ctx);
err:
	etdev_warn(etdev, "failed to generate dump: %d", ret);
	return ret;
}

void edgetpu_debug_dump(struct edgetpu_dev *etdev, u64 dump_reason)
{
	int ret;

	ret = mobile_sscd_generate_dump(etdev);
	if (ret)
		etdev_warn(etdev, "Failed to generate debug dump: %d\n", ret);
}

static int edgetpu_debug_dump_init(struct edgetpu_dev *etdev)
{
	struct edgetpu_mobile_platform_dev *pdev = to_mobile_dev(etdev);
	struct platform_device *sscd_dev;
	struct sscd_platform_data *sscd_pdata;
	int ret;

	sscd_pdata = devm_kzalloc(etdev->dev, sizeof(*sscd_pdata), GFP_KERNEL);
	if (!sscd_pdata)
		return -ENOMEM;

	sscd_dev = devm_kzalloc(etdev->dev, sizeof(*sscd_dev), GFP_KERNEL);
	if (!sscd_dev) {
		ret = -ENOMEM;
		goto out_free_pdata;
	}

	*sscd_dev = (struct platform_device) {
		.name = DRIVER_NAME,
		.driver_override = SSCD_NAME,
		.id = PLATFORM_DEVID_NONE,
		.dev = {
			.platform_data = sscd_pdata,
			.release = sscd_release,
		},
	};
	/* Register SSCD platform device */
	ret = platform_device_register(sscd_dev);
	if (ret) {
		etdev_err(etdev, "SSCD platform device registration failed: %d", ret);
		goto out_free_sscd_dev;
	}
	pdev->sscd_info.pdata = sscd_pdata;
	pdev->sscd_info.dev = sscd_dev;
	edgetpu_setup_debug_dump_fs(etdev);
	return ret;

out_free_sscd_dev:
	devm_kfree(etdev->dev, sscd_dev);
out_free_pdata:
	devm_kfree(etdev->dev, sscd_pdata);
	return ret;
}

static void edgetpu_debug_dump_exit(struct edgetpu_dev *etdev)
{
	platform_device_unregister(to_mobile_dev(etdev)->sscd_info.dev);
}

#else /* IS_ENABLED(CONFIG_SUBSYSTEM_COREDUMP) || IS_ENABLED(CONFIG_EDGETPU_TEST) */

static int edgetpu_debug_dump_init(struct edgetpu_dev *etdev)
{
	return 0;
}

static void edgetpu_debug_dump_exit(struct edgetpu_dev *etdev)
{
}

void edgetpu_debug_dump(struct edgetpu_dev *etdev, u64 dump_reason)
{
}

#endif /* IS_ENABLED(CONFIG_SUBSYSTEM_COREDUMP) || IS_ENABLED(CONFIG_EDGETPU_TEST) */

void edgetpu_debug_init(struct edgetpu_dev *etdev)
{
	int ret;

	ret = edgetpu_debug_dump_init(etdev);
	if (ret)
		etdev_warn(etdev, "debug dump init fail: %d", ret);
	edgetpu_fw_debug_init(etdev);
}

void edgetpu_debug_exit(struct edgetpu_dev *etdev)
{
	edgetpu_debug_dump_exit(etdev);
	edgetpu_fw_debug_exit(etdev);
}
