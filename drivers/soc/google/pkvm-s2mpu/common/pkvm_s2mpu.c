// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_platform.h>

#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/moduleparam.h>

#include <linux/kvm_host.h>
#include "kvm_s2mpu.h"
#include <soc/google/pkvm-s2mpu.h>

/* Print caches in s2mpu faults. */
static bool print_caches;
module_param(print_caches, bool, 0);

/* Declare EL2 module init function as it is needed by pkvm_load_el2_module. */
int __kvm_nvhe_s2mpu_hyp_init(const struct pkvm_module_ops *ops);
/* Token of S2MPU driver, token is the load address of the module and a unique ID for it. */
static unsigned long token;

/* Number of s2mpu devices. */
static int nr_devs_total;
static int nr_devs_registered;

static struct platform_device *__of_get_phandle_pdev(struct device *parent,
						     const char *prop, int index)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_parse_phandle(parent->of_node, prop, index);
	if (!np)
		return NULL;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-EINVAL);

	return pdev;
}

static struct s2mpu_data *s2mpu_dev_data(struct device *dev)
{
	return platform_get_drvdata(to_platform_device(dev));
}

int __pkvm_s2mpu_of_link(struct device *parent)
{
	struct platform_device *pdev;
	struct device_link *link;
	struct s2mpu_data *data;
	int i;

	/* Check that all S2MPUs have been initialized. */
	for (i = 0; (pdev = __of_get_phandle_pdev(parent, "s2mpus", i)); i++) {
		if (IS_ERR(pdev))
			return PTR_ERR(pdev);

		if (!pkvm_s2mpu_ready(&pdev->dev))
			return -EAGAIN;
	}

	/* Link all S2MPUs as suppliers to the parent. */
	for (i = 0; (pdev = __of_get_phandle_pdev(parent, "s2mpus", i)); i++) {
		if (IS_ERR(pdev))
			return PTR_ERR(pdev);

		link = device_link_add(/*consumer=*/parent, /*supplier=*/&pdev->dev,
				       DL_FLAG_AUTOREMOVE_CONSUMER | DL_FLAG_PM_RUNTIME);

		/*
		 * If device has an SysMMU, it has typeA STLB.
		 * This relies on SysMMU nodes not being disabled so the at probe this function
		 * would be called.
		 */
		data  = s2mpu_dev_data(&pdev->dev);
		if (data && of_device_is_compatible(parent->of_node, "samsung,sysmmu-v9"))
			data->has_sysmmu = true;

		if (!link)
			return -EINVAL;
	}

	return 0;
}

struct device *__pkvm_s2mpu_of_parse(struct device *parent)
{
	struct platform_device *pdev;

	pdev = __of_get_phandle_pdev(parent, "s2mpu", 0);
	if (IS_ERR_OR_NULL(pdev))
		return ERR_PTR(PTR_ERR(pdev));

	return &pdev->dev;
}

static irqreturn_t s2mpu_irq_handler(int irq, void *ptr)
{
	return s2mpu_fault_handler((struct s2mpu_data *)ptr, print_caches);
}

/*
 * Parse interrupt information from DT and if found, register IRQ handler.
 * This is considered optional and will not fail even if the initialization is
 * unsuccessful. In that case the IRQ will remain masked.
 */
static void s2mpu_probe_irq(struct platform_device *pdev, struct s2mpu_data *data)
{
	int ret, irq;

	irq = platform_get_irq_optional(pdev, 0);

	if (irq == -ENXIO)
		return; /* No IRQ specified. */

	if (irq < 0) {
		/* IRQ specified but failed to parse. */
		dev_err(data->dev, "failed to parse IRQ, IRQ not enabled");
		return;
	}

	ret = devm_request_irq(data->dev, irq, s2mpu_irq_handler, 0,
			       dev_name(data->dev), data);
	if (ret) {
		dev_err(&pdev->dev, "failed to register IRQ, IRQ not enabled");
		return;
	}
}

int __pkvm_s2mpu_suspend(struct device *dev)
{
	struct s2mpu_data *data = s2mpu_dev_data(dev);

	if(data->always_on)
		return 0;

	if (data->pkvm_registered)
		return pkvm_iommu_suspend(dev);

	return 0;
}

int __pkvm_s2mpu_resume(struct device *dev)
{
	struct s2mpu_data *data = s2mpu_dev_data(dev);

	if (data->pkvm_registered)
		return pkvm_iommu_resume(dev);

	/* Need to bypass S2MPU if pKVM is not there (ex: in userspace fastboot). */
#ifdef S2MPU_V9
	writel_relaxed(0xFF, data->base + REG_NS_V9_CTRL_PROT_EN_PER_VID_CLR);
#else
	writel_relaxed(0, data->base + REG_NS_CTRL0);
#endif
	return 0;
}

static int s2mpu_late_suspend(struct device *dev)
{
	struct s2mpu_data *data = s2mpu_dev_data(dev);

	/*
	 * Some always-on S2MPUs need to allow traffic while the CPU is asleep.
	 * Do not call pkvm_iommu_suspend() here because that would put them
	 * in a blocking state.
	 */
	if (data->always_on || pm_runtime_status_suspended(dev) || !data->has_pd)
		return 0;

	dev->power.must_resume = true;
	return __pkvm_s2mpu_suspend(dev);
}

static int s2mpu_late_resume(struct device *dev)
{
	/*
	 * Some always-on S2MPUs reset while the CPU is asleep. Call
	 * pkvm_iommu_resume() here regardless of always-on to reconfigure them.
	 */

	if (pm_runtime_status_suspended(dev))
		return 0;

	return __pkvm_s2mpu_resume(dev);
}

static int s2mpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct s2mpu_data *data;
	bool off_at_boot, has_sync;
	int ret, nr_devs;

	data = devm_kmalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to parse 'reg'");
		return -EINVAL;
	}

	/* devm_ioremap_resource internally calls devm_request_mem_region. */
	data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base)) {
		dev_err(dev, "could not ioremap resource: %ld", PTR_ERR(data->base));
		return PTR_ERR(data->base);
	}

	data->always_on = !!of_get_property(np, "always-on", NULL);
	off_at_boot = !!of_get_property(np, "off-at-boot", NULL);
	has_sync = !!of_get_property(np, "built-in-sync", NULL);
	data->has_pd = !!of_get_property(np, "power-domains", NULL);
	/*
	 * Try to parse IRQ information. This is optional as it only affects
	 * runtime fault reporting, and therefore errors do not fail the whole
	 * driver initialization.
	 */
	s2mpu_probe_irq(pdev, data);

	ret = pkvm_iommu_s2mpu_register(dev, res->start, has_sync);
	if (ret && ret != -ENODEV) {
		dev_err(dev, "could not register: %d\n", ret);
		return ret;
	}

	data->pkvm_registered = ret != -ENODEV;
	if (!data->pkvm_registered)
		dev_warn(dev, "pKVM disabled, control from kernel\n");
	else {
		nr_devs = nr_devs_registered++;
		dev_info(dev, "registered with hypervisor [%d/%d]\n", nr_devs, nr_devs_total);
	}

	platform_set_drvdata(pdev, data);

	data->has_sysmmu = false;
	/*
	 * Most S2MPUs are in an allow-all state at boot. Call the hypervisor
	 * to initialize the S2MPU to a blocking state. This corresponds to
	 * the state the hypervisor sets on suspend.
	 */
	if (!off_at_boot)
		WARN_ON(__pkvm_s2mpu_suspend(dev));

	pm_runtime_enable(dev);
	if (data->always_on)
		pm_runtime_get_sync(dev);

	return 0;
}

static const struct dev_pm_ops s2mpu_pm_ops = {
	SET_RUNTIME_PM_OPS(__pkvm_s2mpu_suspend, __pkvm_s2mpu_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(s2mpu_late_suspend, s2mpu_late_resume)
};

static const struct of_device_id s2mpu_of_match[] = {
	{ .compatible = "google," S2MPU_NAME },
	{},
};

static struct platform_driver s2mpu_driver = {
	.probe = s2mpu_probe,
	.driver = {
		.name = "pkvm-" S2MPU_NAME,
		.of_match_table = s2mpu_of_match,
		.pm = &s2mpu_pm_ops,
	},
};

static int s2mpu_driver_register(struct platform_driver *driver)
{
	struct device_node *np;
	int ret = 0;

	for_each_matching_node(np, driver->driver.of_match_table)
		if (of_device_is_available(np))
			nr_devs_total++;

	/* No need to force probe devices if pKVM is not enabled. */
	if (!is_protected_kvm_enabled())
		return platform_driver_register(driver);

	/* Only try to register the driver with pKVM if pKVM is enabled. */
	ret = pkvm_load_el2_module(__kvm_nvhe_s2mpu_hyp_init, &token);
	if (ret) {
		pr_err("Failed to load s2mpu el2 module: %d\n", ret);
		return ret;
	}

	ret = pkvm_iommu_s2mpu_init(token);
	if (ret) {
		pr_err("Can't initialize pkvm s2mpu driver: %d\n", ret);
		return ret;
	}

	ret = platform_driver_probe(&s2mpu_driver, s2mpu_probe);

	/* If one device is not probed it will not be controlled by the hypervisor. */
	ret = pkvm_iommu_finalize(WARN_ON(nr_devs_total != nr_devs_registered) ? -ENXIO : 0);
	if (!ret)
		pr_info("List of devices successfully finalized for pkvm s2mpu\n");
	else
		pr_err("Couldn't finalize pkvm s2mpu: %d\n", ret);

	return ret;
}

module_driver(s2mpu_driver, s2mpu_driver_register, platform_driver_unregister);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Brazdil <dbrazdil@google.com>");
