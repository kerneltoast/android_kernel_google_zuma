// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of in-kernel fence. The concept of it is that the kernel is the subject waiting on
 * the DMA fence to be signaled.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/dma-fence.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <gcip/gcip-ikf.h>

#if IS_ENABLED(CONFIG_GCIP_TEST)
#include "unittests/helper/gcip-ikf-controller.h"

#define TEST_NOTIFY_FENCE_SIGNAL() gcip_ikf_controller_notify_fence_signal()
#else
#define TEST_NOTIFY_FENCE_SIGNAL(...)
#endif /* IS_ENABLED(CONFIG_GCIP_TEST) */

static int gcip_ikf_thread_func(void *data)
{
	struct gcip_ikf_thread *thread = data;
	struct gcip_ikf_awaiter *awaiter = thread->awaiter;
	signed long wait_status = thread->timeout_jiffies;
	bool release_thread = false;

	/*
	 * If the thread is interrupted by others such as `kthread_stop()` in
	 * `gcip_ikf_awaiter_exit()`, the function will return -ERESTARTSYS.
	 *
	 * Note that if @thread->timeout_jiffies is MAX_SCHEDULE_TIMEOUT, the function will only
	 * return -ERESTARTSYS or MAX_SCHEDULE_TIMEOUT.
	 */
	wait_status = dma_fence_wait_timeout(thread->fence, true, thread->timeout_jiffies);

	/*
	 * Theoretically, if @wait_status is -ERESTARTSYS, `kthread_should_stop()` should be true.
	 */
	if (unlikely(kthread_should_stop()))
		goto out;

	/*
	 * For testing the race condition that the IP driver is calling `gcip_ikf_awaiter_exit()`
	 * right after the fence has been signaled. The thread should handle the fence signaled as
	 * usual and the function call should wait for the thread termination properly.
	 */
	TEST_NOTIFY_FENCE_SIGNAL();

	/*
	 * We don't need to protect it with any lock because `gcip_ikf_awaiter_exit()` will access
	 * it only after the `kthread_stop()` call which synchronously waits the thread termination.
	 */
	thread->signaled = true;

	if (!wait_status)
		wait_status = -ETIMEDOUT;

	/*
	 * If the timeout is MAX_SCHEDULE_TIMEOUT, it means that the user didn't set the timeout
	 * and `dma_fence_wait_timeout()` will always return MAX_SCHEDULE_TIMEOUT once the fence is
	 * signaled. In this case, return 0 to @wait_status since the remaining timeout doesn't have
	 * any meaning.
	 */
	if (wait_status == MAX_SCHEDULE_TIMEOUT)
		wait_status = 0;

	/*
	 * Notifies the IP driver that the fence is signaled.
	 *
	 * Ignores the return value of the callback since we can't do anything with even if it
	 * returns an error.
	 */
	if (awaiter->signaled_cb)
		awaiter->signaled_cb(thread->fence, wait_status, thread->data);
out:
	spin_lock(&awaiter->threads_lock);

	/*
	 * If @awaiter->stop_threads is true, `gcip_ikf_awaiter_exit()` will releases all resources.
	 * Checks @awaiter->stop_threads instead of `kthread_should_stop()` since there might be a
	 * very short time gap between setting @awaiter->stop_threads to true and the
	 * `kthread_stop()` call. See gcip_ikf_awaiter_exit().
	 */
	if (likely(!awaiter->stop_threads)) {
		release_thread = true;
		list_del(&thread->node);
	}

	spin_unlock(&awaiter->threads_lock);

	/*
	 * From here, as the lock is released above, either @awaiter or the IP driver which
	 * registered @awaiter->signaled_cb might have been cleaned up. We must not access either
	 * of them.
	 */

	/* Do this separately just in case these functions sleep internally. */
	if (release_thread) {
		dma_fence_put(thread->fence);
		kfree(thread);
	}

	return 0;
}

int gcip_ikf_awaiter_init(struct gcip_ikf_awaiter *awaiter, gcip_ikf_signaled_cb_t signaled_cb)
{
	INIT_LIST_HEAD(&awaiter->threads);
	spin_lock_init(&awaiter->threads_lock);
	awaiter->signaled_cb = signaled_cb;
	awaiter->stop_threads = false;

	return 0;
}

void gcip_ikf_awaiter_exit(struct gcip_ikf_awaiter *awaiter)
{
	struct gcip_ikf_thread *cur, *nxt;

	spin_lock(&awaiter->threads_lock);
	awaiter->stop_threads = true;
	spin_unlock(&awaiter->threads_lock);

	list_for_each_entry_safe(cur, nxt, &awaiter->threads, node) {
		/* Waits for the thread termination synchronously. */
		kthread_stop(cur->task);

		/*
		 * If @cur->signaled is false, the thread exited before the fence is signaled.
		 * We should let IP drivers know that it has canceled waiting on the fence.
		 * As `dma_fence_wait_timeout()` returns -ERESTARTSYS when the thread is
		 * interrupted, follow the same error code here.
		 */
		if (!cur->signaled && awaiter->signaled_cb)
			awaiter->signaled_cb(cur->fence, -ERESTARTSYS, cur->data);

		/*
		 * As @awaiter->stop_threads is true, the thread won't have released its resources.
		 * Release them here.
		 */
		list_del(&cur->node);
		dma_fence_put(cur->fence);
		kfree(cur);
	}
}

int gcip_ikf_wait_timeout(struct gcip_ikf_awaiter *awaiter, struct dma_fence *fence,
			  signed long timeout_jiffies, const char *thread_name, void *data)
{
	struct gcip_ikf_thread *thread;
	unsigned long flags;
	int ret;

	if (!fence)
		return -EINVAL;

	dma_fence_enable_sw_signaling(fence);

	thread = kzalloc(sizeof(*thread), GFP_KERNEL);
	if (!thread)
		return -ENOMEM;

	thread->awaiter = awaiter;
	thread->fence = dma_fence_get(fence);
	thread->timeout_jiffies = timeout_jiffies;
	thread->data = data;

	/* Creates a thread. */
	thread->task = kthread_create(gcip_ikf_thread_func, thread, "%s", thread_name);
	if (IS_ERR(thread->task)) {
		ret = PTR_ERR(thread->task);
		goto err_put_fence;
	}

	spin_lock_irqsave(&awaiter->threads_lock, flags);

	/* If @awaiter is going to destroy, don't allow waiting on @fence. */
	if (awaiter->stop_threads) {
		ret = -EPERM;
		goto err_spin_unlock;
	}

	list_add_tail(&thread->node, &awaiter->threads);
	wake_up_process(thread->task);

	spin_unlock_irqrestore(&awaiter->threads_lock, flags);

	return 0;

err_spin_unlock:
	spin_unlock_irqrestore(&awaiter->threads_lock, flags);
	kthread_stop(thread->task);
err_put_fence:
	dma_fence_put(fence);
	kfree(thread);

	return ret;
}
