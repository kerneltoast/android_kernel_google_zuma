// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for Callisto.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "callisto-platform.h"
#include "gxp-kci.h"
#include "gxp-mcu-fs.h"
#include "gxp-uci.h"

#include "gxp-common-platform.c"

void gxp_iommu_setup_shareability(struct gxp_dev *gxp)
{
	void __iomem *addr = gxp->sysreg_shareability;

	if (IS_ERR_OR_NULL(addr))
		return;

	writel_relaxed(SHAREABLE_WRITE | SHAREABLE_READ | INNER_SHAREABLE,
		       addr + GXP_SYSREG_AUR0_SHAREABILITY);
	writel_relaxed(SHAREABLE_WRITE | SHAREABLE_READ | INNER_SHAREABLE,
		       addr + GXP_SYSREG_AUR1_SHAREABILITY);
}

static int callisto_platform_parse_dt(struct platform_device *pdev,
				      struct gxp_dev *gxp)
{
	struct resource *r;
	void *addr;
	struct device *dev = gxp->dev;
	int ret;
	u32 reg;

	/*
	 * Setting BAAW is required for having correct base for CSR accesses.
	 *
	 * BAAW is supposed to be set by bootloader. On production we simply
	 * don't include the register base in DTS to skip this procedure.
	 */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "baaw");
	if (!IS_ERR_OR_NULL(r)) {
		addr = devm_ioremap_resource(dev, r);
		/* start address */
		writel(0x0, addr + 0x0);
		/* Window - size */
		writel(0x8000000, addr + 0x4);
		/* Window - target */
		writel(0, addr + 0x8);
		/* Window - enable */
		writel(0x80000003, addr + 0xc);
	}

	if (!of_find_property(dev->of_node, "gxp,shareability", NULL)) {
		ret = -ENODEV;
		goto err;
	}
	ret = of_property_read_u32_index(dev->of_node,
					 "gxp,shareability", 0, &reg);
	if (ret)
		goto err;
	gxp->sysreg_shareability = devm_ioremap(dev, reg, PAGE_SIZE);
	if (!gxp->sysreg_shareability)
		ret = -ENOMEM;
err:
	if (ret)
		dev_warn(dev, "Failed to enable shareability: %d\n", ret);

	return 0;
}

static int gxp_platform_probe(struct platform_device *pdev)
{
	struct callisto_dev *callisto =
		devm_kzalloc(&pdev->dev, sizeof(*callisto), GFP_KERNEL);
	struct gxp_mcu_dev *mcu_dev = &callisto->mcu_dev;
	struct gxp_dev *gxp = &mcu_dev->gxp;

	if (!callisto)
		return -ENOMEM;

	gxp_mcu_dev_init(mcu_dev);
	gxp->parse_dt = callisto_platform_parse_dt;

	return gxp_common_platform_probe(pdev, gxp);
}

static int gxp_platform_remove(struct platform_device *pdev)
{
	return gxp_common_platform_remove(pdev);
}

static const struct of_device_id gxp_of_match[] = {
	{ .compatible = "google,gxp", },
	{ .compatible = "google,gxp-zuma", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, gxp_of_match);

static struct platform_driver gxp_platform_driver = {
	.probe = gxp_platform_probe,
	.remove = gxp_platform_remove,
	.driver = {
			.name = GXP_DRIVER_NAME,
			.of_match_table = of_match_ptr(gxp_of_match),
#if IS_ENABLED(CONFIG_PM_SLEEP)
			.pm = &gxp_pm_ops,
#endif
		},
};

static int __init gxp_platform_init(void)
{
	int ret;

	ret = gxp_common_platform_init();
	if (ret)
		return ret;

	return platform_driver_register(&gxp_platform_driver);
}

static void __exit gxp_platform_exit(void)
{
	platform_driver_unregister(&gxp_platform_driver);
	gxp_common_platform_exit();
}

MODULE_DESCRIPTION("Google GXP platform driver");
MODULE_LICENSE("GPL v2");
#ifdef GIT_REPO_TAG
MODULE_INFO(gitinfo, GIT_REPO_TAG);
#endif
module_init(gxp_platform_init);
module_exit(gxp_platform_exit);
