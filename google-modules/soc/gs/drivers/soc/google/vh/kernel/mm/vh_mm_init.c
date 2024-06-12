// SPDX-License-Identifier: GPL-2.0-only
/* vendor_mm_init.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2022 Google LLC
 */

#include <linux/kobject.h>
#include <linux/module.h>

#include "../../include/mm.h"

#define VENDOR_MM_RW(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RW(_name)

static ssize_t kswapd_cpu_affinity_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	struct task_struct *tsk;
	cpumask_t cpumask;

	rcu_read_lock();
	for_each_process(tsk) {
		/* assume we only have 1 kswapd */
		if (tsk->flags & PF_KSWAPD) {
			cpumask	= tsk->cpus_mask;
			break;
		}
	}
	rcu_read_unlock();

	return cpumap_print_to_pagebuf(false, buf, &cpumask);
}

static ssize_t kswapd_cpu_affinity_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t len)
{
	struct task_struct *tsk;
	cpumask_t requested_cpumask, dest_cpumask;
	int ret;

	ret = cpumask_parse(buf, &requested_cpumask);
	if (ret < 0 || cpumask_empty(&requested_cpumask))
		return -EINVAL;

	cpumask_and(&dest_cpumask, &requested_cpumask, cpu_possible_mask);

	rcu_read_lock();
	for_each_process(tsk) {
		/* assume we only have 1 kswapd */
		if (tsk->flags & PF_KSWAPD) {
			set_cpus_allowed_ptr(tsk, &dest_cpumask);
			break;
		}
	}
	rcu_read_unlock();

	return len;
}
VENDOR_MM_RW(kswapd_cpu_affinity);

static struct attribute *vendor_mm_attrs[] = {
	&kswapd_cpu_affinity_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vendor_mm);

struct kobject *vendor_mm_kobj;
EXPORT_SYMBOL_GPL(vendor_mm_kobj);

extern int pixel_mm_cma_sysfs(struct kobject *parent);

static int vh_mm_init(void)
{
	int ret;

	vendor_mm_kobj = kobject_create_and_add("vendor_mm", kernel_kobj);
	if (!vendor_mm_kobj)
		return -ENOMEM;

	ret = sysfs_create_groups(vendor_mm_kobj, vendor_mm_groups);
	if (ret)
		goto out_err;

	ret = pixel_mm_cma_sysfs(vendor_mm_kobj);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_ptep_clear_flush_young(
			vh_ptep_clear_flush_young, NULL);
	if(ret)
		goto out_err;

	return ret;

out_err:
	kobject_put(vendor_mm_kobj);
	return ret;
}
module_init(vh_mm_init);
MODULE_LICENSE("GPL v2");
