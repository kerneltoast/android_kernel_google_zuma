// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Inter-IP fences.
 *
 * Copyright (C) 2025 Google LLC
 */

#include <linux/list.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <gcip/gcip-memory.h>

#include <iif/iif-fence.h>
#include <iif/iif-manager.h>
#include <iif/iif-shared.h>

#include "gxp-config.h"
#include "gxp-iif.h"
#include "gxp-internal.h"
#include "gxp-mailbox-driver.h"
#include "gxp-mailbox.h"
#include "gxp-mcu.h"
#include "gxp-uci.h"

#if HAS_TPU_EXT
#include <soc/google/tpu-ext.h>
#endif

/* Currently sized to fit command queue within 16K. */
#define MBOX_CMD_QUEUE_NUM_ENTRIES 256

/* Unblocked IIF to be propagated to the firmware. */
struct gxp_iif_unblocked {
	struct list_head node;
	int fence_id;
};

/*
 * Callback which will be called once @fence is unblocked.
 *
 * This function will add the fence to @mcu->giif->unblocked_list.
 */
static void gxp_iif_unblocked_handler(struct iif_fence *fence, void *data)
{
	struct gxp_dev *gxp = data;
	struct gxp_iif *giif = gxp_mcu_of(gxp)->giif;
	struct gxp_iif_unblocked *unblocked;

	if (fence->signal_error)
		dev_warn(gxp->dev, "IIF has been unblocked with an error, id=%d, error=%d",
			 fence->id, fence->signal_error);

	if (!fence->propagate)
		return;

	unblocked = kzalloc(sizeof(*unblocked), GFP_KERNEL);
	if (!unblocked)
		return;

	unblocked->fence_id = fence->id;

	spin_lock(&giif->enable_iif_mbox_lock);

	if (!giif->enable_iif_mbox) {
		spin_unlock(&giif->enable_iif_mbox_lock);
		dev_warn_ratelimited(gxp->dev, "Sending IIF unblock signal being disabled.");
		kfree(unblocked);
		return;
	}

	spin_lock(&giif->unblocked_lock);
	list_add_tail(&unblocked->node, &giif->unblocked_list);
	spin_unlock(&giif->unblocked_lock);

	spin_unlock(&giif->enable_iif_mbox_lock);

	/*
	 * The worker would handle all fences in unblocked_list, no need to check whether a pending
	 * work exists.
	 */
	schedule_work(&giif->unblocked_work);
}

static const struct iif_manager_ops iif_mgr_ops = {
	.fence_unblocked = gxp_iif_unblocked_handler,
};

/*
 * Work function which consumes @giif->unblocked_list and propagates the fence unblock to the
 * firmware.
 */
static void gxp_iif_unblocked_work_func(struct work_struct *work)
{
	struct gxp_iif *giif = container_of(work, struct gxp_iif, unblocked_work);
	struct gxp_iif_unblocked *cur, *nxt;
	LIST_HEAD(unblocked_list);

	spin_lock(&giif->unblocked_lock);
	list_replace_init(&giif->unblocked_list, &unblocked_list);
	spin_unlock(&giif->unblocked_lock);

	list_for_each_entry_safe(cur, nxt, &unblocked_list, node) {
		if (giif->use_iif_mbox)
			gxp_iif_send_unblock_notification(giif, cur->fence_id);
		else
			gxp_uci_send_iif_unblock_noti(&giif->mcu->uci, cur->fence_id);
		kfree(cur);
	}
}

/* Initializes the work which propagates the IIF unblock to the firmware. */
static void gxp_init_iif_unblocked_work(struct gxp_mcu *mcu)
{
	struct gxp_iif *giif = mcu->giif;

	INIT_LIST_HEAD(&giif->unblocked_list);
	spin_lock_init(&giif->unblocked_lock);
	INIT_WORK(&giif->unblocked_work, &gxp_iif_unblocked_work_func);
}

/* Cancels the work propagating the IIF unblock to the firmware. */
static void gxp_cancel_unblocked_work(struct gxp_mcu *mcu)
{
	struct gxp_iif *giif = mcu->giif;
	struct gxp_iif_unblocked *cur, *nxt;

	cancel_work_sync(&giif->unblocked_work);

	/*
	 * As the work is canceled and it will never be scheduled, we don't need to hold
	 * @giif->unblocked_lock.
	 */
	list_for_each_entry_safe(cur, nxt, &giif->unblocked_list, node) {
		kfree(cur);
	}
}

/* IIF manager and device. */

static void gxp_get_embedded_iif_mgr(struct gxp_dev *gxp)
{
	struct iif_manager *mgr;
	struct gxp_iif *giif = gxp_mcu_of(gxp)->giif;

#if HAS_TPU_EXT
	if (gxp->tpu_dev.dev) {
		int ret = edgetpu_ext_driver_cmd(gxp->tpu_dev.dev, EDGETPU_EXTERNAL_CLIENT_TYPE_DSP,
						 GET_IIF_MANAGER, NULL, &mgr);

		if (!ret) {
			dev_info(gxp->dev, "Use the IIF manager of TPU driver");
			/* Note that we shouldn't call `iif_manager_get` here. */
			giif->iif_mgr = mgr;
			return;
		}
	}
#endif /* HAS_TPU_EXT */

	dev_info(gxp->dev, "Try to get an embedded IIF manager");

	mgr = iif_manager_init(gxp->dev->of_node);
	if (IS_ERR(mgr)) {
		dev_warn(gxp->dev, "Failed to init an embedded IIF manager: %ld", PTR_ERR(mgr));
		return;
	}

	giif->iif_mgr = mgr;
}

static void gxp_get_iif_mgr(struct gxp_mcu *mcu)
{
	struct gxp_dev *gxp = mcu->gxp;
	struct platform_device *pdev;
	struct device_node *node;
	struct iif_manager *mgr;

	node = of_parse_phandle(gxp->dev->of_node, "iif-device", 0);
	if (IS_ERR_OR_NULL(node)) {
		dev_warn(gxp->dev, "There is no iif-device node in the device tree");
		goto get_embed;
	}

	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev) {
		dev_warn(gxp->dev, "Failed to find the IIF device");
		goto get_embed;
	}

	mgr = platform_get_drvdata(pdev);
	if (!mgr) {
		dev_warn(gxp->dev, "Failed to get a manager from IIF device");
		goto put_device;
	}

	dev_info(gxp->dev, "Use the IIF manager of IIF device");

	/* We don't need to call `get_device` since `of_find_device_by_node` takes a refcount. */
	mcu->giif->iif_dev = &pdev->dev;
	mcu->giif->iif_mgr = iif_manager_get(mgr);

	return;

put_device:
	put_device(&pdev->dev);
get_embed:
	gxp_get_embedded_iif_mgr(gxp);
}

static void gxp_put_iif_mgr(struct gxp_mcu *mcu)
{
	struct gxp_iif *giif = mcu->giif;

	if (giif->iif_mgr) {
		iif_manager_put(giif->iif_mgr);
		giif->iif_mgr = NULL;
	}
	/* No-op if `giif->iif_dev` is NULL. */
	put_device(giif->iif_dev);
}

/* Registers IIF operators of @gxp to IIF manager. */
static int gxp_register_iif_mgr_ops(struct gxp_mcu *mcu)
{
	struct gxp_dev *gxp = mcu->gxp;
	struct gxp_iif *giif = mcu->giif;

	if (!giif->iif_mgr)
		return -ENODEV;

	return iif_manager_register_ops(giif->iif_mgr, IIF_IP_DSP, &iif_mgr_ops, gxp);
}

/* Unregisters IIF operators of @gxp from IIF manager. */
static void gxp_unregister_iif_mgr_ops(struct gxp_mcu *mcu)
{
	struct gxp_iif *giif = mcu->giif;

	if (!giif->iif_mgr)
		return;
	iif_manager_unregister_ops(giif->iif_mgr, IIF_IP_DSP);
}

/* IIF Mailbox Ops. */

static int gxp_iif_acquire_resp_queue_lock(struct gcip_mailbox *mailbox, bool try, bool *atomic)
{
	/*
	 * Since the IIF mailbox has no response queue this function should never be called.
	 * As a protection against bugs, this function returns 0 (failure to acquire lock) so that
	 * any caller knows they cannot access the mailbox response queue, which is NULL.
	 */
	return 0;
}

static int gxp_iif_wait_for_cmd_queue_not_full(struct gcip_mailbox *mailbox)
{
	struct gxp_mailbox *gxp_mbx = mailbox->data;
	struct gxp_iif *giif = gxp_mbx->data;
	int ret, cnt = 20;
	/*
	 * Wait until firmware consumes one or more commands.
	 *
	 * This function relies on the MCU crash handling logic to handle MCU abnormal scenarios
	 * where no commands are being consumed in which case IIF mailbox will be disabled till
	 * the firmware again starts working normally.
	 *
	 * Waiting is done with usleep_range over udelay to ensure that if firmware becomes
	 * unresponsive, this thread's waiting does not impact the rest of the system. The
	 * trade-off is potentially higher latency should the queue become full without firmware
	 * hanging. This is acceptable as the queue is not expected to fill as long as firmware is
	 * actively running its scheduler thread.
	 */
	while (cnt--) {
		spin_lock(&giif->enable_iif_mbox_lock);
		ret = giif->enable_iif_mbox ?
			      gxp_mailbox_gcip_ops_wait_for_cmd_queue_not_full(mailbox) :
			      -ESHUTDOWN;
		spin_unlock(&giif->enable_iif_mbox_lock);

		if (ret != -EAGAIN)
			break;

		dev_warn_ratelimited(gxp_mbx->gxp->dev, "IIF queue full");

		if (cnt)
			usleep_range(50, 200);
	}

	return (cnt >= 0) ? ret : -ETIMEDOUT;
}

/* IIF signal commands have no sequence numbers nor command codes. */
static u64 gxp_iif_get_cmd_elem_seq(struct gcip_mailbox *mb, void *cmd)
{
	return 0;
}
static void gxp_iif_set_cmd_elem_seq(struct gcip_mailbox *mb, void *cmd, u64 seq)
{
	/* Not Implemented */
}
static u32 gxp_iif_get_cmd_elem_code(struct gcip_mailbox *mb, void *cmd)
{
	return 0;
}

/* IIF signal commands have no responses. */
static u32 gxp_iif_get_resp_queue_size(struct gcip_mailbox *mb)
{
	return 0;
}
static u32 gxp_iif_get_resp_queue_head(struct gcip_mailbox *mb)
{
	return 0;
}
static u32 gxp_iif_get_resp_queue_tail(struct gcip_mailbox *mb)
{
	return 0;
}
static void gxp_iif_inc_resp_queue_head(struct gcip_mailbox *mb, u32 inc)
{
	/* Not Implemented */
}
static void gxp_iif_release_resp_queue_lock(struct gcip_mailbox *mb)
{
	/* Not Implemented */
}
static u64 gxp_iif_get_resp_elem_seq(struct gcip_mailbox *mb, void *resp)
{
	return 0;
}
static void gxp_iif_set_resp_elem_seq(struct gcip_mailbox *mb, void *resp, u64 seq)
{
	/* Not Implemented */
}

const struct gcip_mailbox_ops gxp_iif_gcip_mbx_ops = {
	.get_cmd_queue_tail = gxp_mailbox_gcip_ops_get_cmd_queue_tail,
	.inc_cmd_queue_tail = gxp_mailbox_gcip_ops_inc_cmd_queue_tail,
	.acquire_cmd_queue_lock = gxp_mailbox_gcip_ops_acquire_cmd_queue_lock,
	.release_cmd_queue_lock = gxp_mailbox_gcip_ops_release_cmd_queue_lock,
	.get_cmd_elem_seq = gxp_iif_get_cmd_elem_seq,
	.set_cmd_elem_seq = gxp_iif_set_cmd_elem_seq,
	.get_cmd_elem_code = gxp_iif_get_cmd_elem_code,
	.get_resp_queue_size = gxp_iif_get_resp_queue_size,
	.get_resp_queue_head = gxp_iif_get_resp_queue_head,
	.get_resp_queue_tail = gxp_iif_get_resp_queue_tail,
	.inc_resp_queue_head = gxp_iif_inc_resp_queue_head,
	.acquire_resp_queue_lock = gxp_iif_acquire_resp_queue_lock,
	.release_resp_queue_lock = gxp_iif_release_resp_queue_lock,
	.get_resp_elem_seq = gxp_iif_get_resp_elem_seq,
	.set_resp_elem_seq = gxp_iif_set_resp_elem_seq,
	.wait_for_cmd_queue_not_full = gxp_iif_wait_for_cmd_queue_not_full,
	.after_enqueue_cmd = gxp_mailbox_gcip_ops_after_enqueue_cmd,
};

static int gxp_iif_mbx_allocate_resources(struct gxp_mailbox *mailbox,
					  struct gxp_virtual_device *vd, uint virt_core)
{
	struct gxp_iif *giif = mailbox->data;
	struct gxp_mcu *mcu = giif->mcu;
	int ret;

	/* Allocate and initialize the command queue */
	ret = gxp_mcu_mem_alloc_data(mcu, &giif->cmd_queue_mem,
				     sizeof(struct gxp_uci_command) * MBOX_CMD_QUEUE_NUM_ENTRIES);
	if (ret)
		goto err_cmd_queue;
	mailbox->cmd_queue_buf.vaddr = giif->cmd_queue_mem.virt_addr;
	mailbox->cmd_queue_buf.dsp_addr = giif->cmd_queue_mem.dma_addr;
	mailbox->cmd_queue_size = MBOX_CMD_QUEUE_NUM_ENTRIES;
	mailbox->cmd_queue_tail = 0;

	/* IIF mailbox has no response queue */
	mailbox->resp_queue_buf.vaddr = NULL;
	mailbox->resp_queue_buf.dsp_addr = 0;
	mailbox->resp_queue_size = 0;
	mailbox->resp_queue_head = 0;

	/* Allocate and initialize the mailbox descriptor */
	ret = gxp_mcu_mem_alloc_data(mcu, &giif->descriptor_mem,
				     sizeof(struct gxp_mailbox_descriptor));
	if (ret)
		goto err_descriptor;

	mailbox->descriptor_buf.vaddr = giif->descriptor_mem.virt_addr;
	mailbox->descriptor_buf.dsp_addr = giif->descriptor_mem.dma_addr;
	mailbox->descriptor = (struct gxp_mailbox_descriptor *)mailbox->descriptor_buf.vaddr;
	mailbox->descriptor->cmd_queue_device_addr = giif->cmd_queue_mem.dma_addr;
	mailbox->descriptor->resp_queue_device_addr = 0;
	mailbox->descriptor->cmd_queue_size = mailbox->cmd_queue_size;
	mailbox->descriptor->resp_queue_size = mailbox->resp_queue_size;

	return 0;

err_descriptor:
	gxp_mcu_mem_free_data(mcu, &giif->cmd_queue_mem);
err_cmd_queue:
	return ret;
}

static void gxp_iif_mbx_release_resources(struct gxp_mailbox *mailbox,
					  struct gxp_virtual_device *vd, uint virt_core)
{
	struct gxp_iif *giif = mailbox->data;

	gxp_mcu_mem_free_data(giif->mcu, &giif->descriptor_mem);
	gxp_mcu_mem_free_data(giif->mcu, &giif->cmd_queue_mem);
}

static struct gxp_mailbox_ops gxp_iif_mbx_ops = {
	.allocate_resources = gxp_iif_mbx_allocate_resources,
	.release_resources = gxp_iif_mbx_release_resources,
	.gcip_ops.mbx = &gxp_iif_gcip_mbx_ops,
};

static int gxp_iif_mailbox_init(struct gxp_iif *giif)
{
	struct gxp_mailbox_args mbx_args = {
		.type = GXP_MBOX_TYPE_GENERAL,
		.ops = &gxp_iif_mbx_ops,
		.queue_wrap_bit = IIF_CIRCULAR_QUEUE_WRAP_BIT,
		.cmd_elem_size = sizeof(struct gxp_uci_command),
		/* IIF mailbox queue being unidirectional has no response queue. */
		.resp_elem_size = 0,
		.data = giif,
	};

	giif->enable_iif_mbox = false;
	spin_lock_init(&giif->enable_iif_mbox_lock);

	giif->mbx =
		gxp_mailbox_alloc(giif->mcu->gxp->mailbox_mgr, NULL, 0, IIF_MAILBOX_ID, &mbx_args);
	if (IS_ERR(giif->mbx))
		return PTR_ERR(giif->mbx);

	return 0;
}

int gxp_iif_init(struct gxp_mcu *mcu)
{
	struct gxp_dev *gxp = mcu->gxp;
	struct gxp_iif *giif;
	int ret;

	giif = devm_kzalloc(gxp->dev, sizeof(*giif), GFP_KERNEL);
	if (!giif)
		return -ENOMEM;

	giif->mcu = mcu;
	mcu->giif = giif;
	giif->use_iif_mbox = gxp->num_mailboxes_compat > IIF_MAILBOX_ID;

	if (giif->use_iif_mbox) {
		ret = gxp_iif_mailbox_init(giif);
		if (ret)
			goto out;
	}

	gxp_get_iif_mgr(mcu);
	gxp_init_iif_unblocked_work(mcu);

	ret = gxp_register_iif_mgr_ops(mcu);
	if (ret) {
		dev_warn(gxp->dev, "Failed to register IIF ops, IIF disabled (ret=%d)", ret);
		goto cancel_unblocked_work;
	}

	return 0;

cancel_unblocked_work:
	gxp_cancel_unblocked_work(mcu);
	gxp_put_iif_mgr(mcu);
	if (giif->use_iif_mbox)
		gxp_mailbox_release(mcu->gxp->mailbox_mgr, NULL, 0, giif->mbx);
out:
	devm_kfree(gxp->dev, giif);
	mcu->giif = NULL;
	return ret;
}

int gxp_iif_reinit(struct gxp_iif *giif)
{
	struct gxp_mailbox *mailbox = giif->mbx;

	if (!giif->use_iif_mbox)
		return 0;

	gxp_mailbox_reinit(mailbox);

	return 0;
}

void gxp_iif_release(struct gxp_iif *giif)
{
	struct gxp_mcu *mcu = giif->mcu;

	gxp_unregister_iif_mgr_ops(mcu);
	gxp_cancel_unblocked_work(mcu);
	gxp_put_iif_mgr(mcu);
	if (giif->use_iif_mbox)
		gxp_mailbox_release(mcu->gxp->mailbox_mgr, NULL, 0, giif->mbx);
}

void gxp_iif_send_unblock_notification(struct gxp_iif *giif, int iif_id)
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
	ret = gcip_pm_get_if_powered(giif->mcu->gxp->power_mgr->pm, true);
	if (ret) {
		dev_warn(giif->mcu->gxp->dev,
			 "Block should be powered on before notifying IIF unblock");
		return;
	}

	cmd.type = IIF_UNBLOCK_COMMAND;
	cmd.iif_id = iif_id;

	ret = gxp_mailbox_send_cmd(giif->mbx, &cmd, NULL, GCIP_MAILBOX_CMD_FLAGS_SKIP_ASSIGN_SEQ);
	if (ret)
		dev_warn(giif->mcu->gxp->dev, "Failed to notify the IIF unblock: id=%d, ret=%d",
			 iif_id, ret);

	gcip_pm_put(giif->mcu->gxp->power_mgr->pm);
}

void gxp_iif_enable_iif_mbox(struct gxp_iif *giif)
{
	if (!giif->use_iif_mbox)
		return;

	spin_lock(&giif->enable_iif_mbox_lock);
	giif->enable_iif_mbox = true;
	spin_unlock(&giif->enable_iif_mbox_lock);
}

void gxp_iif_disable_iif_mbox(struct gxp_iif *giif)
{
	if (!giif->use_iif_mbox)
		return;

	spin_lock(&giif->enable_iif_mbox_lock);
	giif->enable_iif_mbox = false;
	spin_unlock(&giif->enable_iif_mbox_lock);

	gxp_cancel_unblocked_work(giif->mcu);
}
