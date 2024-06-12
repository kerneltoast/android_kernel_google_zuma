/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google, LLC
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

#ifndef MAX77779_FG_MODEL_H_
#define MAX77779_FG_MODEL_H_

#include "max1720x_battery.h"
#include "max77779.h"

/* change to 1 or 0 to load FG model with default parameters on startup */
#define MAX77779_LOAD_MODEL_DISABLED	-1
#define MAX77779_LOAD_MODEL_IDLE	0
#define MAX77779_LOAD_MODEL_REQUEST	5

#define MAX77779_FG_MODEL_START		0x80
#define MAX77779_FG_MODEL_SIZE		48

/* model version */
#define MAX77779_INVALID_VERSION	-1


/** ------------------------------------------------------------------------ */

/*
 * Custom parameters are updated while the device is running.
 * NOTE: a subset (model_state_save) is saved to permanent storage every "n"
 * cycles and restored when the model is reloaded (usually on POR).
 * TODO: handle switching between RC1 and RC2 model types.
 */
struct max77779_custom_parameters {
	u16 iavg_empty; /* WV */
	u16 relaxcfg;
	u16 learncfg;
	u16 config;
	u16 config2;
	u16 fullsocthr;
	u16 fullcaprep; /* WV */
	u16 designcap;
	u16 dpacc;	/* WV */
	u16 dqacc;	/* WV */
	u16 fullcapnom;	/* WV */
	u16 v_empty;
	u16 qresidual00;	/* WV */
	u16 qresidual10;	/* WV */
	u16 qresidual20;	/* WV */
	u16 qresidual30;	/* WV */
	u16 rcomp0;	/* WV */
	u16 tempco;	/* WV */
	u16 ichgterm;
	u16 tgain;
	u16 toff;
	u16 tcurve; 	/* write to 0x00B9 */
	u16 misccfg;	/* 0x9d0 for internal current sense, 0x8d0 external */

	u16 atrate;
	u16 convgcfg;
	u16 filtercfg; 	/* write to 0x0029 */
} __attribute__((packed));

/* this is what is saved and restored to/from GMSR */
struct model_state_save {
	u16 rcomp0;
	u16 tempco;
	u16 fullcaprep;
	u16 cycles;
	u16 fullcapnom;
	u8 padding[6]; /* keep the same size as 59 for consistency GBMS_GMSR_LEN */
	u8 crc;
} __attribute__((packed));

struct max77779_model_data {
	struct device *dev;
	struct max17x0x_regmap *regmap;
	struct regmap *debug_regmap;

	/* initial parameters are in device tree they are also learned */
	struct max77779_custom_parameters parameters;
	u16 cycles;
	u16 cv_mixcap;

	int custom_model_size;
	u16 *custom_model;
	u32 model_version;
	bool force_reset_model_data;

	/* to/from GMSR */
	struct model_state_save model_save;
};

/** ------------------------------------------------------------------------ */

int max77779_model_read_version(const struct max77779_model_data *model_data);
int max77779_model_get_cap_lsb(const struct max77779_model_data *model_data);
int max77779_reset_state_data(struct max77779_model_data *model_data);
int max77779_needs_reset_model_data(const struct max77779_model_data *model_data);

/*
 * max77779 might use the low 8 bits of devname to keep the model version number
 * - 0 not M5, !=0 M5
 */
static inline int max77779_check_devname(u16 devname)
{
	const u16 radix = devname >> 8;

	return radix == 0x62 || radix == 0x63;
}

static inline int max77779_fg_model_version(const struct max77779_model_data *model_data)
{
	return model_data ? model_data->model_version : MAX77779_INVALID_VERSION;
}

/*
 * 0 reload, != 0 no reload
 * always reload when the model version is not specified
 */
static inline int max77779_fg_model_check_version(const struct max77779_model_data *model_data)
{
	if (!model_data)
		return 1;
	if (model_data->model_version == MAX77779_INVALID_VERSION)
		return 0;

	return max77779_model_read_version(model_data) == model_data->model_version;
}

#define MAX77779_FG_REGMAP_WRITE(regmap, what, value) \
	max77779_fg_register_write(regmap, what, value, false)

#define MAX77779_FG_REGMAP_WRITE_VERIFY(regmap, what, value) \
	max77779_fg_register_write(regmap, what, value, true)

/** ------------------------------------------------------------------------ */

int max77779_max17x0x_regmap_init(struct max17x0x_regmap *regmap, struct i2c_client *clnt,
															const struct regmap_config *regmap_config, bool tag);
int max77779_fg_usr_lock(const struct max17x0x_regmap *map, bool enabled);
int max77779_fg_register_write(const struct max17x0x_regmap *regmap, unsigned int reg,
															u16 value, bool verify);
void *max77779_init_data(struct device *dev, struct device_node *batt_node,
			 struct max17x0x_regmap *regmap);
void max77779_free_data(void *data);

int max77779_load_state_data(struct max77779_model_data *model_data);
int max77779_save_state_data(struct max77779_model_data *model_data);

/* read state from the gauge */
int max77779_model_read_state(struct max77779_model_data *model_data);
int max77779_model_check_state(struct max77779_model_data *model_data);

/* load model to gauge */
int max77779_load_gauge_model(struct max77779_model_data *model_data);

ssize_t max77779_model_state_cstr(char *buf, int max, struct max77779_model_data *model_data);
int max77779_fg_model_cstr(char *buf, int max, const struct max77779_model_data *model_data);

/* read saved value */
ssize_t max77779_gmsr_state_cstr(char *buf, int max);

/** ------------------------------------------------------------------------ */

void *max77779_get_model_data(struct i2c_client *client);



#endif
