/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#include <linux/i2c.h>
#include <linux/regmap.h>

#include "max77779_vimon.h"

static const struct regmap_config max77779_vimon_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_VIMON_SIZE,
	.readable_reg = max77779_vimon_is_reg,
	.volatile_reg = max77779_vimon_is_reg,
};

static const struct i2c_device_id max77779_vimon_id[] = {
	{"max77779_vimon", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, max77779_vimon_id);

static int max77779_vimon_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct max77779_vimon_data *data;
	struct regmap *regmap;

	/* pmic-irq driver needs to setup the irq */
	if (client->irq < 0)
		return -EPROBE_DEFER;

	regmap = devm_regmap_init_i2c(client, &max77779_vimon_regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->regmap = regmap;
	data->irq = client->irq;
	i2c_set_clientdata(client, data);

	return max77779_vimon_init(data);
}

static void max77779_vimon_i2c_remove(struct i2c_client *client)
{
	struct max77779_vimon_data *data = i2c_get_clientdata(client);

	max77779_vimon_remove(data);
}

static const struct of_device_id max77779_vimon_of_match_table[] = {
	{ .compatible = "maxim,max77779vimon-i2c"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_vimon_of_match_table);

static struct i2c_driver max77779_vimon_i2c_driver = {
	.driver = {
		.name = "max77779-vimon",
		.owner = THIS_MODULE,
		.of_match_table = max77779_vimon_of_match_table,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = max77779_vimon_id,
	.probe	= max77779_vimon_i2c_probe,
	.remove   = max77779_vimon_i2c_remove,
};

module_i2c_driver(max77779_vimon_i2c_driver);
MODULE_DESCRIPTION("Maxim 77779 Vimon I2C Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
