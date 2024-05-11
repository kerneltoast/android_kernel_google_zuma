// SPDX-License-Identifier: GPL-2.0
/*
 * EdgeTPU power management interface.
 *
 * Copyright (C) 2020 Google, Inc.
 */

#include <linux/mutex.h>

#include <gcip/gcip-pm.h>

#include "edgetpu-internal.h"
#include "edgetpu-pm.h"
#include "edgetpu-wakelock.h"

int edgetpu_pm_create(struct edgetpu_dev *etdev,
		      const struct edgetpu_pm_handlers *handlers)
{
	const struct gcip_pm_args args = {
		.dev = etdev->dev,
		.data = etdev,
		.after_create = handlers->after_create,
		.before_destroy = handlers->before_destroy,
		.power_up = handlers->power_up,
		.power_down = handlers->power_down,
	};
	struct gcip_pm *pm;

	if (etdev->pm) {
		dev_err(etdev->dev,
			"Refusing to replace existing PM interface\n");
		return -EEXIST;
	}

	pm = gcip_pm_create(&args);
	if (IS_ERR(pm))
		return PTR_ERR(pm);

	etdev->pm = pm;

	return 0;
}

void edgetpu_pm_destroy(struct edgetpu_dev *etdev)
{
	gcip_pm_destroy(etdev->pm);
	etdev->pm = NULL;
}

static int __maybe_unused edgetpu_pm_suspend(struct device *dev)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct gcip_pm *pm = etdev->pm;
	struct edgetpu_list_device_client *lc;
	int count;

	if (unlikely(!pm))
		return 0;

	if (!gcip_pm_trylock(pm)) {
		etdev_warn_ratelimited(etdev, "cannot suspend during power state transition\n");
		return -EAGAIN;
	}

	count = gcip_pm_get_count(pm);
	gcip_pm_unlock(etdev->pm);

	if (!count) {
		etdev_info_ratelimited(etdev, "suspended\n");
		return 0;
	}

	etdev_warn_ratelimited(etdev, "cannot suspend with power up count = %d\n", count);

	if (!mutex_trylock(&etdev->clients_lock))
		return -EAGAIN;
	for_each_list_device_client(etdev, lc) {
		if (!lc->client->wakelock->req_count)
			continue;
		etdev_warn_ratelimited(etdev,
				       "client pid %d tgid %d count %d\n",
				       lc->client->pid,
				       lc->client->tgid,
				       lc->client->wakelock->req_count);
	}
	mutex_unlock(&etdev->clients_lock);
	return -EAGAIN;
}

static int __maybe_unused edgetpu_pm_resume(struct device *dev)
{
	return 0;
}

const struct dev_pm_ops edgetpu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(edgetpu_pm_suspend, edgetpu_pm_resume)
};
