/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020, Google Inc
 *
 * MAX77779 Scratch space management
 */

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include "max77779_regs.h"
#include "gbms_storage.h"

#define RSBM_ADDR				0
#define RSBR_ADDR				4
#define SUFG_ADDR				8
#define RS_TAG_LENGTH				4
#define SU_TAG_LENGTH				1
#define RS_TAG_OFFSET_ADDR			0
#define RS_TAG_OFFSET_LENGTH		1
#define RS_TAG_OFFSET_DATA			2
#define OPCODE_USER_SPACE_R_RES_LEN 32

#define MAX77779_SP_DATA		0x80
#define MAX77779_SP_SIZE 		0x255

struct max77779_sp_data {
	struct device *dev;
	struct regmap *regmap;
	struct dentry *de;
	struct mutex  page_lock; /* might need spinlock */
	u32 debug_reg_address;
};

/* hold lock on &data->page_lock */
static int max77779_sp_rd(uint8_t *buff, int addr, size_t count, struct regmap *regmap)
{
	const int page = addr / 256, offset = addr % 256;
	const int base = MAX77779_SP_DATA + ((offset & ~1) / 2);
	int ret = 0;

	/* todo: bulk of odd count, read across pages */
	if ((count > 2 && (count & 1)) || ((offset + count - 1) > 0xff) || page > 3)
		return -ERANGE;

	ret = regmap_write(regmap, MAX77779_SP_PAGE_CTRL, page);
	if (ret < 0)
		return ret;

	if (count > 2) {
		ret = regmap_bulk_read(regmap, base, buff, count);
	} else if (count) {
		unsigned tmp = 0;

		/* one or two bytes, unaligned TODO: 2 bytes unaligned */
		ret = regmap_read(regmap, base, &tmp);
		if (ret < 0)
			return ret;

		if (count == 1) {
			if (offset & 1)
				*((uint8_t *)buff) = (tmp >> 8) & 0xFF;
			else
				*((uint8_t *)buff) = 0xFF & tmp;
		} else {
			*((uint16_t *)buff) = 0xFFFF & tmp;
		}
	}

	return ret;
}

/* hold lock on &data->page_lock */
static int max77779_sp_wr(const uint8_t *buff, int addr, size_t count, struct regmap *regmap)
{
	const int page = addr / 256, offset = addr % 256;
	const int base = MAX77779_SP_DATA + ((offset & ~1) / 2);
	unsigned tmp = 0;
	int ret = 0;

	/* todo: bulk of odd count, read across pages */
	if ((count > 2 && (count & 1)) || ((offset + count - 1) > 0xff) || page > 3)
		return -ERANGE;

	ret = regmap_write(regmap, MAX77779_SP_PAGE_CTRL, page);
	if (ret < 0)
		return ret;

	if (count > 2)
		return regmap_bulk_write(regmap, base, buff, count);

	if (count == 1) {
		/* one or two bytes, unaligned TODO: 2 bytes unaligned */
		ret = regmap_read(regmap, base, &tmp);
		if (ret < 0)
			return ret;
		tmp &= 0xff << (!(offset & 1) * 8);
		tmp |= buff[0] << ((offset & 1) * 8);
	} else {
		tmp = ((uint16_t*)buff)[0];
	}

	return regmap_write(regmap, base, tmp);
}

static int max77779_sp_info(gbms_tag_t tag, size_t *addr, size_t size)
{
	switch (tag) {
	case GBMS_TAG_RS32:
		if (size && size > OPCODE_USER_SPACE_R_RES_LEN)
			return -EINVAL;
		*addr = RSBM_ADDR;
		break;
	case GBMS_TAG_RSBM:
		if (size && size > RS_TAG_LENGTH)
			return -EINVAL;
		*addr = RSBM_ADDR;
		break;
	case GBMS_TAG_RSBR:
		if (size && size > RS_TAG_LENGTH)
			return -EINVAL;
		*addr = RSBR_ADDR;
		break;
	case GBMS_TAG_SUFG:
		if (size && size > SU_TAG_LENGTH)
			return -EINVAL;
		*addr = SUFG_ADDR;
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static int max77779_sp_iter(int index, gbms_tag_t *tag, void *ptr)
{
	static gbms_tag_t keys[] = {GBMS_TAG_RS32, GBMS_TAG_RSBM, GBMS_TAG_RSBR, GBMS_TAG_SUFG};
	const int count = ARRAY_SIZE(keys);

	if (index >= 0 && index < count) {
		*tag = keys[index];
		return 0;
	}
	return -ENOENT;
}

static int max77779_sp_read(gbms_tag_t tag, void *buff, size_t size, void *ptr)
{
	struct max77779_sp_data *data = ptr;
	size_t addr;
	int ret;

	ret = max77779_sp_info(tag, &addr, size);
	if (ret < 0)
		return ret;

	mutex_lock(&data->page_lock);
	ret = max77779_sp_rd(buff, addr, size, data->regmap);
	mutex_unlock(&data->page_lock);

	return ret;
}

static int max77779_sp_write(gbms_tag_t tag, const void *buff, size_t size, void *ptr)
{
	struct max77779_sp_data *data = ptr;
	size_t addr;
	int ret;

	ret = max77779_sp_info(tag, &addr, size);
	if (ret < 0)
		return ret;

	mutex_lock(&data->page_lock);
	ret = max77779_sp_wr(buff, addr, size, data->regmap);
	mutex_unlock(&data->page_lock);

	return ret;
}

/* -- debug --------------------------------------------------------------- */
static int max77779_sp_debug_reg_read(void *d, u64 *val)
{
	struct max77779_sp_data *data = d;
	u8 reg = 0;
	int ret;

	ret = max77779_sp_rd(&reg, data->debug_reg_address, 1, data->regmap);
	if (ret)
		return ret;

	*val = reg;

	return 0;
}

static int max77779_sp_debug_reg_write(void *d, u64 val)
{
	struct max77779_sp_data *data = d;
	const u8 regval = val;

	return max77779_sp_wr(&regval, data->debug_reg_address, 1, data->regmap);
}

DEFINE_SIMPLE_ATTRIBUTE(debug_reg_rw_fops, max77779_sp_debug_reg_read,
			max77779_sp_debug_reg_write, "%02llx\n");

static struct gbms_storage_desc max77779_sp_dsc = {
	.write = max77779_sp_write,
	.read = max77779_sp_read,
	.iter = max77779_sp_iter,
};

static bool max77779_sp_is_reg(struct device *dev, unsigned int reg)
{
	return (reg == MAX77779_SP_PAGE_CTRL) ||
		   (reg >= MAX77779_SP_DATA && reg < MAX77779_SP_SIZE);
}

static const struct regmap_config max77779_regmap_cfg = {
	.name = "max77779_scratch",
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_SP_SIZE,
	.readable_reg = max77779_sp_is_reg,
	.volatile_reg = max77779_sp_is_reg,
};

static int max77779_sp_dbg_init_fs(struct max77779_sp_data *data)
{
	data->de = debugfs_create_dir("max77779_sp", 0);
	if (IS_ERR_OR_NULL(data->de))
		return -EINVAL;

	debugfs_create_u32("address", 0600, data->de, &data->debug_reg_address);
	debugfs_create_file("data", 0600, data->de, data, &debug_reg_rw_fops);

	return 0;
}

static int max77779_sp_probe(struct i2c_client *client,
							  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct max77779_sp_data *data;
	struct regmap *regmap;
	int ret, page;

	regmap = devm_regmap_init_i2c(client, &max77779_regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	ret = regmap_read(regmap, MAX77779_SP_PAGE_CTRL, &page);
	if (ret) {
		dev_err(dev, "Unable to find scratchpad (%d)\n", ret);
		return ret;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->page_lock);

	data->dev = dev;
	data->regmap = regmap;
	i2c_set_clientdata(client, data);

	if (!of_property_read_bool(dev->of_node, "max77779,no-storage")) {
		ret = gbms_storage_register(&max77779_sp_dsc, "max77779_sp", data);
		if (ret < 0)
			dev_err(dev, "register failed, ret:%d\n", ret);
	}

	ret = max77779_sp_dbg_init_fs(data);
	if (ret < 0)
		dev_err(dev, "Failed to initialize debug fs\n");

	return 0;
}

static int max77779_sp_remove(struct i2c_client *client)
{
	struct max77779_sp_data *data = i2c_get_clientdata(client);

	if (data->de)
		debugfs_remove(data->de);
	return 0;
}


static const struct of_device_id max77779_scratch_of_match_table[] = {
	{ .compatible = "adi,max77779_sp"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_scratch_of_match_table);

static const struct i2c_device_id max77779_id[] = {
	{"max77779_sp", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, max77779_id);

static struct i2c_driver max77779_scratch_i2c_driver = {
	.driver = {
		.name = "max77779-sp",
		.owner = THIS_MODULE,
		.of_match_table = max77779_scratch_of_match_table,
	},
	.id_table = max77779_id,
	.probe    = max77779_sp_probe,
	.remove   = max77779_sp_remove,
};

module_i2c_driver(max77779_scratch_i2c_driver);
MODULE_DESCRIPTION("max77779 Scratch Driver");
MODULE_AUTHOR("AleX Pelosi <apelosi@google.com>");
MODULE_LICENSE("GPL");
