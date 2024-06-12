// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * Memory Latency Governor Main Module.
 */
#define pr_fmt(fmt) "gs_governor_memlat: " fmt

#include <dt-bindings/soc/google/zuma-devfreq.h>
#include <governor.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <performance/gs_perf_mon/gs_perf_mon.h>
#include <soc/google/exynos-devfreq.h>
#include <trace/events/power.h>

#include "gs_governor_memlat.h"
#include "gs_governor_utils.h"

/**
 * struct memlat_data - Node containing memlat's global data.
 * @gov_is_on:		Governor's active state.
 * @devfreq_data:	Pointer to the memlat's devfreq.
 * @attr_grp:		Tuneable governor parameters exposed to userspace.
 * @num_cpu_clusters:	Number of CPU clusters the governor will service.
 * @cpu_configs_arr:	Configurations for each cluster's latency vote.
 */
struct memlat_data {
	bool gov_is_on;
	bool devfreq_initialized;
	struct exynos_devfreq_data *devfreq_data;
	struct attribute_group *attr_grp;
	int num_cpu_clusters;
	struct cluster_config *cpu_configs_arr;
};

static void update_memlat_gov(struct gs_cpu_perf_data *data, void* private_data);

/* Global monitor client used to get callbacks when gs_perf_mon data is updated. */
static struct gs_perf_mon_client memlat_perf_client = {
	.client_callback = update_memlat_gov,
	.name = "memlat"
};

/* Memlat datastructure holding memlat governor configurations and metadata. */
static struct memlat_data memlat_node;

/* Macro Expansions for sysfs nodes.*/
MAKE_CLUSTER_ATTR(memlat_node, stall_floor);
MAKE_CLUSTER_ATTR(memlat_node, ratio_ceil);
MAKE_CLUSTER_ATTR(memlat_node, cpuidle_state_depth_threshold);

SHOW_CLUSTER_FREQ_MAP_ATTR(memlat_node, latency_freq_table);

/* These sysfs emitters also depend on the macro expansions from above. */
static struct attribute *memlat_dev_attr[] = {
	&dev_attr_memlat_node_stall_floor.attr,
	&dev_attr_memlat_node_ratio_ceil.attr,
	&dev_attr_memlat_node_cpuidle_state_depth_threshold.attr,
	&dev_attr_memlat_node_latency_freq_table.attr,
	NULL,
};

/* Sysfs files to expose. */
static struct attribute_group memlat_dev_attr_group = {
	.name = "memlat_attr",
	.attrs = memlat_dev_attr,
};

/**
 * update_memlat_gov  -	Callback function from the perf monitor to service
 * 			the memlat governor.
 *
 * Input:
 * @data:		Performance Data from the monitor.
 * @private_data:	Unused.
 *
*/
static void update_memlat_gov(struct gs_cpu_perf_data *data, void* private_data)
{
	struct devfreq *df;
	int err;

	if (memlat_node.devfreq_initialized) {
		df = memlat_node.devfreq_data->devfreq;
		mutex_lock(&df->lock);
		df->governor_data = data;
		err = update_devfreq(df);
		if (err)
			dev_err(&df->dev, "Memlat update failed: %d\n", err);
		df->governor_data = NULL;
		mutex_unlock(&df->lock);
	}
}

/**
 * gov_start - Starts the governor.
 *
 * This is invoked in devfreq_add_governor during probe.
 *
 * Input:
 * @df:	The devfreq to start.
*/
static int gov_start(struct devfreq *df)
{
	int ret = gs_perf_mon_add_client(&memlat_perf_client);
	if (ret)
		goto err_start;
	memlat_node.gov_is_on = true;

	ret = sysfs_create_group(&df->dev.kobj, memlat_node.attr_grp);
	if (ret)
		goto err_sysfs;
	return 0;

err_sysfs:
	memlat_node.gov_is_on = false;
	gs_perf_mon_remove_client(&memlat_perf_client);
err_start:
	return ret;
}

/**
 * gov_suspend - Pauses the governor.
 *
 * This is invoked when the entering system suspends.
 *
 * Input:
 * @df:	The devfreq to suspend.
*/
static int gov_suspend(struct devfreq *df)
{
	memlat_node.gov_is_on = false;
	if (memlat_node.devfreq_initialized) {
		mutex_lock(&df->lock);
		update_devfreq(df);
		mutex_unlock(&df->lock);
	}

	return 0;
}

/**
 * gov_resume - Restarts the governor.
 *
 * Input:
 * @df:	The devfreq to resume.
*/
static int gov_resume(struct devfreq *df)
{
	memlat_node.gov_is_on = true;
	if (memlat_node.devfreq_initialized) {
		mutex_lock(&df->lock);
		update_devfreq(df);
		mutex_unlock(&df->lock);
	}

	return 0;
}

/**
 * gov_stop - Stops the governor.
 *
 * This is invoked by devfreq_remove_governor.
 *
 * Input:
 * @df:	The devfreq to stop.
*/
static void gov_stop(struct devfreq *df)
{
	memlat_node.gov_is_on = false;
	gs_perf_mon_remove_client(&memlat_perf_client);
	if (memlat_node.devfreq_initialized) {
		mutex_lock(&df->lock);
		update_devfreq(df);
		mutex_unlock(&df->lock);

		sysfs_remove_group(&df->dev.kobj, memlat_node.attr_grp);
	}
}

/**
 * gs_governor_memlat_ev_handler - Handles governor
 * Calls start/stop/resume/suspend events.
 *
 * Inputs:
 * @df:		The devfreq to signal.
 * @event:	Start/Stop/Suspend/Resume.
 * @data:	Unused.
 *
 * Return:	Non-zero on error. Notice this also returns
 *		0 if event is not found.
*/
int gs_governor_memlat_ev_handler(struct devfreq *df, unsigned int event, void *data)
{
	int ret;

	/* Check if the governor exists. */
	if (!df) {
		pr_err("Undefined devfreq for Memory Latency governor\n");
		return -ENODEV;
	}

	switch (event) {
	case DEVFREQ_GOV_START:
		ret = gov_start(df);
		if (ret)
			return ret;

		dev_dbg(df->dev.parent, "Enabled Memory Latency governor\n");
		break;

	case DEVFREQ_GOV_STOP:
		gov_stop(df);
		dev_dbg(df->dev.parent, "Disabled Memory Latency governor\n");
		break;

	case DEVFREQ_GOV_SUSPEND:
		ret = gov_suspend(df);
		if (ret) {
			dev_err(df->dev.parent, "Unable to suspend Memory Latency governor (%d)\n", ret);
			return ret;
		}

		dev_dbg(df->dev.parent, "Suspended Memory Latency governor\n");
		break;

	case DEVFREQ_GOV_RESUME:
		ret = gov_resume(df);
		if (ret) {
			dev_err(df->dev.parent, "Unable to resume Memory Latency governor (%d)\n", ret);
			return ret;
		}

		dev_dbg(df->dev.parent, "Resumed Memory Latency governor\n");
		break;
	}
	return 0;
}
EXPORT_SYMBOL(gs_governor_memlat_ev_handler);

/**
 * gs_governor_memlat_get_freq - Calculates memlat freq votes desired by each CPU cluster.
 *
 * This function determines the memlat target frequency.
 *
 * Input:
 * @df:		The devfreq we are deciding a vote for.
 * @freq:	Where to store the computed frequency.
*/
int gs_governor_memlat_get_freq(struct devfreq *df, unsigned long *freq)
{
	int cpu;
	int cluster_idx;
	struct cluster_config *cluster;
	unsigned long max_freq = 0;
	char trace_name[] = { 'c', 'p', 'u', '0', 'm', 'i', 'f', '\0' };

	/* Retrieving the CPU data array from the devfreq governor_data. */
	struct gs_cpu_perf_data *cpu_perf_data_arr = df->governor_data;

	/* If the memlat governor is not active. Reset our vote to minimum. */
	if (!memlat_node.gov_is_on) {
		*freq = 0;
		goto trace_out;
	}

	/* If monitor data is not supplied. Maintain current vote. */
	if (!cpu_perf_data_arr)
		goto trace_out;

	/* For each cluster, we make a frequency decision. */
	for (cluster_idx = 0; cluster_idx < memlat_node.num_cpu_clusters; cluster_idx++) {
		cluster = &memlat_node.cpu_configs_arr[cluster_idx];
		for_each_cpu(cpu, &cluster->cpus) {
			unsigned long ratio, mem_stall_pct, mem_stall_floor, ratio_ceil;
			unsigned long l3_cachemiss, mem_stall, cyc, last_delta_us, inst;
			unsigned long mif_freq = 0, effective_cpu_freq_khz;
			bool memlat_cpuidle_state_aware;
			enum gs_perf_cpu_idle_state memlat_configured_idle_depth_threshold;
			struct gs_cpu_perf_data *cpu_data = &cpu_perf_data_arr[cpu];
			trace_name[3] = '0' + cpu;

			/* Check if the cpu monitor is up. */
			if (!cpu_data->cpu_mon_on)
				goto early_exit;

			l3_cachemiss = cpu_data->perf_ev_last_delta[PERF_L3_CACHE_MISS_IDX];
			mem_stall = cpu_data->perf_ev_last_delta[PERF_STALL_BACKEND_MEM_IDX];
			cyc = cpu_data->perf_ev_last_delta[PERF_CYCLE_IDX];
			inst = cpu_data->perf_ev_last_delta[PERF_INST_IDX];
			last_delta_us = cpu_data->time_delta_us;

			ratio_ceil = cluster->ratio_ceil;
			mem_stall_floor = cluster->stall_floor;
			memlat_cpuidle_state_aware = cluster->cpuidle_state_aware;
			memlat_configured_idle_depth_threshold = cluster->cpuidle_state_depth_threshold;

			/* Compute threshold data. */
			if (l3_cachemiss != 0)
				ratio = inst / l3_cachemiss;
			else
				ratio = inst;

			mem_stall_pct = mult_frac(10000, mem_stall, cyc);
			effective_cpu_freq_khz = MHZ_TO_KHZ * cyc / last_delta_us;

			if (memlat_cpuidle_state_aware && cpu_data->cpu_idle_state >= memlat_configured_idle_depth_threshold)
   				goto early_exit; // Zeroing vote for sufficiently idle CPUs.

			/* If we pass the threshold, use the latency table. */
			if (ratio <= ratio_ceil && mem_stall_pct >= mem_stall_floor) {
				mif_freq = gs_governor_core_to_dev_freq(cluster->latency_freq_table,
									effective_cpu_freq_khz);
				if (mif_freq > max_freq)
					max_freq = mif_freq;
			}
		early_exit:
			/* Leave a trace for the cluster desired MIF frequency. */
			trace_clock_set_rate(trace_name, mif_freq, cpu);
		}
	}

	/* We vote on the max score across all cpus. */
	*freq = max_freq;

trace_out:
	trace_clock_set_rate("Memlat Governor", *freq, raw_smp_processor_id());
	return 0;
}
EXPORT_SYMBOL(gs_governor_memlat_get_freq);

/**
 * gs_memlat_populate_governor - Parses memlat governor data from an input device tree node.
 *
 * Inputs:
 * @dev:		The memlat governor's underlying device.
 * @governor_node:	The tree node contanin governor data.
 *
 * Returns:		Non-zero on error.
*/
static int gs_memlat_populate_governor(struct device *dev, struct device_node *governor_node)
{
	struct device_node *cluster_node = NULL;
	struct cluster_config *cluster;
	int cluster_idx;
	int ret = 0;

	memlat_node.num_cpu_clusters = of_get_child_count(governor_node);

	/* Allocate a container for clusters. */
	memlat_node.cpu_configs_arr = devm_kzalloc(
		dev, sizeof(struct cluster_config) * memlat_node.num_cpu_clusters, GFP_KERNEL);
	if (!memlat_node.cpu_configs_arr) {
		dev_err(dev, "no mem for cluster_config\n");
		return -ENOMEM;
	}

	/* Populate the Components. */
	cluster_idx = 0;
	while ((cluster_node = of_get_next_child(governor_node, cluster_node)) != NULL) {
		cluster = &memlat_node.cpu_configs_arr[cluster_idx];
		if ((ret = populate_cluster_config(dev, cluster_node, cluster)))
			return ret;

		/* Increment pointer. */
		cluster_idx += 1;
	}

	return 0;
}

/**
 * gs_memlat_governor_initialize - Initializes the dsulat governor from a DT Node.
 *
 * Inputs:
 * @governor_node:	The tree node contanin governor data.
 * @data:		The devfreq data to update frequencies.
 *
 * Returns:		Non-zero on error.
*/
int gs_memlat_governor_initialize(struct device_node *governor_node,
				  struct exynos_devfreq_data *data)
{
	int ret = 0;
	struct device *dev = data->dev;

	/* Configure memlat governor. */
	ret = gs_memlat_populate_governor(dev, governor_node);
	if (ret)
		return ret;

	memlat_node.attr_grp = &memlat_dev_attr_group;
	memlat_node.devfreq_data = data;
	return 0;

}
EXPORT_SYMBOL(gs_memlat_governor_initialize);

/* We hold the struct for the governor here. */
static struct devfreq_governor gs_governor_memlat = {
	.name = "gs_memlat",
	.get_target_freq = gs_governor_memlat_get_freq,
	.event_handler = gs_governor_memlat_ev_handler,
};

/* Adds this governor to a devfreq.*/
int gs_memlat_governor_register(void)
{
	return devfreq_add_governor(&gs_governor_memlat);
}
EXPORT_SYMBOL(gs_memlat_governor_register);

/* Remove this governor to a devfreq.*/
void gs_memlat_governor_unregister(void)
{
	devfreq_remove_governor(&gs_governor_memlat);
}
EXPORT_SYMBOL(gs_memlat_governor_unregister);

void gs_memlat_governor_set_devfreq_ready(void) {
	memlat_node.devfreq_initialized = true;
}
EXPORT_SYMBOL(gs_memlat_governor_set_devfreq_ready);

MODULE_AUTHOR("Will Song <jinpengsong@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google Source Memlat Governor");