// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual Inference Interface, implements the protocol between AP kernel and TPU firmware.
 *
 * Copyright (C) 2023 Google LLC
 */

#include <linux/kthread.h>
#include <linux/slab.h>

#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-mailbox.h>

#include "edgetpu-ikv.h"
#include "edgetpu-ikv-mailbox-ops.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-vii-packet.h"
#include "edgetpu.h"

static unsigned int user_ikv_timeout;
module_param(user_ikv_timeout, uint, 0660);

/* size of queue for in-kernel VII mailbox */
#define QUEUE_SIZE CIRC_QUEUE_MAX_SIZE(CIRC_QUEUE_WRAP_BIT)

static void edgetpu_ikv_handle_irq(struct edgetpu_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = mailbox->internal.etikv;

	/*
	 * Process responses directly, to avoid the latency from scheduling a worker thread.
	 *
	 * Since the in-kernel VII `acquire_resp_queue_lock` op sets @atomic to true, the response
	 * processing function will be safe to call in an IRQ context.
	 */
	/* TODO(b/312098074) Rename this function to indicate it is not only called by workers */
	gcip_mailbox_consume_responses_work(ikv->mbx_protocol);
}

static int edgetpu_ikv_alloc_queue(struct edgetpu_ikv *etikv, enum gcip_mailbox_queue_type type)
{
	struct edgetpu_dev *etdev = etikv->etdev;
	u32 size;
	struct edgetpu_coherent_mem *mem;
	int ret;

	switch (type) {
	case GCIP_MAILBOX_CMD_QUEUE:
		size = QUEUE_SIZE * sizeof(struct edgetpu_vii_command);
		mem = &etikv->cmd_queue_mem;
		break;
	case GCIP_MAILBOX_RESP_QUEUE:
		size = QUEUE_SIZE * sizeof(struct edgetpu_vii_response);
		mem = &etikv->resp_queue_mem;
		break;
	}

	/*
	 * in-kernel VII is kernel-to-firmware communication, so its queues are allocated in the
	 * same context as KCI, despite being a separate protocol.
	 */
	ret = edgetpu_iremap_alloc(etdev, size,  mem);
	if (ret)
		return ret;

	ret = edgetpu_mailbox_set_queue(etikv->mbx_hardware, type, mem->dma_addr, QUEUE_SIZE);
	if (ret) {
		etdev_err(etikv->etdev, "failed to set mailbox queue: %d", ret);
		edgetpu_iremap_free(etdev, mem);
		return ret;
	}

	return 0;
}

static void edgetpu_ikv_free_queue(struct edgetpu_ikv *etikv, enum gcip_mailbox_queue_type type)
{
	struct edgetpu_dev *etdev = etikv->etdev;

	switch (type) {
	case GCIP_MAILBOX_CMD_QUEUE:
		edgetpu_iremap_free(etdev, &etikv->cmd_queue_mem);
		break;
	case GCIP_MAILBOX_RESP_QUEUE:
		edgetpu_iremap_free(etdev, &etikv->resp_queue_mem);
		break;
	}
}

int edgetpu_ikv_init(struct edgetpu_mailbox_manager *mgr, struct edgetpu_ikv *etikv)
{
	struct edgetpu_mailbox *mbx_hardware;
	const unsigned int timeout = user_ikv_timeout ? user_ikv_timeout : IKV_TIMEOUT;
	struct gcip_mailbox_args args = {
		.dev = mgr->etdev->dev,
		.queue_wrap_bit = CIRC_QUEUE_WRAP_BIT,
		.cmd_elem_size = edgetpu_vii_command_packet_size(mgr->etdev),
		.resp_elem_size = edgetpu_vii_response_packet_size(mgr->etdev),
		.timeout = timeout,
		.ops = &ikv_mailbox_ops,
		.data = etikv,
	};
	int ret;

	etikv->command_timeout_ms = timeout;
	etikv->etdev = mgr->etdev;
	etikv->enabled = mgr->use_ikv;
	if (!etikv->enabled)
		return 0;

	mbx_hardware = edgetpu_mailbox_ikv(mgr);
	if (IS_ERR_OR_NULL(mbx_hardware))
		return !mbx_hardware ? -ENODEV : PTR_ERR(mbx_hardware);
	mbx_hardware->handle_irq = edgetpu_ikv_handle_irq;
	mbx_hardware->internal.etikv = etikv;
	etikv->mbx_hardware = mbx_hardware;

	etikv->mbx_protocol =
		devm_kzalloc(mgr->etdev->dev, sizeof(*etikv->mbx_protocol), GFP_KERNEL);
	if (!etikv->mbx_protocol) {
		ret = -ENOMEM;
		goto err_mailbox_remove;
	}

	ret = edgetpu_ikv_alloc_queue(etikv, GCIP_MAILBOX_CMD_QUEUE);
	if (ret)
		goto err_mailbox_remove;
	mutex_init(&etikv->cmd_queue_lock);

	ret = edgetpu_ikv_alloc_queue(etikv, GCIP_MAILBOX_RESP_QUEUE);
	if (ret)
		goto err_free_cmd_queue;
	spin_lock_init(&etikv->resp_queue_lock);

	args.cmd_queue = etikv->cmd_queue_mem.vaddr;
	args.resp_queue = etikv->resp_queue_mem.vaddr;
	ret = gcip_mailbox_init(etikv->mbx_protocol, &args);
	if (ret)
		goto err_free_resp_queue;

	init_waitqueue_head(&etikv->pending_commands);
	spin_lock_init(&etikv->wait_list_lock);

	edgetpu_mailbox_enable(mbx_hardware);

	return 0;

err_free_resp_queue:
	edgetpu_ikv_free_queue(etikv, GCIP_MAILBOX_RESP_QUEUE);
err_free_cmd_queue:
	edgetpu_ikv_free_queue(etikv, GCIP_MAILBOX_CMD_QUEUE);
err_mailbox_remove:
	edgetpu_mailbox_remove(mgr, mbx_hardware);
	etikv->mbx_hardware = NULL;

	return ret;
}

int edgetpu_ikv_reinit(struct edgetpu_ikv *etikv)
{
	struct edgetpu_mailbox *mbx_hardware = etikv->mbx_hardware;
	struct edgetpu_mailbox_manager *mgr;
	struct edgetpu_coherent_mem *cmd_queue_mem = &etikv->cmd_queue_mem;
	struct edgetpu_coherent_mem *resp_queue_mem = &etikv->resp_queue_mem;
	unsigned long flags;
	int ret;

	/*
	 * If in-kernel VII is enabled, mailbox hardware is guaranteed to be present, otherwise if
	 * not enabled, there's nothing to re-initialize.
	 */
	if (!etikv->enabled)
		return 0;

	ret = edgetpu_mailbox_set_queue(mbx_hardware, GCIP_MAILBOX_CMD_QUEUE,
					cmd_queue_mem->dma_addr, QUEUE_SIZE);
	if (ret)
		return ret;

	ret = edgetpu_mailbox_set_queue(mbx_hardware, GCIP_MAILBOX_RESP_QUEUE,
					resp_queue_mem->dma_addr, QUEUE_SIZE);
	if (ret)
		return ret;

	mgr = etikv->etdev->mailbox_manager;
	/* Restore irq handler */
	write_lock_irqsave(&mgr->mailboxes_lock, flags);
	mbx_hardware->handle_irq = edgetpu_ikv_handle_irq;
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);

	edgetpu_mailbox_init_doorbells(mbx_hardware);
	edgetpu_mailbox_enable(mbx_hardware);

	return 0;
}

void edgetpu_ikv_release(struct edgetpu_dev *etdev, struct edgetpu_ikv *etikv)
{
	struct edgetpu_mailbox_manager *mgr;
	struct edgetpu_mailbox *mbx_hardware;
	unsigned long flags;

	if (!etikv || !etikv->enabled)
		return;

	mbx_hardware = etikv->mbx_hardware;
	if (mbx_hardware) {
		mgr = etikv->etdev->mailbox_manager;
		/* Remove IRQ handler to stop responding to interrupts */
		write_lock_irqsave(&mgr->mailboxes_lock, flags);
		mbx_hardware->handle_irq = NULL;
		write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
	}

	gcip_mailbox_release(etikv->mbx_protocol);
	etikv->mbx_hardware = NULL;

	edgetpu_ikv_free_queue(etikv, GCIP_MAILBOX_CMD_QUEUE);
	edgetpu_ikv_free_queue(etikv, GCIP_MAILBOX_RESP_QUEUE);
}

struct send_cmd_args {
	struct edgetpu_ikv *etikv;
	struct edgetpu_ikv_response *ikv_resp;
	struct list_head *pending_queue;
	spinlock_t *pending_queue_lock;
	struct dma_fence *fence;
	struct gcip_mailbox_resp_awaiter *err_resp_awaiter;
	void *cmd;
};

static int do_send_cmd(struct send_cmd_args *args) {
	struct edgetpu_ikv *etikv = args->etikv;
	void *cmd = args->cmd;
	struct edgetpu_ikv_response *ikv_resp = args->ikv_resp;
	struct list_head *pending_queue = args->pending_queue;
	spinlock_t *pending_queue_lock = args->pending_queue_lock;
	unsigned long flags;
	struct gcip_mailbox_resp_awaiter *awaiter;
	int ret;

	spin_lock_irqsave(pending_queue_lock, flags);
	list_add_tail(&ikv_resp->list_entry, pending_queue);
	spin_unlock_irqrestore(pending_queue_lock, flags);

	awaiter = gcip_mailbox_put_cmd(etikv->mbx_protocol, cmd, ikv_resp->resp, ikv_resp);
	if (IS_ERR(awaiter)) {
		ret = PTR_ERR(awaiter);
		goto err;
	}

	return 0;

err:
	spin_lock_irqsave(pending_queue_lock, flags);
	list_del(&ikv_resp->list_entry);
	spin_unlock_irqrestore(pending_queue_lock, flags);
	return ret;
}

static inline void build_awaiter_for_error_resp(struct edgetpu_ikv *etikv,
						struct gcip_mailbox_resp_awaiter *awaiter,
						struct edgetpu_ikv_response *ikv_resp)
{
	awaiter->async_resp.resp = &ikv_resp->resp;
	awaiter->mailbox = etikv->mbx_protocol;
	awaiter->data = ikv_resp;
	awaiter->release_data = etikv->mbx_protocol->ops->release_awaiter_data;
	refcount_set(&awaiter->refs, 1);
	ikv_resp->awaiter = awaiter;
}

/* TODO(b/274528886) Finalize timeout value. Set to 10 seconds for now. */
#define VII_IN_FENCE_TIMEOUT_MS 10000

static int send_cmd_thread_fn(void *data)
{
	struct send_cmd_args *args = (struct send_cmd_args *)data;
	/* Save a pointer to the group so it can untrack this task, even if ikv_resp is freed. */
	struct edgetpu_device_group *group_to_notify = args->ikv_resp->group_to_notify;
	int ret, fence_status;
	u16 resp_code;
	u64 resp_data;

	ret = dma_fence_wait_timeout(args->fence, true, msecs_to_jiffies(VII_IN_FENCE_TIMEOUT_MS));
	fence_status = dma_fence_get_status(args->fence);
	dma_fence_put(args->fence);

	/* If the wait was interrupted to kill the thread, then the command is abandoned. */
	if (kthread_should_stop()) {
		/* The command will never be sent at this point so its response must be released. */
		kfree(args->err_resp_awaiter);
		gcip_fence_array_put(args->ikv_resp->out_fence_array);
		gcip_fence_array_put(args->ikv_resp->in_fence_array);
		edgetpu_ikv_additional_info_free(args->ikv_resp->etikv->etdev,
						 &args->ikv_resp->additional_info);
		if (args->ikv_resp->release_callback)
			args->ikv_resp->release_callback(args->ikv_resp->release_data);
		kfree(args->ikv_resp->resp);
		kfree(args->ikv_resp);
		goto out_free_args;
	}

	/* If the wait ended due to a timeout or fence error, enqueue an error response. */
	if (!ret || fence_status < 0) {
		etdev_err(
			args->etikv->etdev,
			"Waiting for client_id=%u's command in-fence failed (ret=%d fence_status=%d)",
			edgetpu_vii_command_get_client_id(args->etikv->etdev, args->cmd), ret,
			fence_status);
		if (!ret) {
			resp_code = VII_RESPONSE_CODE_KERNEL_FENCE_TIMEOUT;
			resp_data = VII_IN_FENCE_TIMEOUT_MS;
			fence_status = -ETIMEDOUT;
		} else {
			resp_code = VII_RESPONSE_CODE_KERNEL_FENCE_ERROR;
			resp_data = (u64)fence_status;
			/* Do not override fence_status, let the error propagate. */
		}
		goto err_send_error_resp;
	}

	ret = do_send_cmd(args);
	if (ret) {
		etdev_err(args->etikv->etdev,
			  "Failed to send command in fence thread for client_id=%u (ret=%d)",
			  edgetpu_vii_command_get_client_id(args->etikv->etdev, args->cmd), ret);
		resp_code = VII_RESPONSE_CODE_KERNEL_ENQUEUE_FAILED;
		resp_data = (u64)ret;
		fence_status = -ECANCELED;
		goto err_send_error_resp;
	}

	/*
	 * The command has been enqueued and has a proper response awaiter now. Free the
	 * pre-allocated awaiter now that it's certain it won't be used.
	 */
	kfree(args->err_resp_awaiter);
	goto out_untrack;

err_send_error_resp:
	/*
	 * Now that the pre-allocated awaiter is being used for the error response, it will be
	 * freed when the response itself is released.
	 */
	build_awaiter_for_error_resp(args->etikv, args->err_resp_awaiter, args->ikv_resp);
	edgetpu_ikv_process_response(args->ikv_resp, &resp_code, &resp_data, fence_status);

out_untrack:
	edgetpu_device_group_untrack_fence_task(group_to_notify, current);

out_free_args:
	kfree(args->cmd);
	kfree(args);
	/*
	 * This is the return status of the thread, and indicates that the thread is exiting
	 * cleanly, not that there were no errors encountered.
	 *
	 * Any errors have been communicated via a VII error response.
	 */
	return 0;
}

int edgetpu_ikv_send_cmd(struct edgetpu_ikv *etikv, void *cmd, struct list_head *pending_queue,
			 struct list_head *ready_queue, spinlock_t *queue_lock,
			 struct edgetpu_device_group *group_to_notify,
			 struct gcip_fence_array *in_fence_array,
			 struct gcip_fence_array *out_fence_array,
			 struct edgetpu_ikv_additional_info *additional_info,
			 void (*release_callback)(void *), void *release_data)
{
	struct edgetpu_ikv_response *ikv_resp;
	struct send_cmd_args *args;
	dma_addr_t additional_info_daddr = 0;
	ssize_t additional_info_size = 0;
	int ret;
	struct task_struct *wait_task;
	struct dma_fence *in_fence;
	int fence_status;
	u16 resp_code;
	u64 resp_data;

	if (!etikv->enabled)
		return -ENODEV;

	in_fence = gcip_fence_array_merge_ikf(in_fence_array);
	if (IS_ERR(in_fence)) {
		ret = PTR_ERR(in_fence);
		etdev_err(etikv->etdev, "Failed to merge in-kernel fences, ret=%d", ret);
		return ret;
	}

	if (in_fence)
		dma_fence_enable_sw_signaling(in_fence);

	if (in_fence && !group_to_notify) {
		etdev_err(etikv->etdev,
			  "Cannot send a command with an in-fence without an owning device_group");
		ret = -EINVAL;
		goto err_put_in_fence;
	}

	ikv_resp = kzalloc(sizeof(*ikv_resp), GFP_KERNEL);
	if (!ikv_resp) {
		ret = -ENOMEM;
		goto err_put_in_fence;
	}

	ikv_resp->resp = kzalloc(edgetpu_vii_response_packet_size(etikv->etdev), GFP_KERNEL);
	if (!ikv_resp->resp) {
		ret = -ENOMEM;
		goto err_free_ikv_resp;
	}

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args) {
		ret = -ENOMEM;
		goto err_free_ikv_resp_resp;
	}

	args->cmd = kzalloc(edgetpu_vii_command_packet_size(etikv->etdev), GFP_KERNEL);
	if (!args->cmd) {
		ret = -ENOMEM;
		goto err_free_args;
	}

	if (additional_info) {
		additional_info_size = edgetpu_ikv_additional_info_alloc_and_copy(
			etikv->etdev, additional_info, &ikv_resp->additional_info);
		if (additional_info_size < 0) {
			ret = additional_info_size;
			goto err_free_args_cmd;
		}
		additional_info_daddr = ikv_resp->additional_info.dma_addr;
	}

	edgetpu_vii_command_set_additional_info(etikv->etdev, cmd, additional_info_daddr,
						additional_info_size);

	ikv_resp->etikv = etikv;
	ikv_resp->dest_queue = ready_queue;
	ikv_resp->dest_queue_lock = queue_lock;
	ikv_resp->processed = false;
	ikv_resp->client_seq = edgetpu_vii_command_get_seq_number(etikv->etdev, cmd);
	ikv_resp->group_to_notify = group_to_notify;
	ikv_resp->in_fence_array = gcip_fence_array_get(in_fence_array);
	ikv_resp->out_fence_array = gcip_fence_array_get(out_fence_array);
	ikv_resp->release_callback = release_callback;
	ikv_resp->release_data = release_data;

	args->etikv = etikv;
	args->ikv_resp = ikv_resp;
	args->pending_queue = pending_queue;
	args->pending_queue_lock = queue_lock;
	args->fence = in_fence;
	memcpy(args->cmd, cmd, edgetpu_vii_command_packet_size(etikv->etdev));

	/* Send the command immediately if there's no fence to wait on. */
	if (!in_fence || dma_fence_get_status(in_fence) == 1) {
		ret = do_send_cmd(args);
		if (ret)
			goto err_put_out_fence_array;
		/* If the command was successfully sent, args is no longer needed. */
		if (in_fence)
			dma_fence_put(in_fence);
		kfree(args->cmd);
		kfree(args);
		return 0;
	}

	/*
	 * Pre-allocate a gcip_mailbox_resp_awaiter to be used for an error response if the command
	 * fails to send for any reason.
	 *
	 * If this function returns success, then a response must always eventually be placed in
	 * @ready_queue. If the awaiter is not allocated here, then an allocation failure in
	 * `send_cmd_thread_fn()` could cause the command to be dropped with no feedback for the
	 * client.
	 */
	args->err_resp_awaiter = kzalloc(sizeof(*args->err_resp_awaiter), GFP_KERNEL);
	if (!args->err_resp_awaiter) {
		ret = -ENOMEM;
		goto err_put_out_fence_array;
	}

	fence_status = in_fence ? dma_fence_get_status(in_fence) : 0;
	if (fence_status < 0) {
		/*
		 * If the in-fence has an error status, the command must not be sent.
		 * Instead enqueue a VII error response which indicates the command failed to send.
		 *
		 * Since the `ikv_resp` is being used to track this error response, cleanup is tied
		 * to the life of the `ikv_resp` and doesn't have to happen here.
		 */
		resp_code = VII_RESPONSE_CODE_KERNEL_FENCE_ERROR;
		resp_data = fence_status;
		build_awaiter_for_error_resp(etikv, args->err_resp_awaiter, ikv_resp);
		edgetpu_ikv_process_response(ikv_resp, &resp_code, &resp_data, fence_status);
		if (in_fence)
			dma_fence_put(in_fence);
		kfree(args->cmd);
		kfree(args);
		return 0;
	}

	wait_task = kthread_create(send_cmd_thread_fn, args,
				   "edgetpu_ikv_send_cmd_client%u_seq%llu",
				   edgetpu_vii_command_get_client_id(etikv->etdev, cmd),
				   edgetpu_vii_command_get_seq_number(etikv->etdev, cmd));
	if (IS_ERR(wait_task)) {
		ret = PTR_ERR(wait_task);
		goto err_free_awaiter;
	}

	ret = edgetpu_device_group_track_fence_task(args->ikv_resp->group_to_notify, wait_task);
	if (ret)
		goto err_stop_thread;

	wake_up_process(wait_task);

	return 0;

err_stop_thread:
	kthread_stop(wait_task);
err_free_awaiter:
	kfree(args->err_resp_awaiter);
err_put_out_fence_array:
	gcip_fence_array_put(out_fence_array);
	gcip_fence_array_put(in_fence_array);
	edgetpu_ikv_additional_info_free(etikv->etdev, &ikv_resp->additional_info);
err_free_args_cmd:
	kfree(args->cmd);
err_free_args:
	kfree(args);
err_free_ikv_resp_resp:
	kfree(ikv_resp->resp);
err_free_ikv_resp:
	kfree(ikv_resp);
err_put_in_fence:
	if (in_fence)
		dma_fence_put(in_fence);
	return ret;
}

void edgetpu_ikv_flush_responses(struct edgetpu_ikv *etikv)
{
	gcip_mailbox_consume_responses(etikv->mbx_protocol);
}
