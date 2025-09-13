// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIF driver sync file.
 *
 * To export fences to the userspace, the driver will allocate a sync file to a fence and will
 * return its file descriptor to the user. The user can distinguish fences with it. The driver will
 * convert the file descriptor to the corresponding fence ID and will pass it to the IP.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sync_file.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <iif/iif-dma-fence.h>
#include <iif/iif-fence.h>
#include <iif/iif-sync-file.h>
#include <iif/iif.h>

static void iif_sync_file_fence_signaled(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb)
{
	struct iif_sync_file *sync_file = container_of(poll_cb, struct iif_sync_file, poll_cb);

	wake_up_all(&sync_file->wq);
}

static int iif_sync_file_release(struct inode *inode, struct file *file)
{
	struct iif_sync_file *sync_file = file->private_data;

	if (atomic_dec_and_test(&sync_file->fence->num_sync_file))
		iif_fence_on_sync_file_release(sync_file->fence);
	if (test_bit(IIF_SYNC_FILE_FLAGS_POLL_ENABLED, &sync_file->flags))
		iif_fence_remove_poll_callback(sync_file->fence, &sync_file->poll_cb);
	iif_fence_put(sync_file->fence);
	kfree(sync_file);

	return 0;
}

static __poll_t iif_sync_file_poll(struct file *file, poll_table *wait)
{
	struct iif_sync_file *sync_file = file->private_data;
	struct iif_fence *fence = sync_file->fence;
	int ret;

	if (unlikely(fence->params.flags & IIF_FLAGS_DISABLE_POLL))
		return 0;

	poll_wait(file, &sync_file->wq, wait);

	if (list_empty(&sync_file->poll_cb.node) &&
	    !test_and_set_bit(IIF_SYNC_FILE_FLAGS_POLL_ENABLED, &sync_file->flags)) {
		ret = iif_fence_add_poll_callback(fence, &sync_file->poll_cb,
						  iif_sync_file_fence_signaled);
		/*
		 * If the fence was already signaled (single-shot) or errored out (reusable), just
		 * wake up all.
		 */
		if (ret < 0)
			wake_up_all(&sync_file->wq);
	}

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT && iif_fence_is_signaled(fence))
		return EPOLLIN;

	if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE &&
	    (sync_file->last_poll_timeline < fence->timeline || fence->signal_error)) {
		sync_file->last_poll_timeline = fence->timeline;
		return EPOLLIN;
	}

	return 0;
}

static int iif_sync_file_ioctl_get_information(struct iif_sync_file *sync_file,
					       struct iif_fence_get_information_ioctl __user *argp)
{
	struct iif_fence *fence = sync_file->fence;
	const struct iif_fence_get_information_ioctl ibuf = {
		.signaler_ip = fence->params.signaler_ip,
		.total_signalers = fence->params.remaining_signalers,
		.submitted_signalers = iif_fence_submitted_signalers(fence),
		.signaled_signalers = iif_fence_signaled_signalers(fence),
		.outstanding_waiters = iif_fence_outstanding_waiters(fence),
		.signal_status = iif_fence_get_signal_status(fence),
	};

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static int iif_sync_file_ioctl_submit_signaler(struct iif_sync_file *sync_file)
{
	struct iif_fence *fence = sync_file->fence;

	if (fence->params.signaler_ip != IIF_IP_AP) {
		iif_err(fence,
			"Only fences with signaler type AP are allowed to submit a signaler (signaler_ip=%d)\n",
			fence->params.signaler_ip);
		return -EPERM;
	}
	return iif_fence_submit_signaler(fence);
}

static int iif_sync_file_ioctl_signal(struct iif_sync_file *sync_file,
				      struct iif_fence_signal_ioctl __user *argp)
{
	struct iif_fence_signal_ioctl ibuf;
	struct iif_fence *fence = sync_file->fence;
	int ret;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	if (fence->params.signaler_ip != IIF_IP_AP) {
		iif_err(fence,
			"Only fences with signaler type AP are allowed to signal (signaler_ip=%d)\n",
			fence->params.signaler_ip);
		return -EPERM;
	}

	ret = iif_fence_signal_with_status(fence, ibuf.error);
	if (ret < 0)
		return ret;

	ibuf.remaining_signals = ret;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static int iif_sync_file_ioctl_set_flags(struct iif_sync_file *sync_file,
					 struct iif_fence_set_flags_ioctl __user *argp)
{
	struct iif_fence_set_flags_ioctl ibuf;
	struct iif_fence *fence = sync_file->fence;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	return iif_fence_set_flags(fence, ibuf.flags, ibuf.clear);
}

static int iif_sync_file_ioctl_get_flags(struct iif_sync_file *sync_file, u32 __user *argp)
{
	struct iif_fence *fence = sync_file->fence;

	if (copy_to_user(argp, &fence->params.flags, sizeof(fence->params.flags)))
		return -EFAULT;

	return 0;
}

static int iif_sync_file_ioctl_bridge_dma_fence(struct iif_sync_file *sync_file, s32 __user *argp)
{
	struct iif_fence *iif_fence = sync_file->fence;
	struct dma_fence *dma_fence;
	struct sync_file *dma_sync_file;
	int fd, ret;

	dma_fence = dma_iif_fence_bridge(iif_fence);
	if (IS_ERR(dma_fence))
		return PTR_ERR(dma_fence);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto err_put_fence;
	}

	if (copy_to_user(argp, &fd, sizeof(fd))) {
		ret = -EFAULT;
		goto err_put_fd;
	}

	dma_sync_file = sync_file_create(dma_fence);
	if (!dma_sync_file) {
		ret = -ENOMEM;
		goto err_put_fd;
	}

	/*
	 * @dma_sync_file holds the refcount of @dma_fence. We can release one which was held when
	 * `dma_iif_fence_bridge()` was called.
	 */
	dma_fence_put(dma_fence);

	/* Installs @fd to @dma_sync_file. */
	fd_install(fd, dma_sync_file->file);

	return 0;

err_put_fd:
	put_unused_fd(fd);
err_put_fence:
	dma_fence_put(dma_fence);

	return ret;
}

static int iif_sync_file_ioctl_signaler_completed(struct iif_sync_file *sync_file)
{
	struct iif_fence *fence = sync_file->fence;

	if (fence->params.signaler_ip != IIF_IP_AP) {
		iif_err(fence,
			"IIF_FENCE_SIGNALER_COMPLETED ioctl is allowed for AP-signaled fences only (signaler_ip=%d)\n",
			fence->params.signaler_ip);
		return -EPERM;
	}

	iif_fence_signaler_completed(fence);

	return 0;
}

static int iif_sync_file_ioctl_add_sync_point(struct iif_sync_file *sync_file, s32 __user *argp)
{
	struct iif_fence_add_sync_point_ioctl ibuf;
	struct iif_fence *fence = sync_file->fence;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	return iif_fence_add_sync_point(fence, ibuf.timeline, ibuf.count);
}

static int iif_sync_file_ioctl_get_information_with_details(
	struct iif_sync_file *sync_file,
	struct iif_fence_get_information_with_details_ioctl __user *argp)
{
	struct iif_fence *fence = sync_file->fence;
	struct iif_fence_get_information_with_details_ioctl ibuf = {
		.signaler_type = fence->params.signaler_type,
		.fence_type = fence->params.fence_type,
		.signaler_ip = fence->params.signaler_ip,
		.total_signalers = fence->params.remaining_signalers,
		.waiters = fence->params.waiters,
		.timeout = fence->params.timeout,
		.flags = fence->params.flags,
		.submitted_signalers = iif_fence_submitted_signalers(fence),
		.signaled_signalers = iif_fence_signaled_signalers(fence),
		.outstanding_waiters = iif_fence_outstanding_waiters(fence),
		.error = fence->signal_error,
	};

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		ibuf.signaled = fence->signaled;
	else if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE)
		ibuf.timeline = fence->timeline;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static long iif_sync_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iif_sync_file *sync_file = file->private_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case IIF_FENCE_GET_INFORMATION:
		return iif_sync_file_ioctl_get_information(sync_file, argp);
	case IIF_FENCE_SUBMIT_SIGNALER:
		return iif_sync_file_ioctl_submit_signaler(sync_file);
	case IIF_FENCE_SIGNAL:
		return iif_sync_file_ioctl_signal(sync_file, argp);
	case IIF_FENCE_SET_FLAGS:
		return iif_sync_file_ioctl_set_flags(sync_file, argp);
	case IIF_FENCE_GET_FLAGS:
		return iif_sync_file_ioctl_get_flags(sync_file, argp);
	case IIF_FENCE_BRIDGE_DMA_FENCE:
		return iif_sync_file_ioctl_bridge_dma_fence(sync_file, argp);
	case IIF_FENCE_SIGNALER_COMPLETED:
		return iif_sync_file_ioctl_signaler_completed(sync_file);
	case IIF_FENCE_ADD_SYNC_POINT:
		return iif_sync_file_ioctl_add_sync_point(sync_file, argp);
	case IIF_FENCE_GET_INFORMATION_WITH_DETAILS:
		return iif_sync_file_ioctl_get_information_with_details(sync_file, argp);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations iif_sync_file_fops = {
	.release = iif_sync_file_release,
	.poll = iif_sync_file_poll,
	.unlocked_ioctl = iif_sync_file_ioctl,
};

struct iif_sync_file *iif_sync_file_create(struct iif_fence *fence)
{
	struct iif_sync_file *sync_file;
	int ret;

	sync_file = kzalloc(sizeof(*sync_file), GFP_KERNEL);
	if (!sync_file)
		return ERR_PTR(-ENOMEM);

	sync_file->file = anon_inode_getfile("iif_file", &iif_sync_file_fops, sync_file, 0);
	if (IS_ERR(sync_file->file)) {
		ret = PTR_ERR(sync_file->file);
		goto err_free_sync_file;
	}

	sync_file->fence = iif_fence_get(fence);
	atomic_inc(&fence->num_sync_file);

	init_waitqueue_head(&sync_file->wq);
	INIT_LIST_HEAD(&sync_file->poll_cb.node);
	sync_file->last_poll_timeline = 0;

	return sync_file;

err_free_sync_file:
	kfree(sync_file);
	return ERR_PTR(ret);
}

struct iif_sync_file *iif_sync_file_fdget(int fd)
{
	struct file *file = fget(fd);

	if (!file)
		return ERR_PTR(-EBADF);

	if (file->f_op != &iif_sync_file_fops) {
		fput(file);
		return ERR_PTR(-EINVAL);
	}

	return file->private_data;
}
