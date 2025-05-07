// SPDX-License-Identifier: GPL-2.0
/*
 * Rio platform device driver for the Google Edge TPU ML accelerator.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "edgetpu-config.h"
#include "edgetpu-internal.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"

#include "edgetpu-mobile-platform.c"

static const struct of_device_id edgetpu_of_match[] = {
	{
		.compatible = "google,edgetpu-gs301",
	},
	{
		.compatible = "google,edgetpu-zuma",
	},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, edgetpu_of_match);

static struct platform_driver edgetpu_platform_driver = {
	.probe = edgetpu_mobile_platform_probe,
	.remove = edgetpu_mobile_platform_remove,
	.driver = {
			.name = "edgetpu_platform",
			.of_match_table = edgetpu_of_match,
			.pm = &edgetpu_pm_ops,
		},
};

static int __init edgetpu_platform_init(void)
{
	int ret;

	ret = edgetpu_init();
	if (ret)
		return ret;
	return platform_driver_register(&edgetpu_platform_driver);
}

static void __exit edgetpu_platform_exit(void)
{
	platform_driver_unregister(&edgetpu_platform_driver);
	edgetpu_exit();
}

MODULE_DESCRIPTION("Google Edge TPU platform driver");
MODULE_LICENSE("GPL v2");
module_init(edgetpu_platform_init);
module_exit(edgetpu_platform_exit);
MODULE_FIRMWARE(EDGETPU_DEFAULT_FIRMWARE_NAME);
