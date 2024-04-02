// SPDX-License-Identifier: GPL-2.0
/*
 * Rio platform device driver for the Google Edge TPU ML accelerator.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "edgetpu-config.h"
#include "edgetpu-internal.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "rio-platform.h"

#include "edgetpu-mobile-platform.c"

static const struct of_device_id edgetpu_of_match[] = {
	{
		.compatible = "google,edgetpu-gs301",
	},
	{
		.compatible = "google,edgetpu-zuma",
	},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, edgetpu_of_match);

#define EDGETPU_S2MPU_REG_CTRL_CLR 0x54
#define EDGEPTU_S2MPU_REG_NUM_CONTEXT 0x100

/*
 * Acquires the register base from OF property and map it.
 *
 * On success, caller calls iounmap() when the returned pointer is not required.
 *
 * Returns -ENODATA on reading property failure, typically caused by the property @prop doesn't
 * exist or doesn't have a 32-bit value.
 * Returns -ENOMEM on mapping register base failure.
 */
static void __iomem *reg_base_of_prop(struct device *dev, const char *prop, size_t size)
{
	u32 reg;
	void __iomem *addr;
	int ret;

	ret = of_property_read_u32_index(dev->of_node, prop, 0, &reg);
	if (ret)
		return ERR_PTR(-ENODATA);
	addr = ioremap(reg, size);
	if (!addr)
		return ERR_PTR(-ENOMEM);

	return addr;
}

/*
 * Set shareability for enabling IO coherency in Rio
 */
static int rio_mmu_set_shareability(struct device *dev)
{
	void __iomem *addr = reg_base_of_prop(dev, "edgetpu,shareability", PAGE_SIZE);

	if (IS_ERR(addr))
		return PTR_ERR(addr);

	writel_relaxed(SHAREABLE_WRITE | SHAREABLE_READ | INNER_SHAREABLE,
		       addr + EDGETPU_SYSREG_TPU0_SHAREABILITY);
	writel_relaxed(SHAREABLE_WRITE | SHAREABLE_READ | INNER_SHAREABLE,
		       addr + EDGETPU_SYSREG_TPU1_SHAREABILITY);
	iounmap(addr);
	return 0;
}

/*
 * Disables all contexts in S2MPU. Only required for platforms where bootloader doesn't disable it
 * for us.
 */
static int rio_disable_s2mpu(struct device *dev)
{
	void __iomem *addr = reg_base_of_prop(dev, "edgetpu,s2mpu", PAGE_SIZE);
	u32 num_context;

	if (IS_ERR(addr)) {
		/* ignore errors when the property doesn't exist */
		if (PTR_ERR(addr) == -ENODATA)
			return 0;
		return PTR_ERR(addr);
	}
	num_context = readl_relaxed(addr + EDGEPTU_S2MPU_REG_NUM_CONTEXT);
	writel_relaxed((1u << num_context) - 1, addr + EDGETPU_S2MPU_REG_CTRL_CLR);
	iounmap(addr);
	return 0;
}

static int rio_parse_set_dt_property(struct edgetpu_mobile_platform_dev *etmdev)
{
	struct edgetpu_dev *etdev = &etmdev->edgetpu_dev;
	struct device *dev = etdev->dev;
	int ret;

	ret = rio_mmu_set_shareability(dev);
	if (ret)
		etdev_warn(etdev, "failed to enable shareability: %d", ret);
	ret = rio_disable_s2mpu(dev);
	if (ret)
		etdev_warn(etdev, "failed to disable S2MPU: %d", ret);
	return 0;
}

static int rio_platform_after_probe(struct edgetpu_mobile_platform_dev *etmdev)
{
	return rio_parse_set_dt_property(etmdev);
}

static int edgetpu_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rio_platform_dev *rpdev;
	struct edgetpu_mobile_platform_dev *etmdev;

	rpdev = devm_kzalloc(dev, sizeof(*rpdev), GFP_KERNEL);
	if (!rpdev)
		return -ENOMEM;

	etmdev = &rpdev->mobile_dev;
	etmdev->after_probe = rio_platform_after_probe;
	return edgetpu_mobile_platform_probe(pdev, etmdev);
}

static int edgetpu_platform_remove(struct platform_device *pdev)
{
	return edgetpu_mobile_platform_remove(pdev);
}

static struct platform_driver edgetpu_platform_driver = {
	.probe = edgetpu_platform_probe,
	.remove = edgetpu_platform_remove,
	.driver = {
			.name = "edgetpu_platform",
			.of_match_table = edgetpu_of_match,
			.pm = &edgetpu_pm_ops,
		},
};

static int __init edgetpu_platform_init(void)
{
	int ret;

	ret = edgetpu_init();
	if (ret)
		return ret;
	return platform_driver_register(&edgetpu_platform_driver);
}

static void __exit edgetpu_platform_exit(void)
{
	platform_driver_unregister(&edgetpu_platform_driver);
	edgetpu_exit();
}

MODULE_DESCRIPTION("Google Edge TPU platform driver");
MODULE_LICENSE("GPL v2");
module_init(edgetpu_platform_init);
module_exit(edgetpu_platform_exit);
MODULE_FIRMWARE(EDGETPU_DEFAULT_FIRMWARE_NAME);
