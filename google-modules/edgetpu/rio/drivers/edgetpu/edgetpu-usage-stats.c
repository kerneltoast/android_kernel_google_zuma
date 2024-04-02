// SPDX-License-Identifier: GPL-2.0
/*
 * EdgeTPU usage stats
 *
 * Copyright (C) 2020-2023 Google, Inc.
 */

#include <linux/device.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>
#include <linux/mutex.h>

#include <gcip/gcip-usage-stats.h>

#include "edgetpu-config.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-usage-stats.h"

/*
 * TODO(b/279844328): Use the default show callback of GCIP (i.e., remove this callback and pass
 * NULL to the show callback of gcip_usage_stats_attr_tpu_usage) after we are ready to update the
 * logging format of this metric.
 */
static ssize_t tpu_usage_show(struct device *dev, struct gcip_usage_stats_attr *attr, char *buf,
			      void *data)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct gcip_usage_stats *ustats = &etdev->usage_stats->ustats;
	int i;
	int ret = 0;
	unsigned int bkt;
	struct gcip_usage_stats_core_usage_uid_entry *uid_entry;

	edgetpu_kci_update_usage(etdev);
	/* uid: state0speed state1speed ... */
	ret += scnprintf(buf, PAGE_SIZE, "uid:");

	mutex_lock(&ustats->dvfs_freqs_lock);
	if (!ustats->dvfs_freqs_num) {
		mutex_unlock(&ustats->dvfs_freqs_lock);
		for (i = 0; i < EDGETPU_NUM_STATES; i++)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, " %d",
					 edgetpu_states_display[i]);
	} else {
		for (i = 0; i < ustats->dvfs_freqs_num; i++)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, " %d", ustats->dvfs_freqs[i]);
		mutex_unlock(&ustats->dvfs_freqs_lock);
	}

	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");

	mutex_lock(&ustats->usage_stats_lock);

	hash_for_each (ustats->core_usage_htable[attr->subcomponent], bkt, uid_entry, node) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%d:", uid_entry->uid);

		for (i = 0; i < EDGETPU_NUM_STATES; i++)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, " %lld",
					 uid_entry->time_in_state[i]);

		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");
	}

	mutex_unlock(&ustats->usage_stats_lock);

	return ret;
}

/* Core usage. */
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_CORE_USAGE, 0, 0, tpu_usage,
				tpu_usage_show, NULL);

/* Component utilization. */
static GCIP_USAGE_STATS_ATTR_RO(GCIP_USAGE_STATS_METRIC_TYPE_COMPONENT_UTILIZATION,
				GCIP_USAGE_STATS_COMPONENT_UTILIZATION_IP, 0, device_utilization,
				NULL);

static GCIP_USAGE_STATS_ATTR_RO(GCIP_USAGE_STATS_METRIC_TYPE_COMPONENT_UTILIZATION,
				GCIP_USAGE_STATS_COMPONENT_UTILIZATION_CORES, 0, tpu_utilization,
				NULL);

/* Counter. */
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_TPU_ACTIVIY_CYCLES, 0,
				tpu_active_cycle_count, NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_TPU_THROTTLE_STALLS, 0,
				tpu_throttle_stall_count, NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_WORKLOAD,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, inference_count, NULL,
				NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_TPU_OP,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, tpu_op_count, NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_PARAM_CACHING_HIT, 0,
				param_cache_hit_count, NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_PARAM_CACHING_MISS, 0,
				param_cache_miss_count, NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_CONTEXT_PREEMPTIONS,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, context_preempt_count,
				NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_HW_PREEMPTIONS,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, hardware_preempt_count,
				NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_TOTAL_HW_CONTEXT_SAVE_TIME,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, hardware_ctx_save_time,
				NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_TOTAL_SCALAR_FENCE_WAIT_TIME,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, scalar_fence_wait_time,
				NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_NUM_OF_LONG_SUSPENDS, 0,
				long_suspend_count, NULL, NULL);

#if EDGETPU_TPU_CLUSTER_COUNT > 1
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_NUM_OF_RECONFIGURATIONS, 0,
				reconfigurations, NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_COUNTER,
				GCIP_USAGE_STATS_COUNTER_NUM_OF_RECONFIGURATIONS_BY_PREEMPTION, 0,
				preempt_reconfigurations, NULL, NULL);
#endif

/* Max watermark. */
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_MAX_WATERMARK,
				GCIP_USAGE_STATS_MAX_WATERMARK_OUTSTANDING_CMDS, 0,
				outstanding_commands_max, NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_MAX_WATERMARK,
				GCIP_USAGE_STATS_MAX_WATERMARK_PREEMPTION_DEPTH,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, preempt_depth_max, NULL,
				NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_MAX_WATERMARK,
				GCIP_USAGE_STATS_MAX_WATERMARK_MAX_HW_CONTEXT_SAVE_TIME,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, hardware_ctx_save_time_max,
				NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_MAX_WATERMARK,
				GCIP_USAGE_STATS_MAX_WATERMARK_MAX_SCALAR_FENCE_WAIT_TIME,
				GCIP_USAGE_STATS_ATTR_ALL_SUBCOMPONENTS, scalar_fence_wait_time_max,
				NULL, NULL);

static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_MAX_WATERMARK,
				GCIP_USAGE_STATS_MAX_WATERMARK_MAX_SUSPEND_TIME, 0,
				suspend_time_max, NULL, NULL);

/* Thread statistics. */
static GCIP_USAGE_STATS_ATTR_RW(GCIP_USAGE_STATS_METRIC_TYPE_THREAD_STATS, 0, 0, fw_thread_stats,
				NULL, NULL);

/* TODO(b/279844328): Add `scaling_available_frequencies` attribute which prints DVFS freqs. */

static struct gcip_usage_stats_attr *attrs[] = {
	&gcip_usage_stats_attr_tpu_usage,
	&gcip_usage_stats_attr_device_utilization,
	&gcip_usage_stats_attr_tpu_utilization,
	&gcip_usage_stats_attr_tpu_active_cycle_count,
	&gcip_usage_stats_attr_tpu_throttle_stall_count,
	&gcip_usage_stats_attr_inference_count,
	&gcip_usage_stats_attr_tpu_op_count,
	&gcip_usage_stats_attr_param_cache_hit_count,
	&gcip_usage_stats_attr_param_cache_miss_count,
	&gcip_usage_stats_attr_context_preempt_count,
	&gcip_usage_stats_attr_hardware_preempt_count,
	&gcip_usage_stats_attr_hardware_ctx_save_time,
	&gcip_usage_stats_attr_scalar_fence_wait_time,
	&gcip_usage_stats_attr_long_suspend_count,
#if EDGETPU_TPU_CLUSTER_COUNT > 1
	&gcip_usage_stats_attr_reconfigurations,
	&gcip_usage_stats_attr_preempt_reconfigurations,
#endif
	&gcip_usage_stats_attr_outstanding_commands_max,
	&gcip_usage_stats_attr_preempt_depth_max,
	&gcip_usage_stats_attr_hardware_ctx_save_time_max,
	&gcip_usage_stats_attr_scalar_fence_wait_time_max,
	&gcip_usage_stats_attr_suspend_time_max,
	&gcip_usage_stats_attr_fw_thread_stats,
};

static int update_usage_kci(void *data)
{
	struct edgetpu_usage_stats *ustats = data;

	return edgetpu_kci_update_usage(ustats->etdev);
}

static int get_default_dvfs_freqs_num(void *data)
{
	return EDGETPU_NUM_STATES;
}

static int get_default_dvfs_freq(int idx, void *data)
{
	if (idx >= EDGETPU_NUM_STATES)
		return 0;
	return edgetpu_states_display[idx];
}

static const struct gcip_usage_stats_ops ustats_ops = {
	.update_usage_kci = update_usage_kci,
	.get_default_dvfs_freqs_num = get_default_dvfs_freqs_num,
	.get_default_dvfs_freq = get_default_dvfs_freq,
};

void edgetpu_usage_stats_process_buffer(struct edgetpu_dev *etdev, void *buf)
{
	if (!etdev->usage_stats)
		return;
	gcip_usage_stats_process_buffer(&etdev->usage_stats->ustats, buf);
}

void edgetpu_usage_stats_init(struct edgetpu_dev *etdev)
{
	struct edgetpu_usage_stats *ustats;
	struct gcip_usage_stats_args args;
	int ret;

	ustats = devm_kzalloc(etdev->dev, sizeof(*etdev->usage_stats), GFP_KERNEL);
	if (!ustats)
		return;

	args.version = EDGETPU_USAGE_METRIC_VERSION;
	args.dev = etdev->dev;
	args.ops = &ustats_ops;
	args.attrs = attrs;
	args.num_attrs = ARRAY_SIZE(attrs);
	args.subcomponents = EDGETPU_TPU_CLUSTER_COUNT;
	args.data = ustats;
	ustats->etdev = etdev;

	ret = gcip_usage_stats_init(&ustats->ustats, &args);
	if (ret) {
		etdev_warn(etdev, "failed to create the usage_stats attrs: %d", ret);
		devm_kfree(etdev->dev, ustats);
		return;
	}

	etdev->usage_stats = ustats;

	etdev_dbg(etdev, "%s init\n", __func__);
}

void edgetpu_usage_stats_exit(struct edgetpu_dev *etdev)
{
	struct edgetpu_usage_stats *ustats = etdev->usage_stats;

	if (ustats) {
		gcip_usage_stats_exit(&ustats->ustats);
		devm_kfree(etdev->dev, ustats);
	}
	etdev->usage_stats = NULL;

	etdev_dbg(etdev, "%s exit\n", __func__);
}
