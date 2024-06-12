// SPDX-License-Identifier: GPL-2.0
/*
 * Common EdgeTPU mobile power management support
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/gsa/gsa_tpu.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <gcip/gcip-thermal.h>

#include "edgetpu-config.h"
#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-thermal.h"
#include "mobile-firmware.h"
#include "mobile-pm.h"

#include "edgetpu-pm.c"
#include "edgetpu-soc.h"

#define BLOCK_DOWN_RETRY_TIMES 1000
#define BLOCK_DOWN_MIN_DELAY_US 1000
#define BLOCK_DOWN_MAX_DELAY_US 1500

enum edgetpu_pwr_state edgetpu_active_states[EDGETPU_NUM_STATES] = {
	TPU_ACTIVE_MIN, TPU_ACTIVE_ULTRA_LOW, TPU_ACTIVE_VERY_LOW, TPU_ACTIVE_SUB_LOW,
	TPU_ACTIVE_LOW, TPU_ACTIVE_MEDIUM,    TPU_ACTIVE_NOM,
};

uint32_t *edgetpu_states_display = edgetpu_active_states;

static int mobile_pwr_state_set_locked(struct edgetpu_mobile_platform_dev *etmdev, u64 val)
{
	int ret;
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;
	struct edgetpu_dev *etdev = &etmdev->edgetpu_dev;
	struct device *dev = etdev->dev;

	dev_dbg(dev, "Power state to %llu\n", val);

	if (val > TPU_OFF && (!platform_pwr->is_block_down || platform_pwr->is_block_down(etdev))) {
		ret = pm_runtime_get_sync(dev);
		if (ret) {
			pm_runtime_put_noidle(dev);
			dev_err(dev, "pm_runtime_get_sync returned %d\n", ret);
			return ret;
		}
	}

	ret = edgetpu_soc_pm_set_rate((unsigned long)val);
	if (ret) {
		dev_err(dev, "error setting tpu state: %d\n", ret);
		pm_runtime_put_sync(dev);
		return ret;
	}

	if (val == TPU_OFF &&
	    (!platform_pwr->is_block_down || !platform_pwr->is_block_down(etdev))) {
		ret = pm_runtime_put_sync(dev);
		if (ret) {
			dev_err(dev, "%s: pm_runtime_put_sync returned %d\n", __func__, ret);
			return ret;
		}
	}

	return ret;
}

static int mobile_pwr_state_get_locked(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	struct device *dev = etdev->dev;

	*val = edgetpu_soc_pm_get_rate(etdev, 0);
	dev_dbg(dev, "current tpu state: %llu\n", *val);

	return 0;
}

static int mobile_pwr_state_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;
	int ret = 0;

	mutex_lock(&platform_pwr->state_lock);
	platform_pwr->requested_state = val;
	if (val >= platform_pwr->min_state)
		ret = mobile_pwr_state_set_locked(etmdev, val);
	mutex_unlock(&platform_pwr->state_lock);
	return ret;
}

static int mobile_pwr_state_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;
	int ret;

	mutex_lock(&platform_pwr->state_lock);
	ret = mobile_pwr_state_get_locked(etdev, val);
	mutex_unlock(&platform_pwr->state_lock);
	return ret;
}

static int mobile_min_pwr_state_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;
	int ret = 0;

	mutex_lock(&platform_pwr->state_lock);
	platform_pwr->min_state = val;
	if (val >= platform_pwr->requested_state)
		ret = mobile_pwr_state_set_locked(etmdev, val);
	mutex_unlock(&platform_pwr->state_lock);
	return ret;
}

static int mobile_min_pwr_state_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;

	mutex_lock(&platform_pwr->state_lock);
	*val = platform_pwr->min_state;
	mutex_unlock(&platform_pwr->state_lock);
	return 0;
}

static int mobile_pwr_policy_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;
	int ret = -EAGAIN;

	mutex_lock(&platform_pwr->policy_lock);

	if (!gcip_pm_get_if_powered(etdev->pm, false)) {
		ret = edgetpu_thermal_set_rate(etdev, val);
		gcip_pm_put(etdev->pm);
	}

	if (ret) {
		dev_err(etmdev->edgetpu_dev.dev,
			"unable to set policy %lld (ret %d)\n", val, ret);
		mutex_unlock(&platform_pwr->policy_lock);
		return ret;
	}

	platform_pwr->curr_policy = val;
	mutex_unlock(&platform_pwr->policy_lock);
	return 0;
}

static int mobile_pwr_policy_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;

	mutex_lock(&platform_pwr->policy_lock);
	*val = platform_pwr->curr_policy;
	mutex_unlock(&platform_pwr->policy_lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_pwr_policy, mobile_pwr_policy_get, mobile_pwr_policy_set,
			"%llu\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_pwr_state, mobile_pwr_state_get, mobile_pwr_state_set, "%llu\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_min_pwr_state, mobile_min_pwr_state_get, mobile_min_pwr_state_set,
			"%llu\n");

static int mobile_power_down(void *data);

static int mobile_power_up(void *data)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;
	int ret;

	if (gcip_thermal_is_device_suspended(etdev->thermal)) {
		etdev_warn_ratelimited(etdev,
				       "power up rejected due to device thermal limit exceeded");
		return -EAGAIN;
	}

	if (platform_pwr->is_block_down) {
		int times = 0;

		do {
			if (platform_pwr->is_block_down(etdev))
				break;
			usleep_range(BLOCK_DOWN_MIN_DELAY_US, BLOCK_DOWN_MAX_DELAY_US);
		} while (++times < BLOCK_DOWN_RETRY_TIMES);
		if (times >= BLOCK_DOWN_RETRY_TIMES && !platform_pwr->is_block_down(etdev))
			return -EAGAIN;
	}

	etdev_info(etdev, "Powering up\n");

	ret = pm_runtime_get_sync(etdev->dev);
	if (ret) {
		pm_runtime_put_noidle(etdev->dev);
		etdev_err(etdev, "pm_runtime_get_sync returned %d\n", ret);
		return ret;
	}

	if (platform_pwr->lpm_up)
		platform_pwr->lpm_up(etdev);

	edgetpu_chip_init(etdev);

	if (etdev->etkci) {
		etdev_dbg(etdev, "Resetting KCI\n");
		edgetpu_kci_reinit(etdev->etkci);
	}
	if (etdev->mailbox_manager) {
		etdev_dbg(etdev, "Resetting (VII/external) mailboxes\n");
		edgetpu_mailbox_reset_mailboxes(etdev->mailbox_manager);
	}

	if (!etdev->firmware)
		goto out;

	/*
	 * Why this function uses edgetpu_firmware_*_locked functions without explicitly holding
	 * edgetpu_firmware_lock:
	 *
	 * gcip_pm_get() is called in two scenarios - one is when the firmware loading is
	 * attempting, another one is when the user-space clients need the device be powered
	 * (usually through acquiring the wakelock).
	 *
	 * For the first scenario edgetpu_firmware_is_loading() below shall return true.
	 * For the second scenario we are indeed called without holding the firmware lock, but the
	 * firmware loading procedures (i.e. the first scenario) always call gcip_pm_get() before
	 * changing the firmware state, and gcip_pm_get() is blocked until this function
	 * finishes. In short, we are protected by the PM lock.
	 */

	if (edgetpu_firmware_is_loading(etdev))
		goto out;

	/* attempt firmware run */
	switch (edgetpu_firmware_status_locked(etdev)) {
	case GCIP_FW_VALID:
		ret = edgetpu_firmware_restart_locked(etdev, false);
		break;
	case GCIP_FW_INVALID:
		ret = edgetpu_firmware_run_default_locked(etdev);
		break;
	default:
		break;
	}

	if (ret)
		mobile_power_down(etdev);
	else if (platform_pwr->post_fw_start)
		platform_pwr->post_fw_start(etdev);

out:
	if (!ret)
		edgetpu_mailbox_restore_active_mailbox_queues(etdev);

	return ret;
}

static void mobile_firmware_down(struct edgetpu_dev *etdev)
{
	int ret = edgetpu_kci_shutdown(etdev->etkci);

	if (ret)
		etdev_warn(etdev, "firmware shutdown failed: %d", ret);
}

static int mobile_power_down(void *data)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;
	int res = 0;
	int min_state = platform_pwr->min_state;

	etdev_info(etdev, "Powering down\n");

	edgetpu_sw_wdt_stop(etdev);

	if (min_state >= MIN_ACTIVE_STATE) {
		etdev_info(etdev, "Power down skipped due to min state = %d\n", min_state);
		return 0;
	}

	if (platform_pwr->is_block_down && platform_pwr->is_block_down(etdev)) {
		etdev_dbg(etdev, "Device already off, skipping shutdown\n");
		return 0;
	}

	if (edgetpu_firmware_status_locked(etdev) == GCIP_FW_VALID) {
		etdev_dbg(etdev, "Power down with valid firmware, device state = %d\n",
			  etdev->state);
		if (etdev->state == ETDEV_STATE_GOOD) {
			/* Update usage stats before we power off fw. */
			edgetpu_kci_update_usage_locked(etdev);
			mobile_firmware_down(etdev);
			/* Ensure firmware is completely off */
			if (platform_pwr->lpm_down)
				platform_pwr->lpm_down(etdev);
			/* Indicate firmware is no longer running */
			etdev->state = ETDEV_STATE_NOFW;
		}
		edgetpu_kci_cancel_work_queues(etdev->etkci);
	}

	if (etdev->firmware) {
		res = edgetpu_mobile_firmware_reset_cpu(etdev, true);

		/* TODO(b/198181290): remove -EIO once gsaproxy wakelock is implemented */
		if (res == -EAGAIN || res == -EIO)
			return -EAGAIN;
		if (res < 0)
			etdev_warn(etdev, "CPU reset request failed (%d)\n", res);
	}

	res = pm_runtime_put_sync(etdev->dev);
	if (res) {
		etdev_err(etdev, "pm_runtime_put_sync returned %d\n", res);
		return res;
	}

	edgetpu_soc_pm_power_down(etdev);

	/*
	 * It should be impossible that power_down() is called when secure_client is set.
	 * Non-null secure_client implies ext mailbox is acquired, which implies wakelock is
	 * acquired.
	 * Clear the state here just in case.
	 */
	etmdev->secure_client = NULL;

	return 0;
}

static int mobile_pm_after_create(void *data)
{
	int ret;
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct device *dev = etdev->dev;
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret) {
		dev_err(dev, "pm_runtime_get_sync returned %d\n", ret);
		goto err_pm_runtime_put;
	}

	mutex_init(&platform_pwr->policy_lock);
	mutex_init(&platform_pwr->state_lock);

	platform_pwr->debugfs_dir = debugfs_create_dir("power", edgetpu_fs_debugfs_dir());
	if (IS_ERR_OR_NULL(platform_pwr->debugfs_dir)) {
		dev_warn(etdev->dev, "Failed to create debug FS power");
		/* don't fail the procedure on debug FS creation fails */
	} else {
		debugfs_create_file("state", 0660, platform_pwr->debugfs_dir, etdev,
				    &fops_tpu_pwr_state);
		debugfs_create_file("min_state", 0660, platform_pwr->debugfs_dir, etdev,
				    &fops_tpu_min_pwr_state);
		debugfs_create_file("policy", 0660, platform_pwr->debugfs_dir, etdev,
				    &fops_tpu_pwr_policy);
	}

	ret = edgetpu_soc_pm_init(etdev);
	if (ret)
		goto err_debugfs_remove;

	return 0;

err_debugfs_remove:
	debugfs_remove_recursive(platform_pwr->debugfs_dir);

err_pm_runtime_put:
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);

	return ret;
}

static void mobile_pm_before_destroy(void *data)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;

	debugfs_remove_recursive(platform_pwr->debugfs_dir);
	pm_runtime_disable(etdev->dev);
	edgetpu_soc_pm_exit(etdev);
}

static struct edgetpu_pm_handlers mobile_pm_handlers = {
	.after_create = mobile_pm_after_create,
	.before_destroy = mobile_pm_before_destroy,
	.power_up = mobile_power_up,
	.power_down = mobile_power_down,
};

int edgetpu_mobile_pm_create(struct edgetpu_dev *etdev)
{
	return edgetpu_pm_create(etdev, &mobile_pm_handlers);
}

void edgetpu_mobile_pm_destroy(struct edgetpu_dev *etdev)
{
	edgetpu_pm_destroy(etdev);
}
