/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SW Support for MAX77779 IF-PMIC
 *
 * Copyright 2023 Google, LLC
 */
#ifndef MAX77779_PMIC
#define MAX77779_PMIC

#include <linux/device.h>
#include "max77779_regs.h"

extern int max77779_pmic_reg_read(struct device *core_dev,
		unsigned int reg, unsigned int *val);

extern int max77779_pmic_reg_write(struct device *core_dev,
		unsigned int reg, unsigned int val);

extern int max77779_pmic_reg_update(struct device *core_dev,
		unsigned int reg, unsigned int mask, unsigned int val);

#endif
