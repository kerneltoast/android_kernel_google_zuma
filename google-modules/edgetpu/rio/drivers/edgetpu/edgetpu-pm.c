// SPDX-License-Identifier: GPL-2.0-only
/*
 * EdgeTPU power management interface.
 *
 * Copyright (C) 2020-2025 Google LLC
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>

#include <gcip/gcip-pm.h>
#include <gcip/gcip-thermal.h>


#include "edgetpu-config.h"
#include "edgetpu-firmware.h"
#include "edgetpu-gsa.h"
#include "edgetpu-iif.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-thermal.h"
#include "edgetpu-wakelock.h"

#define BLOCK_DOWN_RETRY_TIMES 1000
#define BLOCK_DOWN_MIN_DELAY_US 1000
#define BLOCK_DOWN_MAX_DELAY_US 1500

/* For edgetpu_poll_block_off */
#define POLL_BLOCK_OFF_DELAY_US_MIN 200
#define POLL_BLOCK_OFF_DELAY_US_MAX 200
#define POLL_BLOCK_OFF_MAX_DELAY_COUNT 20

static bool edgetpu_always_on(void)
{
	return IS_ENABLED(CONFIG_EDGETPU_TEST) || EDGETPU_FEATURE_ALWAYS_ON;
}

static bool edgetpu_poll_block_off(struct edgetpu_dev *etdev)
{
	int timeout_cnt = 0;

	do {
		usleep_range(POLL_BLOCK_OFF_DELAY_US_MIN, POLL_BLOCK_OFF_DELAY_US_MAX);
		if (edgetpu_soc_pm_is_block_off(etdev))
			return true;
		timeout_cnt++;
	} while (timeout_cnt < POLL_BLOCK_OFF_MAX_DELAY_COUNT);

	return false;
}

/* Caller must hold pm->freq_limits_lock. */
static int mobile_pwr_update_freq_limits_locked(struct edgetpu_dev *etdev)
{
	int ret;

	ret = edgetpu_kci_set_freq_limits(etdev->etkci, etdev->pm->min_freq, etdev->pm->max_freq);
	switch (ret) {
	case GCIP_KCI_ERROR_OK:
		return 0;
	case GCIP_KCI_ERROR_INVALID_ARGUMENT:
		dev_err(etdev->dev,
			"No valid values within debugfs frequency limits: (%u, %u)\n",
			etdev->pm->min_freq, etdev->pm->max_freq);
		etdev->pm->min_freq = 0;
		etdev->pm->max_freq = 0;
		return -EINVAL;
	default:
		dev_err(etdev->dev, "Fw rejected frequency limits command with KCI err %d", ret);
		return -EIO;
	}
}

int edgetpu_pm_set_freq_limits(struct edgetpu_dev *etdev, u32 *min_freq, u32 *max_freq)
{
	bool limits_updated = false;
	int ret = 0;

	/*
	 * Need to hold pm lock to prevent races with power up/down when checking block state and
	 * sending the KCI command to update limits.
	 *
	 * Since power_up will also acquire freq_limits_lock to send initial limits, pm lock must be
	 * held first to avoid lock inversion.
	 */
	edgetpu_pm_lock(etdev);
	mutex_lock(&etdev->pm->freq_limits_lock);

	if (min_freq && *min_freq != etdev->pm->min_freq) {
		etdev->pm->min_freq = *min_freq;
		limits_updated = true;
	}

	if (max_freq && *max_freq != etdev->pm->max_freq) {
		etdev->pm->max_freq = *max_freq;
		limits_updated = true;
	}

	if (limits_updated && (edgetpu_always_on() || !edgetpu_soc_pm_is_block_off(etdev)))
		ret = mobile_pwr_update_freq_limits_locked(etdev);

	mutex_unlock(&etdev->pm->freq_limits_lock);
	edgetpu_pm_unlock(etdev);
	return ret;
}

static int edgetpu_pm_debugfs_state_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	mutex_lock(&etdev->pm->state_lock);
	*val = edgetpu_pm_get_count(etdev);
	mutex_unlock(&etdev->pm->state_lock);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_pwr_state, edgetpu_pm_debugfs_state_get, NULL, "%llu\n");

static int mobile_pwr_policy_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;
	int ret = -EAGAIN;

	mutex_lock(&etdev->pm->policy_lock);

	if (!edgetpu_pm_get_if_powered(etdev, false)) {
		ret = edgetpu_thermal_set_rate(etdev, val);
		edgetpu_pm_put(etdev);
	}

	if (ret) {
		dev_err(etdev->dev, "unable to set policy %lld (ret %d)\n", val, ret);
		mutex_unlock(&etdev->pm->policy_lock);
		return ret;
	}

	etdev->pm->curr_policy = val;
	mutex_unlock(&etdev->pm->policy_lock);
	return 0;
}

static int mobile_pwr_policy_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	mutex_lock(&etdev->pm->policy_lock);
	*val = etdev->pm->curr_policy;
	mutex_unlock(&etdev->pm->policy_lock);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_pwr_policy, mobile_pwr_policy_get, mobile_pwr_policy_set,
			 "%llu\n");

static int mobile_power_down(void *data);

static int mobile_power_up(void *data)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	int times = 0;
	int ret;

	if (gcip_thermal_is_device_suspended(etdev->thermal)) {
		etdev_warn_ratelimited(etdev,
				       "power up rejected due to device thermal limit exceeded");
		return -EAGAIN;
	}

	if (!edgetpu_always_on()) {
		do {
			if (edgetpu_poll_block_off(etdev))
				break;
			usleep_range(BLOCK_DOWN_MIN_DELAY_US, BLOCK_DOWN_MAX_DELAY_US);
		} while (++times < BLOCK_DOWN_RETRY_TIMES);
		if (times >= BLOCK_DOWN_RETRY_TIMES && !edgetpu_poll_block_off(etdev)) {
			etdev_err(etdev, "power up failed: device not in correct power state");
			edgetpu_soc_pm_dump_block_state(etdev);
			return -EAGAIN;
		}
	}

	etdev_info(etdev, "Powering up\n");

	ret = pm_runtime_get_sync(etdev->dev);
	if (ret) {
		pm_runtime_put_noidle(etdev->dev);
		etdev_err(etdev, "pm_runtime_get_sync returned %d\n", ret);
		return ret;
	}

	edgetpu_soc_pm_lpm_up(etdev);

	/* TODO(b/269374029) Do *_reinit() results need to be checked? */
	if (etdev->etkci) {
		etdev_dbg(etdev, "Resetting KCI\n");
		edgetpu_kci_reinit(etdev->etkci);
	}
	if (etdev->etikv) {
		etdev_dbg(etdev, "Resetting in-kernel VII\n");
		edgetpu_ikv_reinit(etdev->etikv);
	}
	if (etdev->etiif) {
		etdev_dbg(etdev, "Resetting IIF\n");
		edgetpu_iif_reinit_mailbox(etdev->etiif);
	}
	if (etdev->mailbox_manager) {
		etdev_dbg(etdev, "Resetting (VII/external) mailboxes\n");
		edgetpu_mailbox_reset_mailboxes(etdev->mailbox_manager);
	}

	if (!etdev->firmware)
		goto out;

	/* State is set to shutdown only when unloading the driver, firmware loader is shutdown. */
	if (etdev->state == ETDEV_STATE_SHUTDOWN)
		return 0;

	/*
	 * Why this function uses edgetpu_firmware_*_locked functions without explicitly holding
	 * edgetpu_firmware_lock:
	 *
	 * edgetpu_pm_get() is called in two scenarios - one is when the firmware loading is
	 * attempting, another one is when the user-space clients need the device be powered
	 * (usually through acquiring the wakelock).
	 *
	 * For the first scenario edgetpu_firmware_is_loading() below shall return true.
	 * For the second scenario we are indeed called without holding the firmware lock, but the
	 * firmware loading procedures (i.e. the first scenario) always call edgetpu_pm_get() before
	 * changing the firmware state, and edgetpu_pm_get() is blocked until this function
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
	else
		edgetpu_soc_pm_post_fw_start(etdev);

out:
	if (!ret) {
		edgetpu_mailbox_restore_active_mailbox_queues(etdev);
		mutex_lock(&etdev->pm->freq_limits_lock);
		/* Only send limits to FW if at least one has been set. */
		if (etdev->pm->min_freq || etdev->pm->max_freq)
			mobile_pwr_update_freq_limits_locked(etdev);
		mutex_unlock(&etdev->pm->freq_limits_lock);
	}

	return ret;
}

static void mobile_firmware_down(struct edgetpu_dev *etdev)
{
	int ret = 0;

	if (!edgetpu_always_on())
		ret = edgetpu_kci_shutdown(etdev->etkci);

	if (!ret)
		return;

	etdev_warn(etdev, "firmware shutdown failed (%d), resetting", ret);
	edgetpu_firmware_watchdog_restart(etdev, true);
	ret = edgetpu_kci_shutdown(etdev->etkci);
	if (ret)
		etdev_warn(etdev, "firmware shutdown retry failed (%d)", ret);
}

static int mobile_power_down(void *data)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	int res = 0;

	etdev_info(etdev, "Powering down\n");

	edgetpu_sw_wdt_stop(etdev);

	if (!edgetpu_always_on() && edgetpu_soc_pm_is_block_off(etdev)) {
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
			if (!edgetpu_always_on())
				edgetpu_soc_pm_lpm_down(etdev);
			/* Indicate firmware is no longer running */
			etdev->state = ETDEV_STATE_NOFW;
		}
		edgetpu_kci_cancel_work_queues(etdev->etkci);
	}

	/*
	 * If this function was called due to a failed power-up, the CPU may never have booted.
	 * In that case, it's not necessary to attempt to put the CPU into reset here and the block
	 * should be powered-down.
	 *
	 * This also ensures that a failed, unnecessary, CPU reset request failure does not keep the
	 * block powered-up.
	 */
	if (etdev->firmware && etdev->firmware_cpu_on) {
		res = edgetpu_firmware_reset_cpu(etdev, true);

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
	struct device *dev = etdev->dev;

	devm_pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret) {
		dev_err(dev, "pm_runtime_get_sync returned %d\n", ret);
		goto err_pm_runtime_put;
	}

	mutex_init(&etdev->pm->policy_lock);
	mutex_init(&etdev->pm->state_lock);
	mutex_init(&etdev->pm->freq_limits_lock);

	etdev->pm->debugfs_dir = debugfs_create_dir("power", edgetpu_fs_debugfs_dir());
	if (IS_ERR_OR_NULL(etdev->pm->debugfs_dir)) {
		dev_warn(etdev->dev, "Failed to create debug FS power");
		/* don't fail the procedure on debug FS creation fails */
	} else {
		debugfs_create_file("state", 0660, etdev->pm->debugfs_dir, etdev,
				    &fops_tpu_pwr_state);
		debugfs_create_file("policy", 0660, etdev->pm->debugfs_dir, etdev,
				    &fops_tpu_pwr_policy);
	}

	ret = edgetpu_soc_pm_init(etdev);
	if (ret)
		goto err_debugfs_remove;

	return 0;

err_debugfs_remove:
	debugfs_remove_recursive(etdev->pm->debugfs_dir);

err_pm_runtime_put:
	pm_runtime_put_noidle(dev);

	return ret;
}

static void debugfs_wakelock_remove(struct edgetpu_dev *etdev)
{
	if (!etdev->debugfs_wakelock_client)
		return;

	edgetpu_client_remove(etdev->debugfs_wakelock_client);
	etdev->debugfs_wakelock_client = NULL;
}

static void mobile_pm_before_destroy(void *data)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;

	/* If debugfs wakelock client exists, release its wakelocks now. */
	debugfs_wakelock_remove(etdev);
	debugfs_remove_recursive(etdev->pm->debugfs_dir);
	edgetpu_soc_pm_exit(etdev);
}

int edgetpu_pm_create(struct edgetpu_dev *etdev)
{
	const struct gcip_pm_args args = {
		.dev = etdev->dev,
		.data = etdev,
		.after_create = mobile_pm_after_create,
		.before_destroy = mobile_pm_before_destroy,
		.power_up = mobile_power_up,
		.power_down =  mobile_power_down,
	};
	int ret = 0;

	if (etdev->pm) {
		dev_err(etdev->dev,
			"Refusing to replace existing PM interface\n");
		return -EEXIST;
	}

	etdev->pm = devm_kzalloc(etdev->dev, sizeof(*etdev->pm), GFP_KERNEL);
	if (!etdev->pm)
		return -ENOMEM;

	mutex_init(&etdev->pm->policy_lock);
	etdev->pm->curr_policy = etdev->max_active_state;
	etdev->pm->gpm = gcip_pm_create(&args);
	if (IS_ERR(etdev->pm->gpm)) {
		ret = PTR_ERR(etdev->pm->gpm);
		devm_kfree(etdev->dev, etdev->pm);
	}

	return ret;
}

void edgetpu_pm_destroy(struct edgetpu_dev *etdev)
{
	gcip_pm_destroy(etdev->pm->gpm);
	devm_kfree(etdev->dev, etdev->pm);
	etdev->pm = NULL;
}

static int __maybe_unused edgetpu_pm_suspend(struct device *dev)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_list_device_client *lc;
	int count;
	bool all_wakelocks_suspendable = true;
	int nonsuspend_req_count = 0;
	int suspendable_req_count = 0;

	if (!edgetpu_pm_trylock(etdev)) {
		etdev_warn_ratelimited(etdev, "cannot suspend during power state transition\n");
		return -EAGAIN;
	}

	count = edgetpu_pm_get_count(etdev);
	edgetpu_pm_unlock(etdev);

	if (!count) {
		etdev_info_ratelimited(etdev, "suspended\n");
		return 0;
	}

	if (!mutex_trylock(&etdev->clients_lock))
		return -EAGAIN;
	for_each_list_device_client(etdev, lc) {
		if (!lc->client->wakelock.req_count)
			continue;
		if (lc->client->wakelock.suspendable) {
			suspendable_req_count++;
			continue;
		}
		if (lc->client == etdev->debugfs_wakelock_client)
			etdev_warn_ratelimited(etdev,
					       "debugfs client count %d\n",
					       lc->client->wakelock.req_count);
		else
			etdev_warn_ratelimited(etdev,
					       "client pid %d tgid %d count %d\n",
					       lc->client->pid,
					       lc->client->tgid,
					       lc->client->wakelock.req_count);
		nonsuspend_req_count += lc->client->wakelock.req_count;
		all_wakelocks_suspendable = false;
	}
	mutex_unlock(&etdev->clients_lock);

	/*
	 * Check count in case a driver-issued power up (not a client wakelock) must block suspend.
	 */
	if (all_wakelocks_suspendable && count == suspendable_req_count) {
		etdev_info_ratelimited(etdev, "suspend allowed while powered\n");
		device_set_wakeup_path(etdev->dev);
		return 0;
	}

	etdev_warn_ratelimited(etdev,
			       "cannot suspend; power up count = %d, clients nonsusp=%d susp=%d\n",
			       count, nonsuspend_req_count, suspendable_req_count);
	return -EAGAIN;
}

static int __maybe_unused edgetpu_pm_resume(struct device *dev)
{
	return 0;
}

const struct dev_pm_ops edgetpu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(edgetpu_pm_suspend, edgetpu_pm_resume)
};
