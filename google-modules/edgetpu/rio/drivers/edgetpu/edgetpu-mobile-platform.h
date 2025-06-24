/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common platform interfaces for mobile TPU chips.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __EDGETPU_MOBILE_PLATFORM_H__
#define __EDGETPU_MOBILE_PLATFORM_H__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "edgetpu-config.h"
#include "edgetpu-debug.h"
#include "edgetpu-internal.h"

#define to_mobile_dev(etdev) container_of(etdev, struct edgetpu_mobile_platform_dev, edgetpu_dev)

struct edgetpu_mobile_platform_dev {
	/* Generic edgetpu device */
	struct edgetpu_dev edgetpu_dev;
	/* subsystem coredump info struct */
	struct mobile_sscd_info sscd_info;
	/* Protects TZ Mailbox client pointer */
	struct mutex tz_mailbox_lock;
	/* TZ mailbox client */
	struct edgetpu_client *secure_client;

	/* Length of @mailbox_irq */
	int n_mailbox_irq;
	/* Array of mailbox IRQ numbers */
	int *mailbox_irq;
};

#endif /* __EDGETPU_MOBILE_PLATFORM_H__ */
