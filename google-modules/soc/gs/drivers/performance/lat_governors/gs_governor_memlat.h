/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google, Inc. All rights reserved.
 */

#ifndef _GS_GOVERNOR_MEMLAT_H
#define _GS_GOVERNOR_MEMLAT_H

#include <linux/kernel.h>
#include <linux/devfreq.h>

/**
 * gs_memlat_governor_register - Adds the governor to the devfreq system.
*/
int gs_memlat_governor_register(void);

/**
 * gs_memlat_governor_unregister - Removes governor from the devfreq system.
*/
void gs_memlat_governor_unregister(void);

/**
 * gs_memlat_governor_initialize - Parse and init memlat governor data.
 *
 * Inputs:
 * @governor_node:	The device tree node containing memlat data.
 * @data:		Devfreq data for governor.
 *
 * Returns:		Non-zero on error.
*/
int gs_memlat_governor_initialize(struct device_node *governor_node,
				  struct exynos_devfreq_data *data);

/**
 * gs_memlat_governor_set_devfreq_ready: Informs governor devfreq is initialized.
 *
 * TODO: Remove with devfreqs in b/327482673.
*/
void gs_memlat_governor_set_devfreq_ready(void);

#endif /* _GS_GOVERNOR_MEMLAT_H */
