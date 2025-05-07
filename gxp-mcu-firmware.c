// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP MicroController Unit firmware management.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <gcip/gcip-alloc-helper.h>
#include <gcip/gcip-common-image-header.h>
#include <gcip/gcip-fault-injection.h>
#include <gcip/gcip-image-config.h>
#include <gcip/gcip-iommu.h>
#include <gcip/gcip-pm.h>
#include <gcip/gcip-thermal.h>

#include "gxp-config.h"
#include "gxp-core-telemetry.h"
#include "gxp-debug-dump.h"
#include "gxp-doorbell.h"
#include "gxp-firmware-loader.h"
#include "gxp-gsa.h"
#include "gxp-internal.h"
#include "gxp-kci.h"
#include "gxp-lpm.h"
#include "gxp-mailbox-driver.h"
#include "gxp-mcu-firmware.h"
#include "gxp-mcu-platform.h"
#include "gxp-mcu.h"
#include "gxp-monitor.h"
#include "gxp-pm.h"
#include "gxp-uci.h"
#include "mobile-soc.h"

#if IS_GXP_TEST
#define TEST_FLUSH_KCI_WORKERS(kci)                                   \
	do {                                                          \
		kthread_flush_worker(&(kci).mbx->response_worker);    \
		flush_work(&(kci).mbx->mbx_impl.gcip_kci->work);      \
		flush_work(&(kci).mbx->mbx_impl.gcip_kci->rkci.work); \
	} while (0)
#else
#define TEST_FLUSH_KCI_WORKERS(...)
#endif

/* Value of Magic field in the common header "DSPF' as a 32-bit LE int */
#define GXP_FW_MAGIC 0x46505344

/* The number of times trying to rescue MCU. */
#define MCU_RESCUE_TRY 3

/*
 * Programs instruction remap CSRs.
 */
static int program_iremap_csr(struct gxp_dev *gxp, struct gxp_mapped_resource *buf)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	size_t size;

	dev_info(gxp->dev, "Program instruction remap CSRs");
	gxp_soc_set_iremap_context(gxp);

	if (mcu_fw->dynamic_fw_buffer) {
		if (buf->daddr + buf->size > GXP_IREMAP_DYNAMIC_CODE_BASE) {
			dev_err(gxp->dev,
				"Bad dynamic firmware base %x, carveout daddr: %pad size: %llx",
				GXP_IREMAP_DYNAMIC_CODE_BASE, &buf->daddr, buf->size);
			return -EINVAL;
		}
		gxp_write_32(gxp, GXP_REG_CFGVECTABLE0, GXP_IREMAP_DYNAMIC_CODE_BASE);
		size = mcu_fw->dynamic_fw_buffer->size;
		gxp_write_32(gxp, GXP_REG_IREMAP_LOW, buf->daddr);
		gxp_write_32(gxp, GXP_REG_IREMAP_HIGH, GXP_IREMAP_DYNAMIC_CODE_BASE + size);
	} else {
		gxp_write_32(gxp, GXP_REG_CFGVECTABLE0, buf->daddr);
		gxp_write_32(gxp, GXP_REG_IREMAP_LOW, buf->daddr);
		gxp_write_32(gxp, GXP_REG_IREMAP_HIGH, buf->daddr + buf->size);
	}
	gxp_write_32(gxp, GXP_REG_IREMAP_TARGET, buf->daddr);
	gxp_write_32(gxp, GXP_REG_IREMAP_ENABLE, 1);
	return 0;
}

/*
 * Check whether the firmware file is signed or not.
 */
static bool is_signed_firmware(const struct firmware *fw,
			       const struct gcip_common_image_header *hdr)
{
	if (fw->size < GCIP_FW_HEADER_SIZE)
		return false;

	if (hdr->common.magic != GXP_FW_MAGIC)
		return false;

	return true;
}

static int gxp_mcu_firmware_handshake(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;
	struct gxp_mcu *mcu = container_of(mcu_fw, struct gxp_mcu, fw);
	enum gcip_fw_flavor fw_flavor;
	int ret;

	dev_dbg(gxp->dev, "Detecting MCU firmware info...");
	mcu_fw->fw_info.fw_build_time = 0;
	mcu_fw->fw_info.fw_flavor = GCIP_FW_FLAVOR_UNKNOWN;
	mcu_fw->fw_info.fw_changelist = 0;
	ret = gxp_mailbox_wait_for_device_mailbox_init(mcu->kci.mbx);
	if (!ret) {
		fw_flavor = gxp_kci_fw_info(&mcu->kci, &mcu_fw->fw_info);
	} else {
		fw_flavor = ret;
		dev_err(gxp->dev, "Device mailbox init failed: %d", ret);
	}
	dev_info(gxp->dev, "MCU boot stage: %u\n", gxp_read_32(gxp, GXP_REG_MCU_BOOT_STAGE));
	if (fw_flavor < 0) {
		dev_err(gxp->dev, "MCU firmware handshake failed: %d",
			fw_flavor);
		mcu_fw->fw_info.fw_flavor = GCIP_FW_FLAVOR_UNKNOWN;
		mcu_fw->fw_info.fw_changelist = 0;
		mcu_fw->fw_info.fw_build_time = 0;
		return fw_flavor;
	}

	dev_info(gxp->dev, "loaded %s MCU firmware (%u)",
		 gcip_fw_flavor_str(fw_flavor), mcu_fw->fw_info.fw_changelist);

	gxp_monitor_stop(gxp);
	dev_notice(gxp->dev, "MCU Read Data Transactions: %#x",
		   gxp_monitor_get_count_read_data(gxp));

	ret = gxp_mcu_telemetry_kci(mcu);
	if (ret)
		dev_warn(gxp->dev, "telemetry KCI error: %d", ret);

	ret = gcip_thermal_restore_on_powering(gxp->thermal);
	if (ret)
		dev_warn(gxp->dev, "thermal restore error: %d", ret);

	ret = gxp_kci_set_device_properties(&mcu->kci, &gxp->device_prop);
	if (ret)
		dev_warn(gxp->dev, "Failed to pass device_prop to fw: %d\n", ret);

	return 0;
}

/* Shutdowns the MCU firmware and ensure the MCU is in PG state. */
static bool wait_for_pg_state_shutdown_locked(struct gxp_dev *gxp, bool force)
{
	/* For firmwares that supports recovery mode. */
	return gxp_mcu_recovery_boot_shutdown(gxp, force);
}

static struct gxp_mcu_firmware_ns_buffer *map_ns_buffer(struct gxp_dev *gxp, dma_addr_t daddr,
							size_t size)
{
	struct gxp_mcu_firmware_ns_buffer *ns_buffer;
	u64 gcip_map_flags = GCIP_MAP_FLAGS_DMA_RW;

	ns_buffer = kzalloc(sizeof(*ns_buffer), GFP_KERNEL);
	if (!ns_buffer)
		return ERR_PTR(-ENOMEM);

	ns_buffer->daddr = daddr;
	ns_buffer->size = size;
	ns_buffer->sgt = gcip_alloc_noncontiguous(gxp->dev, size, GFP_KERNEL);
	if (!ns_buffer->sgt) {
		kfree(ns_buffer);
		return ERR_PTR(-ENOMEM);
	}
	if (!gcip_iommu_domain_map_sgt_to_iova(gxp_iommu_get_domain_for_dev(gxp), ns_buffer->sgt,
					       daddr, &gcip_map_flags)) {
		dev_err(gxp->dev, "Failed to map NS buffer, daddr: %pad size: %#zx", &daddr, size);
		gcip_free_noncontiguous(ns_buffer->sgt);
		kfree(ns_buffer);
		return ERR_PTR(-EBUSY);
	}
	return ns_buffer;
}

static void unmap_ns_buffer(struct gxp_dev *gxp, struct gxp_mcu_firmware_ns_buffer *ns_buffer)
{
	gcip_iommu_domain_unmap_sgt_from_iova(gxp_iommu_get_domain_for_dev(gxp), ns_buffer->sgt,
					      GCIP_MAP_FLAGS_DMA_RW);
	gcip_free_noncontiguous(ns_buffer->sgt);
	kfree(ns_buffer);
}

static void reset_shadow_memory(struct gxp_dev *gxp)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	struct gxp_mcu_firmware_ns_buffer *shadow_buffer = NULL, *cur;
	u32 shadow_index =
		GCIP_IMAGE_CONFIG_SANITIZER_INDEX(mcu_fw->cfg_parser.last_config.sanitizer_config);
	u32 count = 0;

	mutex_lock(&mcu_fw->ns_buffer_list_lock);
	list_for_each_entry(cur, &mcu_fw->ns_buffer_list, list) {
		if (count == shadow_index) {
			shadow_buffer = cur;
			break;
		}
		count++;
	}
	mutex_unlock(&mcu_fw->ns_buffer_list_lock);

	if (shadow_buffer) {
		memset(gcip_noncontiguous_sgt_to_mem(shadow_buffer->sgt), 0, shadow_buffer->size);
		gxp_dma_sync_sg_for_device(gxp, shadow_buffer->sgt->sgl,
					   shadow_buffer->sgt->orig_nents, DMA_TO_DEVICE);
	} else {
		dev_warn(gxp->dev, "shadow buffer not found");
	}
}

int gxp_mcu_firmware_load(struct gxp_dev *gxp, char *fw_name,
			  const struct firmware **fw)
{
	int ret;
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	struct device *dev = gxp->dev;
	struct gcip_image_config *imgcfg;
	struct gcip_common_image_header *hdr;
	size_t size;

	mutex_lock(&mcu_fw->lock);
	if (mcu_fw->status == GCIP_FW_LOADING ||
	    mcu_fw->status == GCIP_FW_VALID) {
		dev_info(gxp->dev, "MCU firmware is loaded, skip loading");
		goto out;
	}

	mcu_fw->status = GCIP_FW_LOADING;
	if (fw_name == NULL)
		fw_name = GXP_DEFAULT_MCU_FIRMWARE;
	dev_info(gxp->dev, "MCU firmware %s loading", fw_name);

	ret = request_firmware(fw, fw_name, dev);
	if (ret) {
		dev_err(dev, "request firmware '%s' failed: %d", fw_name, ret);
		goto err_out;
	}

	hdr = (struct gcip_common_image_header *)(*fw)->data;

	if (!is_signed_firmware(*fw, hdr)) {
		dev_err(dev, "Invalid firmware format %s", fw_name);
		ret = -EINVAL;
		goto err_release_firmware;
	}

	size = (*fw)->size - GCIP_FW_HEADER_SIZE;

	imgcfg = get_image_config_from_hdr(hdr);
	if (!imgcfg) {
		dev_err(dev, "Unsupported image header generation");
		ret = -EINVAL;
		goto err_release_firmware;
	}
	/* Initialize the secure telemetry buffers if available. */
	if (imgcfg->secure_telemetry_region_start) {
		ret = gxp_secure_core_telemetry_init(
			gxp, imgcfg->secure_telemetry_region_start);
		if (ret)
			dev_warn(dev,
				 "Secure telemetry initialization failed.");
	}
	ret = gcip_image_config_parse(&mcu_fw->cfg_parser, imgcfg);
	if (ret) {
		dev_err(dev, "image config parsing failed: %d", ret);
		goto err_release_firmware;
	}
	if (!gcip_image_config_is_ns(imgcfg) && !gxp->gsa_dev) {
		dev_err(dev,
			"Can't run MCU in secure mode without the GSA device");
		ret = -EINVAL;
		goto err_clear_config;
	}
	mcu_fw->is_secure = !gcip_image_config_is_ns(imgcfg);

	mcu_fw->sanitizer_status = GCIP_IMAGE_CONFIG_SANITIZER_STATUS(imgcfg->sanitizer_config);

	if (size > mcu_fw->image_buf.size || (mcu_fw->sanitizer_status != 0)) {
		if (mcu_fw->is_secure) {
			dev_err(dev, "firmware %s size %#zx exceeds buffer size %#llx", fw_name,
				size, mcu_fw->image_buf.size);
			ret = -ENOSPC;
			goto err_clear_config;
		}

		/*
		 * In non-secure mode, we support allocating buffers to put MCU firmware image
		 * instead of using carveouts.
		 */
		mcu_fw->dynamic_fw_buffer = map_ns_buffer(gxp, GXP_IREMAP_DYNAMIC_CODE_BASE, size);
		if (IS_ERR(mcu_fw->dynamic_fw_buffer))
			goto err_clear_config;
		memcpy(gcip_noncontiguous_sgt_to_mem(mcu_fw->dynamic_fw_buffer->sgt),
		       (*fw)->data + GCIP_FW_HEADER_SIZE, size);
		gxp_dma_sync_sg_for_device(gxp, mcu_fw->dynamic_fw_buffer->sgt->sgl,
					   mcu_fw->dynamic_fw_buffer->sgt->orig_nents,
					   DMA_TO_DEVICE);
	} else {
		memcpy(mcu_fw->image_buf.vaddr, (*fw)->data + GCIP_FW_HEADER_SIZE, size);
	}

out:
	mutex_unlock(&mcu_fw->lock);
	return 0;

err_clear_config:
	gcip_image_config_clear(&mcu_fw->cfg_parser);
err_release_firmware:
	release_firmware(*fw);
err_out:
	mcu_fw->status = GCIP_FW_INVALID;
	mutex_unlock(&mcu_fw->lock);
	return ret;
}

void gxp_mcu_firmware_unload(struct gxp_dev *gxp, const struct firmware *fw)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);

	mutex_lock(&mcu_fw->lock);
	if (mcu_fw->status == GCIP_FW_INVALID) {
		dev_err(mcu_fw->gxp->dev, "Failed to unload MCU firmware");
		mutex_unlock(&mcu_fw->lock);
		return;
	}
	if (mcu_fw->dynamic_fw_buffer) {
		unmap_ns_buffer(gxp, mcu_fw->dynamic_fw_buffer);
		mcu_fw->dynamic_fw_buffer = NULL;
	}
	gcip_image_config_clear(&mcu_fw->cfg_parser);
	mcu_fw->status = GCIP_FW_INVALID;
	mutex_unlock(&mcu_fw->lock);

	if (fw)
		release_firmware(fw);
}

/*
 * Boots up the MCU and program instructions.
 * It sends `START` command to GSA in the secure mode.
 */
static int gxp_mcu_firmware_start(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;
	int ret, state;

	ret = gxp_lpm_up(gxp, GXP_REG_MCU_ID);
	if (ret)
		return ret;

	gxp_monitor_set_count_read_data(gxp);
	gxp_monitor_start(gxp);

	gxp_write_32(gxp, GXP_REG_MCU_BOOT_STAGE, 0);
	gxp_mcu_set_boot_mode(mcu_fw, GXP_MCU_BOOT_MODE_NORMAL);
	if (mcu_fw->is_secure) {
		state = gsa_send_dsp_cmd(gxp->gsa_dev, GSA_DSP_START);
		if (state != GSA_DSP_STATE_RUNNING) {
			gxp_lpm_down(gxp, GXP_REG_MCU_ID);
			return -EIO;
		}
	} else {
		ret = program_iremap_csr(gxp, &mcu_fw->image_buf);
		if (ret) {
			gxp_lpm_down(gxp, GXP_REG_MCU_ID);
			return ret;
		}
		/* Raise wakeup doorbell */
		dev_dbg(gxp->dev, "Raising doorbell %d interrupt\n",
			CORE_WAKEUP_DOORBELL(GXP_REG_MCU_ID));
		gxp_doorbell_enable_for_core(gxp, CORE_WAKEUP_DOORBELL(GXP_REG_MCU_ID),
					     GXP_REG_MCU_ID);
		gxp_doorbell_set(gxp, CORE_WAKEUP_DOORBELL(GXP_REG_MCU_ID));
	}

	return 0;
}

/*
 * Shutdowns the MCU.
 * It sends `SHUTDOWN` command to GSA in the secure mode.
 *
 * Note that this function doesn't call `gxp_lpm_down`.
 *
 * 1. When MCU normally powered off after SHUTDOWN KCI.
 *    : It is already in PG state and we don't need to call that.
 *
 * 2. When we are going to shutdown MCU which is in abnormal state even after trying to rescue it.
 *    : We can't decide the state of MCU PSM or the AUR_BLOCK and accessing LPM CSRs might not be
 *      a good idea.
 *
 * Returns negative error on failure.
 */
int gxp_mcu_firmware_shutdown(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;

	if (mcu_fw->is_secure)
		return gsa_send_dsp_cmd(gxp->gsa_dev, GSA_DSP_SHUTDOWN);
	return 0;
}

/*
 * Rescues the MCU which is not working properly. After the rescue, the MCU must be in PS0 state
 * with an expectation of working normally. Basically, what this function doing is resetting MCU,
 * block power cycling and handshaking with MCU.
 *
 * Must be called with holding @mcu_fw->lock and @pm->lock.
 *
 * Returns 0 if it successfully rescued and hanshaked with the MCU.
 */
static int gxp_mcu_firmware_rescue(struct gxp_dev *gxp)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	struct gxp_mcu *mcu = container_of(mcu_fw, struct gxp_mcu, fw);
	int try = MCU_RESCUE_TRY, ret = 0;

	gcip_pm_lockdep_assert_held(gxp->power_mgr->pm);
	lockdep_assert_held(&mcu_fw->lock);

	do {
		dev_warn(gxp->dev, "Try to rescue MCU (try=%d)", try);

		if (!wait_for_pg_state_shutdown_locked(gxp, true)) {
			dev_err(gxp->dev,
				"Cannot proceed MCU rescue because it is not in PG state");
			ret = -EAGAIN;
			continue;
		}

		ret = gxp_pm_blk_reboot(gxp, 5000);
		if (ret) {
			dev_err(gxp->dev, "Failed to power cycle AUR block, (ret=%d)", ret);
			continue;
		}

		gxp_mcu_reset_mailbox(mcu);
		/* Try booting MCU up again and handshaking with it. */
		ret = gxp_mcu_firmware_start(mcu_fw);
		if (ret) {
			dev_err(gxp->dev, "Failed to boot MCU up, (ret=%d)", ret);
			continue;
		}

		ret = gxp_mcu_firmware_handshake(mcu_fw);
		if (ret) {
			dev_err(gxp->dev, "Failed to handshake with MCU even after rescue (ret=%d)",
				ret);
			continue;
		}
		dev_info(gxp->dev, "Succeeded in rescuing MCU");
	} while (ret && --try > 0);

	return ret;
}

static void gxp_mcu_firmware_stop_locked(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;
	struct gxp_mcu *mcu = container_of(mcu_fw, struct gxp_mcu, fw);
	int ret;

	lockdep_assert_held(&mcu_fw->lock);

	gxp_lpm_enable_state(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), LPM_PG_STATE);

	/* Clear doorbell to refuse non-expected interrupts */
	gxp_doorbell_clear(gxp, CORE_WAKEUP_DOORBELL(GXP_REG_MCU_ID));

	/*
	 * As the RKCI requests are processed asynchronously, the driver may return RKCI ACK
	 * responses after the MCU transits to the PG state which will wake it up again. That will
	 * eventually cause the MCU boot failure at the next run. To prevent the race condition,
	 * disable sending RKCI ACK responses before sending SHUTDOWN KCI.
	 */
	gxp_kci_disable_rkci_ack(&mcu->kci);

	ret = gxp_kci_shutdown(&mcu->kci);
	if (ret)
		dev_warn(gxp->dev, "KCI shutdown failed: %d", ret);

	/* TODO(b/296980539): revert this change after the bug is fixed. */
#if IS_ENABLED(CONFIG_GXP_GEM5)
	gxp_lpm_set_state(gxp, LPM_PSM_MCU, LPM_PG_STATE, true);
#else
	/*
	 * Waits for MCU transiting to PG state. If KCI shutdown was failed above (ret != 0), it
	 * will force to PG state.
	 */
	if (!wait_for_pg_state_shutdown_locked(gxp, /*force=*/ret))
		dev_warn(gxp->dev, "Failed to transit MCU to PG state after KCI shutdown");
#endif /* IS_ENABLED(CONFIG_GXP_GEM5) */

	/* To test the case of the MCU FW sending FW_CRASH RKCI in the middle. */
	TEST_FLUSH_KCI_WORKERS(mcu->kci);

	/*
	 * We should disable the IRQ handler before canceling the KCI works to prevent a potential
	 * race condition that the works are scheduled while canceling them.
	 */
	gxp_kci_disable_irq_handler(&mcu->kci);

	gxp_kci_cancel_work_queues(&mcu->kci);
	/*
	 * Clears up all remaining UCI/KCI commands. Otherwise, MCU may drain them improperly after
	 * it reboots.
	 */
	gxp_mcu_reset_mailbox(mcu);

	/*
	 * Since the KCI mailbox has been flushed, it is safe to enable sending RKCI ACK responses
	 * and the IRQ handler again.
	 *
	 * Unlike disabling, enable sending RKCI ACK responses first since the driver should do that
	 * if the MCU firmware sends RKCI requests and IRQ has been triggered. However, this should
	 * not happen theoretically unless the MCU has been failed to be turned off normally.
	 */
	gxp_kci_enable_rkci_ack(&mcu->kci);
	gxp_kci_enable_irq_handler(&mcu->kci);
}

/*
 * Caller must hold firmware lock.
 */
static int gxp_mcu_firmware_run_locked(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;
	struct gxp_mcu *mcu = container_of(mcu_fw, struct gxp_mcu, fw);
	int ret;

	lockdep_assert_held(&mcu_fw->lock);

	if (mcu_fw->sanitizer_status & GCIP_FW_ASAN_ENABLED)
		reset_shadow_memory(gxp);

	/*
	 * Resets UCI/KCI CSRs to ensure that no unconsumed commands are carried over from the last
	 * execution.
	 */
	gxp_mcu_reset_mailbox(mcu);

	ret = gxp_mcu_firmware_start(mcu_fw);
	if (ret)
		return ret;

	gcip_fault_inject_send(mcu_fw->fault_inject);

	ret = gxp_mcu_firmware_handshake(mcu_fw);
	if (ret) {
		dev_warn(gxp->dev, "Retry MCU firmware handshake with resetting MCU");
		if (!gxp_mcu_reset(gxp, true))
			ret = gxp_mcu_firmware_handshake(mcu_fw);
	}

	/*
	 * We don't need to handshake again if it successfully rescues MCU because it will try
	 * handshake internally.
	 */
	if (ret) {
		ret = gxp_mcu_firmware_rescue(gxp);
		if (ret) {
			dev_err(gxp->dev, "Failed to run MCU even after trying to rescue it: %d",
				ret);
			wait_for_pg_state_shutdown_locked(gxp, true);
			return ret;
		}
	}

	mcu_fw->status = GCIP_FW_VALID;

	dev_info(gxp->dev, "MCU firmware run succeeded");

	return 0;
}

static int init_mcu_firmware_buf(struct gxp_dev *gxp,
				 struct gxp_mapped_resource *buf)
{
	struct resource r;
	int ret;

	ret = gxp_acquire_rmem_resource(gxp, &r, "gxp-mcu-fw-region");
	if (ret) {
		dev_err(gxp->dev, "Failed to find reserved memory for MCU FW: %d", ret);
		return ret;
	}
	buf->size = resource_size(&r);
	buf->paddr = r.start;
	buf->daddr = GXP_IREMAP_CODE_BASE;
	buf->vaddr =
		devm_memremap(gxp->dev, buf->paddr, buf->size, MEMREMAP_WC);
	if (IS_ERR(buf->vaddr))
		ret = PTR_ERR(buf->vaddr);
	return ret;
}

static char *fw_name_from_buf(struct gxp_dev *gxp, const char *buf)
{
	size_t len;
	char *name;

	len = strlen(buf);
	/* buf from sysfs attribute contains the last line feed character */
	if (len == 0 || buf[len - 1] != '\n')
		return ERR_PTR(-EINVAL);

	name = devm_kstrdup(gxp->dev, buf, GFP_KERNEL);
	if (!name)
		return ERR_PTR(-ENOMEM);
	/* name should not contain the last line feed character */
	name[len - 1] = '\0';
	return name;
}

static ssize_t load_firmware_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct gxp_dev *gxp = dev_get_drvdata(dev);
	ssize_t ret;
	char *firmware_name = gxp_firmware_loader_get_mcu_fw_name(gxp);

	ret = sysfs_emit(buf, "%s\n", firmware_name);
	kfree(firmware_name);
	return ret;
}

static ssize_t load_firmware_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct gxp_dev *gxp = dev_get_drvdata(dev);
	int ret;
	char *name;

	name = fw_name_from_buf(gxp, buf);
	if (IS_ERR(name))
		return PTR_ERR(name);
	if (gcip_pm_is_powered(gxp->power_mgr->pm)) {
		dev_err(gxp->dev,
			"Reject firmware loading because wakelocks are holding");
		return -EBUSY;
		/*
		 * Note: it's still possible a wakelock is acquired by
		 * clients after the check above, but this function is for
		 * development purpose only, we don't insist on preventing
		 * race condition bugs.
		 */
	}
	dev_info(gxp->dev, "loading firmware %s from SysFS", name);
	/*
	 * It's possible a race condition bug here that someone opens a gxp
	 * device and loads the firmware between below unload/load functions in
	 * another thread, but this interface is only for developer debugging.
	 * We don't insist on preventing the race condition bug.
	 */
	gxp_firmware_loader_unload(gxp);
	gxp_firmware_loader_set_mcu_fw_name(gxp, name);
	ret = gxp_firmware_loader_load_if_needed(gxp);
	if (ret) {
		dev_err(gxp->dev, "Failed to load MCU firmware: %s\n", name);
		return ret;
	}
	return count;
}

static DEVICE_ATTR_RW(load_firmware);

/* Provide the version info of the firmware including CL and privilege_level */
static ssize_t firmware_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gxp_dev *gxp = dev_get_drvdata(dev);
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	const char *priv;

	if (mcu_fw->status != GCIP_FW_VALID) {
		dev_warn(gxp->dev, "firmware_version is not available, firmware status: %d",
			 mcu_fw->status);
		return -ENODEV;
	}

	switch (mcu_fw->cfg_parser.last_config.privilege_level) {
	case GCIP_FW_PRIV_LEVEL_GSA:
		priv = "GSA";
		break;
	case GCIP_FW_PRIV_LEVEL_TZ:
		priv = "secure";
		break;
	case GCIP_FW_PRIV_LEVEL_NS:
		priv = "non-secure";
		break;
	default:
		priv = "error";
	}

	return sysfs_emit(buf, "cl=%d priv=%s\n", mcu_fw->fw_info.fw_changelist, priv);
}

static DEVICE_ATTR_RO(firmware_version);

/* Provide the count of firmware crash. */
static ssize_t firmware_crash_counter_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct gxp_dev *gxp = dev_get_drvdata(dev);
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);

	return sysfs_emit(buf, "%d\n", mcu_fw->crash_cnt);
}

static DEVICE_ATTR_RO(firmware_crash_counter);

static struct attribute *dev_attrs[] = {
	&dev_attr_load_firmware.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_firmware_crash_counter.attr,
	NULL,
};

static const struct attribute_group firmware_attr_group = {
	.attrs = dev_attrs,
};

static int image_config_map_ns(struct gxp_dev *gxp, dma_addr_t daddr, size_t size)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	struct gxp_mcu_firmware_ns_buffer *ns_buffer;

	ns_buffer = map_ns_buffer(gxp, daddr, size);
	if (IS_ERR(ns_buffer))
		return PTR_ERR(ns_buffer);

	mutex_lock(&mcu_fw->ns_buffer_list_lock);
	list_add_tail(&ns_buffer->list, &mcu_fw->ns_buffer_list);
	mutex_unlock(&mcu_fw->ns_buffer_list_lock);
	return 0;
}

static void image_config_unmap_ns(struct gxp_dev *gxp, dma_addr_t daddr, size_t size)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	struct gxp_mcu_firmware_ns_buffer *ns_buffer = NULL, *cur;

	mutex_lock(&mcu_fw->ns_buffer_list_lock);
	list_for_each_entry(cur, &mcu_fw->ns_buffer_list, list) {
		if (cur->daddr == daddr && cur->size == size) {
			ns_buffer = cur;
			list_del(&cur->list);
			break;
		}
	}
	mutex_unlock(&mcu_fw->ns_buffer_list_lock);

	if (ns_buffer) {
		unmap_ns_buffer(gxp, cur);
	} else {
		dev_warn(gxp->dev, "Failed to find NS buffer, daddr: %pad size: %#zx", &daddr,
			 size);
	}
}

static int image_config_map(void *data, dma_addr_t daddr, phys_addr_t paddr, size_t size,
			    unsigned int cfg_map_flags, unsigned int cfg_op_flags)
{
	struct gxp_dev *gxp = data;
	const bool ns = !(cfg_op_flags & GCIP_IMAGE_CONFIG_FLAGS_SECURE);
	u64 gcip_map_flags = GCIP_MAP_FLAGS_DMA_RW;

	if (ns)
		return image_config_map_ns(gxp, daddr, size);

	if (GCIP_IMAGE_CONFIG_MAP_MMIO(cfg_map_flags))
		gcip_map_flags |= GCIP_MAP_FLAGS_MMIO_TO_FLAGS(1);

	return gcip_iommu_map(gxp_iommu_get_domain_for_dev(gxp), daddr, paddr, size,
			      gcip_map_flags);
}

static void image_config_unmap(void *data, dma_addr_t daddr, size_t size,
			       unsigned int cfg_map_flags, unsigned int cfg_op_flags)
{
	struct gxp_dev *gxp = data;
	const bool ns = !(cfg_op_flags & GCIP_IMAGE_CONFIG_FLAGS_SECURE);

	if (ns) {
		image_config_unmap_ns(gxp, daddr, size);
		return;
	}

	gcip_iommu_unmap(gxp_iommu_get_domain_for_dev(gxp), daddr, size);
}

static void gxp_mcu_firmware_crash_handler_work(struct work_struct *work)
{
	struct gxp_mcu_firmware *mcu_fw =
		container_of(work, struct gxp_mcu_firmware, fw_crash_handler_work);

	gxp_mcu_firmware_crash_handler(mcu_fw->gxp, GCIP_FW_CRASH_UNRECOVERABLE_FAULT);
}

static int gxp_mcu_firmware_fault_inject_init(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;
	struct gxp_mcu *mcu = container_of(mcu_fw, struct gxp_mcu, fw);
	struct gcip_fault_inject *injection;
	const struct gcip_fault_inject_args args = { .dev = gxp->dev,
						     .parent_dentry = gxp->d_entry,
						     .pm = gxp->power_mgr->pm,
						     .send_kci = gxp_kci_fault_injection,
						     .kci_data = &mcu->kci };

	injection = gcip_fault_inject_create(&args);

	if (IS_ERR(injection))
		return PTR_ERR(injection);

	mcu_fw->fault_inject = injection;

	return 0;
}

static void gxp_mcu_firmware_fault_inject_exit(struct gxp_mcu_firmware *mcu_fw)
{
	gcip_fault_inject_destroy(mcu_fw->fault_inject);
}

int gxp_mcu_firmware_init(struct gxp_dev *gxp, struct gxp_mcu_firmware *mcu_fw)
{
	static const struct gcip_image_config_ops image_config_parser_ops = {
		.map = image_config_map,
		.unmap = image_config_unmap,
	};
	int ret;

	ret = gcip_image_config_parser_init(
		&mcu_fw->cfg_parser, &image_config_parser_ops, gxp->dev, gxp);
	if (unlikely(ret)) {
		dev_err(gxp->dev, "failed to init config parser: %d", ret);
		return ret;
	}
	ret = init_mcu_firmware_buf(gxp, &mcu_fw->image_buf);
	if (ret) {
		dev_err(gxp->dev, "failed to init MCU firmware buffer: %d",
			ret);
		return ret;
	}
	mcu_fw->gxp = gxp;
	mcu_fw->status = GCIP_FW_INVALID;
	mcu_fw->crash_cnt = 0;
	mutex_init(&mcu_fw->lock);
	INIT_LIST_HEAD(&mcu_fw->ns_buffer_list);
	mutex_init(&mcu_fw->ns_buffer_list_lock);
	INIT_WORK(&mcu_fw->fw_crash_handler_work, gxp_mcu_firmware_crash_handler_work);

	ret = device_add_group(gxp->dev, &firmware_attr_group);
	if (ret) {
		dev_err(gxp->dev, "failed to create firmware device group");
		return ret;
	}

	ret = gxp_mcu_firmware_fault_inject_init(mcu_fw);
	if (ret)
		dev_warn(gxp->dev, "failed to init fault injection: %d", ret);

	return 0;
}

void gxp_mcu_firmware_exit(struct gxp_mcu_firmware *mcu_fw)
{
	if (IS_GXP_TEST && (!mcu_fw || !mcu_fw->gxp))
		return;

	gxp_mcu_firmware_fault_inject_exit(mcu_fw);
	cancel_work_sync(&mcu_fw->fw_crash_handler_work);
	device_remove_group(mcu_fw->gxp->dev, &firmware_attr_group);
}

int gxp_mcu_firmware_run(struct gxp_mcu_firmware *mcu_fw)
{
	int ret;

	mutex_lock(&mcu_fw->lock);
	if (mcu_fw->status == GCIP_FW_INVALID)
		ret = -EINVAL;
	else
		ret = gxp_mcu_firmware_run_locked(mcu_fw);
	mutex_unlock(&mcu_fw->lock);
	return ret;
}

void gxp_mcu_firmware_stop(struct gxp_mcu_firmware *mcu_fw)
{
	mutex_lock(&mcu_fw->lock);
	gxp_mcu_firmware_stop_locked(mcu_fw);
	mutex_unlock(&mcu_fw->lock);
}

void gxp_mcu_firmware_crash_handler(struct gxp_dev *gxp,
				    enum gcip_fw_crash_type crash_type)
{
	struct gxp_mcu *mcu = &to_mcu_dev(gxp)->mcu;
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	struct gxp_client *client;
	struct gcip_pm *pm = gxp->power_mgr->pm;
	int ret;

	dev_err(gxp->dev, "MCU firmware is crashed, crash_type=%d", crash_type);

	/*
	 * This crash handler can be triggered in two cases:
	 * 1. The MCU firmware detects some unrecoverable faults and sends FW_CRASH RKCI to the
	 *    kernel driver. (GCIP_FW_CRASH_UNRECOVERABLE_FAULT)
	 * 2. The MCU firmware is crashed some reasons which cannot be detected by itself and the
	 *    kernel driver notices the MCU crash with the HW watchdog timeout.
	 *    (GCIP_FW_CRASH_HW_WDG_TIMEOUT)
	 *
	 * As those two cases are asynchronous, they can happen simultaneously. In the first case,
	 * the MCU firmware must turn off the HW watchdog first to prevent that race case.
	 */
	if (crash_type != GCIP_FW_CRASH_UNRECOVERABLE_FAULT &&
	    crash_type != GCIP_FW_CRASH_HW_WDG_TIMEOUT)
		return;

	dev_err(gxp->dev, "Unrecoverable MCU firmware fault, handle it");

	mcu_fw->crash_cnt += 1;

	/*
	 * In the case of stopping MCU FW while it is handling `CLIENT_FATAL_ERROR` RKCI, it will
	 * acquire locks in this order:
	 *   gcip_pm_put -> holds @pm->lock -> gxp_mcu_firmware_stop -> holds @mcu_fw->lock
	 *   -> waits for the completion of RKCI handler -> gxp_vd_invalidate_with_client_id
	 *   -> holds @gxp->client_list_lock -> hold @client->semaphore -> holds @gxp->vd_semaphore
	 *
	 * Also, in the case of starting MCU FW, the locking order will be:
	 *   gcip_pm_get -> holds @pm->lock -> gxp_mcu_firmware_run -> holds @mcu_fw->lock
	 *
	 * To prevent a deadlock issue, we have to follow the same locking order from here.
	 */

	/*
	 * Holding the PM lock due to the reasons listed below.
	 *   1. As we are recovering the MCU firmware, we should block the PM requests (e.g.,
	 *      acquiring or releasing the block wakelock) until the rescuing is finished.
	 *   2. Restarting the MCU firmware might involve restore functions (e.g.,
	 *      gcip_thermal_restore_on_powering) which require the caller to hold the PM lock.
	 */
	gcip_pm_lock(pm);

	/*
	 * By the race, if all clients left earlier than this handler, all block wakleock should be
	 * already released and the BLK is turned off. We don't have to rescue the MCU firmware.
	 */
	if (!gcip_pm_is_powered(pm)) {
		dev_info(
			gxp->dev,
			"The block wakelock is already released, skip restarting MCU firmware");
		goto out_unlock_pm;
	}

	/* Hold @mcu_fw->lock because manipulating the MCU FW state must be a critical section. */
	mutex_lock(&mcu_fw->lock);

	/*
	 * Prevent @gxp->client_list is being changed while handling the crash.
	 * The user cannot open or close a fd until this function releases the lock.
	 */
	mutex_lock(&gxp->client_list_lock);

	/*
	 * Hold @client->semaphore first to prevent deadlock.
	 * By holding this lock, clients cannot proceed most IOCTLs.
	 */
	list_for_each_entry (client, &gxp->client_list, list_entry) {
		down_write(&client->semaphore);
	}

	/*
	 * Holding @client->semaphore will block the most client actions, but let's make sure
	 * it by holding the locks directly related to the actions we want to block accordingly.
	 * For example, in the case of the block wakelock, the debug dump can try to acquire it
	 * which cannot be blocked by holding @client->semaphore.
	 *
	 * However, we don't lock @gxp->vd_semaphore for not increasing lock dependency since
	 * holding @gxp->client_list_lock and @client->semaphore is enough to ensure no new VD
	 * being allocated.
	 */

	/* We should consume all arrived responses first before canceling pending commands. */
	gxp_uci_consume_responses(&mcu->uci);

	/*
	 * Discard all pending/unconsumed UCI responses and change the state of all virtual devices
	 * to GXP_VD_UNAVAILABLE. From now on, all clients cannot request new UCI commands.
	 */
	list_for_each_entry (client, &gxp->client_list, list_entry) {
		if (client->has_block_wakelock && client->vd) {
			gxp_vd_invalidate(gxp, client->vd, GXP_INVALIDATED_MCU_CRASH);
			client->vd->mcu_crashed = true;
			gxp_uci_cancel(client->vd);
		}
	}

	/* Dump diagnostic information for MCU crash before resetting it. */
	gxp_debug_dump_report_mcu_crash(gxp);

	/* Waits for the MCU transiting to PG state and restart the MCU firmware. */
	if (!wait_for_pg_state_shutdown_locked(gxp, crash_type == GCIP_FW_CRASH_HW_WDG_TIMEOUT)) {
		dev_warn(gxp->dev, "Failed to transit MCU LPM state to PG");
		goto out;
	}

	ret = gxp_mcu_firmware_run_locked(mcu_fw);
	if (ret)
		dev_warn(gxp->dev, "Failed to run MCU firmware (ret=%d)", ret);

out:
	list_for_each_entry (client, &gxp->client_list, list_entry) {
		up_write(&client->semaphore);
	}
	mutex_unlock(&gxp->client_list_lock);
	mutex_unlock(&mcu_fw->lock);
out_unlock_pm:
	gcip_pm_unlock(pm);
}
