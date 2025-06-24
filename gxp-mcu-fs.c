// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common file system operations for devices with MCU support.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/bits.h>
#include <linux/dma-fence-array.h>
#include <linux/fs.h>
#include <linux/mm_types.h>
#include <linux/rwsem.h>
#include <linux/slab.h>

#include <gcip/gcip-dma-fence.h>
#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-fence.h>
#include <gcip/gcip-telemetry.h>

#include <trace/events/gxp.h>

#include "gxp-client.h"
#include "gxp-internal.h"
#include "gxp-mcu-fs.h"
#include "gxp-mcu-telemetry.h"
#include "gxp-mcu.h"
#include "gxp-uci.h"
#include "gxp.h"

static int gxp_ioctl_uci_command_compat(struct gxp_client *client,
					struct gxp_mailbox_uci_command_compat_ioctl __user *argp)
{
	struct gxp_mailbox_uci_command_compat_ioctl ibuf;
	struct gxp_dev *gxp = client->gxp;
	struct gxp_mcu *mcu = gxp_mcu_of(gxp);
	u64 cmd_seq;
	int ret;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	cmd_seq = gcip_mailbox_inc_seq_num(mcu->uci.mbx->mbx_impl.gcip_mbx, 1);

	ret = gxp_uci_create_and_send_cmd(client, cmd_seq, 0, ibuf.opaque, 0, NULL, NULL);
	if (ret) {
		dev_err(gxp->dev, "Failed to request an UCI command (ret=%d)", ret);
		return ret;
	}

	ibuf.sequence_number = cmd_seq;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

/*
 * Returns the number of fences. If the runtime passed an invalid fence, returns an errno
 * accordingly.
 */
static int get_num_fences(const int *fences)
{
	int i;
	struct gcip_fence *fence;

	for (i = 0; i < GXP_MAX_FENCES_PER_UCI_COMMAND; i++) {
		if (fences[i] == GXP_FENCE_ARRAY_TERMINATION)
			break;
		fence = gcip_fence_fdget(fences[i]);
		/*
		 * TODO(b/312819593): once the runtime adopts `GXP_FENCE_ARRAY_TERMINATION` to
		 * indicate the end of array, always returns PTR_ERR(fence).
		 */
		if (IS_ERR(fence))
			return !fences[i] ? 0 : PTR_ERR(fence);
		gcip_fence_put(fence);
	}

	return i;
}

static int gxp_ioctl_uci_command(struct gxp_client *client,
				 struct gxp_mailbox_uci_command_ioctl __user *argp)
{
	struct gxp_mcu *mcu = gxp_mcu_of(client->gxp);
	struct gxp_mailbox_uci_command_ioctl ibuf;
	struct gcip_fence_array *in_fences, *out_fences;
	struct dma_fence *polled_dma_fence;
	u64 cmd_seq;
	int ret, num_in_fences, num_out_fences;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	cmd_seq = gcip_mailbox_inc_seq_num(mcu->uci.mbx->mbx_impl.gcip_mbx, 1);

	trace_gxp_uci_cmd_start(cmd_seq);

	num_in_fences = get_num_fences(ibuf.in_fences);
	if (num_in_fences < 0)
		return num_in_fences;

	num_out_fences = get_num_fences(ibuf.out_fences);
	if (num_out_fences < 0)
		return num_out_fences;

	in_fences = gcip_fence_array_create(ibuf.in_fences, num_in_fences, true);
	if (IS_ERR(in_fences))
		return PTR_ERR(in_fences);

	out_fences = gcip_fence_array_create(ibuf.out_fences, num_out_fences, false);
	if (IS_ERR(out_fences)) {
		gcip_fence_array_put(in_fences);
		return PTR_ERR(out_fences);
	}

	/* @polled_dma_fence can be NULL if @in_fences are not DMA fences or there are no fences. */
	polled_dma_fence = gcip_fence_array_merge_ikf(in_fences);
	if (IS_ERR(polled_dma_fence)) {
		ret = PTR_ERR(polled_dma_fence);
		goto out;
	}

	ret = gxp_uci_cmd_work_create_and_schedule(polled_dma_fence, client, &ibuf, cmd_seq,
						   in_fences, out_fences);
	if (ret) {
		dev_err(client->gxp->dev, "Failed to request an UCI command (ret=%d)", ret);
		goto err_put_fence;
	}

	ibuf.sequence_number = cmd_seq;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		ret = -EFAULT;

err_put_fence:
	/*
	 * Put the reference count of the fence acqurired in gcip_fence_array_merge_ikf.
	 * If the fence is a dma_fence_array and the callback is failed to be added,
	 * the whole object and the array it holds will be freed.
	 * If it is a NULL pointer, it's still safe to call this function.
	 */
	dma_fence_put(polled_dma_fence);
out:
	gcip_fence_array_put(out_fences);
	gcip_fence_array_put(in_fences);

	trace_gxp_uci_cmd_end(cmd_seq);

	return ret;
}

static int
gxp_ioctl_uci_response(struct gxp_client *client,
		       struct gxp_mailbox_uci_response_ioctl __user *argp)
{
	struct gxp_mailbox_uci_response_ioctl ibuf;
	int ret = 0;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	down_read(&client->semaphore);

	if (!client->vd) {
		dev_err(client->gxp->dev,
			"GXP_MAILBOX_UCI_RESPONSE requires the client allocate a VIRTUAL_DEVICE\n");
		ret = -ENODEV;
		goto out;
	}

	/* Caller must hold BLOCK wakelock */
	if (!client->has_block_wakelock) {
		dev_err(client->gxp->dev,
			"GXP_MAILBOX_UCI_RESPONSE requires the client hold a BLOCK wakelock\n");
		ret = -ENODEV;
		goto out;
	}

	ret = gxp_uci_wait_async_response(
		&client->vd->mailbox_resp_queues[UCI_RESOURCE_ID],
		&ibuf.sequence_number, &ibuf.error_code, ibuf.opaque);
	if (unlikely(ret == -ETIMEDOUT))
		goto out;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		ret = -EFAULT;

out:
	up_read(&client->semaphore);

	return ret;
}

static int gxp_ioctl_set_device_properties(
	struct gxp_dev *gxp,
	struct gxp_set_device_properties_ioctl __user *argp)
{
	struct gxp_dev_prop *device_prop = &gxp->device_prop;
	struct gxp_set_device_properties_ioctl ibuf;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	mutex_lock(&device_prop->lock);

	memcpy(&device_prop->opaque, &ibuf.opaque, sizeof(device_prop->opaque));
	device_prop->initialized = true;

	mutex_unlock(&device_prop->lock);

	return 0;
}

static int gxp_ioctl_create_iif_fence(struct gxp_client *client,
				      struct gxp_create_iif_fence_ioctl __user *argp)
{
	struct gxp_create_iif_fence_ioctl ibuf;
	int fd;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	fd = gcip_fence_create_iif(client->gxp->iif_mgr, ibuf.signaler_ip, ibuf.total_signalers);
	if (fd < 0)
		return fd;

	ibuf.fence = fd;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static int
gxp_ioctl_fence_remaining_signalers(struct gxp_client *client,
				    struct gxp_fence_remaining_signalers_ioctl __user *argp)
{
	struct gxp_fence_remaining_signalers_ioctl ibuf;
	struct gcip_fence_array *fences;
	int ret, num_fences;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	num_fences = get_num_fences(ibuf.fences);
	if (num_fences < 0)
		return num_fences;

	fences = gcip_fence_array_create(ibuf.fences, num_fences, true);
	if (IS_ERR(fences))
		return PTR_ERR(fences);

	ret = gcip_fence_array_wait_signaler_submission(fences, ibuf.eventfd,
							ibuf.remaining_signalers);
	if (ret)
		goto out;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		ret = -EFAULT;

out:
	gcip_fence_array_put(fences);
	return ret;
}

static inline enum gcip_telemetry_type to_gcip_telemetry_type(u8 type)
{
	if (type == GXP_TELEMETRY_TYPE_LOGGING)
		return GCIP_TELEMETRY_LOG;
	else
		return GCIP_TELEMETRY_TRACE;
}

static int
gxp_ioctl_register_mcu_telemetry_eventfd(struct gxp_client *client,
					 struct gxp_register_telemetry_eventfd_ioctl __user *argp)
{
	struct gxp_mcu *mcu = gxp_mcu_of(client->gxp);
	struct gxp_register_telemetry_eventfd_ioctl ibuf;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	return gxp_mcu_telemetry_register_eventfd(
		mcu, to_gcip_telemetry_type(ibuf.type), ibuf.eventfd);
}

static int
gxp_ioctl_unregister_mcu_telemetry_eventfd(struct gxp_client *client,
					   struct gxp_register_telemetry_eventfd_ioctl __user *argp)
{
	struct gxp_mcu *mcu = gxp_mcu_of(client->gxp);
	struct gxp_register_telemetry_eventfd_ioctl ibuf;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	return gxp_mcu_telemetry_unregister_eventfd(
		mcu, to_gcip_telemetry_type(ibuf.type));
}

long gxp_mcu_ioctl(struct file *file, uint cmd, ulong arg)
{
	struct gxp_client *client = file->private_data;
	void __user *argp = (void __user *)arg;
	long ret;

	if (gxp_is_direct_mode(client->gxp))
		return -ENOTTY;
	switch (cmd) {
	case GXP_MAILBOX_COMMAND:
		ret = -EOPNOTSUPP;
		break;
	case GXP_MAILBOX_RESPONSE:
		ret = -EOPNOTSUPP;
		break;
	case GXP_REGISTER_MCU_TELEMETRY_EVENTFD:
		ret = gxp_ioctl_register_mcu_telemetry_eventfd(client, argp);
		break;
	case GXP_UNREGISTER_MCU_TELEMETRY_EVENTFD:
		ret = gxp_ioctl_unregister_mcu_telemetry_eventfd(client, argp);
		break;
	case GXP_MAILBOX_UCI_COMMAND_COMPAT:
		ret = gxp_ioctl_uci_command_compat(client, argp);
		break;
	case GXP_MAILBOX_UCI_COMMAND:
		ret = gxp_ioctl_uci_command(client, argp);
		break;
	case GXP_MAILBOX_UCI_RESPONSE:
		ret = gxp_ioctl_uci_response(client, argp);
		break;
	case GXP_SET_DEVICE_PROPERTIES:
		ret = gxp_ioctl_set_device_properties(client->gxp, argp);
		break;
	case GXP_CREATE_IIF_FENCE:
		ret = gxp_ioctl_create_iif_fence(client, argp);
		break;
	case GXP_FENCE_REMAINING_SIGNALERS:
		ret = gxp_ioctl_fence_remaining_signalers(client, argp);
		break;
	default:
		ret = -ENOTTY; /* unknown command */
	}

	return ret;
}

int gxp_mcu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gxp_client *client = file->private_data;
	struct gxp_mcu *mcu = gxp_mcu_of(client->gxp);
	int ret;

	if (gxp_is_direct_mode(client->gxp))
		return -EOPNOTSUPP;

	switch (vma->vm_pgoff << PAGE_SHIFT) {
	case GXP_MMAP_MCU_LOG_BUFFER_OFFSET:
		ret = gxp_mcu_telemetry_mmap_buffer(mcu, GCIP_TELEMETRY_LOG,
						    vma);
		break;
	case GXP_MMAP_MCU_TRACE_BUFFER_OFFSET:
		ret = gxp_mcu_telemetry_mmap_buffer(mcu, GCIP_TELEMETRY_TRACE,
						    vma);
		break;
	default:
		ret = -EOPNOTSUPP; /* unknown offset */
	}

	return ret;
}
