// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP MicroController Unit firmware management.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gsa/gsa_dsp.h>
#include <linux/io.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <gcip/gcip-common-image-header.h>
#include <gcip/gcip-image-config.h>
#include <gcip/gcip-pm.h>
#include <gcip/gcip-thermal.h>

#include "gxp-bpm.h"
#include "gxp-config.h"
#include "gxp-core-telemetry.h"
#include "gxp-dma.h"
#include "gxp-doorbell.h"
#include "gxp-firmware-loader.h"
#include "gxp-internal.h"
#include "gxp-kci.h"
#include "gxp-lpm.h"
#include "gxp-mailbox-driver.h"
#include "gxp-mcu-firmware.h"
#include "gxp-mcu-platform.h"
#include "gxp-mcu.h"
#include "gxp-pm.h"

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
static void program_iremap_csr(struct gxp_dev *gxp,
			       struct gxp_mapped_resource *buf)
{
	dev_info(gxp->dev, "Program instruction remap CSRs");
	gxp_write_32(gxp, GXP_REG_CFGVECTABLE0, buf->daddr);

	gxp_write_32(gxp, GXP_REG_IREMAP_LOW, buf->daddr);
	gxp_write_32(gxp, GXP_REG_IREMAP_HIGH, buf->daddr + buf->size);
	gxp_write_32(gxp, GXP_REG_IREMAP_TARGET, buf->daddr);
	gxp_write_32(gxp, GXP_REG_IREMAP_ENABLE, 1);
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
	fw_flavor = gxp_kci_fw_info(&mcu->kci, &mcu_fw->fw_info);
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

	gxp_bpm_stop(gxp, GXP_MCU_CORE_ID);
	dev_notice(gxp->dev, "MCU Instruction read transactions: 0x%x\n",
		   gxp_bpm_read_counter(gxp, GXP_MCU_CORE_ID, INST_BPM_OFFSET));

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

/*
 * Waits for the MCU LPM transition to the PG state.
 *
 * Must be called with holding @mcu_fw->lock.
 *
 * @ring_doorbell: If the situation is that the MCU cannot execute the transition by itself such
 *                 as HW watchdog timeout, it must be passed as true to trigger the doorbell and
 *                 let the MCU do that forcefully.
 *
 * Returns true if MCU successfully transited to PG state, otherwise false.
 */
static bool wait_for_pg_state_locked(struct gxp_dev *gxp, bool ring_doorbell)
{
	struct gxp_mcu *mcu = &to_mcu_dev(gxp)->mcu;
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	int try = MCU_RESCUE_TRY, ret;

	lockdep_assert_held(&mcu_fw->lock);

	do {
		if (ring_doorbell) {
			gxp_mailbox_set_control(mcu->kci.mbx, GXP_MBOX_CONTROL_MAGIC_POWER_DOWN);
			gxp_doorbell_enable_for_core(gxp, CORE_WAKEUP_DOORBELL(GXP_MCU_CORE_ID),
						     GXP_MCU_CORE_ID);
			gxp_doorbell_set(gxp, CORE_WAKEUP_DOORBELL(GXP_MCU_CORE_ID));
		}

		if (gxp_lpm_wait_state_eq(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), LPM_PG_STATE))
			return true;

		dev_warn(gxp->dev, "MCU PSM transition to PS3 fails, current state: %u, try: %d",
			 gxp_lpm_get_state(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID)), try);

		/*
		 * If PG transition fails, MCU will not fall into WFI after the reset below.
		 * Therefore, we must ring doorbell to let it fall into WFI from the next try.
		 */
		ring_doorbell = true;

		ret = gxp_mcu_reset(gxp, true);
		if (ret) {
			dev_err(gxp->dev, "Failed to reset MCU after PG transition fails (ret=%d)",
				ret);
			continue;
		}

		/*
		 * We should give enough time to MCU to register doorbell handler. We hope MCU
		 * successfully registers the handler after the reset even if the handshake fails.
		 */
		ret = gxp_mcu_firmware_handshake(mcu_fw);
		if (ret)
			dev_err(gxp->dev,
				"Failed to handshake with MCU after PG transition fails (ret=%d)",
				ret);
	} while (--try > 0);

	return false;
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

	if (size > mcu_fw->image_buf.size) {
		dev_err(dev, "firmware %s size %#zx exceeds buffer size %#llx",
			fw_name, size, mcu_fw->image_buf.size);
		ret = -ENOSPC;
		goto err_release_firmware;
	}

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

	memcpy(mcu_fw->image_buf.vaddr, (*fw)->data + GCIP_FW_HEADER_SIZE,
	       size);
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
	gcip_image_config_clear(&mcu_fw->cfg_parser);
	mcu_fw->status = GCIP_FW_INVALID;
	mutex_unlock(&mcu_fw->lock);
}

/*
 * Boots up the MCU and program instructions.
 * It sends `START` command to GSA in the secure mode.
 */
static int gxp_mcu_firmware_start(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;
	int ret, state;

	gxp_bpm_configure(gxp, GXP_MCU_CORE_ID, INST_BPM_OFFSET,
			  BPM_EVENT_READ_XFER);

	ret = gxp_lpm_up(gxp, GXP_MCU_CORE_ID);
	if (ret)
		return ret;

	if (mcu_fw->is_secure) {
		state = gsa_send_dsp_cmd(gxp->gsa_dev, GSA_DSP_START);
		if (state != GSA_DSP_STATE_RUNNING) {
			gxp_lpm_down(gxp, GXP_MCU_CORE_ID);
			return -EIO;
		}
	} else {
		program_iremap_csr(gxp, &mcu_fw->image_buf);
		/* Raise wakeup doorbell */
		dev_dbg(gxp->dev, "Raising doorbell %d interrupt\n",
			CORE_WAKEUP_DOORBELL(GXP_MCU_CORE_ID));
		gxp_doorbell_enable_for_core(
			gxp, CORE_WAKEUP_DOORBELL(GXP_MCU_CORE_ID),
			GXP_MCU_CORE_ID);
		gxp_doorbell_set(gxp, CORE_WAKEUP_DOORBELL(GXP_MCU_CORE_ID));
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
 */
static void gxp_mcu_firmware_shutdown(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;

	if (mcu_fw->is_secure)
		gsa_send_dsp_cmd(gxp->gsa_dev, GSA_DSP_SHUTDOWN);
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
	int try = MCU_RESCUE_TRY, ret = 0;

	gcip_pm_lockdep_assert_held(gxp->power_mgr->pm);
	lockdep_assert_held(&mcu_fw->lock);

	do {
		dev_warn(gxp->dev, "Try to rescue MCU (try=%d)", try);

		/*
		 * TODO(b/286179665): Currently, this function must not be called when MCU is in
		 * PS0 state because GSA shutdown will be NO-OP and powering block down will cause
		 * a kernel panic eventually. We need to ask the architecture team for sharing how
		 * to forcefully transit MCU to PS3 state with us.
		 */
		if (!wait_for_pg_state_locked(gxp, true)) {
			dev_err(gxp->dev,
				"Cannot proceed MCU rescue because it is not in PG state");
			ret = -EAGAIN;
			continue;
		}

		/* Try power cycle after resetting the MCU and still holding the reset bits. */
		ret = gxp_mcu_reset(gxp, false);
		if (ret) {
			dev_err(gxp->dev, "Failed to reset MCU (ret=%d)", ret);
			continue;
		}

		gxp_mcu_firmware_shutdown(mcu_fw);

		ret = gxp_pm_blk_reboot(gxp, 5000);
		if (ret) {
			dev_err(gxp->dev, "Failed to power cycle AUR block, (ret=%d)", ret);
			continue;
		}

		/* Try booting MCU up again and hanshaking with it. */
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

	gxp_lpm_enable_state(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), LPM_PG_STATE);

	/* Clear doorbell to refuse non-expected interrupts */
	gxp_doorbell_clear(gxp, CORE_WAKEUP_DOORBELL(GXP_MCU_CORE_ID));

	ret = gxp_kci_shutdown(&mcu->kci);
	if (ret)
		dev_warn(gxp->dev, "KCI shutdown failed: %d", ret);

	/*
	 * Waits for MCU transiting to PG state. If KCI shutdown was failed above (ret != 0), it
	 * will wait for that with ringing the doorbell.
	 */
	if (!wait_for_pg_state_locked(gxp, /*ring_doorbell=*/ret)) {
		dev_err(gxp->dev, "Failed to transit MCU to PG state after KCI shutdown");
		/*
		 * TODO(b/286179665): Call rescue function and ring doorbell to transit MCU to PG
		 * from here.
		 */
	}

	/* To test the case of the MCU FW sending FW_CRASH RKCI in the middle. */
	TEST_FLUSH_KCI_WORKERS(mcu->kci);

	gxp_kci_cancel_work_queues(&mcu->kci);
	/*
	 * Clears up all remaining KCI commands. Otherwise, MCU may drain them improperly after it
	 * reboots.
	 */
	gxp_kci_reinit(&mcu->kci);

	gxp_mcu_firmware_shutdown(mcu_fw);
}

/*
 * Caller must hold firmware lock.
 */
static int gxp_mcu_firmware_run_locked(struct gxp_mcu_firmware *mcu_fw)
{
	struct gxp_dev *gxp = mcu_fw->gxp;
	int ret;

	lockdep_assert_held(&mcu_fw->lock);

	ret = gxp_mcu_firmware_start(mcu_fw);
	if (ret)
		return ret;

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
			gxp_mcu_firmware_shutdown(mcu_fw);
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
	if (ret)
		return ret;
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

	ret = scnprintf(buf, PAGE_SIZE, "%s\n", firmware_name);
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

static struct attribute *dev_attrs[] = {
	&dev_attr_load_firmware.attr,
	NULL,
};

static const struct attribute_group firmware_attr_group = {
	.attrs = dev_attrs,
};

static int image_config_map(void *data, dma_addr_t daddr, phys_addr_t paddr,
			    size_t size, unsigned int flags)
{
	struct gxp_dev *gxp = data;
	const bool ns = !(flags & GCIP_IMAGE_CONFIG_FLAGS_SECURE);

	if (ns) {
		dev_err(gxp->dev, "image config NS mappings are not supported");
		return -EINVAL;
	}

	return gxp_iommu_map(gxp, gxp_iommu_get_domain_for_dev(gxp), daddr,
			     paddr, size, IOMMU_READ | IOMMU_WRITE);
}

static void image_config_unmap(void *data, dma_addr_t daddr, size_t size,
			       unsigned int flags)
{
	struct gxp_dev *gxp = data;

	gxp_iommu_unmap(gxp, gxp_iommu_get_domain_for_dev(gxp), daddr, size);
}

static void gxp_mcu_firmware_crash_handler_work(struct work_struct *work)
{
	struct gxp_mcu_firmware *mcu_fw =
		container_of(work, struct gxp_mcu_firmware, fw_crash_handler_work);

	gxp_mcu_firmware_crash_handler(mcu_fw->gxp, GCIP_FW_CRASH_UNRECOVERABLE_FAULT);
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
	mutex_init(&mcu_fw->lock);
	INIT_WORK(&mcu_fw->fw_crash_handler_work, gxp_mcu_firmware_crash_handler_work);

	ret = device_add_group(gxp->dev, &firmware_attr_group);
	if (ret)
		dev_err(gxp->dev, "failed to create firmware device group");
	return ret;
}

void gxp_mcu_firmware_exit(struct gxp_mcu_firmware *mcu_fw)
{
	if (IS_GXP_TEST && (!mcu_fw || !mcu_fw->gxp))
		return;
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

	/*
	 * Discard all pending/unconsumed UCI responses and change the state of all virtual devices
	 * to GXP_VD_UNAVAILABLE. From now on, all clients cannot request new UCI commands.
	 */
	list_for_each_entry (client, &gxp->client_list, list_entry) {
		if (client->has_block_wakelock && client->vd) {
			gxp_vd_invalidate(gxp, client->vd);
			client->vd->mcu_crashed = true;
		}
	}

	/* Waits for the MCU transiting to PG state and restart the MCU firmware. */
	if (!wait_for_pg_state_locked(gxp, crash_type == GCIP_FW_CRASH_HW_WDG_TIMEOUT)) {
		dev_err(gxp->dev, "Failed to transit MCU LPM state to PG");
		/* TODO(b/286179665): Call rescue function from here. */
		goto out;
	}

	ret = gxp_mcu_firmware_run_locked(mcu_fw);
	if (ret)
		dev_err(gxp->dev, "Failed to run MCU firmware (ret=%d)", ret);

out:
	list_for_each_entry (client, &gxp->client_list, list_entry) {
		up_write(&client->semaphore);
	}
	mutex_unlock(&gxp->client_list_lock);
	mutex_unlock(&mcu_fw->lock);
out_unlock_pm:
	gcip_pm_unlock(pm);
}
