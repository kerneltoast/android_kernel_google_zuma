// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP user command interface.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <uapi/linux/sched/types.h>

#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-fence.h>
#include <iif/iif-shared.h>

#include <trace/events/gxp.h>

#include "gxp-config.h"
#include "gxp-internal.h"
#include "gxp-mailbox-driver.h"
#include "gxp-mailbox.h"
#include "gxp-mcu.h"
#include "gxp-uci.h"
#include "gxp-vd.h"
#include "gxp.h"

#if IS_GXP_TEST
#include "unittests/factory/fake-gxp-mcu-firmware.h"

#define TEST_FLUSH_FIRMWARE_WORK() fake_gxp_mcu_firmware_flush_work_all()
#else
#define TEST_FLUSH_FIRMWARE_WORK()
#endif

#define MBOX_CMD_QUEUE_NUM_ENTRIES 1024
#define MBOX_RESP_QUEUE_NUM_ENTRIES 1024

#define ADDITIONAL_INFO_ALIGN SZ_16
/*
 * As the firmware side will use the same length of the per-cmd timeout, we should give a margin to
 * the kernel-side mailbox to prevent the corner case of the firmware returning a response right
 * after the timeout.
 */
#define PER_CMD_TIMEOUT_MARGIN_MS 1000

#define GXP_UCI_NULL_COMMAND_FLAG BIT(0)

static int gxp_uci_mailbox_manager_execute_cmd(
	struct gxp_client *client, struct gxp_mailbox *mailbox, int virt_core,
	u16 cmd_code, u8 cmd_priority, u64 cmd_daddr, u32 cmd_size,
	u32 cmd_flags, u8 num_cores, struct gxp_power_states power_states,
	u64 *resp_seq, u16 *resp_status)
{
	struct gxp_dev *gxp = client->gxp;
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	struct gxp_virtual_device *vd = client->vd;
	struct gxp_uci_command cmd = {};
	struct gxp_uci_response resp;
	int ret;

	if (gxp_is_direct_mode(gxp))
		return -EOPNOTSUPP;

	if (!gxp_vd_has_and_use_credit(vd))
		return -EBUSY;

	cmd.type = CORE_COMMAND;
	cmd.client_id = vd->client_id;

	/*
	 * Before the response returns, we must prevent unloading the MCU firmware even by
	 * the firmware crash handler. Otherwise, invalid IOMMU access can occur.
	 */
	mutex_lock(&mcu_fw->lock);
	ret = gxp_mailbox_send_cmd(mailbox, &cmd, &resp, 0);
	mutex_unlock(&mcu_fw->lock);

	/* resp.seq and resp.status can be updated even though it failed to process the command */
	if (resp_seq)
		*resp_seq = resp.seq;
	if (resp_status)
		*resp_status = resp.code;

	gxp_vd_release_credit(vd);

	return ret;
}

/*
 * Flushes pending UCI commands or unconsumed responses.
 *
 * This function shouldn't be called while holding @gxp->vd_semaphore. If there are commands to be
 * flushed, it will close all out-IIFs of them and the IIF driver will notify waiter IP drivers
 * including the DSP driver itself if it was waiting on them. That will eventually trigger the
 * `gxp_uci_send_iif_unblock_noti` function and the function will try to hold @pm->lock.
 *
 * However, there are some cases which hold @pm->lock first and then hold @gxp->vd_semaphore like:
 * `gxp_pm_put`
 *  --> Hold @pm->lock
 *  --> `gxp_mcu_firmware_stop`
 *  --> `gxp_kci_cancel_work_queues`
 *  --> `cancel_work_sync(&kci->rkci.work)`
 *  --> `gxp_reverse_kci_handle_response`
 *  --> `gxp_kci_handle_rkci`
 *  --> `gxp_vd_invalidate_with_client_id`
 *  --> Hold @gxp->vd_semaphore
 *
 * It will cause a deadlock if this function can be called while holding @gxp->vd_semaphore.
 */
static void gxp_uci_mailbox_manager_release_unconsumed_async_resps(
	struct gxp_virtual_device *vd)
{
	struct gxp_uci_async_response *cur, *nxt;
	unsigned long flags;

	/*
	 * We should hold a lock to prevent removing WAKELOCK responses from the arrived callback
	 * while iterating @wait_queue.
	 */
	spin_lock_irqsave(&vd->mailbox_resp_queues[UCI_RESOURCE_ID].lock,
			  flags);

	/* Let arrived and timedout callbacks not to handle responses. */
	list_for_each_entry (
		cur, &vd->mailbox_resp_queues[UCI_RESOURCE_ID].wait_queue,
		wait_list_entry) {
		cur->processed = true;
	}
	vd->mailbox_resp_queues[UCI_RESOURCE_ID].wait_queue_closed = true;

	spin_unlock_irqrestore(&vd->mailbox_resp_queues[UCI_RESOURCE_ID].lock,
			       flags);

	/*
	 * From here it is guaranteed that @wait_queue will not be manipulated by the arrived,
	 * timedout callback or `gxp_uci_send_command`.
	 */

	/*
	 * Flush the work of fake firmware to simulate firing arrived or timedout callbacks in the
	 * middle of this function. If there is no work to be done, this is the same as NO-OP.
	 */
	TEST_FLUSH_FIRMWARE_WORK();

	/* Ensure no responses will be called by arrived or timedout handlers. */
	list_for_each_entry (
		cur, &vd->mailbox_resp_queues[UCI_RESOURCE_ID].wait_queue,
		wait_list_entry) {
		gcip_mailbox_cancel_awaiter(cur->awaiter);
		gcip_fence_array_signal(cur->out_fences, -ECANCELED);
		gcip_fence_array_waited(cur->in_fences, IIF_IP_DSP);
	}

	/*
	 * From here it is guaranteed that no responses will access @vd and be handled by arrived
	 * or timedout callbacks. Therefore, @dest_queue will not be changed anymore.
	 *
	 * We don't have to care about the `gxp_uci_wait_async_response` function is being called
	 * in the middle because the meaning of this function is called is that @vd is being
	 * released and the `gxp_uci_wait_async_response` function will never be called anymore.
	 */

	/*
	 * Clean up responses in the @dest_queue.
	 * Responses in this queue are arrived/timedout which means they are removed from the
	 * @wait_queue and put into the @dest_queue. However, the runtime hasn't consumed them via
	 * the `gxp_uci_wait_async_response` function yet. Therefore, we have to remove them from
	 * the queue and release their awaiter.
	 */
	list_for_each_entry_safe (
		cur, nxt, &vd->mailbox_resp_queues[UCI_RESOURCE_ID].dest_queue,
		dest_list_entry) {
		list_del(&cur->dest_list_entry);
		gcip_mailbox_release_awaiter(cur->awaiter);
	}

	/*
	 * Clean up responses in the @wait_queue.
	 * Responses in this queue are not arrived/timedout yet which means they are still in the
	 * @wait_queue and not put into the @dest_queue. Therefore, we have to remove them from the
	 * queue and release their awaiter.
	 */
	list_for_each_entry_safe (
		cur, nxt, &vd->mailbox_resp_queues[UCI_RESOURCE_ID].wait_queue,
		wait_list_entry) {
		list_del(&cur->wait_list_entry);
		gcip_mailbox_release_awaiter(cur->awaiter);
	}
}

static void gxp_uci_mailbox_manager_set_ops(struct gxp_mailbox_manager *mgr)
{
	/* This operator will be used only from the debugfs. */
	mgr->execute_cmd = gxp_uci_mailbox_manager_execute_cmd;
	/*
	 * Most mailbox manager operators are used by the `gxp-common-platform.c` when the device
	 * uses direct mode. The only one that should be implemented among them from the UCI is the
	 * `release_unconsumed_async_resps` operator which is used by the `gxp-vd.c` in both direct
	 * and MCU mode.
	 */
	mgr->release_unconsumed_async_resps =
		gxp_uci_mailbox_manager_release_unconsumed_async_resps;
}

static u64 gxp_uci_get_cmd_elem_seq(struct gcip_mailbox *mailbox, void *cmd)
{
	struct gxp_uci_command *elem = cmd;

	return elem->seq;
}

static u32 gxp_uci_get_cmd_elem_code(struct gcip_mailbox *mailbox, void *cmd)
{
	struct gxp_uci_command *elem = cmd;

	return (u32)elem->type;
}

static void gxp_uci_set_cmd_elem_seq(struct gcip_mailbox *mailbox, void *cmd,
				     u64 seq)
{
	struct gxp_uci_command *elem = cmd;

	elem->seq = seq;
}

static u64 gxp_uci_get_resp_elem_seq(struct gcip_mailbox *mailbox, void *resp)
{
	struct gxp_uci_response *elem = resp;

	return elem->seq;
}

static void gxp_uci_set_resp_elem_seq(struct gcip_mailbox *mailbox, void *resp,
				      u64 seq)
{
	struct gxp_uci_response *elem = resp;

	elem->seq = seq;
}

static int gxp_uci_before_enqueue_wait_list(struct gcip_mailbox *mailbox, void *resp,
					    struct gcip_mailbox_resp_awaiter *awaiter)
{
	struct gxp_uci_async_response *async_resp;
	struct mailbox_resp_queue *mailbox_resp_queue;
	unsigned long flags;

	if (!awaiter)
		return 0;

	async_resp = awaiter->data;
	async_resp->awaiter = awaiter;

	mailbox_resp_queue =
		container_of(async_resp->wait_queue, struct mailbox_resp_queue, wait_queue);

	spin_lock_irqsave(async_resp->queue_lock, flags);

	/*
	 * The client is pushing an UCI command and leaving at the same time which will unlikely
	 * happen.
	 */
	if (unlikely(mailbox_resp_queue->wait_queue_closed)) {
		dev_err(mailbox->dev, "The client is leaving while pushing a command");
		spin_unlock_irqrestore(async_resp->queue_lock, flags);
		return -EIO;
	}

	list_add_tail(&async_resp->wait_list_entry, async_resp->wait_queue);
	spin_unlock_irqrestore(async_resp->queue_lock, flags);

	return 0;
}

/*
 * Sets @async_resp->status to @status, removes @async_resp from the wait list, and pushes it to the
 * destination queue.
 *
 * If @force is true, push the response regardless of @async_resp->processed.
 */
static void gxp_uci_push_async_response(struct gxp_uci_async_response *async_resp,
					enum gxp_response_status status, bool force)
{
	unsigned long flags;
	int errno = 0;

	spin_lock_irqsave(async_resp->queue_lock, flags);

	/*
	 * This function has been called twice - it is possible since canceling the command and
	 * processing it by arrived or timedout handler can happen at the same time by the race.
	 */
	if (async_resp->processed && !force) {
		spin_unlock_irqrestore(async_resp->queue_lock, flags);
		return;
	}

	async_resp->status = status;
	async_resp->processed = true;
	list_del(&async_resp->wait_list_entry);

	gxp_vd_release_credit(async_resp->vd);
	list_add_tail(&async_resp->dest_list_entry, async_resp->dest_queue);
	spin_unlock_irqrestore(async_resp->queue_lock, flags);

	if (status == GXP_RESP_TIMEDOUT)
		errno = -ETIMEDOUT;
	else if (status == GXP_RESP_CANCELED)
		errno = -ECANCELED;

	if (errno && async_resp->out_fences) {
		/*
		 * If the command has been processed abnormally, we can assume that the firmware is
		 * not working properly and the kernel driver should take care of propagating the
		 * fence unblock.
		 *
		 * - -ETIMEDOUT: Theoretically, the firmware should have shorter timeout duration
		 *               than the kernel driver. The meaning that the kernel driver faced
		 *               the timeout error is that the firmware was not responding.
		 *
		 * - -ECANCELED: The command has ben canceled since the firmware has been crashed
		 *               and the command couldn't be processed normally.
		 */
		gcip_fence_array_iif_set_propagate_unblock(async_resp->out_fences);
	}

	if (status == GXP_RESP_OK && async_resp->resp.code) {
		/*
		 * The response has arrived from MCU, but with an error. The request itself or
		 * MCU/cores had a problem.
		 */
		errno = -EIO;
	}

	gcip_fence_array_signal_async(async_resp->out_fences, errno);
	gcip_fence_array_waited_async(async_resp->in_fences, IIF_IP_DSP);
	if (async_resp->eventfd)
		gxp_eventfd_signal(async_resp->eventfd);

	trace_gxp_uci_rsp_end(async_resp->resp.seq);

	wake_up(async_resp->dest_queue_waitq);
}

static void
gxp_uci_handle_awaiter_arrived(struct gcip_mailbox *mailbox,
			       struct gcip_mailbox_resp_awaiter *awaiter)
{
	struct gxp_uci_async_response *async_resp = awaiter->data;

	gxp_uci_push_async_response(async_resp, GXP_RESP_OK, false);
}

static void
gxp_uci_handle_awaiter_timedout(struct gcip_mailbox *mailbox,
				struct gcip_mailbox_resp_awaiter *awaiter)
{
	struct gxp_uci_async_response *async_resp = awaiter->data;

	gxp_uci_push_async_response(async_resp, GXP_RESP_TIMEDOUT, false);
}

static void gxp_uci_release_awaiter_data(void *data)
{
	struct gxp_uci_async_response *async_resp = data;

	/*
	 * This function might be called when the VD is already released, don't do VD operations in
	 * this case.
	 */
	gcip_fence_array_put_async(async_resp->out_fences);
	gcip_fence_array_put_async(async_resp->in_fences);
	if (async_resp->additional_info_buf.vaddr)
		gxp_mcu_mem_free_data(async_resp->uci->mcu, &async_resp->additional_info_buf);
	if (async_resp->eventfd)
		gxp_eventfd_put(async_resp->eventfd);
	gxp_vd_put(async_resp->vd);
	kfree(async_resp);
}

static u32 gxp_uci_get_cmd_timeout(struct gcip_mailbox *mailbox, void *cmd, void *resp, void *data)
{
	struct gxp_uci_async_response *async_resp = data;
	struct gxp_uci_additional_info_header *header;
	struct gxp_uci_additional_info_root *root;

	if (!async_resp->additional_info_buf.vaddr)
		return MAILBOX_TIMEOUT;

	header = async_resp->additional_info_buf.vaddr;
	root = async_resp->additional_info_buf.vaddr + header->root_offset;

	if (!root->timeout_ms)
		return MAILBOX_TIMEOUT;

	return root->timeout_ms + PER_CMD_TIMEOUT_MARGIN_MS;
}

static const struct gcip_mailbox_ops gxp_uci_gcip_mbx_ops = {
	.get_cmd_queue_tail = gxp_mailbox_gcip_ops_get_cmd_queue_tail,
	.inc_cmd_queue_tail = gxp_mailbox_gcip_ops_inc_cmd_queue_tail,
	.acquire_cmd_queue_lock = gxp_mailbox_gcip_ops_acquire_cmd_queue_lock,
	.release_cmd_queue_lock = gxp_mailbox_gcip_ops_release_cmd_queue_lock,
	.get_cmd_elem_seq = gxp_uci_get_cmd_elem_seq,
	.set_cmd_elem_seq = gxp_uci_set_cmd_elem_seq,
	.get_cmd_elem_code = gxp_uci_get_cmd_elem_code,
	.get_resp_queue_size = gxp_mailbox_gcip_ops_get_resp_queue_size,
	.get_resp_queue_head = gxp_mailbox_gcip_ops_get_resp_queue_head,
	.get_resp_queue_tail = gxp_mailbox_gcip_ops_get_resp_queue_tail,
	.inc_resp_queue_head = gxp_mailbox_gcip_ops_inc_resp_queue_head,
	.acquire_resp_queue_lock = gxp_mailbox_gcip_ops_acquire_resp_queue_lock,
	.release_resp_queue_lock = gxp_mailbox_gcip_ops_release_resp_queue_lock,
	.get_resp_elem_seq = gxp_uci_get_resp_elem_seq,
	.set_resp_elem_seq = gxp_uci_set_resp_elem_seq,
	.acquire_wait_list_lock = gxp_mailbox_gcip_ops_acquire_wait_list_lock,
	.release_wait_list_lock = gxp_mailbox_gcip_ops_release_wait_list_lock,
	.wait_for_cmd_queue_not_full = gxp_mailbox_gcip_ops_wait_for_cmd_queue_not_full,
	.before_enqueue_wait_list = gxp_uci_before_enqueue_wait_list,
	.after_enqueue_cmd = gxp_mailbox_gcip_ops_after_enqueue_cmd,
	.after_fetch_resps = gxp_mailbox_gcip_ops_after_fetch_resps,
	.handle_awaiter_arrived = gxp_uci_handle_awaiter_arrived,
	.handle_awaiter_timedout = gxp_uci_handle_awaiter_timedout,
	/*
	 * We didn't implement `handle_awaiter_flushed` callback intentionally. The callback will be
	 * called when the UCI mailbox is going to be released and it is flushing commands remaining
	 * in the wait list of the mailbox. That says the callback will be called when gxp device is
	 * closed which is later than all VDs are already destroyed. Therefore, there is nothing we
	 * can do to handle flushed commands when the callback is called.
	 *
	 * Instead, we introduced the `gxp_uci_mailbox_manager_release_unconsumed_async_resps`
	 * function which flushes unconsumed commands when each VD destroys. Therefore, the
	 * `handle_awaiter_flushed` callback shouldn't be called when the UCI mailbox destroys
	 * theoretically.
	 */
	.release_awaiter_data = gxp_uci_release_awaiter_data,
	.is_block_off = gxp_mailbox_gcip_ops_is_block_off,
	.get_cmd_timeout = gxp_uci_get_cmd_timeout,
};

static int gxp_uci_allocate_resources(struct gxp_mailbox *mailbox,
				      struct gxp_virtual_device *vd,
				      uint virt_core)
{
	struct gxp_uci *uci = mailbox->data;
	struct gxp_mcu *mcu = uci->mcu;
	int ret;

	/* Allocate and initialize the command queue */
	ret = gxp_mcu_mem_alloc_data(mcu, &uci->cmd_queue_mem,
				     sizeof(struct gxp_uci_command) *
					     MBOX_CMD_QUEUE_NUM_ENTRIES);
	if (ret)
		goto err_cmd_queue;
	mailbox->cmd_queue_buf.vaddr = uci->cmd_queue_mem.vaddr;
	mailbox->cmd_queue_buf.dsp_addr = uci->cmd_queue_mem.daddr;
	mailbox->cmd_queue_size = MBOX_CMD_QUEUE_NUM_ENTRIES;
	mailbox->cmd_queue_tail = 0;

	/* Allocate and initialize the response queue */
	ret = gxp_mcu_mem_alloc_data(mcu, &uci->resp_queue_mem,
				     sizeof(struct gxp_uci_response) *
					     MBOX_RESP_QUEUE_NUM_ENTRIES);
	if (ret)
		goto err_resp_queue;
	mailbox->resp_queue_buf.vaddr = uci->resp_queue_mem.vaddr;
	mailbox->resp_queue_buf.dsp_addr = uci->resp_queue_mem.daddr;
	mailbox->resp_queue_size = MBOX_RESP_QUEUE_NUM_ENTRIES;
	mailbox->resp_queue_head = 0;

	/* Allocate and initialize the mailbox descriptor */
	ret = gxp_mcu_mem_alloc_data(mcu, &uci->descriptor_mem,
				     sizeof(struct gxp_mailbox_descriptor));
	if (ret)
		goto err_descriptor;

	mailbox->descriptor_buf.vaddr = uci->descriptor_mem.vaddr;
	mailbox->descriptor_buf.dsp_addr = uci->descriptor_mem.daddr;
	mailbox->descriptor =
		(struct gxp_mailbox_descriptor *)mailbox->descriptor_buf.vaddr;
	mailbox->descriptor->cmd_queue_device_addr = uci->cmd_queue_mem.daddr;
	mailbox->descriptor->resp_queue_device_addr = uci->resp_queue_mem.daddr;
	mailbox->descriptor->cmd_queue_size = mailbox->cmd_queue_size;
	mailbox->descriptor->resp_queue_size = mailbox->resp_queue_size;

	return 0;

err_descriptor:
	gxp_mcu_mem_free_data(mcu, &uci->resp_queue_mem);
err_resp_queue:
	gxp_mcu_mem_free_data(mcu, &uci->cmd_queue_mem);
err_cmd_queue:
	return ret;
}

static void gxp_uci_release_resources(struct gxp_mailbox *mailbox,
				      struct gxp_virtual_device *vd,
				      uint virt_core)
{
	struct gxp_uci *uci = mailbox->data;

	gxp_mcu_mem_free_data(uci->mcu, &uci->descriptor_mem);
	gxp_mcu_mem_free_data(uci->mcu, &uci->resp_queue_mem);
	gxp_mcu_mem_free_data(uci->mcu, &uci->cmd_queue_mem);
}

static struct gxp_mailbox_ops gxp_uci_gxp_mbx_ops = {
	.allocate_resources = gxp_uci_allocate_resources,
	.release_resources = gxp_uci_release_resources,
	.gcip_ops.mbx = &gxp_uci_gcip_mbx_ops,
};

/*
 * Calculates an aligned start offset of the field which is expected to be start at @offset with
 * @size of buffer. If the end offset is already aligned, the returned offset will be the same
 * with @offset. Otherwise, a padded start offset will be returned.
 */
static uint32_t gxp_uci_additional_info_align_offset(uint32_t offset, uint32_t size)
{
	uint32_t end = offset + size, aligned;

	aligned = ALIGN(end, ADDITIONAL_INFO_ALIGN);

	return offset + (aligned - end);
}

/* Fills the header part of the additional_info. */
static void gxp_uci_additional_info_fill_header(struct gxp_uci_additional_info_header *header)
{
	header->identifier = 0;
	header->version = 0;
	header->root_offset = gxp_uci_additional_info_align_offset(
		sizeof(*header), sizeof(struct gxp_uci_additional_info_root));
}

/* Fills the root part of the additional info. */
static void gxp_uci_additional_info_fill_root(struct gxp_uci_additional_info_root *root,
					      uint32_t root_offset, uint32_t in_fences_size,
					      uint32_t out_fences_size, uint32_t timeout_ms,
					      uint32_t runtime_additional_info_size)
{
	uint32_t in_fences_size_b = sizeof(uint16_t) * in_fences_size;
	uint32_t out_fences_size_b = sizeof(uint16_t) * out_fences_size;

	root->object_size = sizeof(*root);
	root->in_fences_offset =
		gxp_uci_additional_info_align_offset(sizeof(*root), in_fences_size_b);
	root->in_fences_size = in_fences_size;
	root->out_fences_offset = gxp_uci_additional_info_align_offset(
		root->in_fences_offset + in_fences_size_b, out_fences_size_b);
	root->out_fences_size = out_fences_size;
	root->timeout_ms = timeout_ms;
	root->runtime_additional_info_offset = gxp_uci_additional_info_align_offset(
		root->out_fences_offset + out_fences_size_b, runtime_additional_info_size);
	root->runtime_additional_info_size = runtime_additional_info_size;
}

/*
 * Allocates a buffer for the additional_info from the MCU data memory pool and copy the data from
 * @info to the allocated buffer.
 */
static int gxp_uci_allocate_additional_info(struct gxp_uci_async_response *async_resp,
					    struct gxp_uci_additional_info *info)
{
	int ret;
	struct gxp_uci *uci = async_resp->uci;
	struct gxp_mapped_resource *buf = &async_resp->additional_info_buf;
	size_t size = info->header.root_offset + info->root.runtime_additional_info_offset +
		      info->root.runtime_additional_info_size;

	ret = gxp_mcu_mem_alloc_data(uci->mcu, buf, size);
	if (ret) {
		dev_err(uci->gxp->dev, "Failed to allocate additional info: %d", ret);
		return ret;
	}

	/* Copy header. */
	memcpy(buf->vaddr, &info->header, sizeof(info->header));

	/* Copy root. */
	memcpy(buf->vaddr + info->header.root_offset, &info->root, sizeof(info->root));

	/* Copy in_fences. */
	if (info->root.in_fences_size)
		memcpy(buf->vaddr + info->header.root_offset + info->root.in_fences_offset,
		       info->in_fences, sizeof(uint16_t) * info->root.in_fences_size);

	/* Copy out_fences. */
	if (info->root.out_fences_size)
		memcpy(buf->vaddr + info->header.root_offset + info->root.out_fences_offset,
		       info->out_fences, sizeof(uint16_t) * info->root.out_fences_size);

	/* Copy runtime-defined additional info. */
	if (info->root.runtime_additional_info_size)
		memcpy(buf->vaddr + info->header.root_offset +
			       info->root.runtime_additional_info_offset,
		       info->runtime_additional_info, info->root.runtime_additional_info_size);

	return 0;
}

int gxp_uci_init(struct gxp_mcu *mcu)
{
	struct gxp_dev *gxp = mcu->gxp;
	struct gxp_uci *uci = &mcu->uci;
	struct gxp_mailbox_args mbx_args = {
		.type = GXP_MBOX_TYPE_GENERAL,
		.ops = &gxp_uci_gxp_mbx_ops,
		.queue_wrap_bit = UCI_CIRCULAR_QUEUE_WRAP_BIT,
		.cmd_elem_size = sizeof(struct gxp_uci_command),
		.resp_elem_size = sizeof(struct gxp_uci_response),
		.data = uci,
	};

	uci->gxp = gxp;
	uci->mcu = mcu;
	uci->mbx = gxp_mailbox_alloc(gxp->mailbox_mgr, NULL, 0, UCI_MAILBOX_ID,
				     &mbx_args);
	if (IS_ERR(uci->mbx))
		return PTR_ERR(uci->mbx);
	gxp_uci_mailbox_manager_set_ops(gxp->mailbox_mgr);

	return 0;
}

int gxp_uci_reinit(struct gxp_uci *uci)
{
	struct gxp_mailbox *mailbox = uci->mbx;

	gxp_mailbox_reinit(mailbox);

	return 0;
}

void gxp_uci_exit(struct gxp_uci *uci)
{
	if (IS_GXP_TEST && (!uci || !uci->mbx))
		return;
	gxp_mailbox_release(uci->gxp->mailbox_mgr, NULL, 0, uci->mbx);
	uci->mbx = NULL;
}

/**
 * gxp_uci_send_command() - Sends the command to the MCU firmware.
 * @uci: The UCI mailbox.
 * @vd: The virtual device sending the command.
 * @cmd: The command to send.
 * @additional_info: The additional information to be serialized and passed to the command.
 * @in_fences: The fences which the command is waiting on to be unblocked.
 * @out_fences: The fences which the command will signal.
 * @wait_queue: The queue where the command will be located before a response arrives.
 * @resp_queue: The queue where the command will be moved after it is processed.
 * @queue_lock: The lock protecting @wait_queue and @dest_queue.
 * @queue_waitq: The wait queue which will be notified when the command is processed.
 * @eventfd: The eventfd which will be notified when the command is processed.
 * @flags: The GCIP mailbox flags.
 *
 * Returns 0 on success or errno on failure.
 */
static int gxp_uci_send_command(struct gxp_uci *uci, struct gxp_virtual_device *vd,
				struct gxp_uci_command *cmd,
				struct gxp_uci_additional_info *additional_info,
				struct gcip_fence_array *in_fences,
				struct gcip_fence_array *out_fences, struct list_head *wait_queue,
				struct list_head *resp_queue, spinlock_t *queue_lock,
				wait_queue_head_t *queue_waitq, struct gxp_eventfd *eventfd,
				gcip_mailbox_cmd_flags_t flags)
{
	struct gxp_uci_async_response *async_resp;
	struct gcip_mailbox_resp_awaiter *awaiter;
	uint32_t additional_info_address = 0;
	uint16_t additional_info_size = 0;
	int ret;

	if (!gxp_vd_has_and_use_credit(vd))
		return -EBUSY;
	async_resp = kzalloc(sizeof(*async_resp), GFP_KERNEL);
	if (!async_resp) {
		ret = -ENOMEM;
		goto err_release_credit;
	}

	async_resp->uci = uci;
	async_resp->vd = gxp_vd_get(vd);
	async_resp->wait_queue = wait_queue;
	async_resp->dest_queue = resp_queue;
	async_resp->queue_lock = queue_lock;
	async_resp->dest_queue_waitq = queue_waitq;
	if (eventfd && gxp_eventfd_get(eventfd))
		async_resp->eventfd = eventfd;
	else
		async_resp->eventfd = NULL;

	if (additional_info) {
		ret = gxp_uci_allocate_additional_info(async_resp, additional_info);
		if (ret)
			goto err_free_async_resp;
		additional_info_address = async_resp->additional_info_buf.daddr;
		additional_info_size = async_resp->additional_info_buf.size;
	}

	cmd->additional_info_address = additional_info_address;
	cmd->additional_info_size = additional_info_size;

	async_resp->in_fences = gcip_fence_array_get(in_fences);
	async_resp->out_fences = gcip_fence_array_get(out_fences);

	/*
	 * @async_resp->awaiter will be set from the `gxp_uci_before_enqueue_wait_list`
	 * callback.
	 */
	awaiter = gxp_mailbox_put_cmd(uci->mbx, cmd, &async_resp->resp, async_resp, flags);
	if (IS_ERR(awaiter)) {
		ret = PTR_ERR(awaiter);
		goto err_put_fences;
	}

	return 0;

err_put_fences:
	gcip_fence_array_put(async_resp->out_fences);
	gcip_fence_array_put(async_resp->in_fences);
	if (additional_info)
		gxp_mcu_mem_free_data(uci->mcu, &async_resp->additional_info_buf);
	if (async_resp->eventfd)
		gxp_eventfd_put(async_resp->eventfd);
	gxp_vd_put(vd);
err_free_async_resp:
	kfree(async_resp);
err_release_credit:
	gxp_vd_release_credit(vd);
	return ret;
}

int gxp_uci_create_and_send_cmd(struct gxp_client *client, u64 cmd_seq, u32 flags, const u8 *opaque,
				u32 timeout_ms, struct gcip_fence_array *in_fences,
				struct gcip_fence_array *out_fences)
{
	struct gxp_dev *gxp = client->gxp;
	struct gxp_mcu *mcu = gxp_mcu_of(gxp);
	struct gxp_uci_command cmd = {};
	struct gxp_uci_additional_info additional_info = {};
	uint16_t *in_iif_fences, *out_iif_fences;
	uint32_t in_iif_fences_size, out_iif_fences_size;
	int ret;

	/*
	 * It will hold the block wakelock internally (i.e., call gcip_pm_get()) and we decided to
	 * always decouple the pm-lock and @client->semaphore while implementing the MCU crash
	 * handler. Therefore, we should submit waiters without holding @client->semaphore.
	 *
	 * The function will hold @iif_mgr->ops_sema to prevent modifying registered operators while
	 * calling the `acquire_block_wakelock` operator and holding the block wakelock. At the same
	 * time, there is another operator, `fence_unblocked`, which will be called when a fence has
	 * been unblocked while holding the same semaphore and the operator will send an UCI command
	 * which will hold @wait_list_lock. (i.e., @iif_mgr->ops_sema -> @wait_list_lock) Therefore,
	 * this function shouldn't be called while holding @wait_list_lock. Otherwise it will hold
	 * two locks in the reverse order and will cause a potential deadlock.
	 */
	ret = gcip_fence_array_submit_waiter_and_signaler(in_fences, out_fences, IIF_IP_DSP);
	if (ret) {
		dev_err(gxp->dev, "Failed to submit waiter or signaler to fences, ret=%d", ret);
		return ret;
	}

	down_read(&client->semaphore);

	if (!gxp_client_has_available_vd(client, "GXP_MAILBOX_UCI_COMMAND[_COMPAT]")) {
		ret = -ENODEV;
		goto out;
	}

	/* Caller must hold BLOCK wakelock */
	if (!client->has_block_wakelock) {
		dev_err(gxp->dev,
			"GXP_MAILBOX_UCI_COMMAND[_COMPAT] requires the client hold a BLOCK wakelock\n");
		ret = -ENODEV;
		goto out;
	}

	in_iif_fences = gcip_fence_array_get_iif_id(in_fences, &in_iif_fences_size, false, 0);
	if (IS_ERR(in_iif_fences)) {
		ret = PTR_ERR(in_iif_fences);
		dev_err(gxp->dev, "Failed to get IIF IDs from in-fences, ret=%d", ret);
		goto out;
	}

	out_iif_fences =
		gcip_fence_array_get_iif_id(out_fences, &out_iif_fences_size, true, IIF_IP_DSP);
	if (IS_ERR(out_iif_fences)) {
		ret = PTR_ERR(out_iif_fences);
		dev_err(gxp->dev, "Failed to get IIF IDs from out-fences, ret=%d", ret);
		goto err_put_in_iif_fences;
	}

	if (opaque)
		memcpy(cmd.opaque, opaque, sizeof(cmd.opaque));

	cmd.client_id = client->vd->client_id;
	cmd.seq = cmd_seq;

	if (flags & GXP_UCI_NULL_COMMAND_FLAG)
		cmd.type = NULL_COMMAND;

	gxp_uci_fill_additional_info(&additional_info, in_iif_fences, in_iif_fences_size,
				     out_iif_fences, out_iif_fences_size, timeout_ms, NULL, 0);

	ret = gxp_uci_send_command(&mcu->uci, client->vd, &cmd, &additional_info, in_fences,
				   out_fences,
				   &client->vd->mailbox_resp_queues[UCI_RESOURCE_ID].wait_queue,
				   &client->vd->mailbox_resp_queues[UCI_RESOURCE_ID].dest_queue,
				   &client->vd->mailbox_resp_queues[UCI_RESOURCE_ID].lock,
				   &client->vd->mailbox_resp_queues[UCI_RESOURCE_ID].waitq,
				   client->mb_eventfds[UCI_RESOURCE_ID],
				   GCIP_MAILBOX_CMD_FLAGS_SKIP_ASSIGN_SEQ);

	kfree(out_iif_fences);
	kfree(in_iif_fences);

	if (ret)
		dev_err(gxp->dev, "Failed to enqueue mailbox command (ret=%d)\n", ret);

	goto out;

err_put_in_iif_fences:
	kfree(in_iif_fences);
out:
	up_read(&client->semaphore);
	if (ret) {
		gcip_fence_array_signal(out_fences, ret);
		gcip_fence_array_waited(in_fences, IIF_IP_DSP);
	}

	return ret;
}

int gxp_uci_wait_async_response(struct mailbox_resp_queue *uci_resp_queue,
				u64 *resp_seq, u16 *error_code, u8 *opaque)
{
	long timeout;
	struct gxp_uci_async_response *async_resp;
	int ret = 0;

	spin_lock_irq(&uci_resp_queue->lock);

	/*
	 * The "exclusive" version of wait_event is used since each wake
	 * corresponds to the addition of exactly one new response to be
	 * consumed. Therefore, only one waiting response can ever proceed
	 * per wake event.
	 */
	timeout = wait_event_interruptible_lock_irq_timeout_exclusive(
		uci_resp_queue->waitq, !list_empty(&uci_resp_queue->dest_queue),
		uci_resp_queue->lock, msecs_to_jiffies(MAILBOX_TIMEOUT));
	if (timeout <= 0) {
		*resp_seq = 0;
		if (list_empty(&uci_resp_queue->wait_queue)) {
			/* This only happens when there is no command pushed or signaled. */
			*error_code = GXP_RESPONSE_ERROR_NOENT;
		} else {
			/*
			 * Might be a race with gcip_mailbox_async_cmd_timeout_work or the command
			 * use a runtime specified timeout that is larger than MAILBOX_TIMEOUT.
			 */
			*error_code = GXP_RESPONSE_ERROR_AGAIN;
		}
		spin_unlock_irq(&uci_resp_queue->lock);

		return ret;
	}
	async_resp = list_first_entry(&uci_resp_queue->dest_queue,
				      struct gxp_uci_async_response,
				      dest_list_entry);

	/* Pop the front of the response list */
	list_del(&(async_resp->dest_list_entry));

	spin_unlock_irq(&uci_resp_queue->lock);

	*resp_seq = async_resp->resp.seq;
	switch (async_resp->status) {
	case GXP_RESP_OK:
		*error_code = async_resp->resp.code;
		if (opaque)
			memcpy(opaque, async_resp->resp.opaque, sizeof(async_resp->resp.opaque));
		if (*error_code)
			dev_err(async_resp->uci->gxp->dev,
				"Completed response with an error from the firmware side %hu\n",
				*error_code);
		break;
	case GXP_RESP_TIMEDOUT:
		*error_code = GXP_RESPONSE_ERROR_TIMEOUT;
		dev_err(async_resp->uci->gxp->dev,
			"Response not received for seq: %llu under %ums\n", *resp_seq,
			gxp_uci_get_cmd_timeout(NULL, NULL, NULL, async_resp));
		break;
	case GXP_RESP_CANCELED:
		*error_code = GXP_RESPONSE_ERROR_CANCELED;
		dev_err(async_resp->uci->gxp->dev, "Command has been canceled for seq: %llu\n",
			*resp_seq);
		break;
	default:
		dev_err(async_resp->uci->gxp->dev, "Possible corruption in response handling\n");
		ret = -ETIMEDOUT;
		break;
	}

	/*
	 * We must be absolutely sure the timeout work has been cancelled
	 * and/or completed before freeing the async response object.
	 * There are 3 possible cases when we arrive at this point:
	 *   1) The response arrived normally and the timeout was cancelled
	 *   2) The response timedout and its timeout handler finished
	 *   3) The response handler and timeout handler raced, and the response
	 *      handler "cancelled" the timeout handler while it was already in
	 *      progress.
	 *
	 * This call handles case #3, and ensures any in-process timeout
	 * handler (which may reference the `gxp_async_response`) has
	 * been able to exit cleanly.
	 */
	gcip_mailbox_cancel_awaiter_timeout(async_resp->awaiter);
	gcip_mailbox_release_awaiter(async_resp->awaiter);

	return ret;
}

void gxp_uci_fill_additional_info(struct gxp_uci_additional_info *info, uint16_t *in_fences,
				  uint32_t in_fences_size, uint16_t *out_fences,
				  uint32_t out_fences_size, uint32_t timeout_ms,
				  uint8_t *runtime_additional_info,
				  uint32_t runtime_additional_info_size)
{
	gxp_uci_additional_info_fill_header(&info->header);
	gxp_uci_additional_info_fill_root(&info->root, info->header.root_offset, in_fences_size,
					  out_fences_size, timeout_ms,
					  runtime_additional_info_size);
	info->in_fences = in_fences;
	info->out_fences = out_fences;
	info->runtime_additional_info = runtime_additional_info;
}

/**
 * in_fence_cb_func() - A dma_fence_func_t wrapper function to schedule the UCI command work.
 * @fence: The fence that is signaled.
 * @cb: The callback object that is registered to the signaled fence.
 *
 * If the fence is signaled without error, the UCI command work will be removed from the client's
 * callback list and added to the client's work list and then the worker will be scheduled.
 *
 * Context: This function will be called in IRQ context.
 */
static void in_fence_cb_func(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct gxp_uci_cmd_work *uci_work = container_of(cb, struct gxp_uci_cmd_work, cb);
	struct gxp_client *client = uci_work->client;
	unsigned long flag1, flag2;

	spin_lock_irqsave(&client->uci_cb_list_lock, flag1);

	if (client->uci_cb_disabled)
		goto out;

	list_del(&uci_work->node);

	if (dma_fence_get_status_locked(fence) == 1) {
		spin_lock_irqsave(&client->uci_work_list_lock, flag2);
		list_add(&uci_work->node, &client->uci_work_list);
		spin_unlock_irqrestore(&client->uci_work_list_lock, flag2);
		schedule_work(&client->uci_worker);
	} else {
		gxp_uci_work_destroy(uci_work);
	}

out:
	spin_unlock_irqrestore(&client->uci_cb_list_lock, flag1);
}

/**
 * gxp_uci_cmd_work_create() - Allocates and initializes the UCI command work object.
 * @fence: Same as gxp_uci_cmd_work_create_and_schedule.
 * @client: Same as gxp_uci_cmd_work_create_and_schedule.
 * @ibuf: Same as gxp_uci_cmd_work_create_and_schedule.
 * @cmd_seq: Same as gxp_uci_cmd_work_create_and_schedule.
 * @in_fences: Same as gxp_uci_cmd_work_create_and_schedule.
 * @out_fences: Same as gxp_uci_cmd_work_create_and_schedule.
 */
static struct gxp_uci_cmd_work *
gxp_uci_cmd_work_create(struct dma_fence *fence, struct gxp_client *client,
			const struct gxp_mailbox_uci_command_ioctl *ibuf, u64 cmd_seq,
			struct gcip_fence_array *in_fences, struct gcip_fence_array *out_fences)
{
	struct gxp_uci_cmd_work *uci_work;

	uci_work = kzalloc(sizeof(*uci_work), GFP_KERNEL);
	if (!uci_work)
		return ERR_PTR(-ENOMEM);

	uci_work->fence = dma_fence_get(fence);
	uci_work->client = client;
	uci_work->cmd_seq = cmd_seq;
	uci_work->flags = ibuf->flags;
	uci_work->timeout_ms = ibuf->timeout_ms;
	uci_work->in_fences = gcip_fence_array_get(in_fences);
	uci_work->out_fences = gcip_fence_array_get(out_fences);
	memcpy(uci_work->opaque, ibuf->opaque, sizeof(ibuf->opaque));

	return uci_work;
}

int gxp_uci_cmd_work_create_and_schedule(struct dma_fence *fence, struct gxp_client *client,
					 const struct gxp_mailbox_uci_command_ioctl *ibuf,
					 u64 cmd_seq, struct gcip_fence_array *in_fences,
					 struct gcip_fence_array *out_fences)
{
	struct gxp_uci_cmd_work *uci_work;
	unsigned long flags;
	int ret;

	if (!fence)
		goto send_cmd;

	uci_work = gxp_uci_cmd_work_create(fence, client, ibuf, cmd_seq, in_fences, out_fences);
	if (IS_ERR(uci_work))
		return PTR_ERR(uci_work);

	spin_lock_irqsave(&client->uci_cb_list_lock, flags);
	list_add(&uci_work->node, &client->uci_cb_list);
	spin_unlock_irqrestore(&client->uci_cb_list_lock, flags);

	ret = dma_fence_add_callback(fence, &uci_work->cb, in_fence_cb_func);

	/*
	 * This means the fence has not been signaled yet and the callback is successfully
	 * registered. The in_fence_cb_func() callback will eventually schedule a work to
	 * create and send UCI command to the firmware once the fence is signaled.
	 * The work object will be destroyed in in_fence_cb_func() or when the client is destroyed.
	 */
	if (!ret)
		return 0;

	spin_lock_irqsave(&client->uci_cb_list_lock, flags);
	list_del_init(&uci_work->node);
	spin_unlock_irqrestore(&client->uci_cb_list_lock, flags);

	gxp_uci_work_destroy(uci_work);

	/*
	 * If @ret is -ENOENT, it means that @fence is already signaled so the callback was
	 * not registered to the fence. We don't have to treat it as an error and can run
	 * the work directly.
	 */
	if (ret == -ENOENT)
		goto send_cmd;

	return ret;

send_cmd:
	return gxp_uci_create_and_send_cmd(client, cmd_seq, ibuf->flags, ibuf->opaque,
					   ibuf->timeout_ms, in_fences, out_fences);
}

void gxp_uci_work_destroy(struct gxp_uci_cmd_work *uci_work)
{
	gcip_fence_array_put_async(uci_work->in_fences);
	gcip_fence_array_put_async(uci_work->out_fences);
	dma_fence_put(uci_work->fence);
	kfree(uci_work);
}

void gxp_uci_send_iif_unblock_noti(struct gxp_uci *uci, int iif_id)
{
	struct gxp_uci_command cmd;
	int ret;

	/*
	 * Theoretically, the meaning of this function call is that MCU is waiting on some fences
	 * to proceed waiter commands so that the block should be already powered on. However,
	 * according to the design of IIF, if signaler IP is crashed, there is a possibility of race
	 * condition that the MCU has processed the commands before this notification which means
	 * the block would be already powered down. To prevent any kernel panic can be caused by it,
	 * acquire the wakelock if the block is powered on. Otherwise, just give up notifying MCU of
	 * the fence unblock.
	 */
	ret = gcip_pm_get_if_powered(uci->gxp->power_mgr->pm, false);
	if (ret) {
		dev_warn(uci->gxp->dev, "Block should be powered on before notifying IIF unblock");
		return;
	}

	cmd.type = IIF_UNBLOCK_COMMAND;
	cmd.iif_id = iif_id;
	cmd.seq = gcip_mailbox_inc_seq_num(uci->mbx->mbx_impl.gcip_mbx, 1);

	ret = gxp_mailbox_send_cmd(uci->mbx, &cmd, NULL, GCIP_MAILBOX_CMD_FLAGS_SKIP_ASSIGN_SEQ);
	if (ret)
		dev_warn(uci->gxp->dev, "Failed to notify the IIF unblock: id=%d, ret=%d", iif_id,
			 ret);

	gcip_pm_put(uci->gxp->power_mgr->pm);
}

void gxp_uci_consume_responses(struct gxp_uci *uci)
{
	gcip_mailbox_consume_responses(uci->mbx->mbx_impl.gcip_mbx);
}

void gxp_uci_cancel(struct gxp_virtual_device *vd)
{
	struct gxp_uci_async_response *cur, *nxt;
	unsigned long flags;

	/*
	 * By setting @cur->processed to true, the responses will be prevented to be processed by
	 * either the arrived or timedout handler even though one of those handlers is fired.
	 * (See gxp_uci_push_async_response.)
	 */
	spin_lock_irqsave(&vd->mailbox_resp_queues[UCI_RESOURCE_ID].lock, flags);

	list_for_each_entry(cur, &vd->mailbox_resp_queues[UCI_RESOURCE_ID].wait_queue,
			    wait_list_entry) {
		cur->processed = true;
	}

	spin_unlock_irqrestore(&vd->mailbox_resp_queues[UCI_RESOURCE_ID].lock, flags);

	TEST_FLUSH_FIRMWARE_WORK();

	/*
	 * Cancels all pending commands and pushes CANCELED responses for them.
	 *
	 * Note that the arrived or timedout handlers can be still fired while canceling commands
	 * by the race condition, but they will directly return without doing anything because of
	 * the logic above.
	 *
	 * In other words, neither ARRIVED nor TIEMDOUT responses will be pushed to the dest_queue
	 * of @vd and one refcount of @cur->awaiter held by the driver won't be released until we
	 * push CANCELED responses and the runtime consumes them. (i.e., there will be no UAF bug.)
	 *
	 * Therefore, we don't need to check the return value of the `gcip_mailbox_cancel_awaiter`
	 * function, it is always safe to push CANCELED responses to the response queue of @vd.
	 *
	 * Note that to prevent a potential race condition between ARRIVED and CANCELED, the caller
	 * is expected to call the `gxp_uci_consume_responses` function first before this function
	 * to ensure consuming all arrived responses from the MCU as described in the header file.
	 *
	 * Another potential race condition that processing TIMEDOUT commands as CANCELED should be
	 * fine.
	 */
	list_for_each_entry_safe(cur, nxt, &vd->mailbox_resp_queues[UCI_RESOURCE_ID].wait_queue,
				 wait_list_entry) {
		gcip_mailbox_cancel_awaiter(cur->awaiter);
		gxp_uci_push_async_response(cur, GXP_RESP_CANCELED, true);
	}
}
