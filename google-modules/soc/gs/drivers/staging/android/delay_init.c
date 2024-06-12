// SPDX-License-Identifier: GPL-2.0-only
/* delay_init.c
 *
 * This driver is designed to stall init so that we can probe more devices
 * before Android's second stage init starts.
 *
 * Copyright 2021 Google LLC
 */

#include <linux/delay.h>
#include <linux/module.h>

static int delay_ms = 2 * 1000;
MODULE_PARM_DESC(delay_ms, "Delay init for set number of milliseconds");
module_param(delay_ms, int, 0600);

static int __init init(void)
{
	if (delay_ms > 0)
		msleep(delay_ms);

	return 0;
}
module_init(init);

MODULE_SOFTDEP("pre: dwc3-exynos-usb");
MODULE_AUTHOR("Will McVicker <willmcvicker@google.com>");
MODULE_LICENSE("GPL");
