// SPDX-License-Identifier: GPL-2.0
/*
 * google_bcl_sysfs.c Google bcl sysfs driver
 *
 * Copyright (c) 2022, Google LLC. All rights reserved.
 *
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <soc/google/bcl.h>
#include <dt-bindings/interrupt-controller/zuma.h>
#include <linux/regulator/pmic_class.h>
#include <linux/mfd/samsung/s2mpg14-register.h>
#include <linux/mfd/samsung/s2mpg15-register.h>

const unsigned int clk_stats_offset[] = {
	CPUCL0_CLKDIVSTEP_STAT,
	CLKDIVSTEP_STAT,
	CLKDIVSTEP_STAT,
	TPU_CLKDIVSTEP_STAT,
	G3D_CLKDIVSTEP_STAT,
	AUR_CLKDIVSTEP_STAT,
};

static const char * const batt_irq_names[] = {
	"uvlo1", "uvlo2", "batoilo"
};

static const char * const concurrent_pwrwarn_irq_names[] = {
	"none", "mmwave", "rffe"
};

static ssize_t batoilo_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[BATOILO]->bcl_cnt));
}

static DEVICE_ATTR_RO(batoilo_count);

static ssize_t vdroop2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[UVLO2]->bcl_cnt));
}

static DEVICE_ATTR_RO(vdroop2_count);

static ssize_t vdroop1_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[UVLO1]->bcl_cnt));
}

static DEVICE_ATTR_RO(vdroop1_count);

static ssize_t smpl_warn_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[SMPL_WARN]->bcl_cnt));
}

static DEVICE_ATTR_RO(smpl_warn_count);

static ssize_t ocp_cpu1_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[OCP_WARN_CPUCL1]->bcl_cnt));
}

static DEVICE_ATTR_RO(ocp_cpu1_count);

static ssize_t ocp_cpu2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[OCP_WARN_CPUCL2]->bcl_cnt));
}

static DEVICE_ATTR_RO(ocp_cpu2_count);

static ssize_t ocp_tpu_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[OCP_WARN_TPU]->bcl_cnt));
}

static DEVICE_ATTR_RO(ocp_tpu_count);

static ssize_t ocp_gpu_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[OCP_WARN_GPU]->bcl_cnt));
}

static DEVICE_ATTR_RO(ocp_gpu_count);

static ssize_t soft_ocp_cpu1_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[SOFT_OCP_WARN_CPUCL1]->bcl_cnt));
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_count);

static ssize_t soft_ocp_cpu2_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[SOFT_OCP_WARN_CPUCL2]->bcl_cnt));
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_count);

static ssize_t soft_ocp_tpu_count_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[SOFT_OCP_WARN_TPU]->bcl_cnt));
}

static DEVICE_ATTR_RO(soft_ocp_tpu_count);

static ssize_t soft_ocp_gpu_count_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", atomic_read(&bcl_dev->zone[SOFT_OCP_WARN_GPU]->bcl_cnt));
}

static DEVICE_ATTR_RO(soft_ocp_gpu_count);

static ssize_t batoilo_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[BATOILO]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(batoilo_cap);

static ssize_t vdroop2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[UVLO2]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(vdroop2_cap);

static ssize_t vdroop1_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[UVLO1]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(vdroop1_cap);

static ssize_t smpl_warn_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SMPL_WARN]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(smpl_warn_cap);

static ssize_t ocp_cpu1_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_CPUCL1]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(ocp_cpu1_cap);

static ssize_t ocp_cpu2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_CPUCL2]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(ocp_cpu2_cap);

static ssize_t ocp_tpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_TPU]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(ocp_tpu_cap);

static ssize_t ocp_gpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_GPU]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(ocp_gpu_cap);

static ssize_t soft_ocp_cpu1_cap_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_CPUCL1]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_cap);

static ssize_t soft_ocp_cpu2_cap_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_CPUCL2]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_cap);

static ssize_t soft_ocp_tpu_cap_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_TPU]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(soft_ocp_tpu_cap);

static ssize_t soft_ocp_gpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_GPU]->bcl_stats.capacity);
}

static DEVICE_ATTR_RO(soft_ocp_gpu_cap);


static ssize_t batoilo_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[BATOILO]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(batoilo_volt);

static ssize_t vdroop2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[UVLO2]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(vdroop2_volt);

static ssize_t vdroop1_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[UVLO1]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(vdroop1_volt);

static ssize_t smpl_warn_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SMPL_WARN]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(smpl_warn_volt);

static ssize_t ocp_cpu1_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_CPUCL1]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(ocp_cpu1_volt);

static ssize_t ocp_cpu2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_CPUCL2]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(ocp_cpu2_volt);

static ssize_t ocp_tpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_TPU]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(ocp_tpu_volt);

static ssize_t ocp_gpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[OCP_WARN_GPU]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(ocp_gpu_volt);

static ssize_t soft_ocp_cpu1_volt_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_CPUCL1]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_volt);

static ssize_t soft_ocp_cpu2_volt_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_CPUCL2]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_volt);

static ssize_t soft_ocp_tpu_volt_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_TPU]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(soft_ocp_tpu_volt);

static ssize_t soft_ocp_gpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_OCP_WARN_GPU]->bcl_stats.voltage);
}

static DEVICE_ATTR_RO(soft_ocp_gpu_volt);


static ssize_t batoilo_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[BATOILO]->bcl_stats._time);
}

static DEVICE_ATTR_RO(batoilo_time);

static ssize_t vdroop2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[UVLO2]->bcl_stats._time);
}

static DEVICE_ATTR_RO(vdroop2_time);

static ssize_t vdroop1_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[UVLO1]->bcl_stats._time);
}

static DEVICE_ATTR_RO(vdroop1_time);

static ssize_t smpl_warn_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[SMPL_WARN]->bcl_stats._time);
}

static DEVICE_ATTR_RO(smpl_warn_time);

static ssize_t ocp_cpu1_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[OCP_WARN_CPUCL1]->bcl_stats._time);
}

static DEVICE_ATTR_RO(ocp_cpu1_time);

static ssize_t ocp_cpu2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[OCP_WARN_CPUCL2]->bcl_stats._time);
}

static DEVICE_ATTR_RO(ocp_cpu2_time);

static ssize_t ocp_tpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[OCP_WARN_TPU]->bcl_stats._time);
}

static DEVICE_ATTR_RO(ocp_tpu_time);

static ssize_t ocp_gpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[OCP_WARN_GPU]->bcl_stats._time);
}

static DEVICE_ATTR_RO(ocp_gpu_time);

static ssize_t soft_ocp_cpu1_time_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[SOFT_OCP_WARN_CPUCL1]->bcl_stats._time);
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_time);

static ssize_t soft_ocp_cpu2_time_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[SOFT_OCP_WARN_CPUCL2]->bcl_stats._time);
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_time);

static ssize_t soft_ocp_tpu_time_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[SOFT_OCP_WARN_TPU]->bcl_stats._time);
}

static DEVICE_ATTR_RO(soft_ocp_tpu_time);

static ssize_t soft_ocp_gpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%lld\n", bcl_dev->zone[SOFT_OCP_WARN_GPU]->bcl_stats._time);
}

static DEVICE_ATTR_RO(soft_ocp_gpu_time);

static ssize_t db_settings_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size, enum MPMM_SOURCE src)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int value;

	if (kstrtouint(buf, 16, &value) < 0)
		return -EINVAL;

	if (src != BIG && src != MID)
		return -EINVAL;

	google_set_db(bcl_dev, value, src);

        return size;
}

static ssize_t db_settings_show(struct device *dev, struct device_attribute *attr,
				char *buf, enum MPMM_SOURCE src)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if ((!bcl_dev->sysreg_cpucl0) || (src == LITTLE) || (src == MPMMEN))
		return -EIO;

	return sysfs_emit(buf, "%#x\n", google_get_db(bcl_dev, src));
}

static ssize_t mid_db_settings_store(struct device *dev,
				     struct device_attribute *attr, const char *buf, size_t size)
{
	return db_settings_store(dev, attr, buf, size, MID);
}

static ssize_t mid_db_settings_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return db_settings_show(dev, attr, buf, MID);
}

static DEVICE_ATTR_RW(mid_db_settings);

static ssize_t big_db_settings_store(struct device *dev,
				     struct device_attribute *attr, const char *buf, size_t size)
{
	return db_settings_store(dev, attr, buf, size, BIG);
}

static ssize_t big_db_settings_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return db_settings_show(dev, attr, buf, BIG);
}

static DEVICE_ATTR_RW(big_db_settings);

static ssize_t irq_delay_store(struct device *dev,
                               struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = sscanf(buf, "%d", &value);
	if (ret != 1)
		return -EINVAL;

        bcl_dev->irq_delay = value;

	return size;
}

static ssize_t irq_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->irq_delay);
}

static DEVICE_ATTR_RW(irq_delay);

static ssize_t enable_mitigation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->enabled);
}

static ssize_t enable_mitigation_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret, i;
	void __iomem *addr;
	unsigned int reg;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	if (bcl_dev->enabled == value)
		return size;

	bcl_dev->enabled = value;
	if (bcl_dev->enabled) {
		bcl_dev->core_conf[SUBSYSTEM_TPU].clkdivstep |= 0x1;
		bcl_dev->core_conf[SUBSYSTEM_GPU].clkdivstep |= 0x1;
		bcl_dev->core_conf[SUBSYSTEM_AUR].clkdivstep |= 0x1;
		for (i = 0; i < CPU_CLUSTER_MAX; i++) {
			addr = bcl_dev->core_conf[i].base_mem + CLKDIVSTEP;
			mutex_lock(&bcl_dev->ratio_lock);
			if (bcl_disable_power(i)) {
				reg = __raw_readl(addr);
				__raw_writel(reg | 0x1, addr);
				bcl_enable_power(i);
			}
			mutex_unlock(&bcl_dev->ratio_lock);
		}
		for (i = 0; i < TRIGGERED_SOURCE_MAX; i++)
			if (bcl_dev->zone[i] && i != BATOILO)
				enable_irq(bcl_dev->zone[i]->bcl_irq);
	} else {
		bcl_dev->core_conf[SUBSYSTEM_TPU].clkdivstep &= ~(1 << 0);
		bcl_dev->core_conf[SUBSYSTEM_GPU].clkdivstep &= ~(1 << 0);
		bcl_dev->core_conf[SUBSYSTEM_AUR].clkdivstep &= ~(1 << 0);
		for (i = 0; i < CPU_CLUSTER_MAX; i++) {
			addr = bcl_dev->core_conf[i].base_mem + CLKDIVSTEP;
			mutex_lock(&bcl_dev->ratio_lock);
			if (bcl_disable_power(i)) {
				reg = __raw_readl(addr);
				__raw_writel(reg & ~(1 << 0), addr);
				bcl_enable_power(i);
			}
			mutex_unlock(&bcl_dev->ratio_lock);
		}
		for (i = 0; i < TRIGGERED_SOURCE_MAX; i++)
			if (bcl_dev->zone[i] && i != BATOILO)
				disable_irq(bcl_dev->zone[i]->bcl_irq);
	}
	return size;
}

static DEVICE_ATTR_RW(enable_mitigation);

static ssize_t main_offsrc1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->main_offsrc1);
}

static DEVICE_ATTR_RO(main_offsrc1);

static ssize_t main_offsrc2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->main_offsrc2);
}

static DEVICE_ATTR_RO(main_offsrc2);

static ssize_t sub_offsrc1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->sub_offsrc1);
}

static DEVICE_ATTR_RO(sub_offsrc1);

static ssize_t sub_offsrc2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->sub_offsrc2);
}

static DEVICE_ATTR_RO(sub_offsrc2);

static ssize_t pwronsrc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->pwronsrc);
}

static DEVICE_ATTR_RO(pwronsrc);

static ssize_t ready_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->ready);
}
static DEVICE_ATTR_RO(ready);

static struct attribute *instr_attrs[] = {
	&dev_attr_mid_db_settings.attr,
	&dev_attr_big_db_settings.attr,
	&dev_attr_enable_mitigation.attr,
	&dev_attr_main_offsrc1.attr,
	&dev_attr_main_offsrc2.attr,
	&dev_attr_sub_offsrc1.attr,
	&dev_attr_sub_offsrc2.attr,
	&dev_attr_pwronsrc.attr,
	&dev_attr_ready.attr,
	&dev_attr_irq_delay.attr,
	NULL,
};

static const struct attribute_group instr_group = {
	.attrs = instr_attrs,
	.name = "instruction",
};

static ssize_t uvlo1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int uvlo1_lvl;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO1])
		return -EIO;
	if (!bcl_dev->intf_pmic_i2c)
		return -EBUSY;
	if (bcl_cb_uvlo1_read(bcl_dev, &uvlo1_lvl) < 0)
		return -EINVAL;
	bcl_dev->zone[UVLO1]->bcl_lvl = VD_BATTERY_VOLTAGE - uvlo1_lvl - THERMAL_HYST_LEVEL;
	return sysfs_emit(buf, "%dmV\n", uvlo1_lvl);
}

static ssize_t uvlo1_lvl_store(struct device *dev,
			       struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO1])
		return -EIO;
	if (value < VD_LOWER_LIMIT || value > VD_UPPER_LIMIT) {
		dev_err(bcl_dev->device, "UVLO1 %d outside of range %d - %d mV.", value,
			VD_LOWER_LIMIT, VD_UPPER_LIMIT);
		return -EINVAL;
	}
	if (!bcl_dev->intf_pmic_i2c)
		return -EIO;
	disable_irq(bcl_dev->zone[UVLO1]->bcl_irq);
	if (bcl_cb_uvlo1_write(bcl_dev, value) < 0) {
		enable_irq(bcl_dev->zone[UVLO1]->bcl_irq);
		return -EIO;
	}
	bcl_dev->zone[UVLO1]->bcl_lvl = VD_BATTERY_VOLTAGE - value - THERMAL_HYST_LEVEL;
	ret = bcl_dev->zone[UVLO1]->tz->ops->set_trip_temp(bcl_dev->zone[UVLO1]->tz, 0,
							   VD_BATTERY_VOLTAGE - value);
	if (bcl_dev->zone[UVLO1]->tz)
		thermal_zone_device_update(bcl_dev->zone[UVLO1]->tz, THERMAL_EVENT_UNSPECIFIED);
	if (ret)
		dev_err(bcl_dev->device, "Fail to set sys_uvlo1 trip temp\n");
	enable_irq(bcl_dev->zone[UVLO1]->bcl_irq);
	return size;

}

static DEVICE_ATTR_RW(uvlo1_lvl);

static ssize_t uvlo2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int uvlo2_lvl;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO2])
		return -EIO;
	if (!bcl_dev->intf_pmic_i2c)
		return -EBUSY;
	if (bcl_cb_uvlo2_read(bcl_dev, &uvlo2_lvl) < 0)
		return -EINVAL;
	bcl_dev->zone[UVLO2]->bcl_lvl = VD_BATTERY_VOLTAGE - uvlo2_lvl - THERMAL_HYST_LEVEL;
	return sysfs_emit(buf, "%umV\n", uvlo2_lvl);
}

static ssize_t uvlo2_lvl_store(struct device *dev,
			       struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO2])
		return -EIO;
	if (value < VD_LOWER_LIMIT || value > VD_UPPER_LIMIT) {
		dev_err(bcl_dev->device, "UVLO2 %d outside of range %d - %d mV.", value,
			VD_LOWER_LIMIT, VD_UPPER_LIMIT);
		return -EINVAL;
	}
	if (!bcl_dev->intf_pmic_i2c)
		return -EIO;
	disable_irq(bcl_dev->zone[UVLO2]->bcl_irq);
	if (bcl_cb_uvlo2_write(bcl_dev, value) < 0) {
		enable_irq(bcl_dev->zone[UVLO2]->bcl_irq);
		return -EIO;
	}
	bcl_dev->zone[UVLO2]->bcl_lvl = VD_BATTERY_VOLTAGE - value - THERMAL_HYST_LEVEL;
	ret = bcl_dev->zone[UVLO2]->tz->ops->set_trip_temp(bcl_dev->zone[UVLO2]->tz, 0,
							   VD_BATTERY_VOLTAGE - value);
	if (bcl_dev->zone[UVLO2]->tz)
		thermal_zone_device_update(bcl_dev->zone[UVLO2]->tz, THERMAL_EVENT_UNSPECIFIED);
	if (ret)
		dev_err(bcl_dev->device, "Fail to set sys_uvlo2 trip temp\n");
	enable_irq(bcl_dev->zone[UVLO2]->bcl_irq);
	return size;
}

static DEVICE_ATTR_RW(uvlo2_lvl);

static ssize_t batoilo_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int batoilo_lvl;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[BATOILO])
		return -EIO;
	if (!bcl_dev->intf_pmic_i2c)
		return -EBUSY;
	if (bcl_cb_batoilo_read(bcl_dev, &batoilo_lvl) < 0)
		return -EINVAL;
	bcl_dev->zone[BATOILO]->bcl_lvl = batoilo_lvl;
	return sysfs_emit(buf, "%umA\n", batoilo_lvl);
}

static ssize_t batoilo_lvl_store(struct device *dev,
				 struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[BATOILO])
		return -EIO;
	if (value < BO_LOWER_LIMIT || value > BO_UPPER_LIMIT) {
		dev_err(bcl_dev->device, "BATOILO %d outside of range %d - %d mA.", value,
			BO_LOWER_LIMIT, BO_UPPER_LIMIT);
		return -EINVAL;
	}

	if (bcl_cb_batoilo_write(bcl_dev, value) < 0) {
		return -EIO;
	}
	bcl_dev->zone[BATOILO]->bcl_lvl = value - THERMAL_HYST_LEVEL;
	ret = bcl_dev->zone[BATOILO]->tz->ops->set_trip_temp(bcl_dev->zone[BATOILO]->tz, 0, value);
	if (bcl_dev->zone[BATOILO]->tz)
		thermal_zone_device_update(bcl_dev->zone[BATOILO]->tz, THERMAL_EVENT_UNSPECIFIED);
	if (ret)
		dev_err(bcl_dev->device, "Fail to set sys_uvlo2 trip temp\n");
	return size;
}

static DEVICE_ATTR_RW(batoilo_lvl);

static ssize_t smpl_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u8 value = 0;
	unsigned int smpl_warn_lvl;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->main_pmic_i2c) {
		return -EBUSY;
	}
	pmic_read(CORE_PMIC_MAIN, bcl_dev, SMPL_WARN_CTRL, &value);
	value >>= SMPL_WARN_SHIFT;

	smpl_warn_lvl = value * 100 + SMPL_LOWER_LIMIT;
	return sysfs_emit(buf, "%umV\n", smpl_warn_lvl);
}

static ssize_t smpl_lvl_store(struct device *dev,
			      struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int val;
	u8 value;
	int ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return ret;

	if (!bcl_dev)
		return -EIO;
	if (val < SMPL_LOWER_LIMIT || val > SMPL_UPPER_LIMIT) {
		dev_err(bcl_dev->device, "SMPL_WARN LEVEL %d outside of range %d - %d mV.", val,
			SMPL_LOWER_LIMIT, SMPL_UPPER_LIMIT);
		return -EINVAL;
	}
	if (!bcl_dev->main_pmic_i2c) {
		dev_err(bcl_dev->device, "MAIN I2C not found\n");
		return -EIO;
	}
	if (pmic_read(CORE_PMIC_MAIN, bcl_dev, SMPL_WARN_CTRL, &value)) {
		dev_err(bcl_dev->device, "S2MPG1415 read 0x%x failed.", SMPL_WARN_CTRL);
		return -EBUSY;
	}
	disable_irq(bcl_dev->zone[SMPL_WARN]->bcl_irq);
	value &= ~SMPL_WARN_MASK;
	value |= ((val - SMPL_LOWER_LIMIT) / 100) << SMPL_WARN_SHIFT;
	if (pmic_write(CORE_PMIC_MAIN, bcl_dev, SMPL_WARN_CTRL, value)) {
		dev_err(bcl_dev->device, "i2c write error setting smpl_warn\n");
		enable_irq(bcl_dev->zone[SMPL_WARN]->bcl_irq);
		return ret;
	}
	bcl_dev->zone[SMPL_WARN]->bcl_lvl = SMPL_BATTERY_VOLTAGE - val - THERMAL_HYST_LEVEL;
	ret = bcl_dev->zone[SMPL_WARN]->tz->ops->set_trip_temp(bcl_dev->zone[SMPL_WARN]->tz, 0,
							       SMPL_BATTERY_VOLTAGE - val);
	if (ret)
		dev_err(bcl_dev->device, "Fail to set smpl_warn trip temp\n");
	if (bcl_dev->zone[SMPL_WARN]->tz)
		thermal_zone_device_update(bcl_dev->zone[SMPL_WARN]->tz, THERMAL_EVENT_UNSPECIFIED);
	enable_irq(bcl_dev->zone[SMPL_WARN]->bcl_irq);

	return size;

}

static DEVICE_ATTR_RW(smpl_lvl);

static int get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 addr, u8 pmic, u8 mask, u16 limit,
		       u16 step)
{
	u8 value = 0;
	unsigned int ocp_warn_lvl;

	if (!bcl_dev)
		return -EIO;
	if (pmic_read(pmic, bcl_dev, addr, &value)) {
		dev_err(bcl_dev->device, "S2MPG1415 read 0x%x failed.", addr);
		return -EBUSY;
	}
	value &= mask;
	ocp_warn_lvl = limit - value * step;
	*val = ocp_warn_lvl;
	return 0;
}

static int set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 addr, u8 pmic, u8 mask,
		       u16 llimit, u16 ulimit, u16 step, u8 id)
{
	u8 value;
	int ret;

	if (!bcl_dev)
		return -EIO;
	if (val < llimit || val > ulimit) {
		dev_err(bcl_dev->device, "OCP_WARN LEVEL %llu outside of range %d - %d mA.", val,
		       llimit, ulimit);
		return -EBUSY;
	}
	if (pmic_read(pmic, bcl_dev, addr, &value)) {
		dev_err(bcl_dev->device, "S2MPG1415 read 0x%x failed.", addr);
		return -EBUSY;
	}
	disable_irq(bcl_dev->zone[id]->bcl_irq);
	value &= ~(OCP_WARN_MASK) << OCP_WARN_LVL_SHIFT;
	value |= ((ulimit - val) / step) << OCP_WARN_LVL_SHIFT;
	ret = pmic_write(pmic, bcl_dev, addr, value);
	if (!ret) {
		bcl_dev->zone[id]->bcl_lvl = val - THERMAL_HYST_LEVEL;
		ret = bcl_dev->zone[id]->tz->ops->set_trip_temp(bcl_dev->zone[id]->tz, 0, val);
		if (ret)
			dev_err(bcl_dev->device, "Fail to set ocp_warn trip temp\n");
	}
	if (bcl_dev->zone[id]->tz)
		thermal_zone_device_update(bcl_dev->zone[id]->tz, THERMAL_EVENT_UNSPECIFIED);
	enable_irq(bcl_dev->zone[id]->bcl_irq);

	return ret;
}

static ssize_t ocp_cpu1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, CPU1_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU1_UPPER_LIMIT, CPU1_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_cpu1_lvl_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, CPU1_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU1_LOWER_LIMIT, CPU1_UPPER_LIMIT, CPU1_STEP, OCP_WARN_CPUCL1) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(ocp_cpu1_lvl);

static ssize_t ocp_cpu2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, CPU2_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU2_UPPER_LIMIT, CPU2_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_cpu2_lvl_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, CPU2_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU2_LOWER_LIMIT, CPU2_UPPER_LIMIT, CPU2_STEP, OCP_WARN_CPUCL2) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(ocp_cpu2_lvl);

static ssize_t ocp_tpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, TPU_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                TPU_UPPER_LIMIT, TPU_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_tpu_lvl_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, TPU_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                TPU_LOWER_LIMIT, TPU_UPPER_LIMIT, TPU_STEP, OCP_WARN_TPU) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(ocp_tpu_lvl);

static ssize_t ocp_gpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, GPU_OCP_WARN, CORE_PMIC_SUB, OCP_WARN_MASK, GPU_UPPER_LIMIT,
			GPU_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_gpu_lvl_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, GPU_OCP_WARN, CORE_PMIC_SUB, OCP_WARN_MASK,
	                GPU_LOWER_LIMIT, GPU_UPPER_LIMIT, GPU_STEP, OCP_WARN_GPU) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(ocp_gpu_lvl);

static ssize_t soft_ocp_cpu1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, SOFT_CPU1_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU1_UPPER_LIMIT, CPU1_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t soft_ocp_cpu1_lvl_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, SOFT_CPU1_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU1_LOWER_LIMIT, CPU1_UPPER_LIMIT, CPU1_STEP, SOFT_OCP_WARN_CPUCL1) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(soft_ocp_cpu1_lvl);

static ssize_t soft_ocp_cpu2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, SOFT_CPU2_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU2_UPPER_LIMIT, CPU2_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t soft_ocp_cpu2_lvl_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, SOFT_CPU2_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                CPU2_LOWER_LIMIT, CPU2_UPPER_LIMIT, CPU2_STEP, SOFT_OCP_WARN_CPUCL2) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(soft_ocp_cpu2_lvl);

static ssize_t soft_ocp_tpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, SOFT_TPU_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                TPU_UPPER_LIMIT, TPU_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t soft_ocp_tpu_lvl_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, SOFT_TPU_OCP_WARN, CORE_PMIC_MAIN, OCP_WARN_MASK,
	                TPU_LOWER_LIMIT, TPU_UPPER_LIMIT, TPU_STEP, SOFT_OCP_WARN_TPU) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(soft_ocp_tpu_lvl);

static ssize_t soft_ocp_gpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (get_ocp_lvl(bcl_dev, &val, SOFT_GPU_OCP_WARN, CORE_PMIC_SUB, OCP_WARN_MASK,
	                GPU_UPPER_LIMIT, GPU_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t soft_ocp_gpu_lvl_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (set_ocp_lvl(bcl_dev, value, SOFT_GPU_OCP_WARN, CORE_PMIC_SUB, OCP_WARN_MASK,
	                GPU_LOWER_LIMIT, GPU_UPPER_LIMIT, GPU_STEP, SOFT_OCP_WARN_GPU) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(soft_ocp_gpu_lvl);

static struct attribute *triggered_lvl_attrs[] = {
	&dev_attr_uvlo1_lvl.attr,
	&dev_attr_uvlo2_lvl.attr,
	&dev_attr_batoilo_lvl.attr,
	&dev_attr_smpl_lvl.attr,
	&dev_attr_ocp_cpu1_lvl.attr,
	&dev_attr_ocp_cpu2_lvl.attr,
	&dev_attr_ocp_tpu_lvl.attr,
	&dev_attr_ocp_gpu_lvl.attr,
	&dev_attr_soft_ocp_cpu1_lvl.attr,
	&dev_attr_soft_ocp_cpu2_lvl.attr,
	&dev_attr_soft_ocp_tpu_lvl.attr,
	&dev_attr_soft_ocp_gpu_lvl.attr,
	NULL,
};

static const struct attribute_group triggered_lvl_group = {
	.attrs = triggered_lvl_attrs,
	.name = "triggered_lvl",
};

static ssize_t clk_div_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	unsigned int reg = 0;
	void __iomem *addr;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case SUBSYSTEM_TPU:
	case SUBSYSTEM_GPU:
	case SUBSYSTEM_AUR:
		return sysfs_emit(buf, "0x%x\n", bcl_dev->core_conf[idx].clkdivstep);
	case SUBSYSTEM_CPU1:
		if (!bcl_is_cluster_on(CPU1_CLUSTER_MIN))
			return sysfs_emit(buf, "off\n");
		break;
	case SUBSYSTEM_CPU2:
		if (!bcl_is_cluster_on(CPU2_CLUSTER_MIN))
			return sysfs_emit(buf, "off\n");
		break;
	}
	addr = bcl_dev->core_conf[idx].base_mem + CLKDIVSTEP;
	if (addr == NULL)
		return -EIO;
	if (!bcl_disable_power(idx))
		return sysfs_emit(buf, "off\n");
	reg = __raw_readl(addr);
	bcl_enable_power(idx);

	return sysfs_emit(buf, "0x%x\n", reg);
}

static ssize_t clk_stats_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	unsigned int reg = 0;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case SUBSYSTEM_TPU:
	case SUBSYSTEM_GPU:
	case SUBSYSTEM_AUR:
		return sysfs_emit(buf, "0x%x\n", bcl_dev->core_conf[idx].clk_stats);
	case SUBSYSTEM_CPU1:
		if (!bcl_is_cluster_on(CPU1_CLUSTER_MIN))
			return sysfs_emit(buf, "off\n");
		break;
	case SUBSYSTEM_CPU2:
		if (!bcl_is_cluster_on(CPU2_CLUSTER_MIN))
			return sysfs_emit(buf, "off\n");
		break;
	case SUBSYSTEM_CPU0:
		break;
	}
	if (!bcl_disable_power(idx))
		return sysfs_emit(buf, "off\n");
	reg = __raw_readl(bcl_dev->core_conf[idx].base_mem + clk_stats_offset[idx]);
	bcl_enable_power(idx);

	return sysfs_emit(buf, "0x%x\n", reg);
}
static ssize_t cpu0_clk_div_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_show(bcl_dev, SUBSYSTEM_CPU0, buf);
}

static ssize_t clk_div_store(struct bcl_device *bcl_dev, int idx,
			     const char *buf, size_t size)
{
	void __iomem *addr;
	unsigned int value;
	int ret;

	ret = sscanf(buf, "0x%x", &value);
	if (ret != 1)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;
	bcl_dev->core_conf[idx].clkdivstep = value;
	switch (idx) {
	case SUBSYSTEM_TPU:
	case SUBSYSTEM_GPU:
	case SUBSYSTEM_AUR:
		return size;
	case SUBSYSTEM_CPU1:
		if (!bcl_is_cluster_on(CPU1_CLUSTER_MIN))
			return -EIO;
		break;
	case SUBSYSTEM_CPU2:
		if (!bcl_is_cluster_on(CPU2_CLUSTER_MIN))
			return -EIO;
		break;
	case SUBSYSTEM_CPU0:
		break;
	}
	addr = bcl_dev->core_conf[idx].base_mem + CLKDIVSTEP;
	if (addr == NULL)
		return -EIO;
	mutex_lock(&bcl_dev->ratio_lock);
	if (!bcl_disable_power(idx)) {
		mutex_unlock(&bcl_dev->ratio_lock);
		return -EIO;
	}
	__raw_writel(value, addr);
	bcl_enable_power(idx);
	mutex_unlock(&bcl_dev->ratio_lock);

	return size;
}

static ssize_t cpu0_clk_div_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_store(bcl_dev, SUBSYSTEM_CPU0, buf, size);
}

static DEVICE_ATTR_RW(cpu0_clk_div);

static ssize_t cpu1_clk_div_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_show(bcl_dev, SUBSYSTEM_CPU1, buf);
}

static ssize_t cpu1_clk_div_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_store(bcl_dev, SUBSYSTEM_CPU1, buf, size);
}

static DEVICE_ATTR_RW(cpu1_clk_div);

static ssize_t cpu2_clk_div_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_show(bcl_dev, SUBSYSTEM_CPU2, buf);
}

static ssize_t cpu2_clk_div_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_store(bcl_dev, SUBSYSTEM_CPU2, buf, size);
}

static DEVICE_ATTR_RW(cpu2_clk_div);

static ssize_t tpu_clk_div_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_show(bcl_dev, SUBSYSTEM_TPU, buf);
}

static ssize_t tpu_clk_div_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_store(bcl_dev, SUBSYSTEM_TPU, buf, size);
}

static DEVICE_ATTR_RW(tpu_clk_div);

static ssize_t aur_clk_div_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_show(bcl_dev, SUBSYSTEM_AUR, buf);
}

static ssize_t aur_clk_div_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_store(bcl_dev, SUBSYSTEM_AUR, buf, size);
}

static DEVICE_ATTR_RW(aur_clk_div);

static ssize_t gpu_clk_div_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_show(bcl_dev, SUBSYSTEM_GPU, buf);
}

static ssize_t gpu_clk_div_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_div_store(bcl_dev, SUBSYSTEM_GPU, buf, size);
}

static DEVICE_ATTR_RW(gpu_clk_div);

static struct attribute *clock_div_attrs[] = {
	&dev_attr_cpu0_clk_div.attr,
	&dev_attr_cpu1_clk_div.attr,
	&dev_attr_cpu2_clk_div.attr,
	&dev_attr_tpu_clk_div.attr,
	&dev_attr_gpu_clk_div.attr,
	&dev_attr_aur_clk_div.attr,
	NULL,
};

static const struct attribute_group clock_div_group = {
	.attrs = clock_div_attrs,
	.name = "clock_div",
};

static ssize_t clk_ratio_show(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx, char *buf,
			      int sub_idx)
{
	unsigned int reg = 0;
	void __iomem *addr;

	if (!bcl_dev)
		return -EIO;

	switch (idx) {
	case TPU_HEAVY:
	case GPU_HEAVY:
		return sysfs_emit(buf, "0x%x\n", bcl_dev->core_conf[sub_idx].con_heavy);
	case TPU_LIGHT:
	case GPU_LIGHT:
		return sysfs_emit(buf, "0x%x\n", bcl_dev->core_conf[sub_idx].con_light);
	case CPU1_LIGHT:
	case CPU2_LIGHT:
		addr = bcl_dev->core_conf[sub_idx].base_mem + CLKDIVSTEP_CON_LIGHT;
		break;
	case CPU1_HEAVY:
	case CPU2_HEAVY:
		addr = bcl_dev->core_conf[sub_idx].base_mem + CLKDIVSTEP_CON_HEAVY;
		break;
	case CPU0_CON:
		addr = bcl_dev->core_conf[sub_idx].base_mem + CPUCL0_CLKDIVSTEP_CON;
		break;
	}
	if (addr == NULL)
		return -EIO;
	if (!bcl_disable_power(sub_idx))
		return sysfs_emit(buf, "off\n");
	reg = __raw_readl(addr);
	bcl_enable_power(sub_idx);
	return sysfs_emit(buf, "0x%x\n", reg);
}

static ssize_t clk_ratio_store(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
			       const char *buf, size_t size, int sub_idx)
{
	void __iomem *addr;
	unsigned int value;
	int ret;

	ret = sscanf(buf, "0x%x", &value);
	if (ret != 1)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;

	switch (idx) {
	case TPU_HEAVY:
	case GPU_HEAVY:
		bcl_dev->core_conf[sub_idx].con_heavy = value;
		return size;
	case TPU_LIGHT:
	case GPU_LIGHT:
		bcl_dev->core_conf[sub_idx].con_light = value;
		return size;
	case CPU1_LIGHT:
	case CPU2_LIGHT:
		addr = bcl_dev->core_conf[sub_idx].base_mem + CLKDIVSTEP_CON_LIGHT;
		break;
	case CPU1_HEAVY:
	case CPU2_HEAVY:
		addr = bcl_dev->core_conf[sub_idx].base_mem + CLKDIVSTEP_CON_HEAVY;
		break;
	case CPU0_CON:
		addr = bcl_dev->core_conf[sub_idx].base_mem + CPUCL0_CLKDIVSTEP_CON;
		break;
	}
	if (addr == NULL)
		return -EIO;
	mutex_lock(&bcl_dev->ratio_lock);
	if (!bcl_disable_power(sub_idx)) {
		mutex_unlock(&bcl_dev->ratio_lock);
		return -EIO;
	}
	__raw_writel(value, addr);
	bcl_enable_power(sub_idx);
	mutex_unlock(&bcl_dev->ratio_lock);

	return size;
}

static ssize_t cpu0_clk_ratio_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, CPU0_CON, buf, SUBSYSTEM_CPU0);
}

static ssize_t cpu0_clk_ratio_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, CPU0_CON, buf, size, SUBSYSTEM_CPU0);
}

static DEVICE_ATTR_RW(cpu0_clk_ratio);

static ssize_t cpu1_heavy_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, CPU1_HEAVY, buf, SUBSYSTEM_CPU1);
}

static ssize_t cpu1_heavy_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, CPU1_HEAVY, buf, size, SUBSYSTEM_CPU1);
}

static DEVICE_ATTR_RW(cpu1_heavy_clk_ratio);

static ssize_t cpu2_heavy_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, CPU2_HEAVY, buf, SUBSYSTEM_CPU2);
}

static ssize_t cpu2_heavy_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, CPU2_HEAVY, buf, size, SUBSYSTEM_CPU2);
}

static DEVICE_ATTR_RW(cpu2_heavy_clk_ratio);

static ssize_t tpu_heavy_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, TPU_HEAVY, buf, SUBSYSTEM_TPU);
}

static ssize_t tpu_heavy_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, TPU_HEAVY, buf, size, SUBSYSTEM_TPU);
}

static DEVICE_ATTR_RW(tpu_heavy_clk_ratio);

static ssize_t gpu_heavy_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, GPU_HEAVY, buf, SUBSYSTEM_GPU);
}

static ssize_t gpu_heavy_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, GPU_HEAVY, buf, size, SUBSYSTEM_GPU);
}

static DEVICE_ATTR_RW(gpu_heavy_clk_ratio);

static ssize_t cpu1_light_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, CPU1_LIGHT, buf, SUBSYSTEM_CPU1);
}

static ssize_t cpu1_light_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, CPU1_LIGHT, buf, size, SUBSYSTEM_CPU1);
}

static DEVICE_ATTR_RW(cpu1_light_clk_ratio);

static ssize_t cpu2_light_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, CPU2_LIGHT, buf, SUBSYSTEM_CPU2);
}

static ssize_t cpu2_light_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, CPU2_LIGHT, buf, size, SUBSYSTEM_CPU2);
}

static DEVICE_ATTR_RW(cpu2_light_clk_ratio);

static ssize_t tpu_light_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, TPU_LIGHT, buf, SUBSYSTEM_TPU);
}

static ssize_t tpu_light_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, TPU_LIGHT, buf, size, SUBSYSTEM_TPU);
}

static DEVICE_ATTR_RW(tpu_light_clk_ratio);

static ssize_t gpu_light_clk_ratio_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_show(bcl_dev, GPU_LIGHT, buf, SUBSYSTEM_GPU);
}

static ssize_t gpu_light_clk_ratio_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_ratio_store(bcl_dev, GPU_LIGHT, buf, size, SUBSYSTEM_GPU);
}

static DEVICE_ATTR_RW(gpu_light_clk_ratio);

static struct attribute *clock_ratio_attrs[] = {
	&dev_attr_cpu0_clk_ratio.attr,
	&dev_attr_cpu1_heavy_clk_ratio.attr,
	&dev_attr_cpu2_heavy_clk_ratio.attr,
	&dev_attr_tpu_heavy_clk_ratio.attr,
	&dev_attr_gpu_heavy_clk_ratio.attr,
	&dev_attr_cpu1_light_clk_ratio.attr,
	&dev_attr_cpu2_light_clk_ratio.attr,
	&dev_attr_tpu_light_clk_ratio.attr,
	&dev_attr_gpu_light_clk_ratio.attr,
	NULL,
};

static const struct attribute_group clock_ratio_group = {
	.attrs = clock_ratio_attrs,
	.name = "clock_ratio",
};

static ssize_t cpu0_clk_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_stats_show(bcl_dev, SUBSYSTEM_CPU0, buf);
}

static DEVICE_ATTR_RO(cpu0_clk_stats);

static ssize_t cpu1_clk_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_stats_show(bcl_dev, SUBSYSTEM_CPU1, buf);
}

static DEVICE_ATTR_RO(cpu1_clk_stats);

static ssize_t cpu2_clk_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_stats_show(bcl_dev, SUBSYSTEM_CPU2, buf);
}

static DEVICE_ATTR_RO(cpu2_clk_stats);

static ssize_t tpu_clk_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_stats_show(bcl_dev, SUBSYSTEM_TPU, buf);
}

static DEVICE_ATTR_RO(tpu_clk_stats);

static ssize_t aur_clk_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_stats_show(bcl_dev, SUBSYSTEM_AUR, buf);
}

static DEVICE_ATTR_RO(aur_clk_stats);

static ssize_t gpu_clk_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return clk_stats_show(bcl_dev, SUBSYSTEM_GPU, buf);
}

static DEVICE_ATTR_RO(gpu_clk_stats);

static struct attribute *clock_stats_attrs[] = {
	&dev_attr_cpu0_clk_stats.attr,
	&dev_attr_cpu1_clk_stats.attr,
	&dev_attr_cpu2_clk_stats.attr,
	&dev_attr_tpu_clk_stats.attr,
	&dev_attr_gpu_clk_stats.attr,
	&dev_attr_aur_clk_stats.attr,
	NULL,
};

static const struct attribute_group clock_stats_group = {
	.attrs = clock_stats_attrs,
	.name = "clock_stats",
};

static struct attribute *triggered_count_attrs[] = {
	&dev_attr_smpl_warn_count.attr,
	&dev_attr_ocp_cpu1_count.attr,
	&dev_attr_ocp_cpu2_count.attr,
	&dev_attr_ocp_tpu_count.attr,
	&dev_attr_ocp_gpu_count.attr,
	&dev_attr_soft_ocp_cpu1_count.attr,
	&dev_attr_soft_ocp_cpu2_count.attr,
	&dev_attr_soft_ocp_tpu_count.attr,
	&dev_attr_soft_ocp_gpu_count.attr,
	&dev_attr_vdroop1_count.attr,
	&dev_attr_vdroop2_count.attr,
	&dev_attr_batoilo_count.attr,
	NULL,
};

static const struct attribute_group triggered_count_group = {
	.attrs = triggered_count_attrs,
	.name = "last_triggered_count",
};

static struct attribute *triggered_time_attrs[] = {
	&dev_attr_smpl_warn_time.attr,
	&dev_attr_ocp_cpu1_time.attr,
	&dev_attr_ocp_cpu2_time.attr,
	&dev_attr_ocp_tpu_time.attr,
	&dev_attr_ocp_gpu_time.attr,
	&dev_attr_soft_ocp_cpu1_time.attr,
	&dev_attr_soft_ocp_cpu2_time.attr,
	&dev_attr_soft_ocp_tpu_time.attr,
	&dev_attr_soft_ocp_gpu_time.attr,
	&dev_attr_vdroop1_time.attr,
	&dev_attr_vdroop2_time.attr,
	&dev_attr_batoilo_time.attr,
	NULL,
};

static const struct attribute_group triggered_timestamp_group = {
	.attrs = triggered_time_attrs,
	.name = "last_triggered_timestamp",
};

static struct attribute *triggered_cap_attrs[] = {
	&dev_attr_smpl_warn_cap.attr,
	&dev_attr_ocp_cpu1_cap.attr,
	&dev_attr_ocp_cpu2_cap.attr,
	&dev_attr_ocp_tpu_cap.attr,
	&dev_attr_ocp_gpu_cap.attr,
	&dev_attr_soft_ocp_cpu1_cap.attr,
	&dev_attr_soft_ocp_cpu2_cap.attr,
	&dev_attr_soft_ocp_tpu_cap.attr,
	&dev_attr_soft_ocp_gpu_cap.attr,
	&dev_attr_vdroop1_cap.attr,
	&dev_attr_vdroop2_cap.attr,
	&dev_attr_batoilo_cap.attr,
	NULL,
};

static const struct attribute_group triggered_capacity_group = {
	.attrs = triggered_cap_attrs,
	.name = "last_triggered_capacity",
};

static struct attribute *triggered_volt_attrs[] = {
	&dev_attr_smpl_warn_volt.attr,
	&dev_attr_ocp_cpu1_volt.attr,
	&dev_attr_ocp_cpu2_volt.attr,
	&dev_attr_ocp_tpu_volt.attr,
	&dev_attr_ocp_gpu_volt.attr,
	&dev_attr_soft_ocp_cpu1_volt.attr,
	&dev_attr_soft_ocp_cpu2_volt.attr,
	&dev_attr_soft_ocp_tpu_volt.attr,
	&dev_attr_soft_ocp_gpu_volt.attr,
	&dev_attr_vdroop1_volt.attr,
	&dev_attr_vdroop2_volt.attr,
	&dev_attr_batoilo_volt.attr,
	NULL,
};

static const struct attribute_group triggered_voltage_group = {
	.attrs = triggered_volt_attrs,
	.name = "last_triggered_voltage",
};

static ssize_t vdroop_flt_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	unsigned int reg = 0;
	void __iomem *addr;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case SUBSYSTEM_TPU:
	case SUBSYSTEM_GPU:
		return sysfs_emit(buf, "0x%x\n", bcl_dev->core_conf[idx].vdroop_flt);
	case SUBSYSTEM_CPU1:
	case SUBSYSTEM_CPU2:
		addr = bcl_dev->core_conf[idx].base_mem + VDROOP_FLT;
		break;
	}

	if (addr == NULL)
		return -EIO;
	if (!bcl_disable_power(idx))
		return sysfs_emit(buf, "off\n");
	reg = __raw_readl(addr);
	bcl_enable_power(idx);

	return sysfs_emit(buf, "0x%x\n", reg);
}

static ssize_t vdroop_flt_store(struct bcl_device *bcl_dev, int idx,
				const char *buf, size_t size)
{
	void __iomem *addr = NULL;
	unsigned int value;

	if (sscanf(buf, "0x%x", &value) != 1)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case SUBSYSTEM_TPU:
	case SUBSYSTEM_GPU:
		bcl_dev->core_conf[idx].vdroop_flt = value;
		return size;
	case SUBSYSTEM_CPU1:
	case SUBSYSTEM_CPU2:
		addr = bcl_dev->core_conf[idx].base_mem + VDROOP_FLT;
		break;
	}
	if (addr == NULL)
		return -EIO;
	mutex_lock(&bcl_dev->ratio_lock);
	if (!bcl_disable_power(idx)) {
		mutex_unlock(&bcl_dev->ratio_lock);
		return -EIO;
	}
	__raw_writel(value, addr);
	bcl_enable_power(idx);
	mutex_unlock(&bcl_dev->ratio_lock);

	return size;
}

static ssize_t cpu1_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, SUBSYSTEM_CPU1, buf);
}

static ssize_t cpu1_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, SUBSYSTEM_CPU1, buf, size);
}

static DEVICE_ATTR_RW(cpu1_vdroop_flt);

static ssize_t cpu2_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, SUBSYSTEM_CPU2, buf);
}

static ssize_t cpu2_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, SUBSYSTEM_CPU2, buf, size);
}

static DEVICE_ATTR_RW(cpu2_vdroop_flt);

static ssize_t tpu_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, SUBSYSTEM_TPU, buf);
}

static ssize_t tpu_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, SUBSYSTEM_TPU, buf, size);
}

static DEVICE_ATTR_RW(tpu_vdroop_flt);

static ssize_t gpu_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, SUBSYSTEM_GPU, buf);
}

static ssize_t gpu_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, SUBSYSTEM_GPU, buf, size);
}

static DEVICE_ATTR_RW(gpu_vdroop_flt);

static struct attribute *vdroop_flt_attrs[] = {
	&dev_attr_cpu1_vdroop_flt.attr,
	&dev_attr_cpu2_vdroop_flt.attr,
	&dev_attr_tpu_vdroop_flt.attr,
	&dev_attr_gpu_vdroop_flt.attr,
	NULL,
};

static const struct attribute_group vdroop_flt_group = {
	.attrs = vdroop_flt_attrs,
	.name = "vdroop_flt",
};

static ssize_t main_pwrwarn_threshold_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx;

	ret = sscanf(attr->attr.name, "main_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d=%lld\n", bcl_dev->main_setting[idx], bcl_dev->main_limit[idx]);
}

static ssize_t main_pwrwarn_threshold_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx, value;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;
	ret = sscanf(attr->attr.name, "main_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	bcl_dev->main_setting[idx] = value;
	bcl_dev->main_limit[idx] = settings_to_current(bcl_dev, CORE_PMIC_MAIN, idx,
	                                               value << LPF_CURRENT_SHIFT);
	meter_write(CORE_PMIC_MAIN, bcl_dev, S2MPG14_METER_PWR_WARN0 + idx, value);

	return size;
}

static ssize_t sub_pwrwarn_threshold_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx;

	ret = sscanf(attr->attr.name, "sub_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d=%lld\n", bcl_dev->sub_setting[idx], bcl_dev->sub_limit[idx]);
}

static ssize_t sub_pwrwarn_threshold_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx, value;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;
	ret = sscanf(attr->attr.name, "sub_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	bcl_dev->sub_setting[idx] = value;
	bcl_dev->sub_limit[idx] = settings_to_current(bcl_dev, CORE_PMIC_SUB, idx,
	                                              value << LPF_CURRENT_SHIFT);
	meter_write(CORE_PMIC_SUB, bcl_dev, S2MPG15_METER_PWR_WARN0 + idx, value);

	return size;
}

#define DEVICE_PWRWARN_ATTR(_name, _num) \
	struct device_attribute attr_##_name##_num = \
		__ATTR(_name##_num, 0644, _name##_show, _name##_store)

static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 0);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 1);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 2);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 3);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 4);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 5);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 6);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 7);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 8);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 9);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 10);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 11);

static struct attribute *main_pwrwarn_attrs[] = {
	&attr_main_pwrwarn_threshold0.attr,
	&attr_main_pwrwarn_threshold1.attr,
	&attr_main_pwrwarn_threshold2.attr,
	&attr_main_pwrwarn_threshold3.attr,
	&attr_main_pwrwarn_threshold4.attr,
	&attr_main_pwrwarn_threshold5.attr,
	&attr_main_pwrwarn_threshold6.attr,
	&attr_main_pwrwarn_threshold7.attr,
	&attr_main_pwrwarn_threshold8.attr,
	&attr_main_pwrwarn_threshold9.attr,
	&attr_main_pwrwarn_threshold10.attr,
	&attr_main_pwrwarn_threshold11.attr,
	NULL,
};

static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 0);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 1);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 2);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 3);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 4);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 5);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 6);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 7);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 8);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 9);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 10);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 11);

static struct attribute *sub_pwrwarn_attrs[] = {
	&attr_sub_pwrwarn_threshold0.attr,
	&attr_sub_pwrwarn_threshold1.attr,
	&attr_sub_pwrwarn_threshold2.attr,
	&attr_sub_pwrwarn_threshold3.attr,
	&attr_sub_pwrwarn_threshold4.attr,
	&attr_sub_pwrwarn_threshold5.attr,
	&attr_sub_pwrwarn_threshold6.attr,
	&attr_sub_pwrwarn_threshold7.attr,
	&attr_sub_pwrwarn_threshold8.attr,
	&attr_sub_pwrwarn_threshold9.attr,
	&attr_sub_pwrwarn_threshold10.attr,
	&attr_sub_pwrwarn_threshold11.attr,
	NULL,
};

static ssize_t qos_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	struct bcl_zone *zone;

	if (!bcl_dev)
		return -EIO;
	zone = bcl_dev->zone[idx];
	if ((!zone) || (!zone->bcl_qos))
		return -EIO;

	return sysfs_emit(buf, "CPU0,CPU1,CPU2,GPU,TPU\n%d,%d,%d,%d,%d\n",
			  zone->bcl_qos->cpu0_limit,
			  zone->bcl_qos->cpu1_limit,
			  zone->bcl_qos->cpu2_limit,
			  zone->bcl_qos->gpu_limit,
			  zone->bcl_qos->tpu_limit);
}

static ssize_t qos_store(struct bcl_device *bcl_dev, int idx, const char *buf, size_t size)
{
	unsigned int cpu0, cpu1, cpu2, gpu, tpu;
	struct bcl_zone *zone;

	if (sscanf(buf, "%d,%d,%d,%d,%d", &cpu0, &cpu1, &cpu2, &gpu, &tpu) != 5)
		return -EINVAL;
	if (!bcl_dev)
		return -EIO;
	zone = bcl_dev->zone[idx];
	if ((!zone) || (!zone->bcl_qos))
		return -EIO;
	zone->bcl_qos->cpu0_limit = cpu0;
	zone->bcl_qos->cpu1_limit = cpu1;
	zone->bcl_qos->cpu2_limit = cpu2;
	zone->bcl_qos->gpu_limit = gpu;
	zone->bcl_qos->tpu_limit = tpu;

	return size;
}

static ssize_t qos_batoilo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, BATOILO, buf);
}

static ssize_t qos_batoilo_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, BATOILO, buf, size);
}

static ssize_t qos_vdroop1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, UVLO1, buf);
}

static ssize_t qos_vdroop1_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, UVLO1, buf, size);
}

static ssize_t qos_vdroop2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, UVLO2, buf);
}

static ssize_t qos_vdroop2_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, UVLO2, buf, size);
}

static ssize_t qos_smpl_warn_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, SMPL_WARN, buf);
}

static ssize_t qos_smpl_warn_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, SMPL_WARN, buf, size);
}

static ssize_t qos_ocp_cpu2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, OCP_WARN_CPUCL2, buf);
}

static ssize_t qos_ocp_cpu2_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, OCP_WARN_CPUCL2, buf, size);
}

static ssize_t qos_ocp_cpu1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, OCP_WARN_CPUCL1, buf);
}

static ssize_t qos_ocp_cpu1_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, OCP_WARN_CPUCL1, buf, size);
}

static ssize_t qos_ocp_tpu_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, OCP_WARN_TPU, buf);
}

static ssize_t qos_ocp_tpu_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, OCP_WARN_TPU, buf, size);
}

static ssize_t qos_ocp_gpu_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, OCP_WARN_GPU, buf);
}

static ssize_t qos_ocp_gpu_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, OCP_WARN_GPU, buf, size);
}

static DEVICE_ATTR_RW(qos_batoilo);
static DEVICE_ATTR_RW(qos_vdroop1);
static DEVICE_ATTR_RW(qos_vdroop2);
static DEVICE_ATTR_RW(qos_smpl_warn);
static DEVICE_ATTR_RW(qos_ocp_cpu2);
static DEVICE_ATTR_RW(qos_ocp_cpu1);
static DEVICE_ATTR_RW(qos_ocp_gpu);
static DEVICE_ATTR_RW(qos_ocp_tpu);

static struct attribute *qos_attrs[] = {
	&dev_attr_qos_batoilo.attr,
	&dev_attr_qos_vdroop1.attr,
	&dev_attr_qos_vdroop2.attr,
	&dev_attr_qos_smpl_warn.attr,
	&dev_attr_qos_ocp_cpu2.attr,
	&dev_attr_qos_ocp_cpu1.attr,
	&dev_attr_qos_ocp_gpu.attr,
	&dev_attr_qos_ocp_tpu.attr,
	NULL,
};

static const struct attribute_group main_pwrwarn_group = {
	.attrs = main_pwrwarn_attrs,
	.name = "main_pwrwarn",
};

static const struct attribute_group sub_pwrwarn_group = {
	.attrs = sub_pwrwarn_attrs,
	.name = "sub_pwrwarn",
};

static ssize_t less_than_5ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.lt_5ms_count);
			count += scnprintf(buf + count, PAGE_SIZE - count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx].lt_5ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx].lt_5ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

static DEVICE_ATTR_RO(less_than_5ms_count);

static ssize_t between_5ms_to_10ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.bt_5ms_10ms_count);
			count += scnprintf(buf + count, PAGE_SIZE - count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx]
					.bt_5ms_10ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx]
					.bt_5ms_10ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

static DEVICE_ATTR_RO(between_5ms_to_10ms_count);

static ssize_t greater_than_10ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.gt_10ms_count);
			count += scnprintf(buf + count, PAGE_SIZE - count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx].gt_10ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx].gt_10ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

static DEVICE_ATTR_RO(greater_than_10ms_count);

static struct attribute *irq_dur_cnt_attrs[] = {
	&dev_attr_less_than_5ms_count.attr,
	&dev_attr_between_5ms_to_10ms_count.attr,
	&dev_attr_greater_than_10ms_count.attr,
	NULL,
};

static const struct attribute_group irq_dur_cnt_group = {
	.attrs = irq_dur_cnt_attrs,
	.name = "irq_dur_cnt",
};

static const struct attribute_group qos_group = {
	.attrs = qos_attrs,
	.name = "qos",
};

const struct attribute_group *mitigation_groups[] = {
	&instr_group,
	&triggered_lvl_group,
	&clock_div_group,
	&clock_ratio_group,
	&clock_stats_group,
	&triggered_count_group,
	&triggered_timestamp_group,
	&triggered_capacity_group,
	&triggered_voltage_group,
	&vdroop_flt_group,
	&main_pwrwarn_group,
	&sub_pwrwarn_group,
	&irq_dur_cnt_group,
	&qos_group,
	NULL,
};
