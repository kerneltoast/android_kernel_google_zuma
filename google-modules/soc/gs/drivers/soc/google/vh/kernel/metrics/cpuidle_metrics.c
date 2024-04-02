// SPDX-License-Identifier: GPL-2.0-only
/* cpuidle_metrics.c
 *
 * Support for CPU Idle metrics
 *
 * Copyright 2023 Google LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <trace/events/power.h>
#include <soc/google/cpuidle_metrics.h>

/* Constants for histogram bins */
#define NUM_TARGET_BINS 8
#define PERCENT_INCREMENT 25

/* Constants for cluster stats */
#define NAME_LEN 32
#define MAX_CLUSTERS 10

/* struct to store histogram statistics per state per cpu or cluster*/
struct histogram_stats {
	/* time entered power mode */
	ktime_t entry_time;

	/* histogram bins by % of target residency incremented by %increment -> 0%, 25%, 50%, 75%, 100%, 125%, 150%  */
	unsigned int target_time_histogram[NUM_TARGET_BINS + 1];
};

struct power_stats {
	char name[NAME_LEN];
	int uid;
	int target_residency;
	int entered_state;
	struct histogram_stats hist_stats[CPUIDLE_STATE_MAX];
	bool initialized;
};

// variables for cpu and cluster stats
DEFINE_PER_CPU(struct power_stats, all_cpu_stats);
static DEFINE_PER_CPU(spinlock_t, cpu_spinlocks);
static struct power_stats all_cluster_stats[MAX_CLUSTERS];
static spinlock_t cluster_spinlocks[MAX_CLUSTERS];
static int cpuidle_state_max;

/*********************************************************************
 *                          HELPER FUNCTIONS                         *
 *********************************************************************/

// method to append to the cpuidle histogram
static void histogram_append(struct histogram_stats *hist_stat, s64 time_us,
			     s64 target_residency_us)
{
	int percent_index;

	// append to target residency histogram
	percent_index = time_us * 100 / target_residency_us / PERCENT_INCREMENT;
	if (percent_index > NUM_TARGET_BINS)
		percent_index = NUM_TARGET_BINS;
	hist_stat->target_time_histogram[percent_index] += 1;
}

// method to register all of the cpu cluster stats
void cpuidle_metrics_histogram_register(char *name, int uid, s64 target_residency_us)
{
	if (uid < 0 || uid >= MAX_CLUSTERS) {
		pr_err("Invalid uid %d passed for registration of histrogram stats", uid);
		return;
	}
	spin_lock(&cluster_spinlocks[uid]);
	if (all_cluster_stats[uid].initialized) {
		spin_unlock(&cluster_spinlocks[uid]);
		return;
	}
	strcpy(all_cluster_stats[uid].name, name);
	all_cluster_stats[uid].target_residency = target_residency_us;
	all_cluster_stats[uid].initialized = true;
	spin_unlock(&cluster_spinlocks[uid]);
}

EXPORT_SYMBOL_GPL(cpuidle_metrics_histogram_register);

// method to append to histogram from external modules
inline void cpuidle_metrics_histogram_append(int uid, s64 time_us)
{
	if (uid < 0 || uid >= MAX_CLUSTERS) {
		pr_err("Invalid uid passed for histogram creation\n");
		return;
	}
	spin_lock(&cluster_spinlocks[uid]);
	if (!all_cluster_stats[uid].initialized) {
		pr_err("Uid %d not initialized for histogram creation", uid);
		spin_unlock(&cluster_spinlocks[uid]);
		return;
	}
	histogram_append(&all_cluster_stats[uid].hist_stats[0], time_us,
			 all_cluster_stats[uid].target_residency);
	spin_unlock(&cluster_spinlocks[uid]);
}

EXPORT_SYMBOL_GPL(cpuidle_metrics_histogram_append);

/*********************************************************************
 *                          HOOKS
 *********************************************************************/

static void hook_cpu_idle(void *data, unsigned int state, unsigned int cpu)
{
	struct power_stats *cpu_stat;
	spin_lock(&per_cpu(cpu_spinlocks, cpu));
	cpu_stat = &per_cpu(all_cpu_stats, cpu);

	if (state != -1) {
		// log entered state and time
		cpu_stat->entered_state = state;
		cpu_stat->hist_stats[cpu_stat->entered_state].entry_time = ktime_get_mono_fast_ns();
	} else {
		s64 time_delta;
		struct histogram_stats hist_stat = cpu_stat->hist_stats[cpu_stat->entered_state];

		// find time delta and append to histogram
		time_delta = ktime_to_us(ktime_sub(ktime_get_mono_fast_ns(), hist_stat.entry_time));
		histogram_append(&cpu_stat->hist_stats[cpu_stat->entered_state], time_delta,
				 cpu_stat->target_residency);
	}

	spin_unlock(&per_cpu(cpu_spinlocks, cpu));
}

/*******************************************************************
 *                       		SYSFS			   				   *
 *******************************************************************/

static ssize_t cpuidle_histogram_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i, j, cpu, ret = 0;
	struct power_stats cpu_stat;

	// output header
	ret += snprintf(
		buf + ret, PAGE_SIZE - ret,
		"\nFormat: target_histogram bins: min = 0 %%, max = %d %%, increment = %d %%\n\n",
		NUM_TARGET_BINS * PERCENT_INCREMENT, PERCENT_INCREMENT);

	// iterate over all idle states
	for (i = 0; i <= cpuidle_state_max; i++) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "[state%d]\n", i);
		for_each_possible_cpu (cpu) {
			// output fixed_time_histogram
			spin_lock(&per_cpu(cpu_spinlocks, cpu));
			cpu_stat = per_cpu(all_cpu_stats, cpu);
			ret += snprintf(buf + ret, PAGE_SIZE - ret,
					"cpu%d, target_residency = %lld us\n", cpu,
					cpu_stat.target_residency);

			// output target time histogram
			for (j = 0; j <= NUM_TARGET_BINS; j++) {
				ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d, ",
						cpu_stat.hist_stats[i].target_time_histogram[j]);
			}
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
			spin_unlock(&per_cpu(cpu_spinlocks, cpu));
		}
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	}

	return ret;
}

static ssize_t cpucluster_histogram_show(struct kobject *kobj, struct kobj_attribute *attr,
					 char *buf)
{
	int j = 0;
	int i = 0;
	int ret = 0;
	struct power_stats cluster_stat;

	ret += snprintf(
		buf + ret, PAGE_SIZE - ret,
		"\nFormat: target_histogram bins: min = 0 %%, max = %d %%, increment = %d %%\n\n",
		NUM_TARGET_BINS * PERCENT_INCREMENT, PERCENT_INCREMENT);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "[modes]\n");
	for (i = 0; i < MAX_CLUSTERS; i++) {
		spin_lock(&cluster_spinlocks[i]);
		if (all_cluster_stats[i].initialized) {
			cluster_stat = all_cluster_stats[i];
			ret += snprintf(buf + ret, PAGE_SIZE - ret,
					"%-7s, target_residency = %lld us: \n", cluster_stat.name,
					cluster_stat.target_residency);
			// output the target time histogram
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "target: ");
			for (j = 0; j <= NUM_TARGET_BINS; j++) {
				ret += snprintf(
					buf + ret, PAGE_SIZE - ret, "%d, ",
					cluster_stat.hist_stats[0].target_time_histogram[j]);
			}
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
		}
		spin_unlock(&cluster_spinlocks[i]);
	}

	return ret;
}

static struct kobj_attribute cpuidle_histogram_attr =
	__ATTR(cpuidle_histogram, 0444, cpuidle_histogram_show, NULL);
static struct kobj_attribute cpucluster_histogram_attr =
	__ATTR(cpucluster_histogram, 0444, cpucluster_histogram_show, NULL);

static struct attribute *cpuidle_histogram_attrs[] = { &cpuidle_histogram_attr.attr,
						       &cpucluster_histogram_attr.attr, NULL };

static const struct attribute_group cpuidle_histogram_attr_group = {
	.attrs = cpuidle_histogram_attrs,
	.name = "cpuidle_histogram"
};

/*********************************************************************
 *                  		INITIALIZE DRIVER                        *
 *********************************************************************/

int cpuidle_metrics_init(struct kobject *metrics_kobj)
{
	int ret = 0;
	int cpu, target_residency, max, cluster_index;

	// create sysfs file
	if (!metrics_kobj) {
		pr_err("metrics_kobj is not initialized\n");
		return -EINVAL;
	}
	if (sysfs_create_group(metrics_kobj, &cpuidle_histogram_attr_group)) {
		pr_err("failed to create cpuidle_histogram folder\n");
		return -ENOMEM;
	}

	// register hook
	ret = register_trace_cpu_idle(hook_cpu_idle, NULL);
	WARN_ON(ret);

	for_each_possible_cpu (cpu) {
		struct device_node *cpu_node, *state_node;
		struct power_stats *stats = &per_cpu(all_cpu_stats, cpu);
		spin_lock_init(&per_cpu(cpu_spinlocks, cpu));

		// find min residency per cpu
		cpu_node = of_cpu_device_node_get(cpu);
		state_node = of_parse_phandle(cpu_node, "cpu-idle-states", 0);
		ret = of_property_read_u32(state_node, "min-residency-us", &target_residency);
		stats->target_residency = target_residency;

		// find maximum idle state
		max = of_count_phandle_with_args(cpu_node, "cpu-idle-states", NULL);
		if (max > cpuidle_state_max)
			cpuidle_state_max = max;
	}

	for (cluster_index = 0; cluster_index < MAX_CLUSTERS; cluster_index++) {
		all_cluster_stats[cluster_index].initialized = false;
		spin_lock_init(&cluster_spinlocks[cluster_index]);
	}

	pr_info("cpuidle_metrics driver initialized! :D\n");
	return ret;
}
