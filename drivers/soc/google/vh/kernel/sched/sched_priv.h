/* SPDX-License-Identifier: GPL-2.0 */
#include "../../include/sched.h"
#include "binder_internal.h"
#include <asm/atomic.h>

#define UCLAMP_STATS_SLOTS  21
#define UCLAMP_STATS_STEP   (100 / (UCLAMP_STATS_SLOTS - 1))
#define DEF_UTIL_THRESHOLD  1280
#define DEF_UTIL_POST_INIT_SCALE  512
#define C1_EXIT_LATENCY     1
#define THREAD_PRIORITY_TOP_APP_BOOST 110
#define THREAD_PRIORITY_BACKGROUND    130
#define THREAD_PRIORITY_LOWEST        139
#define LIST_QUEUED         0xa5a55a5a
#define LIST_NOT_QUEUED     0x5a5aa5a5
/*
 * For cpu running normal tasks, its uclamp.min will be 0 and uclamp.max will be 1024,
 * and the sum will be 1024. We use this as index that cpu is not running important tasks.
 */
#define DEFAULT_IMPRATANCE_THRESHOLD	1024

/*
 * Sets uclamp_max to the task based on the most efficient point of the CPU the
 * task is currently running on.
 */
#define AUTO_UCLAMP_MAX_MAGIC		-2

#define AUTO_UCLAMP_MAX_FLAG_TASK	BIT(0)
#define AUTO_UCLAMP_MAX_FLAG_GROUP	BIT(1)

#define UCLAMP_BUCKET_DELTA DIV_ROUND_CLOSEST(SCHED_CAPACITY_SCALE, UCLAMP_BUCKETS)

/* Iterate thr' all leaf cfs_rq's on a runqueue */
#define for_each_leaf_cfs_rq_safe(rq, cfs_rq, pos)			\
	list_for_each_entry_safe(cfs_rq, pos, &rq->leaf_cfs_rq_list,	\
				 leaf_cfs_rq_list)

#define get_bucket_id(__val)								      \
		min_t(unsigned int,							      \
		      __val / DIV_ROUND_CLOSEST(SCHED_CAPACITY_SCALE, UCLAMP_BUCKETS),	      \
		      UCLAMP_BUCKETS - 1)

extern unsigned int sched_capacity_margin[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_dvfs_headroom[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_auto_uclamp_max[CONFIG_VH_SCHED_MAX_CPU_NR];

extern int pixel_cpu_num;
extern int pixel_cluster_num;
extern int *pixel_cluster_start_cpu;
extern int *pixel_cluster_cpu_num;
extern int *pixel_cpu_to_cluster;
extern int *pixel_cluster_enabled;
extern unsigned int *pixel_cpd_exit_latency;

#define cpu_overutilized(cap, max, cpu)	\
		((cap) * sched_capacity_margin[cpu] > (max) << SCHED_CAPACITY_SHIFT)

#define lsub_positive(_ptr, _val) do {				\
	typeof(_ptr) ptr = (_ptr);				\
	*ptr -= min_t(typeof(*ptr), *ptr, _val);		\
} while (0)

#define sub_positive(_ptr, _val) do {				\
	typeof(_ptr) ptr = (_ptr);				\
	typeof(*ptr) val = (_val);				\
	typeof(*ptr) res, var = READ_ONCE(*ptr);		\
	res = var - val;					\
	if (res > var)						\
		res = 0;					\
	WRITE_ONCE(*ptr, res);					\
} while (0)

#define __container_of(ptr, type, member) ({			\
	void *__mptr = (void *)(ptr);				\
	((type *)(__mptr - offsetof(type, member))); })

#define remove_from_vendor_group_list(__node, __group) do {	\
	raw_spin_lock(&vendor_group_list[__group].lock);	\
	if (__node == vendor_group_list[__group].cur_iterator)	\
		vendor_group_list[__group].cur_iterator = (__node)->prev;	\
	list_del_init(__node);					\
	raw_spin_unlock(&vendor_group_list[__group].lock);	\
} while (0)

#define add_to_vendor_group_list(__node, __group) do {		\
	raw_spin_lock(&vendor_group_list[__group].lock);	\
	list_add_tail(__node, &vendor_group_list[__group].list);	\
	raw_spin_unlock(&vendor_group_list[__group].lock);	\
} while (0)

struct vendor_group_property {
	bool prefer_idle;
	bool prefer_high_cap;
	bool task_spreading;
	bool auto_uclamp_max;
#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
	unsigned int group_throttle;
#endif
	cpumask_t preferred_idle_mask_low;
	cpumask_t preferred_idle_mask_mid;
	cpumask_t preferred_idle_mask_high;
	unsigned int uclamp_min_on_nice_low_value;
	unsigned int uclamp_min_on_nice_mid_value;
	unsigned int uclamp_min_on_nice_high_value;
	unsigned int uclamp_max_on_nice_low_value;
	unsigned int uclamp_max_on_nice_mid_value;
	unsigned int uclamp_max_on_nice_high_value;
	unsigned int uclamp_min_on_nice_low_prio;
	unsigned int uclamp_min_on_nice_mid_prio;
	unsigned int uclamp_min_on_nice_high_prio;
	unsigned int uclamp_max_on_nice_low_prio;
	unsigned int uclamp_max_on_nice_mid_prio;
	unsigned int uclamp_max_on_nice_high_prio;
	bool uclamp_min_on_nice_enable;
	bool uclamp_max_on_nice_enable;
	enum utilization_group ug;
	struct uclamp_se uc_req[UCLAMP_CNT];
};

#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
struct vendor_util_group_property {
#if IS_ENABLED(CONFIG_USE_GROUP_THROTTLE)
	unsigned int group_throttle;
#endif
	struct uclamp_se uc_req[UCLAMP_CNT];
};
#endif

struct uclamp_stats {
	spinlock_t lock;
	bool last_min_in_effect;
	bool last_max_in_effect;
	unsigned int last_uclamp_min_index;
	unsigned int last_uclamp_max_index;
	unsigned int last_util_diff_min_index;
	unsigned int last_util_diff_max_index;
	u64 util_diff_min[UCLAMP_STATS_SLOTS];
	u64 util_diff_max[UCLAMP_STATS_SLOTS];
	u64 total_time;
	u64 last_update_time;
	u64 time_in_state_min[UCLAMP_STATS_SLOTS];
	u64 time_in_state_max[UCLAMP_STATS_SLOTS];
	u64 effect_time_in_state_min[UCLAMP_STATS_SLOTS];
	u64 effect_time_in_state_max[UCLAMP_STATS_SLOTS];
};

#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
struct vendor_cfs_util {
	raw_spinlock_t lock;
	struct sched_avg avg;
	unsigned long util_removed;
	unsigned long util_est;
};
#endif

struct vendor_group_list {
	struct list_head list;
	raw_spinlock_t lock;
	struct list_head *cur_iterator;
};

unsigned long apply_dvfs_headroom(unsigned long util, int cpu, bool tapered);
unsigned long map_util_freq_pixel_mod(unsigned long util, unsigned long freq,
				      unsigned long cap);
void check_migrate_rt_task(struct rq *rq, struct task_struct *p);
void rvh_uclamp_eff_get_pixel_mod(void *data, struct task_struct *p, enum uclamp_id clamp_id,
				  struct uclamp_se *uclamp_max, struct uclamp_se *uclamp_eff,
				  int *ret);

enum vendor_group_attribute {
	VTA_TASK_GROUP,
	VTA_PROC_GROUP,
};

#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
struct vendor_task_group_struct {
	enum vendor_group group;
};

ANDROID_VENDOR_CHECK_SIZE_ALIGN(u64 android_vendor_data1[4], struct vendor_task_group_struct t);
#endif

extern bool vendor_sched_reduce_prefer_idle;
extern struct vendor_group_property vg[VG_MAX];

DECLARE_STATIC_KEY_FALSE(uclamp_min_filter_enable);
DECLARE_STATIC_KEY_FALSE(uclamp_max_filter_enable);

DECLARE_STATIC_KEY_FALSE(tapered_dvfs_headroom_enable);

#define SCHED_PIXEL_FORCE_UPDATE		BIT(8)

/*****************************************************************************/
/*                       Upstream Code Section                               */
/*****************************************************************************/
/*
 * This part of code is copied from Android common GKI kernel and unmodified.
 * Any change for these functions in upstream GKI would require extensive review
 * to make proper adjustment in vendor hook.
 */
extern struct uclamp_se uclamp_default[UCLAMP_CNT];

void set_next_buddy(struct sched_entity *se);

static inline unsigned long task_util(struct task_struct *p)
{
	return READ_ONCE(p->se.avg.util_avg);
}

static inline unsigned long _task_util_est(struct task_struct *p)
{
	struct util_est ue = READ_ONCE(p->se.avg.util_est);

	return max(ue.ewma, (ue.enqueued & ~UTIL_AVG_UNCHANGED));
}

static inline unsigned long task_util_est(struct task_struct *p)
{
	return max(task_util(p), _task_util_est(p));
}

static inline unsigned long capacity_of(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity;
}

static inline unsigned int uclamp_none(enum uclamp_id clamp_id)
{
	if (clamp_id == UCLAMP_MIN)
		return 0;
	return SCHED_CAPACITY_SCALE;
}

static inline void uclamp_se_set(struct uclamp_se *uc_se,
				 unsigned int value, bool user_defined)
{
	uc_se->value = value;
	uc_se->bucket_id = get_bucket_id(value);
	uc_se->user_defined = user_defined;
}

extern inline void uclamp_rq_inc_id(struct rq *rq, struct task_struct *p,
				    enum uclamp_id clamp_id);
extern inline void uclamp_rq_dec_id(struct rq *rq, struct task_struct *p,
				    enum uclamp_id clamp_id);

static inline int util_fits_cpu(unsigned long util,
				unsigned long uclamp_min,
				unsigned long uclamp_max,
				int cpu)
{
	unsigned long capacity_orig, capacity_orig_thermal;
	unsigned long capacity = capacity_of(cpu);
	bool fits, uclamp_max_fits;

	/*
	 * Check if the real util fits without any uclamp boost/cap applied.
	 */
	fits = !cpu_overutilized(util, capacity, cpu);

	if (!uclamp_is_used())
		return fits;

	/*
	 * We must use capacity_orig_of() for comparing against uclamp_min and
	 * uclamp_max. We only care about capacity pressure (by using
	 * capacity_of()) for comparing against the real util.
	 *
	 * If a task is boosted to 1024 for example, we don't want a tiny
	 * pressure to skew the check whether it fits a CPU or not.
	 *
	 * Similarly if a task is capped to capacity_orig_of(little_cpu), it
	 * should fit a little cpu even if there's some pressure.
	 *
	 * Only exception is for thermal pressure since it has a direct impact
	 * on available OPP of the system.
	 *
	 * We honour it for uclamp_min only as a drop in performance level
	 * could result in not getting the requested minimum performance level.
	 *
	 * For uclamp_max, we can tolerate a drop in performance level as the
	 * goal is to cap the task. So it's okay if it's getting less.
	 *
	 * In case of capacity inversion, which is not handled yet, we should
	 * honour the inverted capacity for both uclamp_min and uclamp_max all
	 * the time.
	 */
	capacity_orig = capacity_orig_of(cpu);
	capacity_orig_thermal = capacity_orig - arch_scale_thermal_pressure(cpu);

	/*
	 * We want to force a task to fit a cpu as implied by uclamp_max.
	 * But we do have some corner cases to cater for..
	 *
	 *
	 *                                 C=z
	 *   |                             ___
	 *   |                  C=y       |   |
	 *   |_ _ _ _ _ _ _ _ _ ___ _ _ _ | _ | _ _ _ _ _  uclamp_max
	 *   |      C=x        |   |      |   |
	 *   |      ___        |   |      |   |
	 *   |     |   |       |   |      |   |    (util somewhere in this region)
	 *   |     |   |       |   |      |   |
	 *   |     |   |       |   |      |   |
	 *   +----------------------------------------
	 *         cpu0        cpu1       cpu2
	 *
	 *   In the above example if a task is capped to a specific performance
	 *   point, y, then when:
	 *
	 *   * util = 80% of x then it does not fit on cpu0 and should migrate
	 *     to cpu1
	 *   * util = 80% of y then it is forced to fit on cpu1 to honour
	 *     uclamp_max request.
	 *
	 *   which is what we're enforcing here. A task always fits if
	 *   uclamp_max <= capacity_orig. But when uclamp_max > capacity_orig,
	 *   the normal upmigration rules should withhold still.
	 *
	 *   Only exception is when we are on max capacity, then we need to be
	 *   careful not to block overutilized state. This is so because:
	 *
	 *     1. There's no concept of capping at max_capacity! We can't go
	 *        beyond this performance level anyway.
	 *     2. The system is being saturated when we're operating near
	 *        max capacity, it doesn't make sense to block overutilized.
	 */
	uclamp_max_fits = (capacity_orig == SCHED_CAPACITY_SCALE) && (uclamp_max == SCHED_CAPACITY_SCALE);
	uclamp_max_fits = !uclamp_max_fits && (uclamp_max <= capacity_orig);
	fits = fits || uclamp_max_fits;

	/*
	 *
	 *                                 C=z
	 *   |                             ___       (region a, capped, util >= uclamp_max)
	 *   |                  C=y       |   |
	 *   |_ _ _ _ _ _ _ _ _ ___ _ _ _ | _ | _ _ _ _ _ uclamp_max
	 *   |      C=x        |   |      |   |
	 *   |      ___        |   |      |   |      (region b, uclamp_min <= util <= uclamp_max)
	 *   |_ _ _|_ _|_ _ _ _| _ | _ _ _| _ | _ _ _ _ _ uclamp_min
	 *   |     |   |       |   |      |   |
	 *   |     |   |       |   |      |   |      (region c, boosted, util < uclamp_min)
	 *   +----------------------------------------
	 *         cpu0        cpu1       cpu2
	 *
	 * a) If util > uclamp_max, then we're capped, we don't care about
	 *    actual fitness value here. We only care if uclamp_max fits
	 *    capacity without taking margin/pressure into account.
	 *    See comment above.
	 *
	 * b) If uclamp_min <= util <= uclamp_max, then the normal
	 *    fits_capacity() rules apply. Except we need to ensure that we
	 *    enforce we remain within uclamp_max, see comment above.
	 *
	 * c) If util < uclamp_min, then we are boosted. Same as (b) but we
	 *    need to take into account the boosted value fits the CPU without
	 *    taking margin/pressure into account.
	 *
	 * Cases (a) and (b) are handled in the 'fits' variable already. We
	 * just need to consider an extra check for case (c) after ensuring we
	 * handle the case uclamp_min > uclamp_max.
	 */
	uclamp_min = min(uclamp_min, uclamp_max);
	if (util < uclamp_min && capacity_orig != SCHED_CAPACITY_SCALE)
		fits = fits && (uclamp_min <= capacity_orig_thermal);

	return fits;
}

static inline unsigned long
uclamp_eff_value_pixel_mod(struct task_struct *p, enum uclamp_id clamp_id)
{
	struct uclamp_se uc_max = uclamp_default[clamp_id];
	struct uclamp_se uc_eff;
	int ret;

	/* Task currently refcounted: use back-annotated (effective) value */
	if (p->uclamp[clamp_id].active)
		return (unsigned long)p->uclamp[clamp_id].value;

	// This function will always return uc_eff
	rvh_uclamp_eff_get_pixel_mod(NULL, p, clamp_id, &uc_max, &uc_eff, &ret);

	return (unsigned long)uc_eff.value;
}

/*****************************************************************************/
/*                       New Code Section                                    */
/*****************************************************************************/
/*
 * This part of code is new for this kernel, which are mostly helper functions.
 */
#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
static inline struct vendor_task_group_struct *get_vendor_task_group_struct(struct task_group *tg)
{
	return (struct vendor_task_group_struct *)tg->android_vendor_data1;
}
#endif

struct vendor_rq_struct {
	raw_spinlock_t lock;
	unsigned long util_removed;
	atomic_t num_adpf_tasks;
};

ANDROID_VENDOR_CHECK_SIZE_ALIGN(u64 android_vendor_data1[96], struct vendor_rq_struct t);

static inline struct vendor_rq_struct *get_vendor_rq_struct(struct rq *rq)
{
	return (struct vendor_rq_struct *)rq->android_vendor_data1;
}

static inline bool get_uclamp_fork_reset(struct task_struct *p, bool inherited)
{

	if (inherited)
		return get_vendor_task_struct(p)->uclamp_fork_reset ||
			get_vendor_binder_task_struct(p)->uclamp_fork_reset;
	else
		return get_vendor_task_struct(p)->uclamp_fork_reset;
}

static inline bool get_prefer_idle(struct task_struct *p)
{
	// For group based prefer_idle vote, filter our smaller or low prio or
	// have throttled uclamp.max settings
	// Ignore all checks, if the prefer_idle is from per-task API.

	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	struct vendor_binder_task_struct *vbinder = get_vendor_binder_task_struct(p);

	if (get_uclamp_fork_reset(p, true) || vp->prefer_idle || vbinder->prefer_idle)
		return true;
	else if (vendor_sched_reduce_prefer_idle)
		return vg[vp->group].prefer_idle && p->prio <= DEFAULT_PRIO &&
			uclamp_eff_value_pixel_mod(p, UCLAMP_MAX) == SCHED_CAPACITY_SCALE;
	else
		return vg[vp->group].prefer_idle;
}

static inline void init_vendor_task_struct(struct vendor_task_struct *v_tsk)
{
	/* Guarantee everything is not random first, just in case */
	memset(v_tsk, 0, sizeof(struct vendor_task_struct));

	/* Then explicitly set what we expect init value to be */
	raw_spin_lock_init(&v_tsk->lock);
	v_tsk->group = VG_SYSTEM;
	v_tsk->direct_reclaim_ts = 0;
	INIT_LIST_HEAD(&v_tsk->node);
	v_tsk->queued_to_list = LIST_NOT_QUEUED;
	v_tsk->uclamp_fork_reset = false;
	v_tsk->prefer_idle = false;
	v_tsk->auto_uclamp_max_flags = 0;
	v_tsk->uclamp_filter.uclamp_min_ignored = 0;
	v_tsk->uclamp_filter.uclamp_max_ignored = 0;
	v_tsk->binder_task.prefer_idle = false;
	v_tsk->binder_task.active = false;
	v_tsk->binder_task.uclamp_fork_reset = false;
	v_tsk->uclamp_pi[UCLAMP_MIN] = uclamp_none(UCLAMP_MIN);
	v_tsk->uclamp_pi[UCLAMP_MAX] = uclamp_none(UCLAMP_MAX);
	v_tsk->runnable_start_ns = -1;
}

int acpu_init(void);
extern struct proc_dir_entry *vendor_sched;

extern u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se);
extern unsigned int sysctl_sched_uclamp_min_filter_us;
extern unsigned int sysctl_sched_uclamp_max_filter_divider;
extern unsigned int sysctl_sched_uclamp_min_filter_rt;
extern unsigned int sysctl_sched_uclamp_max_filter_rt;

/*
 * Check if we can ignore uclamp_min requirement of a task. The goal is to
 * prevent small transient tasks from boosting frequency unnecessarily.
 *
 * Returns true if a task can finish its work within a specific threshold.
 *
 * We look at the immediate history of how long the task ran previously.
 * Converting task util_avg into runtime is not trivial and expensive
 * operations.
 */
static inline bool uclamp_can_ignore_uclamp_min(struct rq *rq,
						struct task_struct *p)
{
	struct cpufreq_policy *policy;
	struct sched_entity *se;
	unsigned long runtime;

	if (SCHED_WARN_ON(!uclamp_is_used()))
		return false;

	if (!static_branch_likely(&uclamp_min_filter_enable))
		return false;

	if (task_on_rq_migrating(p))
		return false;

	if (get_uclamp_fork_reset(p, true))
		return false;

	if (p->in_iowait && uclamp_boosted(p))
		return false;

	if (rt_task(p))
		return task_util(p) < sysctl_sched_uclamp_min_filter_rt;

	/*
	 * Based on previous runtime, we check that runtime is sufficiently
	 * larger than a threshold
	 *
	 *
	 *	runtime >= sysctl_sched_uclamp_min_filter_us
	 *
	 * There are 2 caveats:
	 *
	 * 1- When a task migrates on big.LITTLE system, the runtime will not
	 *    be representative then. But this would be one time off error.
	 *
	 * 2. runtime is not frequency invariant. See comment in
	 *    uclamp_can_ignore_uclamp_max()
	 *
	 */
	se = &p->se;
	runtime = se->sum_exec_runtime - se->prev_sum_exec_runtime;
	if (!runtime)
		return false;

	/*
	 * XXX: This can explode if the governor changes in the wrong moment.
	 * We need to create per cpu variables and access those instead. This
	 * will be addressed in the future.
	 */
	policy = cpufreq_cpu_get_raw(cpu_of(rq));
	if (!policy)
		return false;

	if (runtime >= sysctl_sched_uclamp_min_filter_us * 1000)
		return false;

	return true;
}

/*
 * Check if we can ignore uclamp_max requirement of a task. The goal is to
 * prevent small transient tasks that share the rq with other tasks that are
 * capped to lift the capping easily/unnecessarily, hence increase power
 * consumption.
 *
 * Returns true if a task can finish its work within a sched_slice() / divider.
 *
 * We look at the immediate history of how long the task ran previously.
 * Converting task util_avg into runtime or sched_slice() into capacity is not
 * trivial and is an expensive operations. In practice this simple approach
 * proved effective to address the common source of noise. If a task suddenly
 * becomes a busy task, we should detect that and lift the capping at tick, see
 * task_tick_uclamp().
 */
static inline bool uclamp_can_ignore_uclamp_max(struct rq *rq,
						struct task_struct *p)
{
	unsigned long uclamp_max, util;
	unsigned long runtime, slice;
	struct sched_entity *se;
	struct cfs_rq *cfs_rq;
	bool is_rt = rt_task(p);

	if (SCHED_WARN_ON(!uclamp_is_used()))
		return false;

	if (!static_branch_likely(&uclamp_max_filter_enable))
		return false;

	if (task_on_rq_migrating(p))
		return false;

	if (get_uclamp_fork_reset(p, true))
		return false;

	if (p->in_iowait && uclamp_boosted(p))
		return false;

	/*
	 * If util has crossed uclamp_max threshold, then we have to ensure
	 * this is always enforced.
	 */
	util = is_rt ? task_util(p) : task_util_est(p);
	uclamp_max = uclamp_eff_value_pixel_mod(p, UCLAMP_MAX);
	if (util >= uclamp_max)
		return false;

	if (is_rt)
		return util < sysctl_sched_uclamp_max_filter_rt;

	/*
	 * Based on previous runtime, we check the allowed sched_slice() of the
	 * task is large enough for this task to run without preemption.
	 *
	 *
	 *	runtime < sched_slice() / divider
	 *
	 * ==>
	 *
	 *	runtime * divider < sched_slice()
	 *
	 * There are 2 caveats:
	 *
	 * 1- When a task migrates on big.LITTLE system, the runtime will not
	 *    be representative then (not capacity invariant). But this would
	 *    be one time off error.
	 *
	 * 2. runtime is not frequency invariant either. If the
	 *    divider >= fmax/fmin we should be okay in general because that's
	 *    the worst case scenario of how much the runtime will be stretched
	 *    due to it being capped to minimum frequency but the rq should run
	 *    at max. The rule here is that the task should finish its work
	 *    within its sched_slice(). Without this runtime scaling there's a
	 *    small opportunity for the task to ping-pong between capped and
	 *    uncapped state.
	 *
	 */
	se = &p->se;

	runtime = se->sum_exec_runtime - se->prev_sum_exec_runtime;
	if (!runtime)
		return false;

	cfs_rq = cfs_rq_of(se);
	slice = sched_slice(cfs_rq, se);
	runtime *= sysctl_sched_uclamp_max_filter_divider;

	if (runtime >= slice)
		return false;

	return true;
}

static inline void uclamp_set_ignore_uclamp_min(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_min_ignored = 1;
}
static inline void uclamp_reset_ignore_uclamp_min(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_min_ignored = 0;
}
static inline void uclamp_set_ignore_uclamp_max(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_max_ignored = 1;
}
static inline void uclamp_reset_ignore_uclamp_max(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_max_ignored = 0;
}

static inline bool uclamp_is_ignore_uclamp_min(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	return vp->uclamp_filter.uclamp_min_ignored;
}
static inline bool uclamp_is_ignore_uclamp_max(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	return vp->uclamp_filter.uclamp_max_ignored;
}

static inline bool apply_uclamp_filters(struct rq *rq, struct task_struct *p)
{
	bool auto_uclamp_max = get_vendor_task_struct(p)->auto_uclamp_max_flags;
	unsigned long rq_uclamp_min = rq->uclamp[UCLAMP_MIN].value;
	unsigned long rq_uclamp_max = rq->uclamp[UCLAMP_MAX].value;
	bool force_cpufreq_update;

	if (auto_uclamp_max) {
		/* GKI has incremented it already, undo that */
		uclamp_rq_dec_id(rq, p, UCLAMP_MAX);
		/* update uclamp_max if set to auto */
		uclamp_se_set(&p->uclamp_req[UCLAMP_MAX],
			      sched_auto_uclamp_max[task_cpu(p)], true);
		/*
		 * re-apply uclamp_max applying the potentially new
		 * auto value
		 */
		uclamp_rq_inc_id(rq, p, UCLAMP_MAX);

		/* Reset clamp idle holding when there is one RUNNABLE task */
		if (rq->uclamp_flags & UCLAMP_FLAG_IDLE)
			rq->uclamp_flags &= ~UCLAMP_FLAG_IDLE;
	}

	/*
	 * We can't ignore uclamp_min or uclamp_max individually without side
	 * effects due to the way UCLAMP_FLAG_IDLE Is handled. It'll cause
	 * confusions and spit out warnings due to imbalances.
	 *
	 * If one of them needs to be ignored, then we assume the other must be
	 * ignored too.
	 *
	 * This should keep some implicit assumptions about how these values
	 * are inc/dec and how the flag is handled correct.
	 */
	if (uclamp_can_ignore_uclamp_min(rq, p) ||
	    uclamp_can_ignore_uclamp_max(rq, p)) {

		uclamp_set_ignore_uclamp_min(p);
		uclamp_set_ignore_uclamp_max(p);

		/* GKI has incremented it already, undo that */
		uclamp_rq_dec_id(rq, p, UCLAMP_MIN);
		uclamp_rq_dec_id(rq, p, UCLAMP_MAX);
	}

	/*
	 * Force cpufreq update if we filtered and the new rq eff value is
	 * smaller than it was at func entry.
	 */
	force_cpufreq_update = rq_uclamp_min > rq->uclamp[UCLAMP_MIN].value;
	force_cpufreq_update |= rq_uclamp_max > rq->uclamp[UCLAMP_MAX].value;

	return force_cpufreq_update;
}

static inline void inc_adpf_counter(struct task_struct *p, struct rq *rq)
{
	struct vendor_rq_struct *vrq;

	if (rt_task(p))
		return;

	vrq = get_vendor_rq_struct(rq);

	atomic_inc(&vrq->num_adpf_tasks);
	/*
	 * Tell the scheduler that this tasks really wants to run next
	 */
	set_next_buddy(&p->se);
}

static inline void dec_adpf_counter(struct task_struct *p, struct rq *rq)
{
	struct vendor_rq_struct *vrq = get_vendor_rq_struct(rq);

	if (rt_task(p))
		return;

	vrq = get_vendor_rq_struct(rq);

	atomic_dec(&vrq->num_adpf_tasks);
}
