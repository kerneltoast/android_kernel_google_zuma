/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Define configuration macros.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#ifndef __GCIP_CONFIG_H__
#define __GCIP_CONFIG_H__

#include <linux/version.h>

/* Macros to check the availability of features and APIs */

#define GCIP_IOMMU_MAP_HAS_GFP (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))

#endif /* __GCIP_CONFIG_H__ */
