/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fuel gauge driver for Maxim Fuel Gauges with M5 Algo
 *
 * Copyright (C) 2023 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s " fmt, __func__

#include <linux/crc8.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include "google_bms.h"
#include "google_psy.h"

#include "max77779_fg.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

/*
 * TODO: b/285191457 - Validate that this register is ok to use for
 *     model version read/write
 */
int max77779_model_read_version(const struct max77779_model_data *model_data)
{
	return 0;
}

int max77779_reset_state_data(struct max77779_model_data *model_data)
{
	return 0;
}

int max77779_needs_reset_model_data(const struct max77779_model_data *model_data)
{
	return 0;
}

/* 0 is ok */
/* TODO: b/283487421 - Implement model loading */
int max77779_load_gauge_model(struct max77779_model_data *model_data)
{
	return 0;
}

/*
 * TODO: b/283487421 - Implement model loading
 * Non-volatile registers need to be added via secondary i2c address see max17201
*/

/*
 * Load parameters and model state from permanent storage.
 * Called on boot after POR
 */
int max77779_load_state_data(struct max77779_model_data *model_data)
{
	return 0;
}

/* TODO: b/283487421 - Implement model loading */
/* save/commit parameters and model state to permanent storage */
int max77779_save_state_data(struct max77779_model_data *model_data)
{
	return 0;
}

/* TODO: b/283487421 - Implement model loading */
/* 0 ok, < 0 error. Call after reading from the FG */
int max77779_model_check_state(struct max77779_model_data *model_data)
{
	return 0;
}

/* TODO: b/283487421 - Implement model loading */
/*
 * read fuel gauge state to parameters/model state.
 * NOTE: Called on boot if POR is not set or during save state.
 */
int max77779_model_read_state(struct max77779_model_data *model_data)
{
	return 0;
}

/* TODO: b/283487421 - Implement model loading */
ssize_t max77779_model_state_cstr(char *buf, int max,
				struct max77779_model_data *model_data)
{
	return 0;
}

/* TODO: b/283487421 - Implement model loading */
ssize_t max77779_gmsr_state_cstr(char *buf, int max)
{
	return 0;
}

/* TODO: b/283487421 - Implement model loading */
/* custom model parameters */
int max77779_fg_model_cstr(char *buf, int max, const struct max77779_model_data *model_data)
{
	return 0;
}

void max77779_free_data(void *data)
{
	/* TODO: b/284199040 free data */
}

/* TODO: b/283487421 - Implement model loading */
void *max77779_init_data(struct device *dev, struct device_node *node,
		       struct max17x0x_regmap *regmap)
{
	const char *propname = "max77779,fg-model";
	struct max77779_model_data *model_data;

	model_data = devm_kzalloc(dev, sizeof(*model_data), GFP_KERNEL);
	if (!model_data) {
		dev_err(dev, "fg-model: %s not found\n", propname);
		return ERR_PTR(-ENOMEM);
	}

	return model_data;
}
