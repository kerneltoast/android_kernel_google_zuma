// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google, Inc.
 *
 * Google Memlat Devfreq Control Source.
 */
#define pr_fmt(fmt) "gs_memlat_devfreq: " fmt

#include <dt-bindings/soc/google/zuma-devfreq.h>
#include <governor.h>
#include <linux/of_platform.h>
#include <soc/google/cal-if.h>
#include <soc/google/ect_parser.h>
#include <soc/google/exynos-devfreq.h>
#include <trace/events/power.h>

#include "gs_governor_memlat.h"

#define MEMLAT_DEVFREQ_MODULE_NAME "gs-memlat-devfreq"
#define HZ_PER_KHZ 1000

static int gs_memlat_devfreq_target(struct device *parent,
				  unsigned long *target_freq, u32 flags)
{
	struct platform_device *pdev = container_of(parent, struct platform_device, dev);
	struct exynos_devfreq_data *data = platform_get_drvdata(pdev);

	if (exynos_pm_qos_request_active(&data->sys_pm_qos_min)) {
		exynos_pm_qos_update_request_async(&data->sys_pm_qos_min, *target_freq);
		trace_clock_set_rate(dev_name(data->dev), *target_freq, raw_smp_processor_id());
	}

	return 0;
}

static int gs_memlat_devfreq_get_cur_min_freq(struct device *dev,
				       unsigned long *freq)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct exynos_devfreq_data *data = platform_get_drvdata(pdev);
	int ret = 0;

	if (freq && data->pm_qos_class)
		*freq = exynos_pm_qos_read_req_value(data->pm_qos_class,
		  &data->sys_pm_qos_min);

	return ret;
}

static ssize_t show_scaling_devfreq_min(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct device *parent = dev->parent;
	struct platform_device *pdev =
		container_of(parent, struct platform_device, dev);
	struct exynos_devfreq_data *data = platform_get_drvdata(pdev);
	ssize_t count = 0;
	int val = 0;

	if (data->pm_qos_class)
		val = exynos_pm_qos_read_req_value(data->pm_qos_class, &data->sys_pm_qos_min);

	if (val <= 0) {
		dev_err(dev, "failed to read requested value\n");
		return count;
	}

	count += sysfs_emit_at(buf, count, "%d\n", val);

	return count;
}

static ssize_t store_scaling_devfreq_min(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct device *parent = dev->parent;
	struct platform_device *pdev =
		container_of(parent, struct platform_device, dev);
	struct exynos_devfreq_data *data = platform_get_drvdata(pdev);
	u32 qos_value;

	if (kstrtou32(buf, 10, &qos_value))
		return -EINVAL;

	if (exynos_pm_qos_request_active(&data->sys_pm_qos_min))
		exynos_pm_qos_update_request(&data->sys_pm_qos_min, qos_value);

	return count;
}

static DEVICE_ATTR(scaling_devfreq_min, 0440, show_scaling_devfreq_min,
		   store_scaling_devfreq_min);

#ifdef CONFIG_OF
static int exynos_devfreq_parse_dt(struct device_node *np,
				   struct exynos_devfreq_data *data)
{

#if IS_ENABLED(CONFIG_ECT)
	const char *devfreq_domain_name;
#endif
	if (!np) {
		pr_err("device tree node undefined\n");
		return -ENODEV;
	}
	/* Expected to be one of SIMPLE_INTERACTIVE MEM_LATENCY DSU_LATENCY. */
	if (of_property_read_u32(np, "devfreq_type", &data->devfreq_type)) {
		pr_err("devfreq_type undefined\n");
		return -ENODEV;
	}
	if (of_property_read_u32(np, "pm_qos_class", &data->pm_qos_class)) {
		pr_err("pm_qos_class undefined\n");
		return -ENODEV;
	}
	if (of_property_read_u32(np, "pm_qos_class_max", &data->pm_qos_class_max)) {
		pr_err("pm_qos_class undefined\n");
		return -ENODEV;
	}

	data->min_freq = 0;

	if (of_property_read_u32(np, "max-freq", &data->max_freq)) {
		pr_debug("max-freq undefined defaulting to INT_MAX\n");
		data->max_freq = INT_MAX;
	}

#if IS_ENABLED(CONFIG_ECT)
	if (of_property_read_string(np, "devfreq_domain_name",
				    &devfreq_domain_name))
		return -ENODEV;

	exynos_devfreq_parse_ect(data, devfreq_domain_name);
#endif


	if (of_property_read_u32(np, "governor", &data->gov_type))
		return -ENODEV;

	if (data->gov_type == MEM_LATENCY) {
		data->governor_name = "gs_memlat";
	} else {
		dev_err(data->dev, "invalid governor name (%s)\n",
			data->governor_name);
		return -EINVAL;
	}

	if (of_property_read_u32(np, "dfs_id", &data->dfs_id))
		return -ENODEV;

	of_property_read_u32(np, "polling_ms",
			     &data->devfreq_profile.polling_ms);

	return 0;
}
#else
static int exynos_devfreq_parse_dt(struct device_node *np,
				   struct exynos_devfrq_data *data)
{
	return -EINVAL;
}
#endif

static int exynos_init_freq_table(struct exynos_devfreq_data *data)
{
	int i, ret;
	u32 freq, volt;

	for (i = 0; i < data->max_state; i++) {
		freq = data->opp_list[i].freq;
		volt = data->opp_list[i].volt;

		data->devfreq_profile.freq_table[i] = freq;

		ret = dev_pm_opp_add(data->dev, freq, volt);
		if (ret) {
			dev_err(data->dev, "failed to add opp entries %uKhz\n",
				freq);
			return ret;
		}

		dev_dbg(data->dev, "DEVFREQ : %8uKhz, %8uuV\n", freq,
			 volt);
	}
	ret = exynos_devfreq_init_freq_table(data);
	if (ret) {
		dev_err(data->dev, "failed init frequency table\n");
		return ret;
	}

	return 0;
}

static int gs_memlat_devfreq_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct exynos_devfreq_data *data;
	struct dev_pm_opp;
	struct device_node *governor_node;
	struct device *dev = &pdev->dev;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err_data;
	}

	data->dev = dev;

	mutex_init(&data->lock);

	/* Find and initialize governor. */
	governor_node = of_get_child_by_name(dev->of_node, "memlat_governor");
	if (!governor_node) {
		dev_err(dev, "Memlat Governor Node Not Defined.\n");
		goto err_parse_dt;
	}
	ret = gs_memlat_governor_initialize(governor_node, data);
	if (ret) {
		dev_err(dev, "failed to parse private governor data\n");
		goto err_parse_dt;
	}

	ret = exynos_devfreq_parse_dt(dev->of_node, data);
	if (ret) {
		dev_err(dev, "failed to parse private devfreq data\n");
		goto err_parse_dt;
	}

	data->devfreq_profile.max_state = data->max_state;
	data->devfreq_profile.target = gs_memlat_devfreq_target;
	data->devfreq_profile.get_cur_freq = gs_memlat_devfreq_get_cur_min_freq;

	data->devfreq_profile.freq_table =
		devm_kcalloc(dev, data->max_state,
		  sizeof(*data->devfreq_profile.freq_table),
		  GFP_KERNEL);
	if (!data->devfreq_profile.freq_table) {
		dev_err(dev, "failed to allocate freq_table\n");
		ret = -ENOMEM;
		goto err_freqtable;
	}

	ret = exynos_init_freq_table(data);
	if (ret) {
		dev_err(dev, "failed initialize freq_table\n");
		goto err_init_table;
	}

	platform_set_drvdata(pdev, data);

	/* We add the governor before the device. */
	ret = gs_memlat_governor_register();

	data->devfreq =
		devfreq_add_device(data->dev, &data->devfreq_profile,
				   data->governor_name, data->governor_data);
	if (IS_ERR(data->devfreq)) {
		dev_err(dev, "failed devfreq device added\n");
		ret = -EINVAL;
		goto err_devfreq;
	}

	exynos_pm_qos_add_request(&data->sys_pm_qos_min, (int)data->pm_qos_class, data->min_freq);
	ret = sysfs_create_file(&data->devfreq->dev.kobj,
				&dev_attr_scaling_devfreq_min.attr);
	if (ret)
		dev_warn(dev, "failed create sysfs for devfreq pm_qos_min\n");

	gs_memlat_governor_set_devfreq_ready();

	return 0;

err_devfreq:
	platform_set_drvdata(pdev, NULL);
err_init_table:
err_freqtable:
err_parse_dt:
	mutex_destroy(&data->lock);
err_data:
	return ret;
}

static int gs_memlat_devfreq_remove(struct platform_device *pdev)
{
	struct exynos_devfreq_data *data = platform_get_drvdata(pdev);
	gs_memlat_governor_unregister();

	sysfs_remove_file(&data->devfreq->dev.kobj,
			  &dev_attr_scaling_devfreq_min.attr);

	exynos_pm_qos_remove_request(&data->sys_pm_qos_min);
	devfreq_remove_device(data->devfreq);
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&data->lock);

	return 0;
}

static struct platform_device_id gs_memlat_devfreq_driver_ids[] = {
	{
		.name = MEMLAT_DEVFREQ_MODULE_NAME,
	},
	{},
};

MODULE_DEVICE_TABLE(platform, gs_memlat_devfreq_driver_ids);

static const struct of_device_id gs_memlat_devfreq_match[] = {
	{
		.compatible = "gs-memlat-devfreq",
	},
	{},
};

MODULE_DEVICE_TABLE(of, gs_memlat_devfreq_match);

static struct platform_driver gs_memlat_devfreq_driver = {
	.probe = gs_memlat_devfreq_probe,
	.remove = gs_memlat_devfreq_remove,
	.id_table = gs_memlat_devfreq_driver_ids,
	.driver = {
		.name = MEMLAT_DEVFREQ_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = gs_memlat_devfreq_match,
	},
};

static int gs_memlat_devfreq_root_probe(struct platform_device *pdev)
{
	struct device_node *np;
	int num_domains;

	np = pdev->dev.of_node;

	platform_driver_register(&gs_memlat_devfreq_driver);

	/* alloc memory for devfreq data structure */
	num_domains = of_get_child_count(np);

	/* probe each devfreq node */
	of_platform_populate(np, NULL, NULL, NULL);

	return 0;
}

static const struct of_device_id gs_memlat_devfreq_root_match[] = {
	{
		.compatible = "gs-memlat-devfreq-root",
	},
	{}
};

static struct platform_driver gs_memlat_devfreq_root_driver = {
	.probe = gs_memlat_devfreq_root_probe,
	.driver = {
		.name = "gs-devfreq-memlat",
		.owner = THIS_MODULE,
		.of_match_table = gs_memlat_devfreq_root_match,
	},
};

module_platform_driver(gs_memlat_devfreq_root_driver);
MODULE_DESCRIPTION("Google Sourced Memory Latency Driver");
MODULE_AUTHOR("Wei Wang <wvw@google.com>");
MODULE_AUTHOR("Will Song <jinpengsong@google.com>");
MODULE_DESCRIPTION("Memory latency driver");
MODULE_LICENSE("GPL");
