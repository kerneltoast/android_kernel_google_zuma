// SPDX-License-Identifier: GPL-2.0-only
/*
 * Callisto power management implementations.
 *
 * Copyright (C) 2023 Google LLC
 */

#include <linux/thermal.h> /* for fixing bug in gs_tmu_v3 */
#include <linux/types.h>
#include <soc/google/bcl.h>
#include <soc/google/gs_tmu_v3.h>

#include "gxp-internal.h"
#include "gxp-pm.h"

static int callisto_after_blk_power_up(struct gxp_dev *gxp)
{
	int ret;

	if (gxp->power_mgr->bcl_dev) {
		ret = google_init_aur_ratio(gxp->power_mgr->bcl_dev);
		if (ret)
			dev_warn(gxp->dev, "init BCL ratio failed: %d", ret);
	}
	/* Inform TMU the block is up. */
	return set_acpm_tj_power_status(TZ_AUR, true);
}

static void callisto_before_blk_power_down(struct gxp_dev *gxp)
{
	int ret;

	ret = set_acpm_tj_power_status(TZ_AUR, false);
	if (ret)
		dev_err(gxp->dev,
			"set Tj power status on blk down failed: %d\n", ret);
}

static const struct gxp_pm_ops gxp_pm_ops = {
	.after_blk_power_up = callisto_after_blk_power_up,
	.before_blk_power_down = callisto_before_blk_power_down,
};

void gxp_pm_chip_set_ops(struct gxp_power_manager *mgr)
{
	mgr->ops = &gxp_pm_ops;
}

void gxp_pm_chip_init(struct gxp_dev *gxp)
{
	gxp->power_mgr->bcl_dev = google_retrieve_bcl_handle();
}
