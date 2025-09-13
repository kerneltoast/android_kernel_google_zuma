// SPDX-License-Identifier: GPL-2.0-only
/*
 * The inter-IP fence.
 *
 * The actual functionality (waiting and signaling) won't be done by the kernel driver. The main
 * role of it is creating fences with assigning fence IDs, initializing the fence table and managing
 * the life cycle of them.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#include <linux/atomic.h>
#include <linux/container_of.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/lockdep.h>
#include <linux/lockdep_types.h>
#include <linux/sched.h>
#include <linux/sort.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <iif/iif-fence-table.h>
#include <iif/iif-fence.h>
#include <iif/iif-manager.h>
#include <iif/iif-shared.h>
#include <iif/iif-sync-file.h>
#include <iif/iif.h>

/*
 * A callback instance which will be created when `iif_fence_wait_timeout()` is called and
 * registered to a fence as a poll callback.
 */
struct iif_fence_wait_cb {
	struct iif_fence_poll_cb base;
	struct task_struct *task;
};

/* Returns true if @fence is a direct fence. */
static bool iif_fence_is_direct(struct iif_fence *fence)
{
	return fence->params.flags & IIF_FLAGS_DIRECT;
}

/*
 * Returns true if @fence is unblocked.
 *
 * If the fence is a single-shot fence, it will return true if @fence->signaled is true.
 *
 * If the fence is a reusable fence, it will return true if @fence->signal_error is set or
 * @fence->timeline is bigger than @timeline. The caller can pass @fence->timeline to @timeline
 * to let the function consider @fence->signal_error only.
 */
static bool iif_fence_is_unblocked_locked(struct iif_fence *fence, u64 timeline)
{
	lockdep_assert_held(&fence->fence_lock);

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		return fence->signaled;

	if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE) {
		if (fence->signal_error)
			return true;

		if (fence->timeline > timeline)
			return true;
	}

	return false;
}

static inline int iif_fence_ops_fence_create(struct iif_fence *fence,
					     const struct iif_fence_params *params)
{
	return fence->fence_ops->fence_create(fence, params, fence->driver_data);
}

static inline void iif_fence_ops_fence_retire(struct iif_fence *fence)
{
	fence->fence_ops->fence_retire(fence, fence->driver_data);
}

static inline int iif_fence_ops_fence_add_sync_point(struct iif_fence *fence, u64 timeline,
						     u64 count)
{
	if (fence->fence_ops->fence_add_sync_point)
		return fence->fence_ops->fence_add_sync_point(fence, timeline, count,
							      fence->driver_data);
	return 0;
}

static inline int iif_fence_ops_fence_signal(struct iif_fence *fence, int status)
{
	return fence->fence_ops->fence_signal(fence, status, fence->driver_data);
}

static inline int iif_fence_ops_add_poll_cb(struct iif_fence *fence)
{
	return fence->fence_ops->add_poll_cb(fence, &fence->sync_unit_poll_cb, fence->driver_data);
}

static inline bool iif_fence_ops_remove_poll_cb(struct iif_fence *fence)
{
	return fence->fence_ops->remove_poll_cb(fence, &fence->sync_unit_poll_cb,
						fence->driver_data);
}

/* A compare function to sort fences by their ID. */
static int compare_iif_fence_by_id(const void *lhs, const void *rhs)
{
	const struct iif_fence *lfence = *(const struct iif_fence **)lhs;
	const struct iif_fence *rfence = *(const struct iif_fence **)rhs;

	if (lfence->id < rfence->id)
		return -1;
	if (lfence->id > rfence->id)
		return 1;
	return 0;
}

/*
 * Sorts fences by their ID.
 *
 * If developers are going to hold locks of multiple fences at the same time, they should sort them
 * using this function to prevent a potential deadlock.
 *
 * Returns 0 if there are no repeating fences. Otherwise, returns -EDEADLK.
 */
static inline int iif_fences_sort_by_id(struct iif_fence **fences, int size)
{
	int i;

	sort(fences, size, sizeof(*fences), &compare_iif_fence_by_id, NULL);

	for (i = 1; i < size; i++) {
		if (fences[i - 1]->id == fences[i]->id) {
			iif_err(fences[i], "Duplicated fences in the fence array\n");
			return -EDEADLK;
		}
	}

	return 0;
}

/*
 * Checks whether all fences in @in_fences and @out_fences are unique.
 *
 * This check is required before submitting signalers or waiters to the multiple fences of one
 * command since if there are fences existing in both @in_fences and @out_fences, it will cause a
 * deadlock.
 *
 * Both fence arrays should be sorted first using the `iif_fences_sort_by_id` function above.
 *
 * Returns 0 if there is no cycle. Otherwise, returns -EDEADLK.
 */
static inline int iif_fences_check_fence_uniqueness(struct iif_fence **in_fences, int num_in_fences,
						    struct iif_fence **out_fences,
						    int num_out_fences)
{
	int i = 0, j = 0;

	while (i < num_in_fences && j < num_out_fences) {
		if (in_fences[i]->id < out_fences[j]->id) {
			i++;
		} else if (in_fences[i]->id > out_fences[j]->id) {
			j++;
		} else {
			iif_err(in_fences[i], "Duplicated fences in in-fences and out-fences\n");
			return -EDEADLK;
		}
	}

	return 0;
}

/*
 * Holds the rwlocks which protect the number of signalers of each fence in @fences without saving
 * the IRQ state.
 *
 * To prevent a deadlock, the caller should sort @fences using the `iif_fences_sort_by_id` function
 * first.
 *
 * The caller must use the `iif_fences_write_unlock` function to release the locks.
 */
static void iif_fences_write_lock(struct iif_fence **fences, int num_fences, unsigned long *flags)
{
	int i;

	if (!fences || !num_fences)
		return;

	for (i = 0; i < num_fences; i++)
		write_lock_irqsave(&fences[i]->fence_lock, flags[i]);
}

/*
 * Releases the rwlocks held by the `iif_fences_write_lock` function without restoring the IRQ
 * state.
 */
static void iif_fences_write_unlock(struct iif_fence **fences, int num_fences, unsigned long *flags)
{
	int i;

	if (!fences || !num_fences)
		return;

	for (i = num_fences - 1; i >= 0; i--)
		write_unlock_irqrestore(&fences[i]->fence_lock, flags[i]);
}

/*
 * Returns the number of remaining signalers to be submitted. Returns 0 if all signalers are
 * submitted.
 */
static int iif_fence_unsubmitted_signalers_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->params.remaining_signalers - fence->submitted_signalers;
}

/*
 * Returns the number of outstanding signalers which have submitted signaler commands, but haven't
 * signaled @fence yet.
 */
static int iif_fence_outstanding_signalers_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->submitted_signalers - fence->signaled_signalers;
}

/* Checks whether all signalers have signaled @fence or not. */
static bool iif_fence_all_signalers_signaled_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->signaled_signalers == fence->params.remaining_signalers;
}

/* Checks whether @fence is already retired or not. */
static inline bool iif_fence_has_retired_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->state == IIF_FENCE_STATE_RETIRED;
}

/* Prints a warning log when @fence is retiring when there are remaining outstand waiters. */
static void iif_fence_retire_print_outstanding_waiters_warning(struct iif_fence *fence)
{
	char waiters[64] = { 0 };
	int i = 0, written = 0, tmp;
	enum iif_ip_type waiter;

	for_each_waiting_ip(&fence->mgr->fence_table, fence->id, waiter, tmp) {
		written += scnprintf(waiters + written, sizeof(waiters) - written, "%.*s%d", i, " ",
				     waiter);
		i++;
	}

	iif_warn(
		fence,
		"Fence is retiring when outstanding waiters > 0, it's likely a bug of the waiter IP driver, waiter_ips=[%s]\n",
		waiters);
}

/* Returns the fence ID to the ID pool. */
static void iif_fence_retire_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	if (iif_fence_has_retired_locked(fence))
		return;

	/*
	 * If the waiter IP driver calls `iif_fence_put()` before it waits on waiter commands and
	 * calls `iif_fence_waiter_completed()` somehow, the case that @fence retires while it is
	 * destroying can happen.
	 *
	 * In this case, there is a potential bug that the IP accesses @fence which is already
	 * retired. The waiter IP driver should ensure that calling `iif_fence_waiter_completed()`
	 * first and then `iif_fence_put()` in any case to guarantee that water IPs are not
	 * referring @fence anymore.
	 */
	if (fence->outstanding_waiters)
		iif_fence_retire_print_outstanding_waiters_warning(fence);

	/* Removes the fence from the ID to fence object hash table. */
	iif_manager_remove_fence_from_hlist(fence->mgr, fence);

	/* Removes the poll callback from the sync-unit before retire the fence. */
	iif_fence_ops_remove_poll_cb(fence);

	/* Asks the sync-unit to retire the fence. */
	iif_fence_ops_fence_retire(fence);

	fence->state = IIF_FENCE_STATE_RETIRED;
}

/*
 * If there are no more outstanding waiters and no file binding to this fence, we can assume that
 * there will be no more signalers/waiters. Therefore, we can retire the fence ID earlier to not
 * block allocating an another fence.
 */
static void iif_fence_retire_if_possible_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	if (!(fence->params.flags & IIF_FLAGS_RETIRE_ON_RELEASE) && !fence->outstanding_waiters &&
	    !iif_fence_outstanding_signalers_locked(fence) && !atomic_read(&fence->num_sync_file))
		iif_fence_retire_locked(fence);
}

/*
 * Increases the number of signaled signalers of @fence.
 *
 * If @complete is true, it will forcefully set the number of signaled signalers to the total.
 * This must be used only when @fence is going to be released before it is signaled and let the
 * fence can retire.
 *
 * The function returns the number of remaining signalers which will signal the fence. If the
 * function returns a positive number, it means that there are (or will be) more signalers which
 * can signal the fence.
 */
static int iif_fence_inc_signaled_signalers_locked(struct iif_fence *fence, bool complete)
{
	int ret;

	lockdep_assert_held(&fence->fence_lock);

	if (iif_fence_all_signalers_signaled_locked(fence))
		return 0;

	if (!complete)
		fence->signaled_signalers++;
	else
		fence->signaled_signalers = fence->params.remaining_signalers;

	ret = fence->params.remaining_signalers - fence->signaled_signalers;

	/*
	 * Normally @fence won't be retired here and it will be retired when there are no more
	 * outstanding waiters and all file descriptors linked to @fence are closed. However, if
	 * somehow all runtime and waiter IPs are crashed at the same time (or even the signaler IP
	 * is also crashed), the fence can be retired at this moment.
	 */
	iif_fence_retire_if_possible_locked(fence);

	return ret;
}

/*
 * Increases the number of outstanding waiters of @waiter_ip.
 *
 * The caller must hold the block wakelock of the IP before calling this.
 */
static int iif_fence_inc_outstanding_waiters_locked(struct iif_fence *fence,
						    enum iif_ip_type waiter_ip)
{
	lockdep_assert_held(&fence->fence_lock);

	if (iif_fence_has_retired_locked(fence))
		return -EPERM;

	fence->outstanding_waiters++;
	fence->outstanding_waiters_per_ip[waiter_ip]++;

	return 0;
}

/* Adds @waiter_ip to the waiters list and increases the number of outstanding waiters. */
static int iif_fence_add_waiter_locked(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	struct iif_fence_params params;
	int ret;

	lockdep_assert_held(&fence->fence_lock);

	ret = iif_fence_inc_outstanding_waiters_locked(fence, waiter_ip);
	if (ret)
		return ret;

	/* If the waiter is already set, we don't need to update params. */
	if (fence->params.waiters & BIT(waiter_ip))
		return 0;

	/* Fills @params out with new waiters. */
	params = fence->params;
	params.waiters |= BIT(waiter_ip);

	/* TODO(b/398979516): Propagates updates waiters to the underlying sync-unit. */
	iif_fence_table_set_waiting_ip(&fence->mgr->fence_table, fence->id, waiter_ip);

	/* Updates @params of the kernel fence object. */
	fence->params = params;

	return 0;
}

/* Decreases the number of outstanding waiters of @waiter_ip. */
static void iif_fence_remove_waiter_locked(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	lockdep_assert_held(&fence->fence_lock);

	WARN_ON(waiter_ip >= IIF_IP_NUM);
	WARN_ON(!fence->outstanding_waiters || !fence->outstanding_waiters_per_ip[waiter_ip]);

	fence->outstanding_waiters--;
	fence->outstanding_waiters_per_ip[waiter_ip]--;
	iif_fence_retire_if_possible_locked(fence);
}

/* Decreases the number of outstanding waiters of @waiter_ip. */
static void iif_fence_remove_waiter(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);
	iif_fence_remove_waiter_locked(fence, waiter_ip);
	write_unlock_irqrestore(&fence->fence_lock, flags);
}

static int iif_fence_add_sync_point_locked(struct iif_fence *fence, u64 timeline, u64 count)
{
	lockdep_assert_held(&fence->fence_lock);

	/*
	 * TODO(b/389607552): To support sync-unit which doesn't support adding sync point, the
	 * registered sync points should be also managed by IIF driver. Locking @fence_lock will
	 * be required once we implement that.
	 *
	 * For now, as we support direct fences only, skip that and let the iif-direct handles it.
	 */
	return iif_fence_ops_fence_add_sync_point(fence, timeline, count);
}

/*
 * Increases the number of submitted signalers of @fence.
 *
 * If @complete is true, it will make @fence have finished the signaler submission. This must be
 * used only when @fence is going to be released before the signaler submission is being finished
 * and let the IP driver side notice that there was some problem by triggering registered callbacks.
 *
 * Returns 0 on success. Otherwise, a negative errno.
 */
static int iif_fence_inc_submitted_signalers_locked(struct iif_fence *fence, bool complete)
{
	lockdep_assert_held(&fence->fence_lock);

	/* Already all signalers are submitted. No more submission is allowed. */
	if (fence->submitted_signalers >= fence->params.remaining_signalers)
		return -EPERM;

	if (!complete)
		fence->submitted_signalers++;
	else
		fence->submitted_signalers = fence->params.remaining_signalers;

	return 0;
}

/* Notifies poll callbacks for a single-shot fence. */
static void iif_fence_notify_poll_cb_single_shot_locked(struct iif_fence *fence)
{
	struct iif_fence_poll_cb *cur, *tmp;

	lockdep_assert_held(&fence->fence_lock);

	if (!fence->signaled)
		return;

	list_for_each_entry_safe(cur, tmp, &fence->poll_cb_list, node) {
		list_del_init(&cur->node);
		cur->func(fence, cur);
	}
}

/* Notifies poll callbacks for a reusable fence. */
static void iif_fence_notify_poll_cb_reusable_locked(struct iif_fence *fence)
{
	struct iif_fence_poll_cb *cur, *tmp;

	lockdep_assert_held(&fence->fence_lock);

	/*
	 * TODO(b/389607552): Currently, it relies on the sync point filtering logic of direct
	 * fence. Updates here to be generic for other sync-unit drivers.
	 */

	list_for_each_entry_safe(cur, tmp, &fence->poll_cb_list, node) {
		if (fence->signal_error)
			list_del_init(&cur->node);
		cur->func(fence, cur);
	}
}

/*
 * Sets the status passed from the sync-unit driver to @fence. This function is supposed to be
 * called when the underlying sync-unit driver invokes the poll callback registered by the IIF
 * driver.
 *
 * Returns true if the status has been updated.
 */
static bool iif_fence_set_status_locked(struct iif_fence *fence,
					const struct iif_fence_status *status)
{
	int timeline = status->timeline;
	int error = status->error;
	bool signaled = status->signaled;
	bool updated = false;

	lockdep_assert_held(&fence->fence_lock);

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT) {
		if (unlikely(fence->signaled && !signaled)) {
			iif_warn(fence, "The fence was already signaled, shouldn't revert it\n");
			signaled = fence->signaled;
		}

		if (fence->signaled != signaled) {
			fence->signaled = signaled;
			updated = true;
		}
	} else {
		if (unlikely(fence->timeline > timeline))
			iif_warn(fence, "The fence timeline shouldn't be decreased\n");

		if (fence->timeline < timeline) {
			fence->timeline = timeline;
			updated = true;
		}
	}

	if (unlikely(fence->signal_error && !error)) {
		iif_warn(fence, "The fence was already errored out, shouldn't revert it\n");
		error = fence->signal_error;
	}

	if (unlikely(fence->signal_error && fence->signal_error != error))
		iif_warn(fence, "The fence error has been changed, %d -> %d\n", fence->signal_error,
			 error);

	if (fence->signal_error != error) {
		fence->signal_error = error;

		/*
		 * Single-shot fences should be considered as updated only if @signaled is updated.
		 */
		if (fence->params.fence_type != IIF_FENCE_TYPE_SINGLE_SHOT)
			updated = true;
	}

	return updated;
}

/*
 * Notifies the poll callbacks registered to @fence.
 *
 * This function must be called only if @fence is unblocked so that @fence->fence_lock doesn't have
 * to be held.
 */
static void iif_fence_notify_poll_cb_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	if (unlikely(fence->params.flags & IIF_FLAGS_DISABLE_POLL)) {
		fence->poll_cb_pended = true;
		return;
	}

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		iif_fence_notify_poll_cb_single_shot_locked(fence);
	else if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE)
		iif_fence_notify_poll_cb_reusable_locked(fence);

	fence->poll_cb_pended = false;
}

/*
 * The poll callback which will be registered to direct fences.
 *
 * It is supposed to be called when the signaler IP driver calls `iif_fence_signal()` which holds
 * @iif->fence_lock.
 */
static void iif_fence_direct_poll_cb_func(struct iif_fence *iif,
					  const struct iif_fence_status *status)
{
	lockdep_assert_held(&iif->fence_lock);

	/* If the fence status hasn't been updated, ignore it. */
	if (!iif_fence_set_status_locked(iif, status))
		return;

	/*
	 * Don't need to check the return value because the purpose of `signaled_work` is to invoke
	 * `fence_unblocked()` operators and the IP drivers don't need to get the notification for
	 * every single signal. It is enough to read the latest fence status at the last
	 * `signaler_work` invocation.
	 */
	schedule_work(&iif->signaled_work);

	/* Notifies registered poll callbacks. */
	iif_fence_notify_poll_cb_locked(iif);
}

/* Notifies the all_signaler_submitted callbacks registered to @fence. */
static void iif_fence_notify_all_signaler_submitted_cb_locked(struct iif_fence *fence)
{
	struct iif_fence_all_signaler_submitted_cb *cur, *tmp;

	lockdep_assert_held(&fence->fence_lock);

	/* We should notify waiters only if all signalers have been submitted. */
	if (iif_fence_unsubmitted_signalers_locked(fence))
		return;

	list_for_each_entry_safe(cur, tmp, &fence->all_signaler_submitted_cb_list, node) {
		list_del_init(&cur->node);
		cur->func(fence, cur);
	}
}

/*
 * Signals @fence with @error.
 *
 * If @force is true, it will signal the fence even though there is no outstanding signaler. It is
 * supposed to be used only when @fence is destroying.
 */
static int iif_fence_signal_with_status_locked(struct iif_fence *fence, int error, bool force)
{
	lockdep_assert_held(&fence->fence_lock);

	if (iif_fence_all_signalers_signaled_locked(fence)) {
		iif_err(fence, "The fence is already signaled by all signalers\n");
		return -EBUSY;
	}

	if (!force && !iif_fence_outstanding_signalers_locked(fence)) {
		iif_err(fence, "There is no outstanding signalers\n");
		return -EPERM;
	}

	/*
	 * If @fence->propagate is not set, ignore it.
	 *
	 * For the backward compatibility, if the fence is a direct fence, we should ask the
	 * iif-direct to set @error as there is no sync-unit even if @fence->propagate is false.
	 */
	if (!fence->propagate && !iif_fence_is_direct(fence))
		return 0;

	return iif_fence_ops_fence_signal(fence, error);
}

/*
 * Increases the number of signaled signalers of @fence and schedules @waited_work if all signalers
 * have signaled the fence.
 *
 * Returns the number of remaining signalers which can signal the fence.
 */
static int
iif_fence_do_waited_work_if_no_outstanding_signalers(struct iif_fence *fence,
						     void (*waited_work)(struct iif_fence *))
{
	unsigned long flags;
	int outstanding_signalers = 0;
	int ret;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (iif_fence_all_signalers_signaled_locked(fence)) {
		ret = -EBUSY;
		goto out;
	}

	if (!iif_fence_outstanding_signalers_locked(fence)) {
		ret = -EPERM;
		goto out;
	}

	/*
	 * Increases the number of signaled signalers and retires the fence is possible.
	 * The returned value should be the number of remaining signalers.
	 */
	ret = iif_fence_inc_signaled_signalers_locked(fence, false);
	outstanding_signalers = iif_fence_outstanding_signalers_locked(fence);
out:
	write_unlock_irqrestore(&fence->fence_lock, flags);

	/* If there are no outstanding signalers, execute @waited_work. */
	if (ret >= 0 && !outstanding_signalers)
		waited_work(fence);

	return ret;
}

/* The fence signaling function which supports the backward compatiblilty with a direct fence. */
static int iif_fence_signal_and_update(struct iif_fence *fence, int error,
				       void (*waited_work)(struct iif_fence *))
{
	unsigned long flags;
	int ret;

	write_lock_irqsave(&fence->fence_lock, flags);
	ret = iif_fence_signal_with_status_locked(fence, error, false);
	write_unlock_irqrestore(&fence->fence_lock, flags);

	/*
	 * This is for the backward compatibility for a direct fence. For non-direct fences, the
	 * fence status is supposed to be updated when the underlying sync unit notifies the IIF
	 * driver of the fence signal.
	 */
	if (!ret && iif_fence_is_direct(fence)) {
		/*
		 * If there are no outstanding signalers, execute @waited_work to release block
		 * wakelocks which were pended to be released because of outstanding signalers when
		 * `waited_work()` was called.
		 *
		 * If the fence is reusable, `iif_fence_signaler_completed()` will be called by the
		 * IP driver when a signaler command completes which will execute @waited_work.
		 */
		if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
			ret = iif_fence_do_waited_work_if_no_outstanding_signalers(fence,
										   waited_work);
	}

	return ret;
}

/*
 * Acquires the block wakelock of @ip.
 *
 * This function can be called in the normal context only.
 */
static inline int iif_fence_acquire_block_wakelock(struct iif_fence *fence, enum iif_ip_type ip)
{
	return iif_manager_acquire_block_wakelock(fence->mgr, ip);
}

/*
 * Releases the block wakelock of @ip for @locks times.
 *
 * This function can be called in the normal context only.
 */
static inline void iif_fence_release_block_wakelock(struct iif_fence *fence, enum iif_ip_type ip,
						    int locks)
{
	while (locks > 0 && locks--)
		iif_manager_release_block_wakelock(fence->mgr, ip);
}

/* Releases all block wakelock which hasn't been released yet. */
static void iif_fence_release_all_block_wakelock(struct iif_fence *fence)
{
	int i;
	uint16_t locks[IIF_IP_RESERVED] = { 0 };
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);

	for (i = 0; i < IIF_IP_RESERVED; i++) {
		locks[i] = fence->outstanding_block_wakelock[i];
		fence->outstanding_block_wakelock[i] = 0;
	}

	write_unlock_irqrestore(&fence->fence_lock, flags);

	for (i = 0; i < IIF_IP_RESERVED; i++) {
		while (locks[i]) {
			iif_manager_release_block_wakelock(fence->mgr, i);
			locks[i]--;
		}
	}
}

/* Cleans up @fence which was initialized by the `iif_fence_init` function. */
static void iif_fence_do_destroy(struct iif_fence *fence)
{
	struct iif_fence_status status;
	unsigned long flags;
	bool updated = false;

	/*
	 * If the IP driver puts @fence asynchronously, the works might be not finished. We should
	 * wait for them.
	 */
	flush_work(&fence->signaled_work);
	flush_work(&fence->waited_work);

	/* Checks whether there is remaining all_signaler_submitted and poll callbacks. */
	write_lock_irqsave(&fence->fence_lock, flags);

	if (!list_empty(&fence->all_signaler_submitted_cb_list) &&
	    fence->submitted_signalers < fence->params.remaining_signalers) {
		fence->all_signaler_submitted_error = -EDEADLK;
		iif_fence_inc_submitted_signalers_locked(fence, true);
		iif_fence_notify_all_signaler_submitted_cb_locked(fence);
	}

	if (!iif_fence_all_signalers_signaled_locked(fence)) {
		/*
		 * This case can happen when:
		 * - The signaler runtime just didn't submit enough signaler commands or it
		 *   becomes unavailable to submit commands in the middle (e.g., IP crashes).
		 * - The signaler IP kernel driver didn't call `iif_fence_signaler_completed()`
		 *   before calling `iif_fence_put()` somehow.
		 */
		iif_warn(
			fence,
			"Fence is destroying before signaled, likely a bug of the signaler, signaler_ip=%d\n",
			fence->params.signaler_ip);

		/*
		 * Theoretically, the meaning of this destroy() function has been called is that the
		 * signaler IP kernel driver has cleaned up (canceled) all signaler commands and the
		 * signaler IP won't signal the fence anymore. Therefore, it is safe to signal the
		 * fence by the IIF kernel driver.
		 */
		iif_fence_set_propagate_unblock(fence);

		/*
		 * Errors the fence out forcefully.
		 *
		 * The meaning of reaching this if-branch is that the fence is going to destroy
		 * even though not all signalers have signaled the fence which means the fence would
		 * not be signaled enoughly and it should be errored out. If the fence actually
		 * needs to be errored out, @updated will be set to true.
		 *
		 * Normally, the fence had to retire before it destroys. Therefore, we don't need to
		 * propagate the error to the underlying sync-unit fence and can error the kernel
		 * fence object out forcefully to propagate the error to the ones polling on the
		 * fence via invoking poll callbacks below.
		 *
		 * However, if the fence is going to destroy without any interaction (no signaler,
		 * waiter was submitted and no FD was installed), this function can be called before
		 * retirement. Even in this case, theoretically, as there should be no waiters, we
		 * don't need to propagate the error to the underlying sync-unit fence and can set
		 * an error to the kernel fence object forcefully. Also, the underlying sync-unit
		 * fence will retire right below and how they will clean the fence up is independent
		 * from this kernel fence object release.
		 */
		if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
			status.signaled = true;
		else
			status.timeline = fence->params.timeout;
		status.error = -EDEADLK;
		updated = iif_fence_set_status_locked(fence, &status);
		iif_fence_inc_signaled_signalers_locked(fence, true);
	}

	/*
	 * It is supposed to be retired when the file is closed and there are no more outstanding
	 * waiters. However, let's ensure that the fence is retired before releasing it.
	 */
	iif_fence_retire_locked(fence);

	/*
	 * It is always safe to call this function.
	 * - If the if-clause above was executed, it means that the fence has been unblocked and it
	 *   is good to call this function.
	 * - If @fence->poll_cb_list was empty, this function call will be NO-OP.
	 * - If `iif_fence_all_signalers_signaled_locked(fence)` was true, it means that the fence
	 *   was already unblocked and it is good to call it. (In this case, all callbacks should be
	 *   called when the fence was unblocked and @fence->poll_cb_list should be already empty.
	 *   It means that the function call will be NO-OP theoretically.)
	 */
	iif_fence_notify_poll_cb_locked(fence);

	write_unlock_irqrestore(&fence->fence_lock, flags);

	if (updated)
		iif_manager_broadcast_fence_unblocked(fence->mgr, fence);

	/*
	 * If @fence is not signaled normally or IP drivers haven't called
	 * `iif_fence_waiter_completed()` with some reasons, there would be block wakelocks which
	 * haven't released yet. We should release all of them.
	 */
	iif_fence_release_all_block_wakelock(fence);

#if IS_ENABLED(CONFIG_DEBUG_SPINLOCK)
	lockdep_unregister_key(&fence->fence_lock_key);
#endif /* IS_ENABLED(CONFIG_DEBUG_SPINLOCK) */

	iif_manager_unset_fence_ops(fence->mgr, fence);
	iif_manager_put(fence->mgr);

	if (fence->ops && fence->ops->on_release)
		fence->ops->on_release(fence);
}

/* Will be called once the refcount of @fence becomes 0 and destroy it. */
static void iif_fence_destroy(struct kref *kref)
{
	struct iif_fence *fence = container_of(kref, struct iif_fence, kref);

	iif_fence_do_destroy(fence);
}

/* Will be called once the refcount of @fence becomes 0 and destroy it asynchronously. */
static void iif_fence_destroy_async(struct kref *kref)
{
	struct iif_fence *fence = container_of(kref, struct iif_fence, kref);

	/* No need to check return value since this destroy should only happen once per fence. */
	schedule_work(&fence->put_work);
}

/* A worker function which will be called when @fence is signaled. */
static void iif_fence_signaled_work_func(struct work_struct *work)
{
	struct iif_fence *fence = container_of(work, struct iif_fence, signaled_work);

	iif_manager_broadcast_fence_unblocked(fence->mgr, fence);
}

static void iif_fence_waited_work_func(struct work_struct *work)
{
	struct iif_fence *fence = container_of(work, struct iif_fence, waited_work);
	uint16_t locks[IIF_IP_RESERVED] = { 0 };
	unsigned long flags;
	int i;

	write_lock_irqsave(&fence->fence_lock, flags);

	/*
	 * Note that if there are outstanding signalers, releasing the block wakelock will be pended
	 * there are no outstanding signalers or the fence destroys.
	 *
	 * For single-shot fences, this case can happen when the signaler IPx is not responding in
	 * time and the waiter IPy processes its command as timeout. For reusable fences, it can
	 * always happen as signalers will keep signaling the fence and waiters will leave in the
	 * middle once the fence timeline reaches the value they are waiting for.
	 *
	 * This pending logic is required because if IPy releases its block wakelock and IPy signals
	 * the fence, IPx may try to notify IPy whose block is already powered down and it may cause
	 * an unexpected bug if IPy spec doesn't allow that.
	 */
	if (iif_fence_outstanding_signalers_locked(fence)) {
		write_unlock_irqrestore(&fence->fence_lock, flags);
		return;
	}

	for (i = 0; i < IIF_IP_RESERVED; i++) {
		if (fence->outstanding_block_wakelock[i] > fence->outstanding_waiters_per_ip[i]) {
			locks[i] = fence->outstanding_block_wakelock[i] -
				   fence->outstanding_waiters_per_ip[i];
			fence->outstanding_block_wakelock[i] = fence->outstanding_waiters_per_ip[i];
		}
	}

	write_unlock_irqrestore(&fence->fence_lock, flags);

	for (i = 0; i < IIF_IP_RESERVED; i++)
		iif_fence_release_block_wakelock(fence, i, locks[i]);
}

static inline void iif_fence_waited_work(struct iif_fence *fence)
{
	iif_fence_waited_work_func(&fence->waited_work);
}

static inline void iif_fence_waited_work_async(struct iif_fence *fence)
{
	/*
	 * Don't need to check the return as it is not required to execute
	 * `iif_fence_waited_work_func()` per `waiter_completed()` call. The function only refers to
	 * the last number of outstanding block wakelocks and waiters.
	 */
	schedule_work(&fence->waited_work);
}

static void iif_fence_put_work_func(struct work_struct *work)
{
	struct iif_fence *fence = container_of(work, struct iif_fence, put_work);

	iif_fence_do_destroy(fence);
}

static void iif_fence_wait_poll(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb)
{
	struct iif_fence_wait_cb *wait_cb = container_of(poll_cb, struct iif_fence_wait_cb, base);

	wake_up_process(wait_cb->task);
}

int iif_fence_init(struct iif_manager *mgr, struct iif_fence *fence,
		   const struct iif_fence_ops *ops, enum iif_ip_type signaler_ip,
		   uint16_t total_signalers)
{
	struct iif_fence_params params;

	params.signaler_type = IIF_FENCE_SIGNALER_TYPE_IP;
	params.fence_type = IIF_FENCE_TYPE_SINGLE_SHOT;
	params.signaler_ip = signaler_ip;
	params.remaining_signalers = total_signalers;
	params.waiters = 0;
	params.timeout = 0;
	params.flags = IIF_FLAGS_DIRECT;

	return iif_fence_init_with_params(mgr, fence, ops, &params);
}

int iif_fence_init_with_params(struct iif_manager *mgr, struct iif_fence *fence,
			       const struct iif_fence_ops *ops,
			       const struct iif_fence_params *params)
{
	int id, ret;

	if (params->signaler_ip >= IIF_IP_NUM)
		return -EINVAL;

	ret = iif_manager_set_fence_ops(mgr, fence, params);
	if (ret)
		return ret;

	id = iif_fence_ops_fence_create(fence, params);
	if (id < 0) {
		iif_manager_unset_fence_ops(mgr, fence);
		return id;
	}

	if (params->flags & IIF_FLAGS_DIRECT) {
		fence->sync_unit_poll_cb.iif = fence;
		fence->sync_unit_poll_cb.func = iif_fence_direct_poll_cb_func;
	} else {
		/* TODO(b/389607552): Support registering poll callbacks to sync-unit drivers. */
		iif_fence_ops_fence_retire(fence);
		iif_manager_unset_fence_ops(mgr, fence);
		return -EOPNOTSUPP;
	}

	ret = iif_fence_ops_add_poll_cb(fence);
	if (ret < 0) {
		iif_fence_ops_fence_retire(fence);
		iif_manager_unset_fence_ops(mgr, fence);
		return ret;
	}

	fence->id = id;
	fence->params = *params;
	fence->mgr = iif_manager_get(mgr);
	fence->signaler_ip = params->signaler_ip;
	fence->submitted_signalers = 0;
	fence->signaled_signalers = 0;
	fence->outstanding_waiters = 0;
	fence->signal_error = 0;
	fence->all_signaler_submitted_error = 0;
	fence->timeline = 0;
	fence->signaled = false;
	fence->ops = ops;
	fence->state = IIF_FENCE_STATE_INITIALIZED;
	fence->propagate = params->signaler_ip == IIF_IP_AP;
	fence->poll_cb_pended = false;
	kref_init(&fence->kref);
#if IS_ENABLED(CONFIG_DEBUG_SPINLOCK)
	lockdep_register_key(&fence->fence_lock_key);
	__rwlock_init(&fence->fence_lock, "&fence->fence_lock", &fence->fence_lock_key);
#else
	rwlock_init(&fence->fence_lock);
#endif /* IS_ENABLED(CONFIG_DEBUG_SPINLOCK) */
	INIT_LIST_HEAD(&fence->poll_cb_list);
	INIT_LIST_HEAD(&fence->all_signaler_submitted_cb_list);
	atomic_set(&fence->num_sync_file, 0);
	INIT_WORK(&fence->signaled_work, &iif_fence_signaled_work_func);
	INIT_WORK(&fence->waited_work, &iif_fence_waited_work_func);
	INIT_WORK(&fence->put_work, &iif_fence_put_work_func);
	memset(fence->outstanding_waiters_per_ip, 0, sizeof(fence->outstanding_waiters_per_ip));
	memset(fence->outstanding_block_wakelock, 0, sizeof(fence->outstanding_block_wakelock));

	/* Adds the fence to the ID to fence object hash table. */
	iif_manager_add_fence_to_hlist(fence->mgr, fence);

	return 0;
}

void iif_fence_set_priv_fence_data(struct iif_fence *iif, void *fence_data)
{
	iif->fence_data = fence_data;
}

void *iif_fence_get_priv_fence_data(struct iif_fence *iif)
{
	return iif->fence_data;
}

int iif_fence_set_flags(struct iif_fence *fence, unsigned long flags, bool clear)
{
	unsigned long irq_flags;
	int ret = 0;

	write_lock_irqsave(&fence->fence_lock, irq_flags);

	/* Changing the direct mode after the fence creation is not allowed. */
	if ((flags & IIF_FLAGS_DIRECT) &&
	    (clear == (bool)(fence->params.flags & IIF_FLAGS_DIRECT))) {
		ret = -EINVAL;
		goto out;
	}

	if (!clear)
		fence->params.flags |= flags;
	else
		fence->params.flags &= ~flags;

	/* Invokes poll callbacks which were pended because of IIF_FLAGS_DISABLE_POLL flag. */
	if (clear && (flags & IIF_FLAGS_DISABLE_POLL) && fence->poll_cb_pended)
		iif_fence_notify_poll_cb_locked(fence);
out:
	write_unlock_irqrestore(&fence->fence_lock, irq_flags);

	return ret;
}

int iif_fence_install_fd(struct iif_fence *fence)
{
	struct iif_sync_file *sync_file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	sync_file = iif_sync_file_create(fence);
	if (IS_ERR(sync_file)) {
		put_unused_fd(fd);
		return PTR_ERR(sync_file);
	}

	fd_install(fd, sync_file->file);

	return fd;
}

void iif_fence_on_sync_file_release(struct iif_fence *fence)
{
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);
	iif_fence_retire_if_possible_locked(fence);
	write_unlock_irqrestore(&fence->fence_lock, flags);
}

struct iif_fence *iif_fence_get(struct iif_fence *fence)
{
	if (fence)
		kref_get(&fence->kref);
	return fence;
}

struct iif_fence *iif_fence_fdget(int fd)
{
	struct iif_sync_file *sync_file;
	struct iif_fence *fence;

	sync_file = iif_sync_file_fdget(fd);
	if (IS_ERR(sync_file))
		return ERR_CAST(sync_file);

	fence = iif_fence_get(sync_file->fence);

	/*
	 * Since `iif_sync_file_fdget` opens the file and increases the file refcount, put here as
	 * we don't need to access the file anymore in this function.
	 */
	fput(sync_file->file);

	return fence;
}

void iif_fence_put(struct iif_fence *fence)
{
	if (fence)
		kref_put(&fence->kref, iif_fence_destroy);
}

void iif_fence_put_async(struct iif_fence *fence)
{
	if (fence)
		kref_put(&fence->kref, iif_fence_destroy_async);
}

int iif_fence_submit_signaler(struct iif_fence *fence)
{
	enum iif_ip_type ip;
	int ret, tmp;
	unsigned long flags;
	u16 wakelock_held = 0;

	might_sleep();

	/*
	 * If there are waiters which were set before sending signaler commands, try to hold the
	 * block wakelock of waiters. As we don't increase the number of outstanding waiters here,
	 * the held block wakelocks will be considered as locks which were pended to be released.
	 * That means they won't be released until there are no more outstanding signalers and
	 * `iif_fence_waited_work_func()` is called which releases all pended block wakelocks.
	 *
	 * We don't need to hold @fence->fence_lock here because:
	 * - Holding block wakelock usually can be done in the normal context only.
	 * - If there is a new waiter which were not informed to the IIF driver before submitting
	 *   signalers, the waiter driver will call `submit_waiter()` before submitting its command
	 *   to its IP and the block wakelock will be held there. Also, the held block wakelock
	 *   won't be released until there are no more outstanding signalers.
	 */
	for_each_ip(fence->params.waiters, ip, tmp) {
		ret = iif_fence_acquire_block_wakelock(fence, ip);
		if (ret)
			break;
		wakelock_held |= BIT(ip);
	}

	if (ret)
		goto release_block_wakelock;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (iif_fence_has_retired_locked(fence)) {
		ret = -EPERM;
		goto unlock_fence_lock;
	}

	ret = iif_fence_inc_submitted_signalers_locked(fence, false);
	if (ret)
		goto unlock_fence_lock;

	/* Notifies the waiters if all signalers have been submitted. */
	iif_fence_notify_all_signaler_submitted_cb_locked(fence);

	/* Increases the number of outstanding block wakelock held above. */
	for_each_ip(wakelock_held, ip, tmp)
		fence->outstanding_block_wakelock[ip]++;

unlock_fence_lock:
	write_unlock_irqrestore(&fence->fence_lock, flags);

release_block_wakelock:
	/* Releases all block wakelocks held already on error. */
	if (ret) {
		for_each_ip(wakelock_held, ip, tmp)
			iif_fence_release_block_wakelock(fence, ip, 1);
	}

	return ret;
}

int iif_fence_submit_waiter(struct iif_fence *fence, enum iif_ip_type ip)
{
	unsigned long flags;
	int unsubmitted = iif_fence_unsubmitted_signalers(fence);
	int ret;

	might_sleep();

	if (ip >= IIF_IP_NUM)
		return -EINVAL;

	if (unsubmitted)
		return unsubmitted;

	ret = iif_fence_acquire_block_wakelock(fence, ip);
	if (ret) {
		iif_err(fence, "Failed to acquire the block wakelock of IP=%d\n", ip);
		return ret;
	}

	write_lock_irqsave(&fence->fence_lock, flags);

	ret = iif_fence_add_waiter_locked(fence, ip);
	if (ret) {
		write_unlock_irqrestore(&fence->fence_lock, flags);
		iif_fence_release_block_wakelock(fence, ip, 1);
		return ret;
	}

	fence->outstanding_block_wakelock[ip]++;

	write_unlock_irqrestore(&fence->fence_lock, flags);

	return ret;
}

int iif_fence_add_sync_point(struct iif_fence *fence, u64 timeline, u64 count)
{
	unsigned long flags;
	int ret;

	if (fence->params.fence_type != IIF_FENCE_TYPE_REUSABLE)
		return -EOPNOTSUPP;

	if (!timeline || !count)
		return -EINVAL;

	write_lock_irqsave(&fence->fence_lock, flags);
	ret = iif_fence_add_sync_point_locked(fence, timeline, count);
	write_unlock_irqrestore(&fence->fence_lock, flags);

	return ret;
}

int iif_fence_submit_signaler_and_waiter(struct iif_fence **in_fences, int num_in_fences,
					 struct iif_fence **out_fences, int num_out_fences,
					 enum iif_ip_type waiter_ip)
{
	enum iif_ip_type ip;
	u16 *wakelock_held;
	unsigned long *flags;
	int i, tmp, ret;

	might_sleep();

	if (waiter_ip >= IIF_IP_NUM)
		return -EINVAL;

	flags = kcalloc(num_in_fences > num_out_fences ? num_in_fences : num_out_fences,
			sizeof(*flags), GFP_KERNEL);
	if (!flags)
		return -ENOMEM;

	wakelock_held = kcalloc(num_out_fences, sizeof(*wakelock_held), GFP_KERNEL);
	if (!wakelock_held) {
		ret = -ENOMEM;
		goto err_free_flags;
	}

	ret = iif_fences_sort_by_id(in_fences, num_in_fences);
	if (ret)
		goto err_free_wakelock_held;

	ret = iif_fences_sort_by_id(out_fences, num_out_fences);
	if (ret)
		goto err_free_wakelock_held;

	ret = iif_fences_check_fence_uniqueness(in_fences, num_in_fences, out_fences,
						num_out_fences);
	if (ret)
		goto err_free_wakelock_held;

	iif_fences_write_lock(in_fences, num_in_fences, flags);

	/*
	 * Checks whether we can submit a waiter to @in_fences.
	 * If there are unsubmitted signalers, the caller should retry submitting waiters later.
	 */
	for (i = 0; in_fences && i < num_in_fences; i++) {
		if (iif_fence_unsubmitted_signalers_locked(in_fences[i])) {
			iif_fences_write_unlock(in_fences, num_in_fences, flags);
			ret = -EAGAIN;
			goto err_free_wakelock_held;
		}

		if (iif_fence_has_retired_locked(in_fences[i])) {
			iif_fences_write_unlock(in_fences, num_in_fences, flags);
			ret = -EPERM;
			goto err_free_wakelock_held;
		}
	}

	/*
	 * We can release the lock of @in_fences because once they are able to submit a waiter, it
	 * means that all signalers have been submitted to @in_fences and the fact won't be changed.
	 * Will submit a waiter to @in_fences if @out_fences are able to submit a signaler.
	 */
	iif_fences_write_unlock(in_fences, num_in_fences, flags);

	/* Holds the block wakelocks of waiters of @out_fences. */
	for (i = 0; out_fences && i < num_out_fences && !ret; i++) {
		for_each_ip(out_fences[i]->params.waiters, ip, tmp) {
			ret = iif_fence_acquire_block_wakelock(out_fences[i], ip);
			if (ret)
				break;
			wakelock_held[i] |= BIT(ip);
		}
	}

	if (ret)
		goto release_block_wakelock;

	iif_fences_write_lock(out_fences, num_out_fences, flags);

	/*
	 * Checks whether we can submit a signaler to @out_fences.
	 * If all signalers are already submitted, submitting signalers is not allowed anymore.
	 */
	for (i = 0; out_fences && i < num_out_fences; i++) {
		if (!iif_fence_unsubmitted_signalers_locked(out_fences[i]) ||
		    iif_fence_has_retired_locked(out_fences[i])) {
			iif_fences_write_unlock(out_fences, num_out_fences, flags);
			ret = -EPERM;
			goto release_block_wakelock;
		}
	}

	/* Submits a signaler to @out_fences. */
	for (i = 0; out_fences && i < num_out_fences; i++) {
		iif_fence_inc_submitted_signalers_locked(out_fences[i], false);

		/* Notifies the waiters if all signalers have been submitted. */
		iif_fence_notify_all_signaler_submitted_cb_locked(out_fences[i]);
	}

	/* Increases the number of outstanding block wakelock held above. */
	for (i = 0; out_fences && i < num_out_fences; i++) {
		for_each_ip(wakelock_held[i], ip, tmp)
			out_fences[i]->outstanding_block_wakelock[ip]++;
	}

	iif_fences_write_unlock(out_fences, num_out_fences, flags);

	/* Submits a waiter to @in_fences. */
	for (i = 0; in_fences && i < num_in_fences; i++)
		iif_fence_submit_waiter(in_fences[i], waiter_ip);

release_block_wakelock:
	/* Releases all block wakelocks held already on error. */
	if (ret) {
		for (i = 0; out_fences && i < num_out_fences; i++) {
			for_each_ip(wakelock_held[i], ip, tmp)
				iif_fence_release_block_wakelock(out_fences[i], ip, 1);
		}
	}
err_free_wakelock_held:
	kfree(wakelock_held);
err_free_flags:
	kfree(flags);

	return ret;
}

void iif_fence_signaler_completed(struct iif_fence *fence)
{
	/*
	 * For the backward compatibility, if the fence is a direct single-shot IIF, ignore this
	 * function call. Removing signaler will be done when `iif_fence_signal{_*}()` functions
	 * are called.
	 */
	if (iif_fence_is_direct(fence) && fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		return;

	iif_fence_do_waited_work_if_no_outstanding_signalers(fence, iif_fence_waited_work);
}

void iif_fence_signaler_completed_async(struct iif_fence *fence)
{
	/*
	 * For the backward compatibility, if the fence is a direct single-shot IIF, ignore this
	 * function call. Removing signaler will be done when `iif_fence_signal{_*}()` functions
	 * are called.
	 */
	if (iif_fence_is_direct(fence) && fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		return;

	iif_fence_do_waited_work_if_no_outstanding_signalers(fence, iif_fence_waited_work_async);
}

void iif_fence_waiter_completed(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	unsigned long flags;
	bool release_block_wakelock = true;

	write_lock_irqsave(&fence->fence_lock, flags);

	iif_fence_remove_waiter_locked(fence, waiter_ip);

	if (!iif_fence_outstanding_signalers_locked(fence) &&
	    fence->outstanding_block_wakelock[waiter_ip])
		fence->outstanding_block_wakelock[waiter_ip]--;
	else
		release_block_wakelock = false;

	write_unlock_irqrestore(&fence->fence_lock, flags);

	if (release_block_wakelock)
		iif_fence_release_block_wakelock(fence, waiter_ip, 1);
}

void iif_fence_waiter_completed_async(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	iif_fence_remove_waiter(fence, waiter_ip);
	iif_fence_waited_work_async(fence);
}

int iif_fence_signal(struct iif_fence *fence)
{
	return iif_fence_signal_with_status(fence, 0);
}

int iif_fence_signal_async(struct iif_fence *fence)
{
	return iif_fence_signal_with_status_async(fence, 0);
}

int iif_fence_signal_with_status(struct iif_fence *fence, int error)
{
	int ret;

	ret = iif_fence_signal_and_update(fence, error, iif_fence_waited_work);
	if (!ret)
		flush_work(&fence->signaled_work);

	return ret;
}

int iif_fence_signal_with_status_async(struct iif_fence *fence, int error)
{
	return iif_fence_signal_and_update(fence, error, iif_fence_waited_work_async);
}

int iif_fence_get_signal_status(struct iif_fence *fence)
{
	unsigned long flags;
	int status = 0;

	read_lock_irqsave(&fence->fence_lock, flags);

	if (fence->signaled)
		status = fence->signal_error ? fence->signal_error : 1;

	read_unlock_irqrestore(&fence->fence_lock, flags);

	return status;
}

void iif_fence_get_status(struct iif_fence *fence, struct iif_fence_status *status)
{
	unsigned long flags;

	read_lock_irqsave(&fence->fence_lock, flags);

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		status->signaled = fence->signaled;
	else
		status->timeline = fence->timeline;
	status->error = fence->signal_error;

	read_unlock_irqrestore(&fence->fence_lock, flags);
}

void iif_fence_set_propagate_unblock(struct iif_fence *fence)
{
	/*
	 * It is safe to not hold any locks because this function is expected to be called before
	 * signaling @fence and @fence->propagate will be accessed only when the fence has been
	 * unblocked and the poll callbacks are executed. The value won't be changed while the
	 * callbacks are being processed.
	 */
	fence->propagate = true;
}

bool iif_fence_is_signaled(struct iif_fence *fence)
{
	/* TODO(b/379110867): Consider reusable fence. */
	return iif_fence_get_signal_status(fence);
}

signed long iif_fence_wait_timeout(struct iif_fence *fence, bool intr, signed long timeout_jiffies)
{
	struct iif_fence_wait_cb wait_cb;
	unsigned long flags;
	signed long ret = timeout_jiffies ? timeout_jiffies : 1;
	u64 timeline;

	write_lock_irqsave(&fence->fence_lock, flags);

	/* If @fence is already unblocked, exit the function directly. */
	if (iif_fence_is_unblocked_locked(fence, fence->timeline))
		goto out;

	/* If the thread is already interrupted, return an error right away. */
	if (intr && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	/* If the timeout is 0, but the fence is not signaled yet, return 0 which means timeout. */
	if (!timeout_jiffies) {
		ret = 0;
		goto out;
	}

	/* Registers the poll callback to @fence. */
	wait_cb.base.func = iif_fence_wait_poll;
	wait_cb.task = current;
	list_add_tail(&wait_cb.base.node, &fence->poll_cb_list);

	/*
	 * Stores the current timeline. It will be used for checking if the fence is a reusable
	 * fence and its timeline has been increased.
	 */
	timeline = fence->timeline;

	/* Wait until @fence to be unblocked. */
	while (!iif_fence_is_unblocked_locked(fence, timeline) && ret > 0) {
		/* Sets interruptible status of the current thread before sleep. */
		if (intr)
			__set_current_state(TASK_INTERRUPTIBLE);
		else
			__set_current_state(TASK_UNINTERRUPTIBLE);

		/* Releases the lock and waits for the signal. */
		write_unlock_irqrestore(&fence->fence_lock, flags);

		/* Sleeps until interrupt, timeout or `iif_fence_wait_poll()` is invoked. */
		ret = schedule_timeout(ret);

		/* Re-holds the lock and checks the interrupt and the signal status. */
		write_lock_irqsave(&fence->fence_lock, flags);

		/* If timeout hasn't elapsed yet, but the thread is interrupted, return an error. */
		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}

	/* Unregisters the callback from @fence. */
	if (!list_empty(&wait_cb.base.node))
		list_del_init(&wait_cb.base.node);

out:
	write_unlock_irqrestore(&fence->fence_lock, flags);

	return ret;
}

void iif_fence_waited(struct iif_fence *fence, enum iif_ip_type ip)
{
	iif_fence_waiter_completed(fence, ip);
}

void iif_fence_waited_async(struct iif_fence *fence, enum iif_ip_type ip)
{
	iif_fence_waiter_completed_async(fence, ip);
}

int iif_fence_add_poll_callback(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb,
				iif_fence_poll_cb_t func)
{
	unsigned long flags;
	int ret = 0;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (((fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT) && fence->signaled) ||
	    ((fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE) && fence->signal_error) ||
	    iif_fence_all_signalers_signaled_locked(fence)) {
		INIT_LIST_HEAD(&poll_cb->node);
		ret = -EPERM;
		goto out;
	}

	poll_cb->func = func;
	list_add_tail(&poll_cb->node, &fence->poll_cb_list);

	/*
	 * If the fence is a reusable fence and it was signaled at least once, invoke the poll
	 * callback right away.
	 *
	 * Otherwise, if the caller registers a sync point after this function call and the sync
	 * point is already passed, the poll callback won't be notified for that sync point forever.
	 *
	 * Even if the caller registers a sync point first and then calls this function, a race
	 * condition that the poll callback won't be invoked can happen if the fence timeline passes
	 * the sync point in the middle.
	 *
	 * Therefore, it is much safer to invoke the poll callback if the fence has been signaled at
	 * least once and let the caller decide whether the fence timeline is the value they are
	 * waiting for.
	 */
	if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE && fence->timeline)
		poll_cb->func(fence, poll_cb);
out:
	write_unlock_irqrestore(&fence->fence_lock, flags);

	return ret;
}

bool iif_fence_remove_poll_callback(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb)
{
	unsigned long flags;
	bool removed = false;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (!list_empty(&poll_cb->node)) {
		list_del_init(&poll_cb->node);
		removed = true;
	}

	write_unlock_irqrestore(&fence->fence_lock, flags);

	return removed;
}

int iif_fence_add_all_signaler_submitted_callback(struct iif_fence *fence,
						  struct iif_fence_all_signaler_submitted_cb *cb,
						  iif_fence_all_signaler_submitted_cb_t func)
{
	int ret = 0;
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);

	cb->remaining_signalers = iif_fence_unsubmitted_signalers_locked(fence);

	/* Already all signalers are submitted. */
	if (!cb->remaining_signalers) {
		ret = -EPERM;
		goto out;
	}

	cb->func = func;
	list_add_tail(&cb->node, &fence->all_signaler_submitted_cb_list);
out:
	write_unlock_irqrestore(&fence->fence_lock, flags);

	return ret;
}

bool iif_fence_remove_all_signaler_submitted_callback(
	struct iif_fence *fence, struct iif_fence_all_signaler_submitted_cb *cb)
{
	bool removed = false;
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (!list_empty(&cb->node)) {
		list_del_init(&cb->node);
		removed = true;
	}

	write_unlock_irqrestore(&fence->fence_lock, flags);

	return removed;
}

int iif_fence_unsubmitted_signalers(struct iif_fence *fence)
{
	unsigned long flags;
	int unsubmitted;

	read_lock_irqsave(&fence->fence_lock, flags);
	unsubmitted = iif_fence_unsubmitted_signalers_locked(fence);
	read_unlock_irqrestore(&fence->fence_lock, flags);

	return unsubmitted;
}

int iif_fence_submitted_signalers(struct iif_fence *fence)
{
	return fence->params.remaining_signalers - iif_fence_unsubmitted_signalers(fence);
}

int iif_fence_signaled_signalers(struct iif_fence *fence)
{
	unsigned long flags;
	int signaled;

	read_lock_irqsave(&fence->fence_lock, flags);
	signaled = fence->signaled_signalers;
	read_unlock_irqrestore(&fence->fence_lock, flags);

	return signaled;
}

int iif_fence_outstanding_waiters(struct iif_fence *fence)
{
	unsigned long flags;
	int outstanding;

	read_lock_irqsave(&fence->fence_lock, flags);
	outstanding = fence->outstanding_waiters;
	read_unlock_irqrestore(&fence->fence_lock, flags);

	return outstanding;
}
