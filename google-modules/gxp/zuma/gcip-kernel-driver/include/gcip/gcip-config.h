/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Define configuration macros.
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __GCIP_CONFIG_H__
#define __GCIP_CONFIG_H__

#include <linux/version.h>

#define GCIP_IS_GKI IS_ENABLED(CONFIG_ANDROID_VENDOR_HOOKS)

/* Macros to check the availability of features and APIs */

/* TODO(b/298697777): temporarily check 6.1.25 until previous kernel version no longer in use. */
#define GCIP_HAS_VMA_FLAGS_API                                                                \
	((GCIP_IS_GKI && LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 25)) ||                   \
	 (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)))

#define GCIP_HAS_IOMMU_PASID (GCIP_IS_GKI || LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0))

#define GCIP_HAS_AUX_DOMAINS 0

/*
 * TODO(b/277649169) Best fit IOVA allocator was removed in 6.1 GKI
 * The API needs to either be upstreamed, integrated into this driver, or disabled for 6.1
 * compatibility. For now, disable best-fit for IOVAD.
 */
#define GCIP_HAS_IOVAD_BEST_FIT_ALGO 0

#define GCIP_IOMMU_MAP_HAS_GFP (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))

#endif /* __GCIP_CONFIG_H__ */
