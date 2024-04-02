// SPDX-License-Identifier: GPL-2.0-only
/* core.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2021 Google LLC
 */
#include <linux/sched.h>
#include <kernel/sched/sched.h>
#include <../../../vh/include/sched.h>

#define LIB_PATH_LENGTH 512
static char sched_lib_name[LIB_PATH_LENGTH];
unsigned long sched_lib_mask_out_val;
unsigned long sched_lib_mask_in_val;

static DEFINE_MUTEX(__sched_lib_name_mutex);

ssize_t sched_lib_name_store(struct file *filp,
			     const char __user *ubuffer, size_t count,
			     loff_t *ppos)
{
	if (count >= sizeof(sched_lib_name))
		return -EINVAL;

	mutex_lock(&__sched_lib_name_mutex);

	if (copy_from_user(sched_lib_name, ubuffer, count)) {
		sched_lib_name[0] = '\0';
		mutex_unlock(&__sched_lib_name_mutex);
		return -EFAULT;
	}

	sched_lib_name[count] = '\0';
	mutex_unlock(&__sched_lib_name_mutex);
	return count;
}

int sched_lib_name_show(struct seq_file *m, void *v)
{
	mutex_lock(&__sched_lib_name_mutex);
	seq_printf(m, "%s\n", sched_lib_name);
	mutex_unlock(&__sched_lib_name_mutex);
	return 0;
}

static bool is_sched_lib_based_app(pid_t pid)
{
	const char *name = NULL;
	struct vm_area_struct *vma;
	char path_buf[LIB_PATH_LENGTH];
	char tmp_lib_name[LIB_PATH_LENGTH];
	bool found = false;
	struct task_struct *p;
	struct mm_struct *mm;
	struct vendor_task_struct *vp;

	rcu_read_lock();
	p = pid ? get_pid_task(find_vpid(pid), PIDTYPE_PID) : get_task_struct(current);
	rcu_read_unlock();
	if (!p)
		return false;

	// top app
	vp = get_vendor_task_struct(p);
	if (!vp || ((vp->group != VG_TOPAPP) && (vp->group != VG_FOREGROUND)))
		goto put_task_struct;

	// Copy lib name for thread safe access
	mutex_lock(&__sched_lib_name_mutex);
	if (strnlen(sched_lib_name, LIB_PATH_LENGTH) == 0)
		goto put_task_struct;
	strlcpy(tmp_lib_name, sched_lib_name, LIB_PATH_LENGTH);
	mutex_unlock(&__sched_lib_name_mutex);

	mm = get_task_mm(p);
	if (!mm)
		goto put_task_struct;

	down_read(&mm->mmap_lock);
	for (vma = mm->mmap; vma ; vma = vma->vm_next) {
		if (vma->vm_file && vma->vm_flags & VM_EXEC) {
			name = d_path(&vma->vm_file->f_path,
					path_buf, LIB_PATH_LENGTH);
			if (IS_ERR(name))
				goto release_sem;

			if (strnstr(name, tmp_lib_name, strnlen(name, LIB_PATH_LENGTH))) {
				found = true;
				goto release_sem;
			}
		}
	}

release_sem:
	up_read(&mm->mmap_lock);
	mmput(mm);
put_task_struct:
	put_task_struct(p);
	return found;
}

void rvh_sched_setaffinity_mod(void *data, struct task_struct *task,
				struct cpumask *in_mask, int *res)
{
	if (*res != 0)
		return;

	if (!(sched_lib_mask_in_val && sched_lib_mask_out_val))
		return;

	if (in_mask->bits[0] != sched_lib_mask_in_val)
		return;

	if (!is_sched_lib_based_app(current->pid))
		return;

	in_mask->bits[0] = sched_lib_mask_out_val;
	set_cpus_allowed_ptr(task, in_mask);

	pr_debug("schedlib setaff tid: %d, mask out: %*pb\n",
		 task_pid_nr(task), cpumask_pr_args(in_mask));
}
