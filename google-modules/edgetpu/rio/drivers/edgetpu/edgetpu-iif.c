// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support Inter-IP Fences.
 *
 * Copyright (C) 2025 Google LLC
 */

#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <gcip/gcip-memory.h>

#include <iif/iif-fence.h>
#include <iif/iif-manager.h>
#include <iif/iif-shared.h>

#include "edgetpu-iif.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-pm.h"
#include "edgetpu-vii-litebuf.h"

/* Currently sized to fit within 16K of command queue */
#define QUEUE_SIZE 128

/* IIF Unblock Propagation */

/* Unblocked IIF to be propagated to the firmware. */
struct edgetpu_iif_unblocked {
	struct list_head node;
	int fence_id;
};

/*
 * Callback which will be called once @fence is unblocked.
 *
 * This function will add the fence to @etdev->etiif->iif_unblocked_list.
 */
static void edgetpu_iif_unblocked(struct iif_fence *fence, void *data)
{
	struct edgetpu_dev *etdev = data;
	struct edgetpu_iif *etiif = etdev->etiif;
	struct edgetpu_iif_unblocked *unblocked;

	if (fence->signal_error)
		etdev_warn(etdev, "IIF has been unblocked with an error, id=%d, error=%d",
			   fence->id, fence->signal_error);

	if (fence->propagate) {
		unblocked = kzalloc(sizeof(*unblocked), GFP_KERNEL);
		if (!unblocked)
			return;

		unblocked->fence_id = fence->id;

		spin_lock(&etiif->unblocked_lock);

		/*
		 * Propagates the fence unblock to the firmware through a deferred work instead of
		 * doing that here directly to avoid any potential deadlock.
		 *
		 * For example, when driver sends a VII command to the firmware, it will hold the
		 * mailbox lock and submit waiters/signalers to inter-IP fences if needed which
		 * requires to hold @etiif->iif_mgr->ops_sema. By the way, this `iif_unblocked()`
		 * callback will be invoked with holding @etiif->iif_mgr->ops_sema. Therefore, if
		 * the driver send a VII command here to the firmware to propagate the fence
		 * unblock, a deadlock can happen.
		 */
		list_add_tail(&unblocked->node, &etiif->unblocked_list);

		spin_unlock(&etiif->unblocked_lock);
		/*
		 * The unblocked work consumes all fences in unblocked_list, no need to check
		 * whether a pending work exists in queue.
		 */
		schedule_work(&etiif->unblocked_work);
	}
}

static const struct iif_manager_ops iif_mgr_ops = {
	.fence_unblocked = edgetpu_iif_unblocked,
};

/*
 * Work function which consumes @etiif->unblocked_list and propagates the fence unblock to the
 * firmware.
 */
static void edgetpu_iif_unblocked_work_func(struct work_struct *work)
{
	struct edgetpu_iif *etiif = container_of(work, struct edgetpu_iif, unblocked_work);
	struct edgetpu_iif_unblocked *cur, *nxt;
	LIST_HEAD(iif_unblocked_list);

	spin_lock(&etiif->unblocked_lock);
	list_replace_init(&etiif->unblocked_list, &iif_unblocked_list);
	spin_unlock(&etiif->unblocked_lock);

	list_for_each_entry_safe(cur, nxt, &iif_unblocked_list, node) {
		edgetpu_iif_send_unblock_notification(etiif, cur->fence_id);
		kfree(cur);
	}
}

/* Initializes the work which propagates the IIF unblock to the firmware. */
static void edgetpu_init_iif_unblocked_work(struct edgetpu_iif *etiif)
{
	INIT_LIST_HEAD(&etiif->unblocked_list);
	spin_lock_init(&etiif->unblocked_lock);
	INIT_WORK(&etiif->unblocked_work, &edgetpu_iif_unblocked_work_func);
}

/* Cancels the work propagating the IIF unblock to the firmware. */
static void edgetpu_cancel_iif_unblocked_work(struct edgetpu_iif *etiif)
{
	struct edgetpu_iif_unblocked *cur, *nxt;

	cancel_work_sync(&etiif->unblocked_work);

	/*
	 * As the work is canceled and it will never be scheduled, we don't need to hold
	 * @etiif->unblocked_lock.
	 */
	list_for_each_entry_safe(cur, nxt, &etiif->unblocked_list, node) {
		kfree(cur);
	}
}

/* IIF Manager and Dev */

/* TODO(b/410689519): remove embedded case */
static void edgetpu_get_embedded_iif_mgr(struct edgetpu_iif *etiif)
{
	struct edgetpu_dev *etdev = etiif->etdev;
	struct iif_manager *mgr;

	if (!IS_ENABLED(CONFIG_EDGETPU_TEST))
		return;
	etdev_dbg(etdev, "Try to get an embedded IIF manager");

	mgr = iif_manager_init(etdev->dev->of_node);
	if (IS_ERR(mgr)) {
		etdev_warn(etdev, "Failed to init an embedded IIF manager: %ld", PTR_ERR(mgr));
		return;
	}

	etiif->iif_mgr = mgr;
}

static void edgetpu_get_iif_mgr(struct edgetpu_iif *etiif)
{
	struct edgetpu_dev *etdev = etiif->etdev;
	struct platform_device *pdev;
	struct device_node *node;
	struct iif_manager *mgr;

	node = of_parse_phandle(etdev->dev->of_node, "iif-device", 0);
	if (IS_ERR_OR_NULL(node)) {
		etdev_warn(etdev, "There is no iif-device node in the device tree");
		goto get_embed;
	}

	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev) {
		etdev_warn(etdev, "Failed to find the IIF device");
		goto get_embed;
	}

	mgr = platform_get_drvdata(pdev);
	if (!mgr) {
		etdev_warn(etdev, "Failed to get a manager from IIF device");
		goto put_device;
	}

	etdev_dbg(etdev, "Use the IIF manager of IIF device");

	/* We don't need to call `get_device` since `of_find_device_by_node` takes a refcount. */
	etiif->iif_dev = &pdev->dev;
	etiif->iif_mgr = iif_manager_get(mgr);
	return;

put_device:
	put_device(&pdev->dev);
get_embed:
	edgetpu_get_embedded_iif_mgr(etiif);
}

static void edgetpu_put_iif_mgr(struct edgetpu_iif *etiif)
{
	if (etiif->iif_mgr) {
		iif_manager_put(etiif->iif_mgr);
		etiif->iif_mgr = NULL;
	}
	/* NO-OP if `etiif->iif_dev.dev` is NULL. */
	put_device(etiif->iif_dev);
	etiif->iif_dev = NULL;
}

static int edgetpu_register_iif_ops(struct edgetpu_iif *etiif)
{
	if (!etiif->iif_mgr)
		return 0;

	return iif_manager_register_ops(etiif->iif_mgr, IIF_IP_TPU, &iif_mgr_ops, etiif->etdev);
}

static void edgetpu_unregister_iif_ops(struct edgetpu_iif *etiif)
{
	if (!etiif->iif_mgr)
		return;

	iif_manager_unregister_ops(etiif->iif_mgr, IIF_IP_TPU);
}

/* IIF Mailbox Ops */

static u32 edgetpu_iif_get_cmd_queue_tail(struct gcip_mailbox *mailbox)
{
	struct edgetpu_iif *etiif = gcip_mailbox_get_data(mailbox);

	return etiif->mbx_hardware->cmd_queue_tail;
}

static void edgetpu_iif_inc_cmd_queue_tail(struct gcip_mailbox *mailbox, u32 inc)
{
	struct edgetpu_iif *etiif = gcip_mailbox_get_data(mailbox);

	edgetpu_mailbox_inc_cmd_queue_tail(etiif->mbx_hardware, inc);
}

static int edgetpu_iif_acquire_cmd_queue_lock(struct gcip_mailbox *mailbox, bool try, bool *atomic)
{
	struct edgetpu_iif *etiif = gcip_mailbox_get_data(mailbox);

	*atomic = false;
	mutex_lock(&etiif->cmd_queue_lock);
	return 1;
}

static void edgetpu_iif_release_cmd_queue_lock(struct gcip_mailbox *mailbox)
{
	struct edgetpu_iif *etiif = gcip_mailbox_get_data(mailbox);

	mutex_unlock(&etiif->cmd_queue_lock);
}

static int edgetpu_iif_acquire_resp_queue_lock(struct gcip_mailbox *mailbox, bool try, bool *atomic)
{
	/*
	 * Since the IIF mailbox has no response queue this function should never be called.
	 * As a protection against bugs, this function returns 0 (failure to acquire lock) so that
	 * any caller knows they cannot access the mailbox response queue, which is NULL.
	 */
	return 0;
}

static int edgetpu_iif_wait_for_cmd_queue_not_full(struct gcip_mailbox *mailbox)
{
	struct edgetpu_iif *etiif = gcip_mailbox_get_data(mailbox);
	u32 tail = mailbox->ops->get_cmd_queue_tail(mailbox);
	bool is_queue_full;
	bool is_mbox_flushing;
	unsigned long flags;

	/* Need to check @is_flushing in case it was set while waiting for the queue lock. */
	spin_lock_irqsave(&etiif->flush_lock, flags);
	is_mbox_flushing = etiif->is_flushing;
	spin_unlock_irqrestore(&etiif->flush_lock, flags);
	is_queue_full = EDGETPU_MAILBOX_CMD_QUEUE_READ(etiif->mbx_hardware, head) ==
			(tail ^ mailbox->queue_wrap_bit);

	/*
	 * Wait until firmware consumes one or more commands.
	 *
	 * This function relies on the software watchdog to handle "timeout" scenarios where no
	 * commands are being consumed. If firmware becomes unresponsive, it will be stopped and
	 * the mailbox will be flushed.
	 *
	 * Waiting is done with usleep_range over udelay to ensure that if firmware becomes
	 * unresponsive, this thread's waiting does not impact the rest of the system. The
	 * trade-off is potentially higher latency should the queue become full without firmware
	 * hanging. This is acceptable as the queue is not expected to fill as long as firmware is
	 * actively running its scheduler thread.
	 */
	while (is_queue_full && !is_mbox_flushing) {
		etdev_warn_ratelimited(etiif->etdev, "IIF queue full");
		usleep_range(50, 200);
		spin_lock_irqsave(&etiif->flush_lock, flags);
		is_mbox_flushing = etiif->is_flushing;
		spin_unlock_irqrestore(&etiif->flush_lock, flags);
		is_queue_full = EDGETPU_MAILBOX_CMD_QUEUE_READ(etiif->mbx_hardware, head) ==
				(tail ^ mailbox->queue_wrap_bit);
	}

	return is_mbox_flushing ? -ESHUTDOWN : 0;
}

static int edgetpu_iif_after_enqueue_cmd(struct gcip_mailbox *mailbox, void *cmd)
{
	struct edgetpu_iif *etiif = gcip_mailbox_get_data(mailbox);

	EDGETPU_MAILBOX_CMD_QUEUE_WRITE_SYNC(etiif->mbx_hardware, doorbell_set, 1);

	return 0;
}

/* IIF signal commands have no sequence numbers nor command codes. */
static u64 edgetpu_iif_get_cmd_elem_seq(struct gcip_mailbox *mb, void *cmd) { return 0; }
static void edgetpu_iif_set_cmd_elem_seq(struct gcip_mailbox *mb, void *cmd, u64 seq) { return; }
static u32 edgetpu_iif_get_cmd_elem_code(struct gcip_mailbox *mb, void *cmd) { return 0; }

/* IIF signal commands have no responses. */
static u32 edgetpu_iif_get_resp_queue_size(struct gcip_mailbox *mb) { return 0; }
static u32 edgetpu_iif_get_resp_queue_head(struct gcip_mailbox *mb) { return 0; }
static u32 edgetpu_iif_get_resp_queue_tail(struct gcip_mailbox *mb) { return 0; }
static void edgetpu_iif_inc_resp_queue_head(struct gcip_mailbox *mb, u32 inc) { return; }
static void edgetpu_iif_release_resp_queue_lock(struct gcip_mailbox *mb) { return; }
static u64 edgetpu_iif_get_resp_elem_seq(struct gcip_mailbox *mb, void *resp) { return 0; }
static void edgetpu_iif_set_resp_elem_seq(struct gcip_mailbox *mb, void *resp, u64 seq) { return; }

const struct gcip_mailbox_ops iif_mailbox_ops = {
	.get_cmd_queue_tail = edgetpu_iif_get_cmd_queue_tail,
	.inc_cmd_queue_tail = edgetpu_iif_inc_cmd_queue_tail,
	.acquire_cmd_queue_lock = edgetpu_iif_acquire_cmd_queue_lock,
	.release_cmd_queue_lock = edgetpu_iif_release_cmd_queue_lock,
	.get_cmd_elem_seq = edgetpu_iif_get_cmd_elem_seq,
	.set_cmd_elem_seq = edgetpu_iif_set_cmd_elem_seq,
	.get_cmd_elem_code = edgetpu_iif_get_cmd_elem_code,
	.get_resp_queue_size = edgetpu_iif_get_resp_queue_size,
	.get_resp_queue_head = edgetpu_iif_get_resp_queue_head,
	.get_resp_queue_tail = edgetpu_iif_get_resp_queue_tail,
	.inc_resp_queue_head = edgetpu_iif_inc_resp_queue_head,
	.acquire_resp_queue_lock = edgetpu_iif_acquire_resp_queue_lock,
	.release_resp_queue_lock = edgetpu_iif_release_resp_queue_lock,
	.get_resp_elem_seq = edgetpu_iif_get_resp_elem_seq,
	.set_resp_elem_seq = edgetpu_iif_set_resp_elem_seq,
	.wait_for_cmd_queue_not_full = edgetpu_iif_wait_for_cmd_queue_not_full,
	.after_enqueue_cmd = edgetpu_iif_after_enqueue_cmd,
};

/* edgetpu-iif Interface */

int edgetpu_iif_init(struct edgetpu_mailbox_manager *mgr, struct edgetpu_iif *etiif)
{
	struct edgetpu_dev *etdev = mgr->etdev;
	int ret;

	etiif->etdev = etdev;

	ret = edgetpu_iif_init_mailbox(mgr, etiif);
	if (ret)
		return ret;

	edgetpu_init_iif_unblocked_work(etiif);
	edgetpu_get_iif_mgr(etiif);

	ret = edgetpu_register_iif_ops(etiif);
	if (ret) {
		etdev_err(etdev, "Failed to register IIF ops, disable IIF (ret=%d)", ret);
		goto err_put_iif_mgr;
	}

	return ret;

err_put_iif_mgr:
	edgetpu_put_iif_mgr(etiif);
	edgetpu_iif_release_mailbox(etiif);

	return ret;
}

void edgetpu_iif_release(struct edgetpu_iif *etiif)
{
	edgetpu_iif_release_mailbox(etiif);
	edgetpu_unregister_iif_ops(etiif);
	edgetpu_put_iif_mgr(etiif);
	edgetpu_cancel_iif_unblocked_work(etiif);
}

int edgetpu_iif_init_mailbox(struct edgetpu_mailbox_manager *mgr, struct edgetpu_iif *etiif)
{
	struct edgetpu_dev *etdev = mgr->etdev;
	struct edgetpu_mailbox *mbx_hardware;
	struct gcip_mailbox_args args = {
		.dev = mgr->etdev->dev,
		.queue_wrap_bit = CIRC_QUEUE_WRAP_BIT,
		.cmd_elem_size = sizeof(struct edgetpu_vii_litebuf_command),
		/* No responses are to be sent for IIF signal commands. */
		.resp_queue = NULL,
		.resp_elem_size = 0,
		.timeout = 0,
		.ops = &iif_mailbox_ops,
		.data = etiif,
	};
	int ret;

	if (!mgr->use_iif) {
		etdev_info(etdev, "IIF mailbox is not supported");
		return 0;
	}

	etiif->is_flushing = false;
	spin_lock_init(&etiif->flush_lock);

	mbx_hardware = edgetpu_mailbox_iif(mgr);
	if (IS_ERR_OR_NULL(mbx_hardware))
		return !mbx_hardware ? -ENODEV : PTR_ERR(mbx_hardware);
	mbx_hardware->internal.etiif = etiif;
	etiif->mbx_hardware = mbx_hardware;
	edgetpu_mailbox_disable_doorbells(mbx_hardware);
	edgetpu_mailbox_clear_doorbells(mbx_hardware);

	etiif->mbx_protocol = devm_kzalloc(etdev->dev, sizeof(*etiif->mbx_protocol), GFP_KERNEL);
	if (!etiif->mbx_protocol) {
		ret = -ENOMEM;
		goto err_mailbox_remove;
	}

	ret = edgetpu_iremap_alloc(etdev, QUEUE_SIZE * VII_CMD_SIZE_BYTES, &etiif->cmd_queue_mem);
	if (ret) {
		etdev_err(etdev, "Failed to allocate IIF mailbox queue (%d)", ret);
		goto err_mailbox_remove;
	}
	ret = edgetpu_mailbox_set_queue(mbx_hardware, GCIP_MAILBOX_CMD_QUEUE,
					etiif->cmd_queue_mem.dma_addr, QUEUE_SIZE);
	if (ret) {
		etdev_err(etdev, "Failed to set IIF mailbox queue (%d)", ret);
		goto err_free_cmd_queue;
	}
	edgetpu_mailbox_set_queue_as_unused(mbx_hardware, GCIP_MAILBOX_RESP_QUEUE);
	mutex_init(&etiif->cmd_queue_lock);
	args.cmd_queue = etiif->cmd_queue_mem.virt_addr;

	ret = gcip_mailbox_init(etiif->mbx_protocol, &args);
	if (ret)
		goto err_free_cmd_queue;
	edgetpu_mailbox_enable(mbx_hardware);

	return ret;

err_free_cmd_queue:
	edgetpu_iremap_free(etdev, &etiif->cmd_queue_mem);
err_mailbox_remove:
	edgetpu_mailbox_remove(mgr, mbx_hardware);
	etiif->mbx_hardware = NULL;

	return ret;
}
/* Helper to flush any blocked IIF commands when reinitializing/releasing the IIF mailbox. */
static void flush_pending_iif_commands(struct edgetpu_iif *etiif)
{
	unsigned long flags;

	spin_lock_irqsave(&etiif->flush_lock, flags);
	etiif->is_flushing = true;
	while (etiif->pending_signals) {
		spin_unlock_irqrestore(&etiif->flush_lock, flags);
		usleep_range(50, 200);
		spin_lock_irqsave(&etiif->flush_lock, flags);
	}
	spin_unlock_irqrestore(&etiif->flush_lock, flags);
}

void edgetpu_iif_release_mailbox(struct edgetpu_iif *etiif)
{
	/* Return immediately if the IIF mailbox was never initialized. */
	if (!etiif->mbx_protocol)
		return;

	flush_pending_iif_commands(etiif);

	gcip_mailbox_release(etiif->mbx_protocol);
	etiif->mbx_protocol = NULL;
	edgetpu_iremap_free(etiif->etdev, &etiif->cmd_queue_mem);
	edgetpu_mailbox_remove(etiif->etdev->mailbox_manager, etiif->mbx_hardware);
	etiif->mbx_hardware = NULL;
}

int edgetpu_iif_reinit_mailbox(struct edgetpu_iif *etiif)
{
	int ret;
	unsigned long flags;

	if (!etiif->mbx_protocol)
		return 0;

	flush_pending_iif_commands(etiif);
	spin_lock_irqsave(&etiif->flush_lock, flags);
	etiif->is_flushing = false;
	spin_unlock_irqrestore(&etiif->flush_lock, flags);

	edgetpu_mailbox_disable_doorbells(etiif->mbx_hardware);
	edgetpu_mailbox_clear_doorbells(etiif->mbx_hardware);
	ret = edgetpu_mailbox_set_queue(etiif->mbx_hardware, GCIP_MAILBOX_CMD_QUEUE,
					etiif->cmd_queue_mem.dma_addr, QUEUE_SIZE);
	if (ret)
		return ret;
	edgetpu_mailbox_set_queue_as_unused(etiif->mbx_hardware, GCIP_MAILBOX_RESP_QUEUE);
	edgetpu_mailbox_init_doorbells(etiif->mbx_hardware);
	edgetpu_mailbox_enable(etiif->mbx_hardware);

	return 0;
}

void edgetpu_iif_send_unblock_notification(struct edgetpu_iif *etiif, int fence_id)
{
	struct edgetpu_dev *etdev = etiif->etdev;
	struct edgetpu_vii_litebuf_command cmd;
	int ret;
	unsigned long flags;

	if (!etiif->mbx_protocol) {
		etdev_err_ratelimited(
			etdev, "Platform does not support TPU IIF signaling from AP (fence_id=%d)",
			fence_id);
		return;
	}

	spin_lock_irqsave(&etiif->flush_lock, flags);
	if (etiif->is_flushing) {
		spin_unlock_irqrestore(&etiif->flush_lock, flags);
		etdev_warn(etdev,
			   "Unable to send unblock, IIF mailbox is being flushed (fence_id=%d)",
			   fence_id);
		return;
	}
	etiif->pending_signals++;
	spin_unlock_irqrestore(&etiif->flush_lock, flags);

	/*
	 * Theoretically, the meaning of this function call is that MCU is waiting on some fences
	 * to proceed waiter commands so that the block should be already powered on. However,
	 * according to the design of IIF, if signaler IP is crashed, there is a possibility of race
	 * condition that the MCU has processed the commands before this notification which means
	 * the block would be already powered down. To prevent any kernel panic can be caused by it,
	 * acquire the wakelock if the block is powered on. Otherwise, just give up notifying MCU of
	 * the fence unblock.
	 */
	ret = edgetpu_pm_get_if_powered(etdev, true);
	if (ret) {
		etdev_warn(etdev,
			   "Unable to send IIF unblock notification due to the block being off");
		goto out_dec_pending_signals;
	}

	cmd.signal_fence_command.fence_id = fence_id;
	cmd.type = EDGETPU_VII_LITEBUF_SIGNAL_FENCE_COMMAND;

	ret = gcip_mailbox_send_cmd(etiif->mbx_protocol, &cmd, /*resp=*/NULL,
				    GCIP_MAILBOX_CMD_FLAGS_SKIP_ASSIGN_SEQ);
	if (ret)
		etdev_warn(etiif->etdev, "Failed to send IIF signal command, id=%d, error=%d",
			   fence_id, ret);

	edgetpu_pm_put(etdev);

out_dec_pending_signals:
	spin_lock_irqsave(&etiif->flush_lock, flags);
	etiif->pending_signals--;
	spin_unlock_irqrestore(&etiif->flush_lock, flags);
}
