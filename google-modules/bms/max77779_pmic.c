// SPDX-License-Identifier: GPL-2.0-only
/*
 * max77779 pmic driver
 *
 * Copyright (C) 2023 Google, LLC.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": %s " fmt, __func__

#include <linux/ctype.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)
# include <linux/debugfs.h>
# include <linux/seq_file.h>
#endif

#include "max77779_pmic.h"

#define MAX77779_PMIC_ID_VAL	0x79

struct max77779_pmic_info {
	struct device		*dev;
	struct regmap		*regmap;
	struct i2c_client	*client;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry		*de;
	unsigned int		addr;
#endif
};

int max77779_pmic_reg_read(struct device *core_dev,
		unsigned int reg, unsigned int *val)
{
	struct max77779_pmic_info *info = dev_get_drvdata(core_dev);
	int err;

	err = regmap_read(info->regmap, reg, val);
	if (err)
		dev_err(core_dev, "error reading %#02x err = %d\n", reg, err);

	return err;
}
EXPORT_SYMBOL_GPL(max77779_pmic_reg_read);

int max77779_pmic_reg_write(struct device *core_dev,
		unsigned int reg, unsigned int val)
{
	struct max77779_pmic_info *info = dev_get_drvdata(core_dev);
	int err;

	err = regmap_write(info->regmap, reg, val);
	if (err)
		dev_err(core_dev, "error writing %#02x err = %d\n",
				reg, err);
	return err;
}
EXPORT_SYMBOL_GPL(max77779_pmic_reg_write);

int max77779_pmic_reg_update(struct device *core_dev, unsigned int reg,
		unsigned int mask, unsigned int val)
{
	struct max77779_pmic_info *info = dev_get_drvdata(core_dev);
	int err;

	err = regmap_update_bits(info->regmap, reg, mask, val);
	if (err)
		dev_err(core_dev, "error updating %#02x err = %d\n",
				reg, err);
	return err;
}
EXPORT_SYMBOL_GPL(max77779_pmic_reg_update);

#ifdef CONFIG_DEBUG_FS
static int addr_write(void *d, u64 val)
{
	struct max77779_pmic_info *info = d;

	info->addr = val & 0xff;
	return 0;
}

static int addr_read(void *d, u64 *val)
{
	struct max77779_pmic_info *info = d;

	*val  = info->addr;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(addr_fops, addr_read, addr_write, "%llx\n");

static int data_write(void *d, u64 val)
{
	struct max77779_pmic_info *info = d;

	return max77779_pmic_reg_write(info->dev, info->addr,
			(unsigned int)(val & 0xff));
}

static int data_read(void *d, u64 *val)
{
	struct max77779_pmic_info *info = d;
	unsigned int rd_val;
	int ret;

	ret = max77779_pmic_reg_read(info->dev, info->addr, &rd_val);
	*val = rd_val;

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(data_fops, data_read, data_write, "%llx\n");

static int dbg_init_fs(struct max77779_pmic_info *info)
{
	info->de = debugfs_create_dir("max77779_pmic", 0);
	if (!info->de)
		return -EINVAL;

	debugfs_create_file("addr", 0600, info->de,
			info, &addr_fops);

	debugfs_create_file("data", 0600, info->de,
			info, &data_fops);

	return 0;
}
static void dbg_remove_fs(struct max77779_pmic_info *info)
{
	debugfs_remove_recursive(info->de);
	info->de = NULL;
}
#else
static inline int dbg_init_fs(struct max77779_pmic_info *info)
{
	return 0;
}
static inline void dbg_remove_fs(struct max77779_pmic_info *info) {}
#endif

static bool max77779_pmic_is_readable(struct device *dev, unsigned int reg)
{
	return true; /* FIXME(jwylder) */
}

static bool max77779_pmic_is_volatile(struct device *dev, unsigned int reg)
{
	return reg > MAX77779_PMIC_INTSRC_STS;
}

static const struct regmap_config max77779_pmic_regmap_cfg = {
	.name = "max77779_pmic",
	.reg_bits = 8,
	.val_bits = 8,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = 0xff,
	.readable_reg = max77779_pmic_is_readable,
	.volatile_reg = max77779_pmic_is_volatile,
};

static const struct mfd_cell max77779_pmic_devs[] = {
	{
		.name = "max77779-pmic-irq",
		.of_compatible = "max77779-pmic-irq",
	},
	{
		.name = "max77779-pinctrl",
		.of_compatible = "max77779-pinctrl",
	},
	{
		.name = "max77779-pmic-sgpio",
		.of_compatible = "max77779-pmic-sgpio",
	},
};

static int max77779_pmic_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct max77779_pmic_info *info;
	unsigned int pmic_id;
	int err = 0;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	i2c_set_clientdata(client, info);
	info->client = client;

	info->regmap = devm_regmap_init_i2c(client, &max77779_pmic_regmap_cfg);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	err = regmap_read(info->regmap, MAX77779_PMIC_ID, &pmic_id);
	if (err) {
		dev_err(dev, "Unable to read Device ID (%d)\n", err);
		return err;
	} else if (MAX77779_PMIC_ID_VAL != pmic_id) {
		dev_err(dev, "Unsupported Device ID (%#02x)\n", pmic_id);
		return -ENODEV;
	}

	mfd_add_devices(dev, PLATFORM_DEVID_AUTO, max77779_pmic_devs,
			ARRAY_SIZE(max77779_pmic_devs), NULL, 0, NULL);

	dbg_init_fs(info);

	return err;
}

static int max77779_pmic_remove(struct i2c_client *client)
{
	struct max77779_pmic_info *info = i2c_get_clientdata(client);

	dbg_remove_fs(info);
	return 0;
}

static const struct of_device_id max77779_pmic_of_match_table[] = {
	{ .compatible = "maxim,max77779_pmic" },
	{ .compatible = "max77779_pmic" },
	{},
};
MODULE_DEVICE_TABLE(of, max77779_pmic_of_match_table);

static const struct i2c_device_id max77779_pmic_id[] = {
	{"max77779_pmic", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, max77779_pmic_id);

static struct i2c_driver max77779_pmic_i2c_driver = {
	.driver = {
		.name = "max77779-pmic",
		.owner = THIS_MODULE,
		.of_match_table = max77779_pmic_of_match_table,
	},
	.id_table = max77779_pmic_id,
	.probe = max77779_pmic_probe,
	.remove = max77779_pmic_remove,
};

module_i2c_driver(max77779_pmic_i2c_driver);
MODULE_DESCRIPTION("Maxim 77779 PMIC driver");
MODULE_AUTHOR("James Wylder <jwylder@google.com>");
MODULE_LICENSE("GPL");
