/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform device driver for Callisto.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __CALLISTO_PLATFORM_H__
#define __CALLISTO_PLATFORM_H__

#include "gxp-mcu-platform.h"

#define to_callisto_dev(gxp)                                                   \
	container_of(to_mcu_dev(gxp), struct callisto_dev, mcu_dev)

struct callisto_dev {
	struct gxp_mcu_dev mcu_dev;
};

#endif /* __CALLISTO_PLATFORM_H__ */
