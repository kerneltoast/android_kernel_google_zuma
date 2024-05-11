/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform device driver for devices with MCU support.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_MCU_PLATFORM_H__
#define __GXP_MCU_PLATFORM_H__

#include "gxp-internal.h"
#include "gxp-mcu.h"

#define to_mcu_dev(gxp) container_of(gxp, struct gxp_mcu_dev, gxp)

#if IS_ENABLED(CONFIG_GXP_TEST)
/* expose this variable to have unit tests set it dynamically */
extern char *gxp_work_mode_name;
#endif

enum gxp_work_mode {
	MCU = 0,
	DIRECT = 1,
};

/* GXP device with MCU support. */
struct gxp_mcu_dev {
	struct gxp_dev gxp;
	struct gxp_mcu mcu;
	enum gxp_work_mode mode;
};

/*
 * Initializes MCU structures.
 * @gxp must be the field embedded in gxp_mcu_dev.
 * It's expected to be called from the common driver probe function.
 *
 * Returns 0 on success, -errno otherwise.
 */
int gxp_mcu_platform_after_probe(struct gxp_dev *gxp);
/* Reverts gxp_mcu_platform_after_probe. */
void gxp_mcu_platform_before_remove(struct gxp_dev *gxp);

/*
 * Initializes @mcu_dev.
 *
 * It's expected to be called after allocation, before the common platform probe.
 */
void gxp_mcu_dev_init(struct gxp_mcu_dev *mcu_dev);

enum gxp_work_mode gxp_dev_parse_work_mode(const char *work_mode);

#endif /* __GXP_MCU_PLATFORM_H__ */
