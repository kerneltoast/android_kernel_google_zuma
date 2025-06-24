/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of in-kernel fence. The concept of it is that the kernel is the subject waiting on
 * the DMA fence to be signaled.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __GCIP_IKF_H__
#define __GCIP_IKF_H__

#include <linux/dma-fence.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * The callback which will be triggered once the in-kernel fence has been signaled or the awaiter
 * has stopped waiting on it.
 *
 * - wait_status >= 0
 *   : @fence has been signaled. The meaning of value is the remaining timeout in jiffies. Note that
 *     if the user didn't pass the timeout (i.e., `gcip_ikf_wait()` is called), 0 will be returned.
 *     The callback should check the fence status to see if it was signaled with an error.
 *
 * - wait_status = -ERESTARTSYS
 *   : The thread waiting on @fence to be signaled has been interrupted by `gcip_ikf_awaiter_exit()`
 *     or others.
 *
 * - wait_status = -ETIMEDOUT
 *   : @fence hasn't been signaled until the timeout elapses. This case will never happen if the
 *     user didn't pass the timeout (i.e., `gcip_ikf_wait()` is called).
 *
 * - wait_status = Other error codes
 *   : See `dma_fence_wait_timeout()` function.
 *
 * @data is the user-data passed to the `gcip_ikf_wait{_timeout}()` function.
 *
 * Context: Normal.
 */
typedef void (*gcip_ikf_signaled_cb_t)(struct dma_fence *fence, signed long wait_status,
				       void *data);

/* Manages threads waiting on in-kernel fences. */
struct gcip_ikf_awaiter {
	/* List of threads waiting on in-kernel fences. */
	struct list_head threads;
	/* Protects @threads. */
	spinlock_t threads_lock;
	/* Whether the awaiter is going to stop waiting on all fences. */
	bool stop_threads;
	/* The callback to be called once any in-kernel fence has been signaled. */
	gcip_ikf_signaled_cb_t signaled_cb;
};

/* A thread waiting on an in-kernel fence. */
struct gcip_ikf_thread {
	/* List node to be linked to @awaiter->threads. */
	struct list_head node;
	/* The awaiter which created this thread. */
	struct gcip_ikf_awaiter *awaiter;
	/* The task which is waiting on the fence. */
	struct task_struct *task;
	/* The fence to wait on. */
	struct dma_fence *fence;
	/* The timeout in jiffies. */
	signed long timeout_jiffies;
	/* Whether the thread has been signaled. */
	bool signaled;
	/* The user-data to be passed to @awaiter->signaled_cb. */
	void *data;
};

/**
 * gcip_ikf_awaiter_init() - Initializes @awaiter.
 * @awaiter: The awaiter to initialize.
 * @signaled_cb: The callback to be called once any in-kernel fence has been signaled.
 *
 * Returns 0 on success or a negative error code.
 */
int gcip_ikf_awaiter_init(struct gcip_ikf_awaiter *awaiter, gcip_ikf_signaled_cb_t signaled_cb);

/**
 * gcip_ikf_awaiter_exit() - Exits @awaiter.
 * @awaiter: The awaiter to exit.
 *
 * If there are any threads waiting on fences, they will be canceled.
 */
void gcip_ikf_awaiter_exit(struct gcip_ikf_awaiter *awaiter);

/**
 * gcip_ikf_wait_timeout() - Waits on @fence to be signaled asynchronously.
 * @awaiter: The awaiter to wait on @fence.
 * @fence: The fence to wait on.
 * @timeout_jiffies: The timeout in jiffies, or MAX_SCHEDULE_TIMEOUT to wait until @fence gets
 *                   signaled.
 * @thread_name: The name of the thread to be created.
 * @data: The user-data to be passed to @awaiter->signaled_cb.
 *
 * Once the fence has been signaled, @awaiter->signaled_cb will be triggered with @data.
 *
 * Returns 0 on success. Otherwise, returns a negative errno.
 */
int gcip_ikf_wait_timeout(struct gcip_ikf_awaiter *awaiter, struct dma_fence *fence,
			  signed long timeout_jiffies, const char *thread_name, void *data);

/**
 * gcip_ikf_wait() - The same as the `gcip_ikf_wait_timeout()` function, but without timeout.
 * @awaiter: The awaiter to wait on @fence.
 * @fence: The fence to wait on.
 * @thread_name: The name of the thread to be created.
 * @data: The user-data to be passed to @awaiter->signaled_cb.
 *
 * The @awaiter->signaled_cb callback won't be invoked until @fence is signaled or the thread is
 * interrupted. Note that if the fence is signaled and the callback is invoked, 0 will be passed to
 * the @wait_status parameter of the callback.
 *
 * See gcip_ikf_wait_timeout() for the details.
 */
static inline int gcip_ikf_wait(struct gcip_ikf_awaiter *awaiter, struct dma_fence *fence,
				const char *thread_name, void *data)
{
	return gcip_ikf_wait_timeout(awaiter, fence, MAX_SCHEDULE_TIMEOUT, thread_name, data);
}

#endif /* __GCIP_IKF_H__ */
