/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EdgeTPU power management interface.
 *
 * Copyright (C) 2020 Google, Inc.
 */

#ifndef __EDGETPU_PM_H__
#define __EDGETPU_PM_H__

#include <linux/pm.h>

#include "edgetpu-internal.h"

struct edgetpu_pm_handlers {
	/* For initial setup after the interface initialized */
	int (*after_create)(void *data);
	/* For clean-up before the interface is destroyed */
	void (*before_destroy)(void *data);
	/* Platform-specific power up. Nesting is handled at generic layer */
	int (*power_up)(void *data);
	/* Platform-specific power down. Nesting is handled at generic layer */
	int (*power_down)(void *data);
};

extern const struct dev_pm_ops edgetpu_pm_ops;

/* Initialize a power management interface for an edgetpu device */
int edgetpu_pm_create(struct edgetpu_dev *etdev,
		      const struct edgetpu_pm_handlers *handlers);

/*
 * Wrapper for chip-specific implementation.
 * Typically calls mobile_pm_create after initializing the platform_pwr struct.
 */
int edgetpu_chip_pm_create(struct edgetpu_dev *etdev);

/* Destroy the power management interface associated with an edgetpu device */
void edgetpu_pm_destroy(struct edgetpu_dev *etdev);

#endif /* __EDGETPU_PM_H__ */
