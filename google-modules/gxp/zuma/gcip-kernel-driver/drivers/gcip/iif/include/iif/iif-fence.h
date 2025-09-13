/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The inter-IP fence.
 *
 * The meaning of "a fence is signaled" or "unblocked" is that the fence has been signaled enough
 * times as many as the expected number of signals which is decided when the fence is initialized
 * and it has been unblocked. That says every single signaler commands are expected to signal the
 * fence (i.e., call `iif_fence_signal{_with_status}` function) even though commands weren't
 * processed normally.
 *
 * Also, unblocking a fence here is only for the kernel perspective. Therefore, the IIF driver will
 * notify the fence unblock to only who are polling the fences (via poll callbacks or poll syscall).
 * It means that if the signaler is an IP, not AP, it is a responsibility of the IP side to unblock
 * a fence and propagate an error to waiter IPs. Therefore, unblocking fence by the IIF driver will
 * not unblock the fences in the IP side unless the IP kernel driver notices the fence unblock via
 * a poll callback and asks their IP to unblock the fence.
 *
 * If the signaler IP requires a support of the kernel driver to unblock the fence in case the IP is
 * already faulty and can't notify waiter IPs, the signaler IP kernel driver can unblock the fence
 * with an error and each waiter IP kernel driver can notice it by `fence_unblocked` operator of the
 * fence manager or registering a poll callback to the fence directly and propagate the error to the
 * IP of each.
 *
 * Besides, one of the main roles of the IIF driver is creating fences with assigning fence IDs,
 * initializing the fence table and managing the life cycle of them.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#ifndef __IIF_IIF_FENCE_H__
#define __IIF_IIF_FENCE_H__

#include <linux/kref.h>
#include <linux/lockdep_types.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <iif/iif-manager.h>
#include <iif/iif-shared.h>

#define iif_pr_level(pr_level, fence, fmt, ...)                                             \
	pr_level("iif: %s: %d: " fmt, fence->fence_ops->sync_unit_name(fence->driver_data), \
		 fence->id, ##__VA_ARGS__)

#define iif_err(fence, fmt, ...) iif_pr_level(pr_err, fence, fmt, ##__VA_ARGS__)
#define iif_warn(fence, fmt, ...) iif_pr_level(pr_warn, fence, fmt, ##__VA_ARGS__)
#define iif_info(fence, fmt, ...) iif_pr_level(pr_info, fence, fmt, ##__VA_ARGS__)
#define iif_dbg(fence, fmt, ...) iif_pr_level(pr_dbg, fence, fmt, ##__VA_ARGS__)

struct iif_fence;
struct iif_fence_ops;
struct iif_fence_poll_cb;
struct iif_fence_all_signaler_submitted_cb;

/*
 * The callback which will be called when all signalers have signaled @fence.
 *
 * It will be called while @fence->fence_lock is held and it is safe to access @fence->signaled,
 * @fence->timeline, @fence->signal_error and @fence->propagate.
 *
 * The callback must be lightweight to avoid blocking a thread who signals @fence for a long time.
 *
 * Expected logic of registering / unregistering this callback is:
 *   Before submitting a waiter command to IP:
 *     iif_fence_submit_waiter(fence, IP);
 *     iif_fence_add_poll_callback(fence, ...);
 *
 *   After a response has arrived from IP:
 *     iif_fence_remove_poll_callback(fence, ...);
 *     iif_fence_waiter_completed(fence);
 *
 * Note that the callback was registered AFTER the `submit_waiter()` function call and unregistered
 * BEFORE the `waited()` function call because those functions will acquire or release the wakelock
 * of the IP if `{acquire,release}_block_wakelock()` operators are registered to the IIF manager and
 * the poll callback would be meaningful only if the wakelock is held.
 *
 * The callback will be invoked in the spin-lock context. If the IP driver needs to handle the fence
 * unblock in the normal context, the `fence_unblocked()` operator of `struct iif_manager_ops` can
 * be considered. The operator will be invoked in a separate thread which will be scheduled once
 * @fence is unblocked. Therefore, its latency would be worse than this poll callback.
 *
 * Following is describing how to handle the fence signal.
 *
 * For single-shot fences:
 * The callback is supposed to be invoked when the signaler signals @fence as many times as the
 * number of total signalers which was decided when the fence was created and the fence has been
 * unblocked eventually. The callback must check @fence->signaled to see if the fence is actually
 * unblocked.
 *
 * Note that even though @fence->error is non-zero, @fence->signaled wouldn't be true if the fence
 * hasn't been signaled enough times. In this case, the IP driver shouldn't consider the fence as
 * unblocked. It depends on how the underlying sync-unit fence is implemented.
 *
 * For reusable fences:
 * The callback is supposed to be invoked when the signaler signals @fence which increases the fence
 * timeline and the timeline has reached any registered sync points. The callback must check whether
 * @fence->timeline has become the one that the IP driver is waiting for.
 *
 * Also, unlike single-shot fences, if @fence->error becomes non-zero, the IP driver should treat
 * the fence as errored out immediately.
 */
typedef void (*iif_fence_poll_cb_t)(struct iif_fence *fence, struct iif_fence_poll_cb *cb);

/*
 * The callback which will be called when all signalers have been submitted to @fence.
 *
 * It will be called while @fence->signalers_lock is held and it is safe to read
 * @fence->all_signaler_submitted_error inside.
 */
typedef void (*iif_fence_all_signaler_submitted_cb_t)(
	struct iif_fence *fence, struct iif_fence_all_signaler_submitted_cb *cb);

/*
 * The state of a fence object.
 * The state transition is
 *   INITED {-> FILE_CREATED -> FILE_RELEASED} -> RETIRED
 * i.e. Sync file creation is optional.
 */
enum iif_fence_state {
	/* Initial state. */
	IIF_FENCE_STATE_INITIALIZED,
	/* The fence ID has been retired. */
	IIF_FENCE_STATE_RETIRED,
};

/*
 * Contains the callback function which will be called when the fence has been unblocked.
 *
 * The callback can be registered to the fence by the `iif_fence_add_poll_callback` function.
 */
struct iif_fence_poll_cb {
	/* Node to be added to the list. */
	struct list_head node;
	/* Actual callback function to be called. */
	iif_fence_poll_cb_t func;
};

/*
 * Contains the callback function which will be called when all signalers have been submitted.
 *
 * The callback will be registered to the fence when the `iif_fence_submit_waiter` function fails
 * in the submission.
 */
struct iif_fence_all_signaler_submitted_cb {
	/* Node to be added to the list. */
	struct list_head node;
	/* Actual callback function to be called. */
	iif_fence_all_signaler_submitted_cb_t func;
	/* The number of remaining signalers to be submitted. */
	int remaining_signalers;
};

/* Parameters which will be used when creating a fence. */
struct iif_fence_params {
	/* The signaler type. */
	enum iif_fence_signaler_type signaler_type;
	/* The fence type. */
	enum iif_fence_type fence_type;
	/* The signaler IP. Used only if @signaler_type is IIF_FENCE_SIGNALER_TYPE_IP. */
	enum iif_ip_type signaler_ip;
	/* The number of signalers (commands) which will signal the fence. */
	uint16_t remaining_signalers;
	/* The bitwise value where each bit represents an IP. (See enum iif_ip_type) */
	uint16_t waiters;
	/* Used only if the fence is a reusable IP fence or timer fence. */
	uint64_t timeout;
	/* The fence flags. (See `IIF_FLAGS_*` macros) */
	uint32_t flags;
};

/* Describes the fence status. */
struct iif_fence_status {
	/*
	 * The fence error.
	 *
	 * For single-shot fences, it can be set before @signaled becomes true. It depends on the
	 * policy of the underlying sync-unit. For example, in case of a direct fence, @signaled
	 * will be still false even when @error is set if not all signalers have signaled the fence.
	 *
	 * For reusable fences, the waiters must consider the fence as errored out immediately if
	 * this field is set to a non-zero value.
	 */
	int error;
	union {
		/*
		 * True if the fence is signaled. (Only for single-shot fences)
		 *
		 * The waiter must consider the fence is unblocked only if this field is set to
		 * true. Even though @error is set, if this field is false, the fence must not be
		 * considered as unblocked. Whether the fence will be errored out immediately when
		 * @error is set or not is dependent on the implementation details of the sync-unit.
		 */
		bool signaled;
		/*
		 * The timeline value of the fence which will be increased by 1 for each signal
		 * starting from 0. (Only for reusable fences)
		 */
		u64 timeline;
	};
};

/* The fence object. */
struct iif_fence {
	/* IIF manager. */
	struct iif_manager *mgr;
	/* Fence ID. */
	int id;
	/* Parameters of the fence. */
	struct iif_fence_params params;
	/* The signaler IP. Keep it separately from @params for the backward compatibility. */
	enum iif_ip_type signaler_ip;
	/* The number of submitted signalers. */
	uint16_t submitted_signalers;
	/* The number of signaled signalers. */
	uint16_t signaled_signalers;
	/* The interrupt state before holding @signalers_lock. */
	unsigned long signalers_lock_flags;
	/* The number of outstanding waiters. */
	uint16_t outstanding_waiters;
	/* The number of outstanding waiters per IP. */
	uint16_t outstanding_waiters_per_ip[IIF_IP_RESERVED];
	/* The number of outstanding wakelock holds per waiter IP. */
	uint16_t outstanding_block_wakelock[IIF_IP_RESERVED];
	union {
		/* The timeline value of the fence. (For reusable fences) */
		uint64_t timeline;
		/* True if the fence is signaled. (For single-shot fences) */
		bool signaled;
	};
	/*
	 * Protects overall properties of the fence. (outstanding signalers / waiters, callbacks,
	 * state, ...)
	 */
	rwlock_t fence_lock;
#if IS_ENABLED(CONFIG_DEBUG_SPINLOCK)
	struct lock_class_key fence_lock_key;
#endif /* IS_ENABLED(CONFIG_DEBUG_SPINLOCK) */
	/* Reference count. */
	struct kref kref;
	/* Operators. */
	const struct iif_fence_ops *ops;
	/* State of this fence object. */
	enum iif_fence_state state;
	/* List of callbacks which will be called when the fence is unblocked. */
	struct list_head poll_cb_list;
	/* Marks true if poll callbacks are pended because of IIF_FLAGS_DISABLE_POLL. */
	bool poll_cb_pended;
	/* List of callbacks which will be called when all signalers have been submitted. */
	struct list_head all_signaler_submitted_cb_list;
	/* Will be set to a negative errno if the fence is signaled with an error. */
	int signal_error;
	/* Will be set to a negative errno if waiting the signaler submission fails. */
	int all_signaler_submitted_error;
	/* The number of sync_file(s) bound to the fence. */
	atomic_t num_sync_file;
	/* If true, the waiter IP drivers should propagate the fence unblock to their IP. */
	bool propagate;
	/* Work which will be executed when the fence has been unblocked. */
	struct work_struct signaled_work;
	/* Work which will be executed when each waiter command finished waiting on the fence. */
	struct work_struct waited_work;
	/* If true, @waited_work will return right away. */
	bool stop_waited_work;
	/* Work decreasing the refcount of fence asynchronously. */
	struct work_struct put_work;
	/* The node to be added to the ID to the fence object hash table in IIF manager. */
	struct hlist_node id_to_fence_node;
	/* The operators of the underlying sync-unit fence. */
	const struct iif_manager_fence_ops *fence_ops;
	/* The private data of the underlying sync-unit driver. */
	void *driver_data;
	/* The private data of the underlying sync-unit fence. */
	void *fence_data;
	/* The callback which will be registered to the sync-unit driver. */
	struct iif_manager_fence_ops_poll_cb sync_unit_poll_cb;
};

/* Operators of `struct iif_fence`. */
struct iif_fence_ops {
	/*
	 * Called on destruction of @fence to release additional resources when its reference count
	 * becomes zero.
	 *
	 * This callback is optional.
	 * Context: normal and in_interrupt().
	 */
	void (*on_release)(struct iif_fence *fence);
};

/*
 * Initializes @fence which will be signaled by @signaler_ip IP. @total_signalers is the number of
 * signalers which must be submitted to the fence. Its initial reference count is 1.
 *
 * The initialized fence will be assigned an ID which depends on @signaler_ip. Each IP will have at
 * most `IIF_NUM_FENCES_PER_IP` number of fences and the assigned fence ID for IP[i] will be one of
 * [i * IIF_NUM_FENCES_PER_IP ~ (i + 1) * IIF_NUM_FENCES_PER_IP - 1].
 *
 * This function initializes @fence as an IP-signaled single-shot fence. If the one needs other
 * types of fence, use `iif_fence_init_with_params()`.
 *
 * Returns 0 on success. Otherwise, a negative errno on failure.
 */
int iif_fence_init(struct iif_manager *mgr, struct iif_fence *fence,
		   const struct iif_fence_ops *ops, enum iif_ip_type signaler_ip,
		   uint16_t total_signalers);

/**
 * iif_fence_init_with_params() - Initializes @fence according to the passed @params.
 * @mgr: The IIF manager which maintains the life-cycle of @fence.
 * @fence: The fence to be initialized.
 * @ops: The operator of @fence.
 * @params: The parameters to initialize @fence.
 *
 * Returns 0 on success. Otherwise, a negative errno on error.
 */
int iif_fence_init_with_params(struct iif_manager *mgr, struct iif_fence *fence,
			       const struct iif_fence_ops *ops,
			       const struct iif_fence_params *params);

/**
 * iif_fence_set_priv_fence_data() - Sets a private fence data to @iif.
 * @iif: The fence to set the private fence data.
 * @fence_data: The private fence data of underlying sync-unit fence.
 */
void iif_fence_set_priv_fence_data(struct iif_fence *iif, void *fence_data);

/**
 * iif_fence_get_priv_fence_data() - Returns the registered private fence data of @iif.
 * @iif: The fence to get the private fence data.
 *
 * Returns the private fence data.
 */
void *iif_fence_get_priv_fence_data(struct iif_fence *iif);

/**
 * iif_fence_set_flags() - Updates the flags of @fence.
 * @fence: The fence to update the flags.
 * @flags: The bitmask to update the fence flags.
 * @clear: If false, the bitmask passed to @flag will be set to @fence. Otherwise, the bitmask will
 *         be cleared from @fence.
 *
 * Returns 0 on success. Otherwise, a negative errno on error.
 */
int iif_fence_set_flags(struct iif_fence *fence, unsigned long flags, bool clear);

/* Gets the flags of @fence. */
static inline unsigned long iif_fence_get_flags(struct iif_fence *fence)
{
	return fence->params.flags;
}

/*
 * Opens a file which syncs with @fence and returns its FD. The file will hold a reference to
 * @fence until it is closed.
 */
int iif_fence_install_fd(struct iif_fence *fence);

/*
 * Has @fence know the sync file bound to it is about to be released. This function would try to
 * retire the fence if applicable.
 */
void iif_fence_on_sync_file_release(struct iif_fence *fence);

/* Increases the reference count of @fence. */
struct iif_fence *iif_fence_get(struct iif_fence *fence);

/*
 * Gets a fence from @fd and increments its reference count of the file pointer.
 *
 * Returns the fence pointer, if @fd is for IIF. Otherwise, returns a negative errno.
 */
struct iif_fence *iif_fence_fdget(int fd);

/*
 * Decreases the reference count of @fence and if it becomes 0, releases @fence.
 *
 * If the caller is going to put @fence in the un-sleepable context such as the IRQ context or spin
 * lock, they should use the async one.
 */
void iif_fence_put(struct iif_fence *fence);
void iif_fence_put_async(struct iif_fence *fence);

/**
 * iif_fence_submit_signaler() - Submits a signaler to the fence.
 * @fence: The fence to submit a signaler.
 *
 * The signaler IP driver must call this function before submitting every single signaler command to
 * its firmware. Once a signaler command has completed either successfully or with an error, the IP
 * driver must call the `iif_fence_signaler_completed()` function to let the IIF driver know that
 * one signaler command has completed.
 *
 * I.e., submit_signaler() and signaler_completed() functions are supposed to be called like this:
 *
 *      // The IP driver is requested to submit a signaler command to the firmware.
 *      ret = iif_fence_submit_signaler(fence);
 *      if (ret)
 *              return ret;
 *      // Now the IP driver can submit the signaler command to the firmware.
 *
 *      ...
 *      (The kernel driver waits for a response from the firmware.)
 *      ...
 *
 *      // A response has arrived from the firmware or the signaler command is errored out.
 *      iif_fence_signal(fence);
 *      iif_fence_signaler_completed(fence);
 *      // The IP driver returns the response to the runtime.
 *
 * Its purpose is to track the number of signalers that have been submitted and signaled the fence.
 * If there are remining signalers which can signal the fence, the IIF driver will pend releasing
 * the block wakelock of waiters if needed until there are no more signalers to signal the fence.
 *
 * Note that this function call cannot be reverted. (The `iif_fence_signaler_completed()` function
 * is NOT for reverting the signaler submission to the fence.) Once the IIF driver notices that all
 * signalers have been submitted to @fence, it will notify waiters that it is time to submit waiter
 * commands which is impossible (or really hard) to revert. That said, the fence must be signaled
 * after this function call to unblock waiters.
 *
 * Therefore, it is recommended to call this function when the kernel driver can guarantee that a
 * signaler command will be submitted to the firmware successfully so that the fence will be always
 * signaled by the firmware according to the status of the command. (Note that we should guarantee
 * that the signaler IP firmware is the only one who will signal the fence as long as the firmware
 * is alive.)
 *
 * If any error is detected at the kernel level after this function call, but before the firmware
 * completes handling the signaler command (e.g., the signaler command submission to the firmware
 * fails, the command has been timedout at the kernel level, the kernel driver has detected the
 * firmware crash, ...), the IP kernel driver must propagate the error to the fence.
 *
 * - If an error implies that the firmware is not working or faulty so that the firmware will never
 *   signal the fence, the kernel driver can signal the fence at the kernel level directly.
 *   (See `iif_fence_set_propagate_unblock()` and `iif_fence_signal_with_status{_async}()`)
 *
 * - If the firmware still works even when an error happens,
 *   - The kernel driver should ask the firmware to signal the fence with an error.
 *   - Or, the kernel driver should ask the firmware to cancel all submitted signaler commands to
 *     guarantee that the fence won't be signaled by the firmware anymore and then signal the fence
 *     at the kernel level.
 *   - Or, just don't signal the fence and let the waiter commands to be timedout.
 *
 * After handling the error, the `iif_fence_signaler_completed()` function must be called at the end
 * as usual.
 *
 * Note that it is different from the case that an error happened at the firmware level while
 * handling the command and the firmware returned an error response to the kernel driver. In this
 * case, the firmware must have signaled the fence with an error and the kernel driver doesn't need
 * to error the fence out.
 *
 * This function cannot be called in the IRQ context.
 *
 * Returns 0 if the submission succeeds. Otherwise, returns a negative errno.
 */
int iif_fence_submit_signaler(struct iif_fence *fence);

/*
 * Submits a waiter of @ip IP. @fence->outstanding_waiters will be incremented by 1.
 * Note that the waiter submission will not be done when not all signalers have been submitted.
 * (i.e., @fence->submitted_signalers < @fence->params.remaining_signalers)
 *
 * This function will acquire the block wakelock of @ip before it updates the IIF's wait table to
 * mark @ip is going to wait on @fence. Otherwise, if the signaler IPx processes its command even
 * earlier than the waiter IPy powers its block up by the race, IPx may try to notify IPy which is
 * not powered up yet. If IPy spec doesn't allow that, it may cause an unexpected bug. Therefore, we
 * should acquire the block wakelock of @ip before updating the wait table.
 *
 * This function cannot be called in the IRQ context.
 *
 * Returns the number of remaining signalers to be submitted (i.e., returning 0 means the submission
 * actually succeeded). Otherwise, returns a negative errno if it fails with other reasons.
 */
int iif_fence_submit_waiter(struct iif_fence *fence, enum iif_ip_type ip);

/**
 * iif_fence_add_sync_point() - Registers a sync point to a reusable fence.
 * @fence: A reusable fence.
 * @timeline: The timeline value that the fence will start to notify waiters. It must be bigger than
 *            0.
 * @count: Waiters will be notified for @count times from @timeline value. If the user passes
 *         `IIF_FENCE_SYNC_POINT_COUNT_ALL`, waiters will be notified every single fence signal from
 *         @timeline. It must be bigger than 0.
 *
 * This function cannot be called in any context.
 *
 * Returns 0 on success. Otherwise, returns a negative errno.
 */
int iif_fence_add_sync_point(struct iif_fence *fence, u64 timeline, u64 count);

/*
 * Submits a waiter of @waiter_ip to each fence in @in_fences and a signaler to each fence in
 * @out_fences. Either @in_fences or @out_fences is allowed to be NULL.
 *
 * For the waiter submission, if at least one fence of @in_fences haven't finished the signaler
 * submission, this function will fail and return -EAGAIN.
 *
 * For the signaler submission, if at least one fence of @out_fences have already finished the
 * signaler submission, this function will fail and return -EPERM.
 *
 * This function will be useful when the caller wants to accomplish the waiter submission and the
 * signaler submission atomically.
 *
 * This function cannot be called in the IRQ context.
 *
 * Note that this function may reorder fences internally. This is to prevent a potential dead lock
 * which can be caused by holding the locks of multiple fences at the same time. Also, fences in
 * @in_fences and @out_fences should be unique. Otherwise, it will return -EDEADLK.
 *
 * The function returns 0 on success.
 */
int iif_fence_submit_signaler_and_waiter(struct iif_fence **in_fences, int num_in_fences,
					 struct iif_fence **out_fences, int num_out_fences,
					 enum iif_ip_type waiter_ip);

/**
 * iif_fence_signaler_completed() - Notifies the IIF driver that a submitted signaler has completed
 *                                  and it will not signal the fence anymore.
 * @fence: The fence to notify.
 *
 * The signaler IP driver must call this function when a signaler command which was submitted (i.e.,
 * `iif_fence_submit_signaler()` was called for the command) has completed regardless of its result.
 *
 * See `iif_fence_submit_signaler()` function for the detailed usage of this function.
 *
 * It may try to release the block wakelock of waiter IPs if there are some IPs which called the
 * `iif_fence_waiter_completed()` function earlier than this function call and releasing the block
 * wakelock of those IPs was pended. (See `iif_fence_waiter_completed()` function below.)
 *
 * This function doesn't have any meaning if the fence is a direct single-shot fence. For backward
 * compatibility, `iif_fence_signal{_*}()` functions imply that this function is called.
 *
 * This function can be called in the normal context only. Use the async one below if the caller is
 * in the IRQ or spin-lock context.
 */
void iif_fence_signaler_completed(struct iif_fence *fence);

/**
 * iif_fence_signaler_completed_async() - Notifies the IIF driver that a submitted signaler has
 *                                        completed and it will not signal the fence anymore.
 * @fence: The fence to notify.
 *
 * This function is the same as `iif_fence_signaler_completed()`, but can be called in the IRQ or
 * spin-lock context.
 *
 * Unlike the synchronous one, releasing the block wakelock of waiter IPs will be done
 * asynchronously.
 */
void iif_fence_signaler_completed_async(struct iif_fence *fence);

/**
 * iif_fence_waiter_completed() - Decreases the number of outstanding waiters of @fence by 1.
 * @fence: The fence to remove a waiter.
 * @waiter_ip: The IP type of waiter.
 *
 * If there are no more signalers which can signal the fence, it will try to release the block
 * wakelock of @waiter_ip which was held when `iif_fence_submit_waiter()` is called if the IP
 * defined the `{acquire,release}_block_wakelock()` operator.
 *
 * Note that if @fence still can be signaled, releasing the block wakelock will be pended until all
 * signalers have finished signaling @fence or the fence is going to be destroyed. This case can
 * happen when the fence is reusable or the fence is single-shot but the signaler IPx is not
 * responding in time and the waiter IPy processes its command as timeout. This pending logic is
 * required to avoid the situation that IPx notifies IPy which is already powered down.
 *
 * This function can be called in the normal context only. Use the async one below if the caller is
 * in the IRQ or spin-lock context.
 */
void iif_fence_waiter_completed(struct iif_fence *fence, enum iif_ip_type waiter_ip);

/**
 * iif_fence_waiter_completed_async() - Decreases the number of outstanding waiters of @fence by 1.
 * @fence: The fence to remove a waiter.
 * @waiter_ip: The IP type of waiter.
 *
 * This function is the same as `iif_fence_waiter_completed()`, but can be called in the IRQ or
 * spin-lock context.
 *
 * Unlike the synchronous one, releasing the block wakelock of waiter IPs will be done
 * asynchronously.
 */
void iif_fence_waiter_completed_async(struct iif_fence *fence, enum iif_ip_type waiter_ip);

/**
 * iif_fence_signal() - Signals @fence. In case of direct fences, it also notifies the IIF driver
 *                      of the fence signal.
 * @fence: The fence to signal.
 *
 * Basically, the fence can be signaled at the kernel-level if:
 * - The fence signaler is AP.
 * - The fence signaler is IP, but its firmware has crashed. Its kernel driver should signal the
 *   fence on behalf of the firmware when it detects that the firmware becomes faulty. In this case,
 *   the kernel driver must call the `iif_fence_set_propagate_unblock()` function before signaling
 *   the fence to let the IIF driver know that the fence is going to be signaled at the kernel
 *   level.
 *
 * In case of non-direct fences, this function doesn't have meaning unless the signaler IP crashed
 * and `iif_fence_set_propagate_unblock()` was called before. However, it does if the fence is a
 * direct fence to notify the IIF driver of the fence signal even when the signaler IP is alive,
 * thus the IIF driver can notify the ones polling on the kernel-level fence. To support it, it is
 * recommended to call this function by the signaler IP driver not only when the driver signals the
 * fence on behalf of the firmware, but also when the fence is signaled by the firmware.
 *
 * If the fence is a non-direct fence, the sync-unit driver will always notify the IIF driver of the
 * fence signal whenever the fence has been signaled. Therefore, the signaler IP driver doesn't need
 * to notify the IIF driver of the fence signal and the IIF driver can notify the ones polling on
 * the kernel-fence object when the sync-unit driver notifies the IIF driver. Even when the signaler
 * IP crashes and this function is called by the signaler IP driver to signal the fence, this
 * function will just ask the sync-unit to handle the fence signal and return. The IIF driver won't
 * think the fence is signaled and won't notify the kernel-fence object until the sync-unit actually
 * processes the signal and it notifies the IIF driver. (i.e., this function handles the signal
 * asynchronously internally.)
 *
 * However, in case of direct fences, there is no sync-unit and the signaler IP driver should notify
 * the IIF driver that the fence has been signaled regardless of the liveness of the signaler IP.
 * Therefore, this function will always notify the IIF driver of the fence signal if the fence is a
 * direct fence. In other words, it is mandatory to call this function if the signaler IP crashed to
 * signal the fence at the kernel-level, but also to notify the IIF driver of the fence signal even
 * when the signaler IP is alive.
 *
 * If the fence is a direct single-shot fence, the signaler IP driver may simply call this function
 * whenever it receives a response of the signaler command from its firmware. Because it implies
 * that the firmware has signaled the fence at the firmware level before returning a response and
 * the driver can call this function to notify the IIF driver of the fence signal.
 *
 * If the fence is a direct reusable fence, the signaler IP may need a specific protocol between the
 * driver and firmware because one signaler command can signal the fence multiple times. In other
 * words, the IP driver should be able to notify the IIF driver of the fence signal even before the
 * firmware returns a response. The firmware will notify the IP driver of the fence signal via the
 * protocol and the IP driver should forward the signal to the IIF driver.
 *
 * As having that kind of protocol would be too much work to support direct reusable fences, the
 * signaler IP driver is still possible to call this function only once after a signaler command has
 * been processed. However, the ones polling on the kernel-level fence will be notified only once
 * at the end.
 *
 * Based on this description, if the signaler IP driver needs to signal the fence, this function
 * must called before the driver calls `iif_fence_signaler_completed()`.
 *
 * This function can be called in the normal context only. For other contexts, use the async one
 * below.
 *
 * Returns 0 on success. Otherwise, returns a negative errno. Exceptionally, if the fence is a
 * direct single-shot fence, it will return the number of remaining signals on success.
 */
int iif_fence_signal(struct iif_fence *fence);

/**
 * iif_fence_signal_async() - Signals @fence.
 * @fence: The fence to signal.
 *
 * Its functionality is the same as `iif_fence_signal()`, but it can be called in the IRQ or
 * spin-lock contexts.
 *
 * See `iif_fence_signal()` for more details.
 *
 * Returns 0 on success. Otherwise, returns a negative errno. Exceptionally, if the fence is a
 * direct single-shot fence, it will return the number of remaining signals on success.
 */
int iif_fence_signal_async(struct iif_fence *fence);

/**
 * iif_fence_signal_with_status() - Signals @fence with a status.
 * @fence: The fence to signal.
 * @error: The fence status.
 *
 * Basically, its functionality is the same as the `iif_fence_signal()` function above, but the user
 * can provide an optional error status.
 *
 * Note that if the fence is a single-shot fence, even though @error is non-zero, @fence won't be
 * unblocked until the number of remaining signals becomes 0.
 *
 * If the caller passes 0 to @error, its functionality is the same as the `iif_fence_signal`
 * function.
 *
 * This function can be called in the normal context only. For other contexts, use the async one
 * below.
 *
 * Returns 0 on success. Otherwise, returns a negative errno.
 *
 * Exceptionally, if the fence is a single-shot direct fence, it will return the number of remaining
 * signals on success.
 */
int iif_fence_signal_with_status(struct iif_fence *fence, int error);

/**
 * iif_fence_signal_async() - Signals @fence with a status.
 * @fence: The fence to signal.
 * @error: The fence status.
 *
 * Its functionality is the same as `iif_fence_signal_with_status()`, but it can be called in the
 * IRQ or spin-lock contexts.
 *
 * See `iif_fence_signal_with_status()` for more details.
 *
 * Returns 0 on success. Otherwise, returns a negative errno.
 *
 * Exceptionally, if the fence is a single-shot direct fence, it will return the number of remaining
 * signals on success.
 */
int iif_fence_signal_with_status_async(struct iif_fence *fence, int error);

/*
 * Returns the signal status of @fence.
 *
 * Returns 0 if the fence hasn't been unblocked yet, 1 if the fence has been unblocked without any
 * error, or a negative errno if the fence has been signaled with an error at least once.
 *
 * This function is meaningful only if @fence is a single-shot fence.
 */
int iif_fence_get_signal_status(struct iif_fence *fence);

/**
 * iif_fence_get_status() - Returns the fence status to @status.
 *
 * This function returns the last fence status passed from the underlying sync-unit.
 */
void iif_fence_get_status(struct iif_fence *fence, struct iif_fence_status *status);

/*
 * Sets @fence->propagate to true.
 *
 * When @fence has been unblocked and the `fence_unblocked` callback is called, the waiter IP
 * drivers will refer to @fence->propagate and they will inform their IP of the fence unblock if
 * that is true.
 *
 * In case of the signaler IPx of @fence is not able to notify waiter IPs of the fence unblock, the
 * IPx driver can utilize this function to propagate the fence unblock to waiter IP drivers. For
 * example, if IPx becomes faulty and it can't propagate the fence unblock with an error to waiter
 * IPs by itself, the IPx driver can utilize this function when it detects the IP crash to set
 * @fence->propagate to true and the waiter IP drivers will inform their IP of the fence unblock
 * when the `fence_unblocked` callback is called.
 *
 * Note that this function must be called before signaling the fence if needed. Also, the IIF driver
 * will take over the responsibility of updating the number of remaining signals in the fence table
 * of @fence from the IP firmware since calling this function means that the signaler IP doesn't
 * have ability of managing the signal of @fence anymore.
 */
void iif_fence_set_propagate_unblock(struct iif_fence *fence);

/*
 * Returns whether all signalers have signaled @fence.
 *
 * As this function doesn't require to hold any lock, even if this function returns false, @fence
 * can be signaled right after this function returns. One should care about this and may not use
 * this function directly. This function will be mostly used when iif_sync_file is polling @fence.
 *
 * This function is meaningful only if @fence is a single-shot fence.
 */
bool iif_fence_is_signaled(struct iif_fence *fence);

/**
 * iif_fence_wait_timeout() - Waits until @fence is unblocked.
 * @fence: The fence to wait on.
 * @intr: If true, do an interruptible wait.
 * @timeout_jiffies: The timeout in jiffies, or MAX_SCHEDULE_TIMEOUT to wait until @fence gets
 *                   signaled.
 *
 * If the fence is a single-shot fence, the function will be blocked until @fence->signaled becomes
 * true.
 *
 * If the fence is a reusable-fence, the function will be blocked until @fence->timeline increases
 * or @fence->signal_error is set.
 *
 * This function is the same as `iif_fence_wait()`, but can specify timeout.
 *
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out. Otherwise, returns remaining
 * timeout in jiffies on success.
 */
signed long iif_fence_wait_timeout(struct iif_fence *fence, bool intr, signed long timeout_jiffies);

/**
 * iif_fence_wait() - Waits until @fence is unblocked.
 * @fence: The fence to wait on.
 * @intr: If true, do an interruptible wait.
 *
 * Returns -ERESTARTSYS if interrupted. Otherwise, returns MAX_SCHEDULE_TIMEOUT on success.
 */
static inline signed long iif_fence_wait(struct iif_fence *fence, bool intr)
{
	return iif_fence_wait_timeout(fence, intr, MAX_SCHEDULE_TIMEOUT);
}

/*
 * Notifies the driver that a waiter of @ip finished waiting on @fence.
 *
 * It will try to release the block wakelock of @ip which was held when `iif_fence_submit_waiter`
 * was called if @fence was already signaled (i.e., `iif_fence_signal` was called) and the IP
 * defined the `release_block_wakelock` operator (See iif-manager.h file).
 *
 * Note that if @fence is not signaled yet, releasing the block wakelock will be pended until @fence
 * is signaled (i.e., `iif_fence_signal` is called) or it is destroyed. This case can happen when
 * the signaler IPx is not responding in time and the waiter IPy processes its command as timeout.
 * This pending logic is required because if IPy doesn't pend releasing its block wakelock and IPx
 * suddenly processes its command, IPx may try to notify IPy whose block is already powered down and
 * it may cause an unexpected bug if IPy spec doesn't allow that.
 *
 * If the caller is going to stop waiting on @fence in the un-sleepable context such as IRQ context
 * or spin lock, one should use the `iif_fence_waited_async` function below. Its functionality is
 * the same, but `release_block_wakelock` callbacks will be called asynchronously.
 *
 * DEPRECATED: use `iif_fence_waiter_completed{_async}()` instead.
 */
void iif_fence_waited(struct iif_fence *fence, enum iif_ip_type ip);
void iif_fence_waited_async(struct iif_fence *fence, enum iif_ip_type ip);

/**
 * iif_fence_add_poll_callback() - Registers a callback which will be called when @fence has been
 *                                 signaled.
 * @fence: The fence to register the poll callback.
 * @poll_cb: The poll callback instance to be managed by @fence.
 * @func: The poll callback function to be called.
 *
 * If the fence is a single-shot fence, the callback will be invoked when @fence->signaled becomes
 * true. Once the callback is called, it will be automatically unregistered from @fence.
 *
 * If the fence is a reusable fence, the callback can be signaled multiple times whenever the fence
 * timeline reaches any sync-point registered to the fence. Once the fence is errored out, the
 * callback will be invoked regardless of the fence timeline and the callback will be automatically
 * unregistered from the fence. Also, if there was any sync-point that the fence reached before, the
 * poll callback will be invoked in this function call.
 *
 * The @func can be called in the IRQ context.
 *
 * Returns 0 if succeeded. Otherwise, returns a negative errno on failure. Note that if the fence is
 * a single-shot fence and it is already signaled, or the fence is a reusable fence and it is
 * already errored out, it won't add the callback and return -EPERM.
 */
int iif_fence_add_poll_callback(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb,
				iif_fence_poll_cb_t func);

/*
 * Unregisters the callback from @fence.
 *
 * Returns true if the callback is removed before @fence is unblocked.
 */
bool iif_fence_remove_poll_callback(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb);

/*
 * Registers a callback which will be called when all signalers are submitted for @fence and
 * returns the number of remaining signalers to be submitted to @cb->remaining_signalers. Once the
 * callback is called, it will be automatically unregistered from @fence.
 *
 * Note that, as the callback can be invoked right after the registration, if the callback releases
 * @cb internally, the caller should be careful of accessing @cb after the function returns.
 *
 * Returns 0 if succeeded. If all signalers are already submitted, returns -EPERM.
 */
int iif_fence_add_all_signaler_submitted_callback(struct iif_fence *fence,
						  struct iif_fence_all_signaler_submitted_cb *cb,
						  iif_fence_all_signaler_submitted_cb_t func);

/*
 * Unregisters the callback which is registered by the callback above.
 *
 * Returns true if the callback is removed before its being called.
 */
bool iif_fence_remove_all_signaler_submitted_callback(
	struct iif_fence *fence, struct iif_fence_all_signaler_submitted_cb *cb);

/*
 * Returns the number of signalers or waiters information accordingly.
 *
 * Note that these functions hold required locks internally and read the value. Therefore, the value
 * of them can be changed after the function returns. The one must use these functions only for the
 * debugging purpose.
 *
 * These functions can be called in the IRQ context.
 */
int iif_fence_unsubmitted_signalers(struct iif_fence *fence);
int iif_fence_submitted_signalers(struct iif_fence *fence);
int iif_fence_signaled_signalers(struct iif_fence *fence);
int iif_fence_outstanding_waiters(struct iif_fence *fence);

#endif /* __IIF_IIF_FENCE_H__ */
