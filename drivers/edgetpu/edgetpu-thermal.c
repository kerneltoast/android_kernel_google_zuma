// SPDX-License-Identifier: GPL-2.0
/*
 * EdgeTPU thermal management.
 *
 * Copyright (C) 2023 Google LLC
 */

#include <linux/device.h>

#include <gcip/gcip-thermal.h>

#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-thermal.h"

static int __edgetpu_thermal_get_rate(void *data, unsigned long *rate)
{
	struct edgetpu_dev *etdev = data;
	long ret = edgetpu_soc_pm_get_rate(etdev, 0);

	if (ret < 0)
		return ret;

	*rate = ret;

	return 0;
}

static int __edgetpu_thermal_set_rate(void *data, unsigned long rate)
{
	struct edgetpu_dev *etdev = data;
	int ret;

	edgetpu_kci_block_bus_speed_control(etdev, true);

	ret = edgetpu_kci_notify_throttling(etdev, rate);
	if (ret)
		etdev_err_ratelimited(etdev, "Failed to notify FW about power rate %lu, error:%d",
				      rate, ret);

	edgetpu_kci_block_bus_speed_control(etdev, false);

	return ret;
}

int edgetpu_thermal_set_rate(struct edgetpu_dev *etdev, unsigned long rate)
{
	return __edgetpu_thermal_set_rate(etdev, rate);
}

static int __edgetpu_thermal_control(void *data, bool enable)
{
	return edgetpu_kci_thermal_control(data, enable);
}

int edgetpu_thermal_create(struct edgetpu_dev *etdev)
{
	const struct gcip_thermal_args args = {
		.dev = etdev->dev,
		.pm = edgetpu_gcip_pm(etdev),
		.dentry = edgetpu_fs_debugfs_dir(),
		.node_name = EDGETPU_COOLING_NAME,
		.type = EDGETPU_COOLING_TYPE,
		.data = etdev,
		.get_rate = __edgetpu_thermal_get_rate,
		.set_rate = __edgetpu_thermal_set_rate,
		.control = __edgetpu_thermal_control,
	};
	struct gcip_thermal *thermal = gcip_thermal_create(&args);

	if (IS_ERR(thermal))
		return PTR_ERR(thermal);

	etdev->thermal = thermal;
	edgetpu_soc_thermal_init(etdev);

	return 0;
}

void edgetpu_thermal_destroy(struct edgetpu_dev *etdev)
{
	edgetpu_soc_thermal_exit(etdev);
	gcip_thermal_destroy(etdev->thermal);
	etdev->thermal = NULL;
}
