/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Power management header for mobile chipsets.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __MOBILE_PM_H__
#define __MOBILE_PM_H__

#include "edgetpu-internal.h"

/*
 * Initialize a power management interface for an edgetpu device on mobile
 * chipsets.
 * Needs to be called after the devices's platform_pwr struct has been initialized.
 */
int edgetpu_mobile_pm_create(struct edgetpu_dev *etdev);

/*
 * Destroy power management interface for an edgetpu device on mobile chipsets.
 */
void edgetpu_mobile_pm_destroy(struct edgetpu_dev *etdev);

#endif /* __MOBILE_PM_H__ */
