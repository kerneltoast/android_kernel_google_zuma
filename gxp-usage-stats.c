// SPDX-License-Identifier: GPL-2.0-only
/*
 * DSP usage stats
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/device.h>

#include <gcip/gcip-usage-stats.h>

#include "gxp-config.h"
#include "gxp-mcu-platform.h"
#include "gxp-mcu.h"
#include "gxp-pm.h"
#include "gxp-usage-stats.h"

/* Core usage. */
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_CORE_USAGE, 0, 0, dsp_usage_0, NULL,
				NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_CORE_USAGE, 0, 1, dsp_usage_1, NULL,
				NULL);

#if GXP_NUM_CORES > 2
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_CORE_USAGE, 0, 2, dsp_usage_2, NULL,
				NULL);
#endif

/* Counter. */
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_WORKLOAD,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, dsp_workload_count, NULL,
				NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_CONTEXT_SWITCHES,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, context_switch_count, NULL,
				NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_CONTEXT_PREEMPTIONS,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, preempt_count, NULL, NULL);

/* Thread statistics. */
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_THREAD_STATS, 0, 0, fw_thread_stats,
				NULL, NULL);

/* DVFS frequency info. */
static GCIP_USAGE_STATS_ATTR_RO(GCIP_USAGE_STATS_METRIC_TYPE_DVFS_FREQUENCY_INFO, 0, 0,
				scaling_available_frequencies, NULL);

static struct gcip_usage_stats_attr *attrs[] = {
	&gcip_usage_stats_attr_dsp_usage_0,
	&gcip_usage_stats_attr_dsp_usage_1,
#if GXP_NUM_CORES > 2
	&gcip_usage_stats_attr_dsp_usage_2,
#endif
	&gcip_usage_stats_attr_dsp_workload_count,
	&gcip_usage_stats_attr_context_switch_count,
	&gcip_usage_stats_attr_preempt_count,
	&gcip_usage_stats_attr_fw_thread_stats,
	&gcip_usage_stats_attr_scaling_available_frequencies,
};

static int update_usage_kci(void *data)
{
	struct gxp_usage_stats *ustats = data;
	struct gxp_dev *gxp = ustats->gxp;
	struct gxp_mcu *mcu = &to_mcu_dev(gxp)->mcu;

	return gxp_kci_update_usage(&mcu->kci);
}

static int get_default_dvfs_freqs_num(void *data)
{
	return AUR_NUM_POWER_STATE;
}

static unsigned int get_default_dvfs_freq(int idx, void *data)
{
	if (idx >= AUR_NUM_POWER_STATE)
		return 0;
	return aur_power_state2rate[idx];
}

static const struct gcip_usage_stats_ops stats_ops = {
	.update_usage_kci = update_usage_kci,
	.get_default_dvfs_freqs_num = get_default_dvfs_freqs_num,
	.get_default_dvfs_freq = get_default_dvfs_freq,
};

void gxp_usage_stats_process_buffer(struct gxp_dev *gxp, void *buf)
{
	if (!gxp->usage_stats)
		return;
	gcip_usage_stats_process_buffer(&gxp->usage_stats->ustats, buf);
}

void gxp_usage_stats_init(struct gxp_dev *gxp)
{
	struct gxp_usage_stats *ustats;
	struct gcip_usage_stats_args args;
	int ret;

	ustats = devm_kzalloc(gxp->dev, sizeof(*gxp->usage_stats), GFP_KERNEL);
	if (!ustats)
		return;

	args.version = GXP_USAGE_METRIC_VERSION;
	args.dev = gxp->dev;
	args.ops = &stats_ops;
	args.attrs = attrs;
	args.num_attrs = ARRAY_SIZE(attrs);
	args.subcomponents = GXP_NUM_CORES;
	args.data = ustats;
	ustats->gxp = gxp;

	ret = gcip_usage_stats_init(&ustats->ustats, &args);
	if (ret) {
		dev_warn(gxp->dev, "failed to create the usage_stats attrs\n");
		devm_kfree(gxp->dev, ustats);
		return;
	}

	gxp->usage_stats = ustats;
}

void gxp_usage_stats_exit(struct gxp_dev *gxp)
{
	if (gxp->usage_stats) {
		gcip_usage_stats_exit(&gxp->usage_stats->ustats);
		devm_kfree(gxp->dev, gxp->usage_stats);
	}
	gxp->usage_stats = NULL;
}
