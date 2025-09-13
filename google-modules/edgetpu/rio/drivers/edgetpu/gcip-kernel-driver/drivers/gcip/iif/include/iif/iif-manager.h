/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The manager of inter-IP fences.
 *
 * It manages the pool of fence IDs. The IIF driver device will initialize a manager and each IP
 * driver will fetch the manager from the IIF device.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#ifndef __IIF_IIF_MANAGER_H__
#define __IIF_IIF_MANAGER_H__

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/of.h>
#include <linux/rwsem.h>
#include <linux/types.h>

#include <iif/iif-fence-table.h>
#include <iif/iif-shared.h>

#define IIF_MANAGER_ID_TO_FENCE_HASH_BITS 3

struct iif_fence;
struct iif_fence_params;
struct iif_fence_status;

/*
 * The callback function which will be called by the sync-unit driver when the underlying sync-unit
 * fence of @iif is signaled. The sync-unit driver must pass the fence status to @status.
 *
 * - Single-shot fence: If the fence is unblocked, @status->signaled must be set to true. Even
 *                      though @status->error is non-zero, if @status->signaled is false, the IIF
 *                      driver will consider the fence is sill blocked. Whether to signal the fence
 *                      or not if the fence is errored out even before all signalers signal the
 *                      fence is dependent on the implementation of the sync-unit.
 *
 * - Reusable fence: In this case, @status->signaled will be ignored and @status->timeline will be
 *                   considered to propagate the fence signal to waiters polling on the kernel-level
 *                   fence object. Waiters will register sync points and the IIF driver will notify
 *                   waiters once @status->timeline reaches one of sync points. The sync-unit driver
 *                   can invoke this callback per every single signal regardless of sync points, but
 *                   it is also recommended to consider sync points to minimize AP involevement.
 *                   Unlike single-shot fence, if @status->error is set, the error will be
 *                   propagated to all waiters immediately, even before @status->timeline reaches
 *                   the value they are waiting for. If @status->timeline reaches the timeout value,
 *                   the sync-unit must error the fence out as timeout.
 */
typedef void (*iif_manager_fence_ops_poll_cb_t)(struct iif_fence *iif,
						const struct iif_fence_status *status);

/* Operators. */
struct iif_manager_ops {
	/* Following callbacks are required. */
	/*
	 * Called when @fence is signaled.
	 *
	 * Note: if it is possible to handle the fence signal in the IRQ context, please consider
	 * registering a poll callback to the fence instead of using this operator. As this operator
	 * is designed to be always called in the normal context which means it will be always
	 * invoked in a deferred kthread, it may have a latency issue. Also, it is not suitable to
	 * get the real-time fence signal.
	 *
	 * The purpose of this operator is mostly for propagating the fence signal to the waiter IP
	 * in these cases:
	 * - The signaler is AP.
	 * - The signaler is IP, but the signaler IP becomes faulty and it cannot propagate the
	 *   fence signal to waiters.
	 *
	 * For those cases, @fence->propagate will be set to true and the IP driver should notify
	 * its IP that the fence has been signaled (i.e., send @fence->id to IP) on behalf of the
	 * signaler. This operator returns void since the IIF driver has nothing can do when the IP
	 * driver fails to notify its IP. It is the responsibility of the IP driver side to handle
	 * if that happens.
	 *
	 * Note that the timing of @fence retirement is nondeterministic since we can't decide the
	 * timing of the runtime or IP crash. However, even if @fence->propagate is true and the
	 * fence has been retired in the middle, it should be still safe to notify the waiter IP of
	 * the fence signal since the firmware will verify whether the fence is actually signaled.
	 *
	 * If the IP driver is still going to use this operator to handle the fence signal, please
	 * refer to the comments of `iif_fence_poll_cb_t` type in the `iif-fence.h` file to
	 * understand how to handle the fence signal according to the fence type.
	 *
	 * For reusable fences, note that this operator wouldn't be signaled for every single
	 * signal. If the fence has been signaled multiple times in very short time, the timeline
	 * value can be bumped up by more than 1. For example, if the IP driver was waiting the
	 * timeline value to be 2, but the it has been increased from 1 to 3, the IP driver should
	 * be able to notice that it already passed the value it was waiting for. If the IP driver
	 * needs to know the timeline increase in real-time, please consider registering a poll
	 * callback to the fence.
	 *
	 * Context: Normal.
	 */
	void (*fence_unblocked)(struct iif_fence *fence, void *data);

	/* Following callbacks are optional. */
	/*
	 * Acquires the block wakelock of IP.
	 *
	 * The callback will be executed when there are outstanding signalers.
	 *
	 * This callback is required if the waiter IPx doesn't allow to be notified by the signaler
	 * IPy when the IPx block is not powered on. If somehow the driver of waiter IPx submits a
	 * waiter command to the firmware late and the signaler IPy processes its command early by
	 * the race, IPy may try to notify IPx which is not powered on yet and it may cause a bug if
	 * the IPx spec doesn't allow that. Therefore, if the IPx implements this callback, the IIF
	 * driver will try to acquire the IPx block wakelock before submitting a waiter of IPx.
	 *
	 * Context: Normal.
	 */
	int (*acquire_block_wakelock)(void *data);
	/*
	 * Releases the block wakelock of IP.
	 *
	 * If there are no outstanding waiters of IPx and there are no outstanding signalers, the
	 * block wakelock of IPx will be released.
	 *
	 * Context: Normal.
	 */
	void (*release_block_wakelock)(void *data);
};

/*
 * The container of a poll callback which will be called by the sync-unit driver when the underlying
 * sync-unit fence of @iif is signaled.
 *
 * The IIF driver will register a poll callback to the sync-unit driver via `add_poll_cb()`
 * operator after the fence creation. Also, the driver will try to remove the registered callback
 * via `remove_poll_cb()` operator before releasing the fence.
 */
struct iif_manager_fence_ops_poll_cb {
	/* The kernel-level fence object of the underlying sync-unit fece. */
	struct iif_fence *iif;
	/*
	 * The list node which can be utilized by the sync-unit driver to manage registered
	 * callbacks in a list. If the fence won't be signaled anymore, the sync-unit driver can
	 * unregister the callback before the IIF driver calls `remove_poll_cb()` operator.
	 */
	struct list_head node;
	/* The function which will be called when the fence is signaled. */
	iif_manager_fence_ops_poll_cb_t func;
};

/* The fence operators which will be implemented by the underlying sync-unit. */
struct iif_manager_fence_ops {
	/*
	 * Returns the name of underlying sync-unit.
	 *
	 * This operator is mandatory.
	 *
	 * Context: Any.
	 */
	const char *(*sync_unit_name)(void *driver_data);

	/*
	 * Creates a underlying sync-unit fence.
	 *
	 * Returns the ID of the created fence on success. Otherwise, returns a negative errno.
	 *
	 * This operator is mandatory.
	 *
	 * Context: Normal.
	 */
	int (*fence_create)(struct iif_fence *iif, const struct iif_fence_params *params,
			    void *driver_data);

	/*
	 * Retires the underlying sync-unit fence.
	 *
	 * This operator can be done asynchronously. The meaning that this operator is called that
	 * there is no one accessing the fence.
	 *
	 * This operator is mandatory.
	 *
	 * Context: @iif->fence_lock (spin-lock).
	 */
	void (*fence_retire)(struct iif_fence *iif, void *driver_data);

	/*
	 * Adds a sync point.
	 *
	 * This operator is meaningful only if the fence is a reusable fence. It will add a sync
	 * point to the fence which describes when the waiters should be notified. From when the
	 * timeline value of the fence reaches @timeline, it will notify waiters @count times. Note
	 * that even if there are multiple sync points that were reached at the same time, waiters
	 * must be notified only once. If @count is IIF_FENCE_SYNC_POINT_COUNT_ALL, waiters should
	 * be notified for every single signal from @timeline.
	 *
	 * This operator can be done asynchronously. When the sync point is actually registered, the
	 * sync-unit should check if the fence timeline already reached or passed @timeline. If it
	 * did, the sync-unit should invoke the poll callback.
	 *
	 * For example, if the current fence timeline is 5 and one user is going to register a
	 * sync point, timeline = 3 and count = 4, the poll callback must be invoked once when the
	 * sync point registered as the fence timeline already exceeds 3. Also, the callback must be
	 * invoked once more when the fence timeline becomes 6.
	 *
	 * Returns 0 on success. Otherwise, returns a negative errno.
	 *
	 * This operator is optional. If the underlying sync-unit driver doesn't implement this
	 * operator, the driver should invoke the registered poll callback every single signal.
	 *
	 * Context: @iif->fence_lock (spin-lock).
	 */
	int (*fence_add_sync_point)(struct iif_fence *iif, u64 timeline, u64 count,
				    void *driver_data);

	/*
	 * Signals the fence.
	 *
	 * This operator will be called only if the fence should be signaled by the kernel driver.
	 * - The signaler is AP.
	 * - The signaler is IP, but the signaler IP has crashed and the kernel drivers should
	 *   support propagating the fence error to waiters on behald of the signaler IP.
	 *
	 * Note that the poll callback registered via `add_poll_cb()` operator must NOT be invoked
	 * in this operator. Otherwise, there would be a deadlock. In case of a fence is being
	 * signaled by IP, the fence signal propagation route will be `IP FW -> sync-unit FW ->
	 * sync-unit KD -> (poll callback) -> IIF KD`. To keep the same convention, even if the
	 * fence is signaled via this operator, the registered poll callbacks must be invoked when
	 * the sync-unit actually handles the fence signal and notifies the sync-unit driver of it.
	 *
	 * Returns 0 on success. Otherwise, returns a negative errno.
	 *
	 * This operator is mandatory.
	 *
	 * Context: @iif->fence_lock (spin-lock).
	 */
	int (*fence_signal)(struct iif_fence *iif, int status, void *driver_data);

	/*
	 * Registers a poll callback to the underlying sync-unit fence.
	 *
	 * This operator registers a callback, @cb, which should be invoked when the underlying
	 * sync-unit fence is signaled. The sync-unit driver can invoke the callback in any context.
	 * The callback is supposed to be called when the sync-unit notifies the sync-unit driver of
	 * the fence signal.
	 *
	 * Returns 0 on success. Otherwise, returns a negative errno.
	 *
	 * This operator is mandatory.
	 *
	 * Context: Normal.
	 */
	int (*add_poll_cb)(struct iif_fence *iif, struct iif_manager_fence_ops_poll_cb *cb,
			   void *driver_data);

	/*
	 * Unregisters a poll callback from the underlying sync-unit fence.
	 *
	 * This operator should unregister the callback which was registered by the `add_poll_cb()`
	 * operator.
	 *
	 * Returns true if the callback is unregistered. Otherwise, returns false if the callback is
	 * already removed.
	 *
	 * This operator is mandatory.
	 *
	 * Context: @iif->fence_lock (spin-lock).
	 */
	bool (*remove_poll_cb)(struct iif_fence *iif, struct iif_manager_fence_ops_poll_cb *cb,
			       void *driver_data);
};

/*
 * The structure overall data required by IIF driver such as fence table.
 *
 * Until we have stand-alone IIF driver, one of the IP drivers will initializes a manager by
 * the `iif_init` function and every IP driver will share it.
 */
struct iif_manager {
	/* Reference count of this instance. */
	struct kref kref;
	/* Fence ID pool. */
	struct ida idp;
	/* Fence table shared with the firmware. */
	struct iif_fence_table fence_table;
	/* Operators per IP. */
	const struct iif_manager_ops *ops[IIF_IP_RESERVED];
	/* Protects @ops. */
	struct rw_semaphore ops_sema;
	/* User-data per IP. */
	void *data[IIF_IP_RESERVED];
	/* Platform bus device. */
	struct device *dev;
	/* Char device structure. */
	struct cdev char_dev;
	/* Char device number. */
	dev_t char_dev_no;
	/* The context of DMA fences created by the driver. */
	u64 dma_fence_context;
	/* The sequence number of DMA fences created by the driver. */
	atomic64_t dma_fence_seqno;
	/* ID to the fence object hash table. */
	struct hlist_head id_to_fence[BIT(IIF_MANAGER_ID_TO_FENCE_HASH_BITS)];
	/* Protects @id_to_fence_lock */
	rwlock_t id_to_fence_lock;
	/* The fence operators of the direct fences. */
	const struct iif_manager_fence_ops *direct_fence_ops;
	/* The fence operators of the underlying sync-unit. */
	const struct iif_manager_fence_ops *fence_ops;
	/* The private sync-unit driver data which will be passed to each fence operator. */
	void *driver_data;
	/* Protects @fence_ops and @driver_data. */
	struct rw_semaphore fence_ops_sema;
};

/*
 * Initializes IIF driver and returns its manager. Its initial reference count is 1. It will map
 * the fence table by parsing the device tree via @np.
 *
 * The returned manager will be destroyed when its reference count becomes 0 by `iif_manager_put`
 * function.
 */
struct iif_manager *iif_manager_init(const struct device_node *np);

/* Increases the reference count of @mgr. */
struct iif_manager *iif_manager_get(struct iif_manager *mgr);

/* Decreases the reference count of @mgr and if it becomes 0, releases @mgr. */
void iif_manager_put(struct iif_manager *mgr);

/*
 * Registers operators of @ip.
 *
 * Note that @ops must not be released until @ip won't be utilized as signaler or waiter anymore.
 */
int iif_manager_register_ops(struct iif_manager *mgr, enum iif_ip_type ip,
			     const struct iif_manager_ops *ops, void *data);

/* Unregisters operators of @ip. */
void iif_manager_unregister_ops(struct iif_manager *mgr, enum iif_ip_type ip);

/**
 * iif_manager_register_fence_ops() - Registers the operators of the underlying sync-unit fences.
 * @mgr: The IIF manager to register the operators.
 * @ops: The operators to be registered.
 * @driv_data: The private driver data which will be passed to each operator call.
 *
 * Note that it is not allowed to register operators if there are other ones already registered.
 * Also, @ops must not be released until it is unregistered from @mgr.
 *
 * Returns 0 on success. Otherwise, returns a negative errno.
 */
int iif_manager_register_fence_ops(struct iif_manager *mgr, const struct iif_manager_fence_ops *ops,
				   void *driv_data);

/**
 * iif_manager_unregister_fence_ops() - Unregisters any fence operators from the IIF manager.
 * @mgr: The IIF manager to unregister fence operators.
 */
void iif_manager_unregister_fence_ops(struct iif_manager *mgr);

/**
 * iif_manager_set_fence_ops() - Sets fence operators to @iif.
 * @mgr: The IIF manager which manages the fence operators.
 * @iif: The fence where the fence operators will be set to.
 * @params: The parameters passed from the user to decide the type of fence.
 *
 * Returns 0 on success. If there are no operators supported, returns a negative errno.
 */
int iif_manager_set_fence_ops(struct iif_manager *mgr, struct iif_fence *iif,
			      const struct iif_fence_params *params);

/**
 * iif_manager_unset_fence_ops() - Unsets fence operators from @iif.
 * @mgr: The IIF manager which manages the fence operators.
 * @iif: The fence where the fence operators will be unset from.
 */
void iif_manager_unset_fence_ops(struct iif_manager *mgr, struct iif_fence *iif);

/*
 * Acquires the block wakelock of @ip.
 *
 * Returns 0 on success or @ip hasn't defined the `acquire_block_wakelock` operator. Otherwise,
 * returns a negative errno.
 */
int iif_manager_acquire_block_wakelock(struct iif_manager *mgr, enum iif_ip_type ip);

/* Releases the block wakelock of @ip. */
void iif_manager_release_block_wakelock(struct iif_manager *mgr, enum iif_ip_type ip);

/*
 * Notifies @fence has been unblocked to IP drivers waiting on the fence.
 *
 * This function will be called if @fence has been unblocked.
 */
void iif_manager_broadcast_fence_unblocked(struct iif_manager *mgr, struct iif_fence *fence);

/**
 * iif_manager_add_fence_to_hlist() - Adds @fence to the @mgr->id_to_fence hash table.
 * @mgr: The IIF manager maintains the hash table.
 * @fence: The fence to map.
 */
void iif_manager_add_fence_to_hlist(struct iif_manager *mgr, struct iif_fence *fence);

/**
 * iif_manager_remove_fence_from_hlist() - Removes @fence from the @mgr->id_to_fence hash table.
 * @mgr: The IIF manager maintains the hash table.
 * @fence: The fence to map.
 */
void iif_manager_remove_fence_from_hlist(struct iif_manager *mgr, struct iif_fence *fence);

/**
 * iif_manager_get_fence_from_id() - Returns the fence object of the fence ID
 * @mgr: The IIF manager maintains the ID to fence object hash table.
 * @id: The fence ID.
 *
 * The caller must put the returned fence object once it doesn't need to access it anymore.
 *
 * Returns the fence pointer on success. Otherwise, returns NULL.
 */
struct iif_fence *iif_manager_get_fence_from_id(struct iif_manager *mgr, int id);

#endif /* __IIF_IIF_MANAGER_H__ */
