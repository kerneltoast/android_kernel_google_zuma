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

#include <gcip/gcip-iommu.h>

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

int gxp_iommu_get_max_vd_activation(struct gxp_dev *gxp)
{
	return gcip_iommu_domain_pool_get_num_pasid(gxp->domain_pool);
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

/* A patch to make LPM work under slow fabric. Sequence is simplified from b/279200152#comment50. */
static void patch_for_slow_noc_clk(struct gxp_dev *gxp)
{
#define LPM_IMEM_OFFSET 0x800
#define LPM_DMEM_OFFSET(psm) (0x1600 + (psm) * 0x1000)

	lpm_write_32(gxp, LPM_IMEM_OFFSET + (116 * 4), 0x11090011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (117 * 4), 0x10080011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (122 * 4), 0x007b5302);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (125 * 4), 0x007e5003);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (127 * 4), 0x00805002);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (128 * 4), 0x11070011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (130 * 4), 0x0011110e);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (132 * 4), 0x100e0011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (133 * 4), 0x10020011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (134 * 4), 0x00111003);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (136 * 4), 0x00111106);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (139 * 4), 0x100c0011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (143 * 4), 0x10070011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (147 * 4), 0x00945302);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (150 * 4), 0x00975003);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (152 * 4), 0x00995002);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (155 * 4), 0x11020011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (156 * 4), 0x00111103);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (157 * 4), 0x0011100e);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (159 * 4), 0x10090011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (160 * 4), 0x00111106);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (162 * 4), 0x100d0011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (165 * 4), 0x100a0011);
	lpm_write_32(gxp, LPM_IMEM_OFFSET + (166 * 4), 0x100c0011);

	lpm_write_32(gxp, LPM_DMEM_OFFSET(0) + (0 * 4), 0x0000004a);
	lpm_write_32(gxp, LPM_DMEM_OFFSET(0) + (1 * 4), 0x0000000a);
	lpm_write_32(gxp, LPM_DMEM_OFFSET(1) + (0 * 4), 0x0000004a);
	lpm_write_32(gxp, LPM_DMEM_OFFSET(1) + (1 * 4), 0x0000000a);
	lpm_write_32(gxp, LPM_DMEM_OFFSET(2) + (0 * 4), 0x0000004a);
	lpm_write_32(gxp, LPM_DMEM_OFFSET(2) + (1 * 4), 0x0000000a);
}

static void callisto_lpm_init(struct gxp_dev *gxp)
{
	patch_for_slow_noc_clk(gxp);
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
	gxp->lpm_init = callisto_lpm_init;

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
