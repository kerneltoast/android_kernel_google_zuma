// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU firmware loader.
 *
 * Copyright (C) 2019-2020 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include <gcip/gcip-pm.h>
#include <gcip/gcip-thermal.h>

#include "edgetpu.h"
#include "edgetpu-debug-dump.h"
#include "edgetpu-device-group.h"
#include "edgetpu-firmware.h"
#include "edgetpu-firmware-util.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-usage-stats.h"

static char *firmware_name;
module_param(firmware_name, charp, 0660);

struct edgetpu_firmware_private {
	const struct edgetpu_firmware_chip_data *chip_fw;
	void *data; /* for edgetpu_firmware_(set/get)_data */

	struct mutex fw_desc_lock;
	struct edgetpu_firmware_desc fw_desc;
	enum gcip_fw_status status;
	struct gcip_fw_info fw_info;
};

void edgetpu_firmware_set_data(struct edgetpu_firmware *et_fw, void *data)
{
	et_fw->p->data = data;
}

void *edgetpu_firmware_get_data(struct edgetpu_firmware *et_fw)
{
	return et_fw->p->data;
}

/* Request firmware and copy to carveout. */
static int edgetpu_firmware_carveout_load_locked(struct edgetpu_firmware *et_fw,
						 struct edgetpu_firmware_desc *fw_desc,
						 const char *name)
{
	int ret;
	struct edgetpu_dev *etdev = et_fw->etdev;
	struct device *dev = etdev->dev;
	const struct firmware *fw;
	size_t aligned_size;

	ret = request_firmware(&fw, name, dev);
	if (ret) {
		etdev_err(etdev, "request firmware '%s' failed: %d\n", name, ret);
		return ret;
	}

	aligned_size = ALIGN(fw->size, fw_desc->buf.used_size_align);
	if (aligned_size > fw_desc->buf.alloc_size) {
		etdev_err(etdev,
			  "firmware buffer too small: alloc size=%#zx, required size=%#zx\n",
			  fw_desc->buf.alloc_size, aligned_size);
		ret = -ENOSPC;
		goto out_release_firmware;
	}

	memcpy(fw_desc->buf.vaddr, fw->data, fw->size);
	fw_desc->buf.used_size = aligned_size;
	/* May return NULL on out of memory, driver must handle properly */
	fw_desc->buf.name = devm_kstrdup(dev, name, GFP_KERNEL);

out_release_firmware:
	release_firmware(fw);
	return ret;
}

static void edgetpu_firmware_carveout_unload_locked(struct edgetpu_firmware *et_fw,
						    struct edgetpu_firmware_desc *fw_desc)
{
	fw_desc->buf.name = NULL;
	fw_desc->buf.used_size = 0;
}

static int edgetpu_firmware_load_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc, const char *name,
		enum edgetpu_firmware_flags flags)
{
	const struct edgetpu_firmware_chip_data *chip_fw = et_fw->p->chip_fw;
	struct edgetpu_dev *etdev = et_fw->etdev;
	int ret;

	fw_desc->buf.flags = flags;

	if (chip_fw->alloc_buffer) {
		ret = chip_fw->alloc_buffer(et_fw, &fw_desc->buf);
		if (ret) {
			etdev_err(etdev, "handler alloc_buffer failed: %d\n",
				  ret);
			return ret;
		}
	}

	ret = edgetpu_firmware_carveout_load_locked(et_fw, fw_desc, name);
	if (ret)
		goto out_free_buffer;

	if (chip_fw->setup_buffer) {
		ret = chip_fw->setup_buffer(et_fw, &fw_desc->buf);
		if (ret) {
			etdev_err(etdev, "handler setup_buffer failed: %d\n",
				  ret);
			goto out_unload_locked;
		}
	}

	return 0;

out_unload_locked:
	edgetpu_firmware_carveout_unload_locked(et_fw, fw_desc);
out_free_buffer:
	if (chip_fw->free_buffer)
		chip_fw->free_buffer(et_fw, &fw_desc->buf);
	return ret;
}

static void edgetpu_firmware_unload_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc)
{
	const struct edgetpu_firmware_chip_data *chip_fw = et_fw->p->chip_fw;

	edgetpu_firmware_carveout_unload_locked(et_fw, fw_desc);
	/*
	 * Platform specific implementation for freeing allocated buffer.
	 */
	if (chip_fw->free_buffer)
		chip_fw->free_buffer(et_fw, &fw_desc->buf);
}

static int edgetpu_firmware_handshake(struct edgetpu_firmware *et_fw)
{
	struct edgetpu_dev *etdev = et_fw->etdev;
	enum gcip_fw_flavor fw_flavor;
	int ret;

	etdev_dbg(etdev, "Detecting firmware info...");
	et_fw->p->fw_info.fw_build_time = 0;
	et_fw->p->fw_info.fw_flavor = GCIP_FW_FLAVOR_UNKNOWN;
	et_fw->p->fw_info.fw_changelist = 0;
	fw_flavor = edgetpu_kci_fw_info(etdev->etkci, &et_fw->p->fw_info);
	if (fw_flavor < 0) {
		etdev_err(etdev, "firmware handshake failed: %d", fw_flavor);
		et_fw->p->fw_info.fw_flavor = GCIP_FW_FLAVOR_UNKNOWN;
		et_fw->p->fw_info.fw_changelist = 0;
		et_fw->p->fw_info.fw_build_time = 0;
		return fw_flavor;
	}

	etdev_info(etdev, "loaded %s firmware (%u.%u %u)",
		   gcip_fw_flavor_str(fw_flavor),
		   etdev->fw_version.major_version,
		   etdev->fw_version.minor_version,
		   et_fw->p->fw_info.fw_changelist);
	ret = edgetpu_telemetry_kci(etdev);
	if (ret)
		etdev_warn(etdev, "telemetry KCI error: %d", ret);

	ret = gcip_firmware_tracing_restore_on_powering(etdev->fw_tracing);
	if (ret)
		etdev_warn(etdev, "firmware tracing restore error: %d", ret);

	ret = gcip_thermal_restore_on_powering(etdev->thermal);
	if (ret)
		etdev_warn(etdev, "thermal restore error: %d", ret);

	ret = edgetpu_kci_set_device_properties(etdev->etkci, &etdev->device_prop);
	if (ret)
		dev_warn(etdev->dev, "Failed to pass device_prop to fw: %d\n", ret);

	/* Set debug dump buffer in FW */
	edgetpu_get_debug_dump(etdev, 0);
	return 0;
}

/*
 * Do gcip_pm_get() but prevent it from running the loaded firmware.
 *
 * On success, caller must later call gcip_pm_put() to decrease the reference count.
 *
 * Caller holds firmware lock.
 */
static int edgetpu_firmware_pm_get(struct edgetpu_firmware *et_fw)
{
	enum gcip_fw_status prev = et_fw->p->status;
	int ret;

	/* Prevent platform-specific code from trying to run the previous firmware */
	et_fw->p->status = GCIP_FW_LOADING;
	etdev_dbg(et_fw->etdev, "Requesting power up for firmware run\n");
	ret = gcip_pm_get(et_fw->etdev->pm);
	if (ret)
		et_fw->p->status = prev;
	return ret;
}

static void edgetpu_firmware_set_loading(struct edgetpu_firmware *et_fw)
{
	struct edgetpu_dev *etdev = et_fw->etdev;

	mutex_lock(&etdev->state_lock);
	etdev->state = ETDEV_STATE_FWLOADING;
	mutex_unlock(&etdev->state_lock);

	et_fw->p->status = GCIP_FW_LOADING;
}

/* Set firmware and etdev state according to @ret, which can be an errno or 0. */
static void edgetpu_firmware_set_state(struct edgetpu_firmware *et_fw, int ret)
{
	struct edgetpu_dev *etdev = et_fw->etdev;

	et_fw->p->status = ret ? GCIP_FW_INVALID : GCIP_FW_VALID;

	mutex_lock(&etdev->state_lock);
	if (ret == -EIO)
		etdev->state = ETDEV_STATE_BAD; /* f/w handshake error */
	else if (ret)
		etdev->state = ETDEV_STATE_NOFW; /* other errors */
	else
		etdev->state = ETDEV_STATE_GOOD; /* f/w handshake success */
	mutex_unlock(&etdev->state_lock);
}

uint32_t
edgetpu_firmware_get_cl(struct edgetpu_firmware *et_fw)
{
	return et_fw->p->fw_info.fw_changelist;
}

uint64_t
edgetpu_firmware_get_build_time(struct edgetpu_firmware *et_fw)
{
	return et_fw->p->fw_info.fw_build_time;
}

/*
 * Try edgetpu_firmware_lock() if it's not locked yet.
 *
 * Returns 1 if the lock is acquired successfully, 0 otherwise.
 */
int edgetpu_firmware_trylock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return 1;
	return mutex_trylock(&et_fw->p->fw_desc_lock);
}

/*
 * Grab firmware lock to protect against firmware state changes.
 * Locks out firmware loading / unloading while caller performs ops that are
 * incompatible with a change in firmware status.  Does not care whether or not
 * the device is joined to a group.
 */
int edgetpu_firmware_lock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return -EINVAL;
	mutex_lock(&et_fw->p->fw_desc_lock);
	return 0;
}

/* Drop f/w lock, let any pending firmware load proceed. */
void edgetpu_firmware_unlock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return;
	mutex_unlock(&et_fw->p->fw_desc_lock);
}

/*
 * Lock firmware for loading.  Disallow group join for device during load.
 * Failed if device is already joined to a group and is in use.
 */
static int edgetpu_firmware_load_lock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw) {
		etdev_err(
			etdev,
			"Cannot load firmware when no loader is available\n");
		return -EINVAL;
	}
	mutex_lock(&et_fw->p->fw_desc_lock);

	/* Disallow group join while loading, fail if already joined */
	if (!edgetpu_set_group_join_lockout(etdev, true)) {
		etdev_err(
			etdev,
			"Cannot load firmware because device is in use");
		mutex_unlock(&et_fw->p->fw_desc_lock);
		return -EBUSY;
	}
	return 0;
}

/* Unlock firmware after lock held for loading, re-allow group join. */
static void edgetpu_firmware_load_unlock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw) {
		etdev_dbg(etdev,
			  "Unlock firmware when no loader available\n");
		return;
	}
	edgetpu_set_group_join_lockout(etdev, false);
	mutex_unlock(&et_fw->p->fw_desc_lock);
}

static int edgetpu_firmware_run_locked(struct edgetpu_firmware *et_fw,
				       const char *name,
				       enum edgetpu_firmware_flags flags)
{
	const struct edgetpu_firmware_chip_data *chip_fw = et_fw->p->chip_fw;
	struct edgetpu_dev *etdev = et_fw->etdev;
	struct edgetpu_firmware_desc new_fw_desc;
	int ret;

	edgetpu_firmware_set_loading(et_fw);
	edgetpu_sw_wdt_stop(et_fw->etdev);
	memset(&new_fw_desc, 0, sizeof(new_fw_desc));
	ret = edgetpu_firmware_load_locked(et_fw, &new_fw_desc, name, flags);
	if (ret)
		goto out_failed;

	etdev_dbg(etdev, "run fw %s flags=%#x", name, flags);
	if (chip_fw->prepare_run) {
		ret = chip_fw->prepare_run(et_fw, &new_fw_desc.buf);
		if (ret)
			goto out_unload_new_fw;
	}

	/*
	 * Previous firmware buffer is not used anymore when the CPU runs on
	 * new firmware buffer. Unload this before et_fw->p->fw_buf is
	 * overwritten by new buffer information.
	 */
	edgetpu_firmware_unload_locked(et_fw, &et_fw->p->fw_desc);
	et_fw->p->fw_desc = new_fw_desc;
	ret = edgetpu_firmware_handshake(et_fw);
	if (!ret)
		edgetpu_sw_wdt_start(et_fw->etdev);
	edgetpu_firmware_set_state(et_fw, ret);
	/* If previous firmware was metrics v1-only reset that flag and probe this again. */
	if (etdev->usage_stats)
		etdev->usage_stats->ustats.version = EDGETPU_USAGE_METRIC_VERSION;
	return ret;

out_unload_new_fw:
	edgetpu_firmware_unload_locked(et_fw, &new_fw_desc);
out_failed:
	edgetpu_firmware_set_state(et_fw, ret);
	return ret;
}

int edgetpu_firmware_run(struct edgetpu_dev *etdev, const char *name,
			 enum edgetpu_firmware_flags flags)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;
	ret = edgetpu_firmware_load_lock(etdev);
	if (ret) {
		etdev_err(etdev, "%s: lock failed (%d)\n", __func__, ret);
		return ret;
	}
	/* will be overwritten when we successfully parse the f/w header */
	etdev->fw_version.kci_version = EDGETPU_INVALID_KCI_VERSION;
	ret = edgetpu_firmware_pm_get(et_fw);
	if (!ret) {
		ret = edgetpu_firmware_run_locked(et_fw, name, flags);
		gcip_pm_put(etdev->pm);
	}

	edgetpu_firmware_load_unlock(etdev);

	return ret;
}

int edgetpu_firmware_run_default_locked(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	const char *run_firmware_name =
		et_fw->p->chip_fw->default_firmware_name;

	if (firmware_name && *firmware_name)
		run_firmware_name = firmware_name;

	return edgetpu_firmware_run_locked(etdev->firmware, run_firmware_name,
					   FW_DEFAULT);
}

int edgetpu_firmware_run_default(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	const char *run_firmware_name =
		et_fw->p->chip_fw->default_firmware_name;

	if (firmware_name && *firmware_name)
		run_firmware_name = firmware_name;

	return edgetpu_firmware_run(etdev, run_firmware_name, FW_DEFAULT);
}

bool edgetpu_firmware_is_loading(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw && et_fw->p->status == GCIP_FW_LOADING;
}

/* Caller must hold firmware lock. */
enum gcip_fw_status edgetpu_firmware_status_locked(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return GCIP_FW_INVALID;
	return et_fw->p->status;
}

/* Caller must hold firmware lock. For unit tests. */
void edgetpu_firmware_set_status_locked(struct edgetpu_dev *etdev, enum gcip_fw_status status)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (et_fw)
		et_fw->p->status = status;
}

/* Caller must hold firmware lock for loading. */
int edgetpu_firmware_restart_locked(struct edgetpu_dev *etdev, bool force_reset)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	const struct edgetpu_firmware_chip_data *chip_fw = et_fw->p->chip_fw;
	int ret = -1;

	edgetpu_firmware_set_loading(et_fw);
	edgetpu_sw_wdt_stop(etdev);
	/*
	 * Try restarting the firmware first, fall back to normal firmware start
	 * if this fails.
	 */
	if (chip_fw->restart)
		ret = chip_fw->restart(et_fw, force_reset);
	if (ret && chip_fw->prepare_run) {
		ret = chip_fw->prepare_run(et_fw, &et_fw->p->fw_desc.buf);
		if (ret)
			goto out;
	}
	ret = edgetpu_firmware_handshake(et_fw);
	if (!ret)
		edgetpu_sw_wdt_start(etdev);
out:
	edgetpu_firmware_set_state(et_fw, ret);
	return ret;
}

ssize_t edgetpu_firmware_get_name(struct edgetpu_dev *etdev, char *buf,
				  size_t buflen)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;
	const char *fw_name;

	if (!et_fw)
		goto fw_none;

	mutex_lock(&et_fw->p->fw_desc_lock);
	if (edgetpu_firmware_status_locked(etdev) != GCIP_FW_VALID)
		goto unlock_fw_none;
	fw_name = et_fw->p->fw_desc.buf.name;
	if (!fw_name)
		goto unlock_fw_none;
	ret = scnprintf(buf, buflen, "%s\n", fw_name);
	mutex_unlock(&et_fw->p->fw_desc_lock);
	return ret;

unlock_fw_none:
	mutex_unlock(&et_fw->p->fw_desc_lock);
fw_none:
	return scnprintf(buf, buflen, "[none]\n");
}

static ssize_t load_firmware_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);

	return edgetpu_firmware_get_name(etdev, buf, PAGE_SIZE);
}

static ssize_t load_firmware_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;
	char *name;

	if (!et_fw)
		return -ENODEV;

	name = edgetpu_fwutil_name_from_attr_buf(buf);
	if (IS_ERR(name))
		return PTR_ERR(name);

	etdev_info(etdev, "loading firmware %s\n", name);
	ret = edgetpu_firmware_run(etdev, name, 0);

	kfree(name);

	if (ret)
		return ret;
	return count;
}

static DEVICE_ATTR_RW(load_firmware);

static ssize_t firmware_type_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;
	ret = scnprintf(buf, PAGE_SIZE, "%s\n",
			gcip_fw_flavor_str(et_fw->p->fw_info.fw_flavor));
	return ret;
}
static DEVICE_ATTR_RO(firmware_type);

static ssize_t firmware_version_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;

	if (etdev->fw_version.kci_version == EDGETPU_INVALID_KCI_VERSION)
		ret = -ENODATA;
	else
		ret = scnprintf(buf, PAGE_SIZE, "%u.%u vii=%u kci=%u cl=%u\n",
				etdev->fw_version.major_version,
				etdev->fw_version.minor_version,
				etdev->fw_version.vii_version,
				etdev->fw_version.kci_version,
				et_fw->p->fw_info.fw_changelist);
	return ret;
}
static DEVICE_ATTR_RO(firmware_version);

static struct attribute *dev_attrs[] = {
	&dev_attr_load_firmware.attr,
	&dev_attr_firmware_type.attr,
	&dev_attr_firmware_version.attr,
	NULL,
};

static const struct attribute_group edgetpu_firmware_attr_group = {
	.attrs = dev_attrs,
};

void edgetpu_firmware_watchdog_restart(struct edgetpu_dev *etdev)
{
	int ret;
	struct edgetpu_firmware *et_fw = etdev->firmware;

	/* Don't attempt f/w restart if device is off. */
	if (!gcip_pm_is_powered(etdev->pm))
		return;

	/*
	 * Zero the FW state of open mailboxes so that when the runtime releases
	 * groups the CLOSE_DEVICE KCIs won't be sent.
	 */
	edgetpu_handshake_clear_fw_state(&etdev->mailbox_manager->open_devices);

	/* Another procedure is loading the firmware, let it do the work. */
	if (edgetpu_firmware_is_loading(etdev))
		return;

	/* edgetpu_firmware_lock() here never fails */
	edgetpu_firmware_lock(etdev);

	ret = edgetpu_firmware_pm_get(et_fw);
	if (!ret) {
		ret = edgetpu_firmware_restart_locked(etdev, true);
		gcip_pm_put(etdev->pm);
	}
	edgetpu_firmware_unlock(etdev);
}

int edgetpu_firmware_create(struct edgetpu_dev *etdev,
			    const struct edgetpu_firmware_chip_data *chip_fw)
{
	struct edgetpu_firmware *et_fw;
	int ret;

	if (etdev->firmware)
		return -EBUSY;

	et_fw = kzalloc(sizeof(*et_fw), GFP_KERNEL);
	if (!et_fw)
		return -ENOMEM;
	et_fw->etdev = etdev;

	et_fw->p = kzalloc(sizeof(*et_fw->p), GFP_KERNEL);
	if (!et_fw->p) {
		ret = -ENOMEM;
		goto out_kfree_et_fw;
	}
	et_fw->p->chip_fw = chip_fw;

	mutex_init(&et_fw->p->fw_desc_lock);

	ret = device_add_group(etdev->dev, &edgetpu_firmware_attr_group);
	if (ret)
		goto out_kfree_et_fw_p;

	if (chip_fw->after_create) {
		ret = chip_fw->after_create(et_fw);
		if (ret) {
			etdev_dbg(etdev,
				  "%s: after create handler failed: %d\n",
				  __func__, ret);
			goto out_device_remove_group;
		}
	}

	etdev->firmware = et_fw;
	ret = edgetpu_sw_wdt_create(etdev, EDGETPU_ACTIVE_DEV_BEAT_MS,
				    EDGETPU_DORMANT_DEV_BEAT_MS);
	if (ret)
		etdev_warn(etdev, "Failed to create software watchdog\n");
	return 0;

out_device_remove_group:
	device_remove_group(etdev->dev, &edgetpu_firmware_attr_group);
out_kfree_et_fw_p:
	kfree(et_fw->p);
out_kfree_et_fw:
	kfree(et_fw);
	return ret;
}

void edgetpu_firmware_destroy(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	const struct edgetpu_firmware_chip_data *chip_fw;

	if (!et_fw)
		return;
	edgetpu_sw_wdt_destroy(etdev);

	if (et_fw->p) {
		chip_fw = et_fw->p->chip_fw;
		/*
		 * Platform specific implementation, which includes stop
		 * running firmware.
		 */
		if (chip_fw->before_destroy)
			chip_fw->before_destroy(et_fw);
	}

	device_remove_group(etdev->dev, &edgetpu_firmware_attr_group);

	if (et_fw->p) {
		mutex_lock(&et_fw->p->fw_desc_lock);
		edgetpu_firmware_unload_locked(et_fw, &et_fw->p->fw_desc);
		mutex_unlock(&et_fw->p->fw_desc_lock);
	}

	etdev->firmware = NULL;

	kfree(et_fw->p);
	kfree(et_fw);
}

/* debugfs mappings dump */
void edgetpu_firmware_mappings_show(struct edgetpu_dev *etdev,
				    struct seq_file *s)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct edgetpu_firmware_buffer *fw_buf;
	unsigned long iova;

	if (!et_fw)
		return;
	fw_buf = &et_fw->p->fw_desc.buf;
	if (!fw_buf->vaddr)
		return;
	iova = edgetpu_chip_firmware_iova(etdev);
	seq_printf(s, "  %#lx %lu fw - %pad\n", iova,
		   DIV_ROUND_UP(fw_buf->alloc_size, PAGE_SIZE),
		   &fw_buf->dma_addr);
}
