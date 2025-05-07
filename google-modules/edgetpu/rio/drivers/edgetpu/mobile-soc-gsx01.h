/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for GSx01 SoCs.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __MOBILE_SOC_GSX01_H__
#define __MOBILE_SOC_GSX01_H__

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <soc/google/exynos_pm_qos.h>

#include <gcip/gcip-kci.h>

struct bcl_device;

/* SoC data for GSx01 platforms */
struct edgetpu_soc_data {
	/* Virtual address of the SSMT block for this chip. */
	void __iomem **ssmt_base;
	/* Number of SSMTs */
	uint num_ssmts;
	/* Virtual address of the CMU block for this chip. */
	void __iomem *cmu_base;
	/* INT/MIF requests for memory bandwidth */
	struct exynos_pm_qos_request int_min;
	struct exynos_pm_qos_request mif_min;
	/* BTS */
	unsigned int performance_scenario;
	int scenario_count;
	struct mutex scenario_lock;
	struct bcl_device *bcl_dev;
	/* PMU status base address for block status */
	void __iomem *pmu_status;
};

/*
 * Request codes from firmware
 * Values must match with firmware code base
 */
enum gsx01_reverse_kci_code {
	RKCI_CODE_PM_QOS = GCIP_RKCI_CHIP_CODE_FIRST + 1,
	RKCI_CODE_BTS = GCIP_RKCI_CHIP_CODE_FIRST + 2,
	/* The above codes have been deprecated. */

	RKCI_CODE_PM_QOS_BTS = GCIP_RKCI_CHIP_CODE_FIRST + 3,
};

#endif /* __MOBILE_SOC_GSX01_H__ */
