/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023, Google Inc
 *
 * MAX77779 BATTVIMON management
 */
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/reboot.h>

#include "google_bms.h"
#include "max77779.h"
#include "max77779_vimon.h"

static inline int max77779_vimon_reg_read(struct max77779_vimon_data *data, unsigned int reg,
					  unsigned int *val)
{
	return regmap_read(data->regmap, reg, val);
}

static inline int max77779_vimon_reg_write(struct max77779_vimon_data *data, unsigned int reg,
					   unsigned int val)
{
	return regmap_write(data->regmap, reg, val);
}

static inline int max77779_vimon_reg_update(struct max77779_vimon_data *data, unsigned int reg,
					    unsigned int mask, unsigned int val)
{
	return regmap_update_bits(data->regmap, reg, mask, val);
}

/* 0 not running, !=0 running, <0 error */
static int max77779_vimon_is_running(struct max77779_vimon_data *data)
{
	unsigned int running;
	int ret;

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_CTRL, &running);
	if (ret < 0)
		return ret;
	return !!(running & MAX77779_BVIM_CTRL_BVIMON_TRIG_MASK);
}

/* requires mutex_lock(&data->vimon_lock); */
static int vimon_is_running(struct max77779_vimon_data *data)
{
	return data->state > MAX77779_VIMON_IDLE;
}

int max77779_external_vimon_reg_read(struct device *dev, uint16_t reg, void *val, int len)
{
	struct max77779_vimon_data *data = dev_get_drvdata(dev);

	if (!data || !data->regmap)
		return -ENODEV;

	return regmap_raw_read(data->regmap, reg, val, len);
}
EXPORT_SYMBOL_GPL(max77779_external_vimon_reg_read);

int max77779_external_vimon_reg_write(struct device *dev, uint16_t reg, const void *val, int len)
{
	struct max77779_vimon_data *data = dev_get_drvdata(dev);

	if (!data || !data->regmap)
		return -ENODEV;

	return regmap_raw_write(data->regmap, reg, val, len);
}
EXPORT_SYMBOL_GPL(max77779_external_vimon_reg_write);

int max77779_external_vimon_read_buffer(struct device *dev, uint16_t *buff, size_t *count,
					size_t buff_max)
{
	struct max77779_vimon_data *data = dev_get_drvdata(dev);
	int ret = 0;
	int copy_count;

	if (!data)
		return -ENODEV;


	copy_count = data->buf_len;

	if (buff_max < data->buf_len)
		copy_count = buff_max;

	memcpy(buff, data->buf, copy_count);
	*count = copy_count;

	return ret;
}
EXPORT_SYMBOL_GPL(max77779_external_vimon_read_buffer);

int max77779_external_vimon_enable(struct device *dev, bool enable)
{
	struct max77779_vimon_data *data = dev_get_drvdata(dev);
	int ret, reg;

	if (!data)
		return -ENODEV;

	mutex_lock(&data->vimon_lock);

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_CTRL, &reg);
	if (ret < 0) {
		mutex_unlock(&data->vimon_lock);
		return -EIO;
	}

	reg = _max77779_bvim_ctrl_bvimon_trig_set(reg, enable);
	ret = max77779_vimon_reg_write(data, MAX77779_BVIM_CTRL, reg);
	if (reg < 0) {
		mutex_unlock(&data->vimon_lock);
		return -EIO;
	}

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_INT_STS, &reg);
	if (ret < 0) {
		mutex_unlock(&data->vimon_lock);
		return -EIO;
	}

	reg = _max77779_bvim_int_sts_bvim_samples_rdy_set(reg, enable);
	ret = max77779_vimon_reg_write(data, MAX77779_BVIM_INT_STS, reg);
	if (ret < 0) {
		mutex_unlock(&data->vimon_lock);
		return -EIO;
	}

	data->state = enable ? MAX77779_VIMON_IDLE : MAX77779_VIMON_DISABLED;

	mutex_unlock(&data->vimon_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(max77779_external_vimon_enable);

static int max77779_vimon_start(struct max77779_vimon_data *data, uint16_t config)
{
	int ret;

	mutex_lock(&data->vimon_lock);

	ret = max77779_vimon_reg_update(data, MAX77779_BVIM_bvim_cfg, config, config);
	if (ret)
		goto vimon_start_exit;

	ret = max77779_vimon_reg_write(data, MAX77779_BVIM_CTRL,
				       MAX77779_BVIM_CTRL_BVIMON_TRIG_MASK);
	if (ret == 0)
		data->state = MAX77779_VIMON_RUNNING;

vimon_start_exit:
	mutex_unlock(&data->vimon_lock);

	return ret;
}

static int max77779_vimon_stop(struct max77779_vimon_data *data)
{
	return max77779_vimon_reg_write(data, MAX77779_BVIM_CTRL, 0);
}

static int max77779_vimon_set_config(struct max77779_vimon_data *data, uint16_t mask)
{
	return max77779_vimon_reg_write(data, MAX77779_BVIM_bvim_cfg, mask);
}

static int max77779_vimon_clear_config(struct max77779_vimon_data *data, uint16_t mask)
{
	return max77779_vimon_reg_write(data, MAX77779_BVIM_bvim_cfg, 0);
}

/*
 * BattVIMon's Buffer: (1024-32) bytes
 * -page[0:2] 256byts, page[3]:224(256-32)
 * -ranges
 *   page0: [0x000:0x07F]
 *   page1: [0x080:0x0FF]  ---> 0x80:0xFF
 *   page2: [0x100:0x17F]
 *   page3: [0x180:0x1EF]
 */
static ssize_t max77779_vimon_access_buffer(struct max77779_vimon_data *data, size_t offset,
					    size_t len, uint16_t *buffer, bool toread)
{
	unsigned int target_addr;
	int ret = -1;
	size_t sz;
	unsigned int page;
	size_t start = offset;
	const char* type = toread ? "read" : "write";

	/* valid range: 0 - (1024-32) */
	if (offset + len > 992) {
		dev_err(data->dev, "Failed to %s BVIM's buffer: out of range\n", type);
		return -EINVAL;
	}

	while (len > 0) {
		/*
		 * page = offset / 128
		 * sz   = 256 - (offset % 256)
		 * target_addr = 0x80 + (offset % 256)
		 */
		page = offset >> 7;
		sz = MAX77779_VIMON_BUFFER_SIZE - (offset & 0x7F);
		if (sz > len)
			sz = len;
		target_addr = MAX77779_VIMON_OFFSET_BASE + (offset & 0x7F);

		ret = regmap_write(data->regmap, MAX77779_BVIM_PAGE_CTRL, page);
		if (ret < 0) {
			dev_err(data->dev, "page write failed: page: %i\n", page);
			break;
		}

		if (toread)
			ret = regmap_raw_read(data->regmap, target_addr, buffer, sz);
		else
			ret = regmap_raw_write(data->regmap, target_addr, buffer, sz);

		if (ret < 0) {
			dev_err(data->dev, "regmap_raw_read or write failed: %d\n", ret);
			break;
		}

		offset += sz;
		buffer += sz / MAX77779_VIMON_BYTES_PER_ENTRY;
		len -= sz;
	}

	if (ret < 0)
		return ret;

	return offset - start;
}

static ssize_t bvim_cfg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = -1;
	unsigned int val=-1;
	struct max77779_vimon_data *data = dev_get_drvdata(dev);

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_bvim_cfg, &val);

	if (ret <0)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static void max77779_vimon_handle_data(struct work_struct *work)
{
	struct max77779_vimon_data *data = container_of(work, struct max77779_vimon_data,
							read_data_work.work);
	unsigned bvim_rfap, rsc, bvim_osc, smpl_start_add;
	int ret;
	int rd_bytes;

	pm_stay_awake(data->dev);
	mutex_lock(&data->vimon_lock);

	if (data->state != MAX77779_VIMON_DATA_AVAILABLE) {
		ret = -ENODATA;
		goto vimon_handle_data_exit;
	}

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_bvim_rfap, &bvim_rfap);
	if (ret)
		goto vimon_handle_data_exit;

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_bvim_rs, &rsc);
	if (ret)
		goto vimon_handle_data_exit;

	rsc = _max77779_bvim_bvim_rs_rsc_get(rsc);
	rd_bytes = rsc * MAX77779_VIMON_BYTES_PER_ENTRY * MAX77779_VIMON_ENTRIES_PER_VI_PAIR;

	ret = max77779_vimon_stop(data);
	if (ret)
		goto vimon_handle_data_exit;

	ret = max77779_vimon_access_buffer(data, bvim_rfap, rd_bytes, data->buf, true);
	if (ret < 0)
		goto vimon_handle_data_exit;

	data->buf_len = ret;

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_bvim_sts, &bvim_osc);
	if (ret)
		goto vimon_handle_data_exit;

	bvim_osc = _max77779_bvim_bvim_sts_bvim_osc_get(bvim_osc);

	ret = max77779_vimon_reg_read(data, MAX77779_BVIM_smpl_math, &smpl_start_add);
	if (ret)
		goto vimon_handle_data_exit;

	smpl_start_add = _max77779_bvim_smpl_math_smpl_start_add_get(smpl_start_add);

vimon_handle_data_exit:

	if (ret)
		dev_dbg(data->dev, "Failed to handle data: (%d).\n", ret);

	data->state = MAX77779_VIMON_IDLE;
	ret = max77779_vimon_reg_write(data, MAX77779_BVIM_CTRL,
				       MAX77779_BVIM_CTRL_BVIMON_TRIG_MASK);
	if (ret)
		dev_err(data->dev, "Failed to rearm bvim_ctrl (%d).\n", ret);

	ret = regmap_write(data->regmap, MAX77779_BVIM_INT_STS,
			   MAX77779_BVIM_INT_STS_BVIM_Samples_Rdy_MASK);
	if (ret)
		dev_err(data->dev, "Failed to clear INT_STS (%d).\n",
				ret);

	mutex_unlock(&data->vimon_lock);
	pm_relax(data->dev);
}

static ssize_t bvim_cfg_store(struct device *dev, struct device_attribute *attr, const char* buf,
			      size_t count)
{
	int ret = -1;
	unsigned int val = -1;
        struct max77779_vimon_data *data = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

        ret = max77779_vimon_reg_write(data, MAX77779_BVIM_bvim_cfg, val);
        return ret < 0 ? ret : count;
}

DEVICE_ATTR(bvim_cfg, 0660, bvim_cfg_show, bvim_cfg_store);

static ssize_t latest_buff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int idx;
	ssize_t count = 0;
	uint16_t rdback;
	struct max77779_vimon_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->vimon_lock);
	for (idx = 0; idx < data->buf_len / MAX77779_VIMON_BYTES_PER_ENTRY; idx++) {
		rdback = data->buf[idx];
		count += sysfs_emit_at(buf, count, "%#x\n", rdback);
	}
	mutex_unlock(&data->vimon_lock);

	return count;
}
static DEVICE_ATTR_RO(latest_buff);

static struct attribute *max77779_vimon_attrs[] = {
	&dev_attr_bvim_cfg.attr,
	&dev_attr_latest_buff.attr,
	NULL,
};

static const struct attribute_group max77779_vimon_attr_grp = {
	.attrs = max77779_vimon_attrs,
};

/* -- debug --------------------------------------------------------------- */
static int max77779_vimon_debug_start(void *d, u64 *val)
{
	struct max77779_vimon_data *data = d;
	int ret;

	ret = max77779_vimon_start(data, MAX77779_BVIM_bvim_cfg_cnt_run_MASK);
	if (ret)
		return ret;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_start_fops, max77779_vimon_debug_start, NULL, "%02llx\n");

static int max77779_vimon_debug_reg_read(void *d, u64 *val)
{
	struct max77779_vimon_data *data = d;
	int ret, reg;

	ret = regmap_read(data->regmap, data->debug_reg_address, &reg);
	if (ret == 0)
		*val = reg & 0xffff;

	return ret;
}

static int max77779_vimon_debug_reg_write(void *d, u64 val)
{
	struct max77779_vimon_data *data = d;

	return regmap_write(data->regmap, data->debug_reg_address, val & 0xffff);
}
DEFINE_SIMPLE_ATTRIBUTE(debug_reg_rw_fops, max77779_vimon_debug_reg_read,
			max77779_vimon_debug_reg_write, "%04llx\n");

static ssize_t max77779_vimon_show_reg_all(struct file *filp, char __user *buf, size_t count,
					   loff_t *ppos)
{
	struct max77779_vimon_data *data = filp->private_data;
	u32 reg_address;
	char *tmp;
	int ret = 0, len = 0;
	int regread;

	if (*ppos)
		return 0;

	if (!data->regmap)
		return -EIO;

	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	for (reg_address = 0; reg_address <= 0x7F; reg_address++) {
		ret = regmap_read(data->regmap, reg_address, &regread);
		if (ret < 0)
			continue;

		len += scnprintf(tmp + len, PAGE_SIZE - len, "%02x: %04x\n", reg_address,
				regread & 0xffff);
	}

	if (len > 0)
		len = simple_read_from_buffer(buf, count, ppos, tmp, len);

	kfree(tmp);

	return len;
}

BATTERY_DEBUG_ATTRIBUTE(debug_vimon_all_reg_fops, max77779_vimon_show_reg_all, NULL);

static ssize_t max77779_vimon_show_buff_all(struct file *filp, char __user *buf,
					    size_t count, loff_t *ppos)
{
	struct max77779_vimon_data *data = filp->private_data;
	char *tmp;
	uint16_t *vals;
	int ret;
	int len = 0;
	int i;
	const size_t last_readback_size = MAX77779_VIMON_LAST_PAGE_SIZE *
					  MAX77779_VIMON_BYTES_PER_ENTRY;
	const size_t readback_size = MAX77779_VIMON_PAGE_SIZE * MAX77779_VIMON_BYTES_PER_ENTRY;
	int readback_cnt;

	if (*ppos)
		return 0;

	if (!data->regmap)
		return -EIO;

	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	vals = kcalloc(MAX77779_VIMON_PAGE_SIZE, sizeof(uint16_t), GFP_KERNEL);

	mutex_lock(&data->vimon_lock);
	ret = regmap_write(data->regmap, MAX77779_BVIM_PAGE_CTRL, data->debug_buffer_page);
	if (ret < 0)
		goto vimon_show_buff_exit;

	if (data->debug_buffer_page < MAX77779_VIMON_PAGE_CNT - 1) {
		ret = regmap_raw_read(data->regmap, MAX77779_VIMON_OFFSET_BASE, vals,
				      readback_size);
		readback_cnt = MAX77779_VIMON_PAGE_SIZE;
	} else {
		ret = regmap_raw_read(data->regmap, MAX77779_VIMON_OFFSET_BASE, vals,
				      last_readback_size);
		readback_cnt = MAX77779_VIMON_LAST_PAGE_SIZE;
	}

	if (ret < 0)
		goto vimon_show_buff_exit;

	for (i = 0; i < readback_cnt; i++)
		len += scnprintf(tmp + len, PAGE_SIZE - len, "%02x: %04x\n",
				 data->debug_buffer_page * MAX77779_VIMON_PAGE_SIZE + i, vals[i]);

	if (len > 0)
		len = simple_read_from_buffer(buf, count, ppos, tmp, strlen(tmp));

	ret = len;

vimon_show_buff_exit:
	mutex_unlock(&data->vimon_lock);

	kfree(tmp);
	kfree(vals);

	return ret;
}

BATTERY_DEBUG_ATTRIBUTE(debug_vimon_all_buff_fops, max77779_vimon_show_buff_all, NULL);

static int max77779_vimon_debug_buff_page_read(void *d, u64 *val)
{
	struct max77779_vimon_data *data = d;

	*val = data->debug_buffer_page;

	return 0;
}

static int max77779_vimon_debug_buff_page_write(void *d, u64 val)
{
	struct max77779_vimon_data *data = d;

	if (val >= MAX77779_VIMON_PAGE_CNT)
		return -EINVAL;

	data->debug_buffer_page = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_buff_page_rw_fops, max77779_vimon_debug_buff_page_read,
			max77779_vimon_debug_buff_page_write, "%llu\n");

bool max77779_vimon_is_reg(struct device *dev, unsigned int reg)
{
	return reg >= 0 && reg <= MAX77779_VIMON_SIZE;
}
EXPORT_SYMBOL_GPL(max77779_vimon_is_reg);

static int max77779_vimon_init_fs(struct max77779_vimon_data *data)
{
	int ret = -1;

	ret = sysfs_create_group(&data->dev->kobj, &max77779_vimon_attr_grp);
	if (ret < 0) {
		dev_err(data->dev, "Failed to create sysfs group ret:%d\n", ret);
		return ret;
	}

	data->de = debugfs_create_dir("max77779_vimon", 0);
	if (IS_ERR_OR_NULL(data->de))
		return -EINVAL;

	debugfs_create_u32("address", 0600, data->de, &data->debug_reg_address);
	debugfs_create_file("data", 0600, data->de, data, &debug_reg_rw_fops);
	debugfs_create_file("registers", 0444, data->de, data, &debug_vimon_all_reg_fops);

	debugfs_create_file("start", 0600, data->de, data, &debug_start_fops);
	debugfs_create_file("buffer", 0444, data->de, data, &debug_vimon_all_buff_fops);
	debugfs_create_file("buffer_page", 0600, data->de, data, &debug_buff_page_rw_fops);
	debugfs_create_bool("run_in_offmode", 0644, data->de, &data->run_in_offmode);

	return 0;
}

static int max77779_vimon_reboot_notifier(struct notifier_block *nb,
					  unsigned long val, void *v)
{
	struct max77779_vimon_data *data =
		container_of(nb, struct max77779_vimon_data, reboot_notifier);
	int running;

	running = max77779_vimon_is_running(data);
	if (running < 0)
		dev_err(data->dev, "cannot read VIMON HW state (%d)\n", running);
	if (running || vimon_is_running(data))
		dev_warn(data->dev, "vimon state HW=%d SW=%d\n",
			 running, data->state);

	/* stop the HW, warn on inconsistency betwee HW and SW state */
	if (!data->run_in_offmode && running) {
		int ret;

		ret = max77779_vimon_stop(data);
		if (ret < 0)
			dev_err(data->dev, "cannot stop vimon acquisition\n");
	}

	return NOTIFY_OK;
}

/* IRQ */
static irqreturn_t max77779_vimon_irq(int irq, void *ptr)
{
	struct max77779_vimon_data *data = ptr;
	int ret;


	if (data->state <= MAX77779_VIMON_DISABLED)
		return IRQ_HANDLED;

	if (data->state >= MAX77779_VIMON_DATA_AVAILABLE)
		goto vimon_rearm_interrupt;

	data->state = MAX77779_VIMON_DATA_AVAILABLE;

	schedule_delayed_work(&data->read_data_work,
			      msecs_to_jiffies(MAX77779_VIMON_DATA_RETRIEVE_DELAY));

vimon_rearm_interrupt:

	ret = regmap_write(data->regmap, MAX77779_BVIM_INT_STS,
			   MAX77779_BVIM_INT_STS_BVIM_Samples_Rdy_MASK);
	if (ret)
		dev_err(data->dev, "Failed to clear INT_STS (%d).\n", ret);


	return IRQ_HANDLED;
}

/*
 * Initialization requirements
 * struct max77779_vimon_data *data
 * - dev
 * - regmap
 * - irq
 */
int max77779_vimon_init(struct max77779_vimon_data *data)
{
	struct device *dev = data->dev;
	unsigned int running;
	uint16_t cfg_mask = 0;
	uint16_t cfg_mask_lower_bits = 0;
	int ret;

	/* VIMON can be used to profile battery drain during reboot */
	running = max77779_vimon_is_running(data);
	if (running)
		dev_warn(data->dev, "VIMON is already running (%d)\n", running);
	mutex_init(&data->vimon_lock);

	/* configure collected sample count with MAX77779_VIMON_SMPL_CNT */
	cfg_mask = MAX77779_BVIM_bvim_cfg_vioaok_stop_MASK |
		   MAX77779_BVIM_bvim_cfg_top_fault_stop_MASK;

	cfg_mask_lower_bits = _max77779_bvim_bvim_cfg_smpl_n_set(cfg_mask_lower_bits,
								 MAX77779_VIMON_SMPL_CNT);

	cfg_mask |= cfg_mask_lower_bits;

	ret = max77779_vimon_set_config(data, cfg_mask);
	if (ret) {
		dev_err(dev, "Failed to configure vimon\n");
		return ret;
	}

	cfg_mask = MAX77779_BVIM_bvim_trig_oilo_stop_source_MASK |
		   MAX77779_BVIM_bvim_trig_batoilo1_tr_MASK |
		   MAX77779_BVIM_bvim_trig_batoilo2_tr_MASK |
		   MAX77779_BVIM_bvim_trig_sysuvlo1_tr_MASK |
		   MAX77779_BVIM_bvim_trig_sysuvlo2_tr_MASK;
	ret = max77779_vimon_reg_write(data, MAX77779_BVIM_bvim_trig, cfg_mask);
	if (ret) {
		dev_err(dev, "Failed to configure vimon trig\n");
		return ret;
	}

	ret = max77779_vimon_reg_write(data, MAX77779_BVIM_CTRL,
				       MAX77779_BVIM_CTRL_BVIMON_TRIG_MASK);
	if (ret) {
		dev_err(dev, "Failed to configure BVIM enable\n");
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "max77779,max_cnt", &data->max_cnt);
	if (ret)
		data->max_cnt = MAX77779_VIMON_DEFAULT_MAX_CNT;

	ret = of_property_read_u32(dev->of_node, "max77779,max_triggers", &data->max_cnt);
	if (ret)
		data->max_triggers = MAX77779_VIMON_DEFAULT_MAX_TRIGGERS;

	data->buf_size = sizeof(*data->buf) * data->max_cnt * data->max_triggers * 2;
	if (!data->buf_size) {
		dev_err(dev, "max_cnt=%d, max_cnt=%d invalid buf_size\n",
			data->max_cnt, data->max_triggers);
		return -EINVAL;
	}
	data->buf = devm_kzalloc(dev, data->buf_size, GFP_KERNEL);
	if (!data->buf)
		return -ENOMEM;

	INIT_DELAYED_WORK(&data->read_data_work, max77779_vimon_handle_data);

	if (data->irq){
		ret = devm_request_threaded_irq(data->dev, data->irq, NULL,
				max77779_vimon_irq,
				IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_ONESHOT,
				"max77779_vimon", data);
		if (ret < 0)
			dev_warn(dev, "Failed to get irq thread.\n");
	} else {
		dev_warn(dev, "irq not setup\n");
	}

	ret = max77779_vimon_init_fs(data);
	if (ret < 0)
		dev_warn(dev, "Failed to initialize debug fs\n");

	/* turn off vimon on reboot */
	data->reboot_notifier.notifier_call = max77779_vimon_reboot_notifier;
	ret = register_reboot_notifier(&data->reboot_notifier);
	if (ret)
		dev_err(data->dev, "failed to register reboot notifier\n");

	ret = max77779_vimon_reg_write(data, MAX77779_BVIM_MASK, 0);
	if (ret)
		dev_err(data->dev, "Failed to unmask INT (%d).\n", ret);

	data->state = MAX77779_VIMON_IDLE;
	dev_info(data->dev, "buf_size=%lu\n", data->buf_size);
	return 0;
}
EXPORT_SYMBOL_GPL(max77779_vimon_init);


void max77779_vimon_remove(struct max77779_vimon_data *data)
{
	unsigned int running;

	running = max77779_vimon_is_running(data);
	if (running < 0)
		dev_err(data->dev, "cannot read VIMON HW state (%d)\n", running);
	if (running || vimon_is_running(data))
		dev_warn(data->dev, "vimon state HW=%d SW=%d\n",
			 running, data->state);

	if (data->de)
		debugfs_remove(data->de);
	if (data->irq)
		free_irq(data->irq, data);
}
EXPORT_SYMBOL_GPL(max77779_vimon_remove);

MODULE_DESCRIPTION("max77779 VIMON Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_AUTHOR("Chungro Lee <chungro@google.com>");
MODULE_AUTHOR("AleX Pelosi <apelosi@google.com>");
MODULE_AUTHOR("Hiroshi Akiyama <hiroshiakiyama@google.com>");
MODULE_LICENSE("GPL");
