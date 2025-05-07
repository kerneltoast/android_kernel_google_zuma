// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel Control Interface, implements the protocol between DSP Kernel driver and MCU firmware.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <gcip/gcip-telemetry.h>
#include <gcip/gcip-usage-stats.h>

#include "gxp-config.h"
#include "gxp-core-telemetry.h"
#include "gxp-debug-dump.h"
#include "gxp-dma.h"
#include "gxp-kci.h"
#include "gxp-lpm.h"
#include "gxp-mailbox-driver.h"
#include "gxp-mailbox.h"
#include "gxp-mcu.h"
#include "gxp-pm.h"
#include "gxp-usage-stats.h"
#include "gxp-vd.h"
#include "mobile-soc.h"

#define GXP_MCU_USAGE_BUFFER_SIZE 4096

#define MBOX_CMD_QUEUE_NUM_ENTRIES 1024
#define MBOX_RESP_QUEUE_NUM_ENTRIES 1024

/*
 * `flags` in `gcip_kci_dma_descriptor` struct is used to pass the gxp kernel driver major and
 * minor version for the `GCIP_KCI_CODE_EXCHANGE_INFO` gcip_kci_code . First 16 bits of `flags`
 * represent the major version and last 16 bits represent the minor version.
 */
#define GXP_INTERFACE_VERSION_MAJOR_SHIFT 16

/* Callback functions for struct gcip_kci. */

static u32 gxp_kci_get_cmd_queue_head(struct gcip_kci *kci)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	return gxp_mailbox_read_cmd_queue_head(mbx);
}

static u32 gxp_kci_get_cmd_queue_tail(struct gcip_kci *kci)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	return mbx->cmd_queue_tail;
}

static void gxp_kci_inc_cmd_queue_tail(struct gcip_kci *kci, u32 inc)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	gxp_mailbox_inc_cmd_queue_tail_nolock(mbx, inc, KCI_CIRCULAR_QUEUE_WRAP_BIT);
}


static u32 gxp_kci_get_resp_queue_size(struct gcip_kci *kci)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	return mbx->resp_queue_size;
}

static u32 gxp_kci_get_resp_queue_head(struct gcip_kci *kci)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	return mbx->resp_queue_head;
}

static u32 gxp_kci_get_resp_queue_tail(struct gcip_kci *kci)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	return gxp_mailbox_read_resp_queue_tail(mbx);
}

static void gxp_kci_inc_resp_queue_head(struct gcip_kci *kci, u32 inc)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	gxp_mailbox_inc_resp_queue_head_nolock(mbx, inc, KCI_CIRCULAR_QUEUE_WRAP_BIT);
}

static void gxp_kci_handle_rkci(struct gxp_kci *gkci,
				struct gcip_kci_response_element *resp)
{
	struct gxp_dev *gxp = gkci->gxp;

	switch (resp->code) {
	case GXP_RKCI_CODE_PM_QOS_BTS:
		/* FW indicates to ignore the request by setting them to undefined values. */
		if (resp->retval != (typeof(resp->retval))~0ull)
			gxp_soc_pm_set_request(gxp, MEMORY_INT_QOS_REQ, resp->retval);
		if (resp->status != (typeof(resp->status))~0ull)
			dev_warn_once(gxp->dev, "BTS is not supported");
		gxp_kci_resp_rkci_ack(gkci, resp);
		break;
	case GCIP_RKCI_COHERENT_FABRIC_QOS_REQUEST:
		gxp_soc_pm_set_request(gxp, COHERENT_FABRIC_QOS_REQ, resp->retval);
		gxp_kci_resp_rkci_ack(gkci, resp);
		break;
	case GXP_RKCI_CODE_CORE_TELEMETRY_READ: {
		uint core;
		uint core_list = (uint)(resp->status);

		for (core = 0; core < GXP_NUM_CORES; core++) {
			if (BIT(core) & core_list) {
				gxp_core_telemetry_status_notify(gxp, core);
			}
		}
		gxp_kci_resp_rkci_ack(gkci, resp);
		break;
	}
	case GCIP_RKCI_CLIENT_FATAL_ERROR_NOTIFY: {
		int client_id = (int)(resp->retval);

		/*
		 * Inside gxp_vd_invalidate_with_client_id(), after invalidating the client,
		 * synchronous call to gxp_kci_release_vmbox() would be made post which debug
		 * dump if enabled would be checked and processed. Due to debug dump processing
		 * being a time consuming task, rkci ack is sent first to unblock the mcu to send
		 * further rkci's.
		 */
		gxp_kci_resp_rkci_ack(gkci, resp);
		gxp_vd_invalidate_with_client_id(gxp, client_id, true);

		break;
	}
	default:
		dev_warn(gxp->dev, "Unrecognized reverse KCI request: %#x",
			 resp->code);
	}
}

/* Handle one incoming request from firmware. */
static void
gxp_reverse_kci_handle_response(struct gcip_kci *kci,
				struct gcip_kci_response_element *resp)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);
	struct gxp_dev *gxp = mbx->gxp;
	struct gxp_kci *gxp_kci = mbx->data;
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);

	if (resp->code <= GCIP_RKCI_CHIP_CODE_LAST) {
		gxp_kci_handle_rkci(gxp_kci, resp);
		return;
	}

	switch (resp->code) {
	case GCIP_RKCI_FIRMWARE_CRASH:
		if (resp->retval == GCIP_FW_CRASH_UNRECOVERABLE_FAULT)
			schedule_work(&mcu_fw->fw_crash_handler_work);
		else
			dev_warn(gxp->dev, "MCU non-fatal crash: %u", resp->retval);
		break;
	case GCIP_RKCI_JOB_LOCKUP:
		dev_dbg(gxp->dev, "Job lookup received from MCU firmware");
		break;
	default:
		dev_warn(gxp->dev, "%s: Unrecognized KCI request: %#x\n",
			 __func__, resp->code);
	}
}

static int gxp_kci_update_usage_wrapper(struct gcip_kci *kci)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);
	struct gxp_kci *gkci = mbx->data;

	return gxp_kci_update_usage(gkci);
}

static inline void
gxp_kci_trigger_doorbell(struct gcip_kci *kci,
			 enum gcip_kci_doorbell_reason reason)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	/* triggers doorbell */
	gxp_mailbox_generate_device_interrupt(mbx, BIT(0));
}

static inline bool gxp_kci_is_block_off(struct gcip_kci *kci)
{
	struct gxp_mailbox *mbx = gcip_kci_get_data(kci);

	return gxp_pm_is_blk_down(mbx->gxp);
}

static const struct gcip_kci_ops kci_ops = {
	.get_cmd_queue_head = gxp_kci_get_cmd_queue_head,
	.get_cmd_queue_tail = gxp_kci_get_cmd_queue_tail,
	.inc_cmd_queue_tail = gxp_kci_inc_cmd_queue_tail,
	.get_resp_queue_size = gxp_kci_get_resp_queue_size,
	.get_resp_queue_head = gxp_kci_get_resp_queue_head,
	.get_resp_queue_tail = gxp_kci_get_resp_queue_tail,
	.inc_resp_queue_head = gxp_kci_inc_resp_queue_head,
	.trigger_doorbell = gxp_kci_trigger_doorbell,
	.reverse_kci_handle_response = gxp_reverse_kci_handle_response,
	.update_usage = gxp_kci_update_usage_wrapper,
	.is_block_off = gxp_kci_is_block_off,
};

/* Callback functions for struct gxp_mailbox. */

static int gxp_kci_allocate_resources(struct gxp_mailbox *mailbox,
				      struct gxp_virtual_device *vd,
				      uint virt_core)
{
	struct gxp_kci *gkci = mailbox->data;
	int ret;

	/* Allocate and initialize the command queue */
	ret = gxp_mcu_mem_alloc_data(gkci->mcu, &gkci->cmd_queue_mem,
				     sizeof(struct gcip_kci_command_element) *
					     MBOX_CMD_QUEUE_NUM_ENTRIES);
	if (ret)
		goto err_cmd_queue;
	mailbox->cmd_queue_buf.vaddr = gkci->cmd_queue_mem.vaddr;
	mailbox->cmd_queue_buf.dsp_addr = gkci->cmd_queue_mem.daddr;
	mailbox->cmd_queue_size = MBOX_CMD_QUEUE_NUM_ENTRIES;
	mailbox->cmd_queue_tail = 0;

	/* Allocate and initialize the response queue */
	ret = gxp_mcu_mem_alloc_data(gkci->mcu, &gkci->resp_queue_mem,
				     sizeof(struct gcip_kci_response_element) *
					     MBOX_RESP_QUEUE_NUM_ENTRIES);
	if (ret)
		goto err_resp_queue;
	mailbox->resp_queue_buf.vaddr = gkci->resp_queue_mem.vaddr;
	mailbox->resp_queue_buf.dsp_addr = gkci->resp_queue_mem.daddr;
	mailbox->resp_queue_size = MBOX_CMD_QUEUE_NUM_ENTRIES;
	mailbox->resp_queue_head = 0;

	/* Allocate and initialize the mailbox descriptor */
	ret = gxp_mcu_mem_alloc_data(gkci->mcu, &gkci->descriptor_mem,
				     sizeof(struct gxp_mailbox_descriptor));
	if (ret)
		goto err_descriptor;

	mailbox->descriptor_buf.vaddr = gkci->descriptor_mem.vaddr;
	mailbox->descriptor_buf.dsp_addr = gkci->descriptor_mem.daddr;
	mailbox->descriptor =
		(struct gxp_mailbox_descriptor *)mailbox->descriptor_buf.vaddr;
	mailbox->descriptor->cmd_queue_device_addr =
		mailbox->cmd_queue_buf.dsp_addr;
	mailbox->descriptor->resp_queue_device_addr =
		mailbox->resp_queue_buf.dsp_addr;
	mailbox->descriptor->cmd_queue_size = mailbox->cmd_queue_size;
	mailbox->descriptor->resp_queue_size = mailbox->resp_queue_size;

	return 0;

err_descriptor:
	gxp_mcu_mem_free_data(gkci->mcu, &gkci->resp_queue_mem);
err_resp_queue:
	gxp_mcu_mem_free_data(gkci->mcu, &gkci->cmd_queue_mem);
err_cmd_queue:
	return -ENOMEM;
}

static void gxp_kci_release_resources(struct gxp_mailbox *mailbox, struct gxp_virtual_device *vd,
				      uint virt_core)
{
	struct gxp_kci *gkci = mailbox->data;

	gxp_mcu_mem_free_data(gkci->mcu, &gkci->descriptor_mem);
	gxp_mcu_mem_free_data(gkci->mcu, &gkci->resp_queue_mem);
	gxp_mcu_mem_free_data(gkci->mcu, &gkci->cmd_queue_mem);
}

static struct gxp_mailbox_ops mbx_ops = {
	.allocate_resources = gxp_kci_allocate_resources,
	.release_resources = gxp_kci_release_resources,
	.gcip_ops.kci = &kci_ops,
};

/*
 * Wrapper function of the `gxp_mailbox_send_cmd` which passes @resp as NULL.
 *
 * KCI sends all commands as synchronous, but the caller will not utilize the responses by passing
 * the pointer of `struct gcip_kci_response_element` to the @resp of the `gxp_mailbox_send_cmd`
 * function which is the simple wrapper function of the `gcip_kci_send_cmd` function.
 *
 * Even though the caller passes the pointer of `struct gcip_kci_response_element`, it will be
 * ignored. The `gcip_kci_send_cmd` function creates a temporary instance of that struct internally
 * and returns @code of the instance as its return value.
 *
 * If the caller needs the `struct gcip_kci_response_element` as the response, it should use the
 * `gcip_kci_send_cmd_return_resp` function directly.
 * (See the implementation of `gcip-kci.c`.)
 *
 * In some commands, such as the `fw_info` KCI command, if the firmware should have to return
 * a response which is not fit into the `struct gcip_kci_response_element`, the caller will
 * allocate a buffer for it to @cmd->dma and the firmware will write the response to it.
 */
static inline int gxp_kci_send_cmd(struct gxp_mailbox *mailbox,
				   struct gcip_kci_command_element *cmd)
{
	int ret;

	gxp_pm_busy(mailbox->gxp);
	ret = gxp_mailbox_send_cmd(mailbox, cmd, NULL, 0);
	gxp_pm_idle(mailbox->gxp);

	return ret;
}

/**
 * gxp_kci_send_cmd_with_data() - Sends the KCI command with given kci code and data.
 * @gkci: The container of gxp_mailbox and gxp_mcu.
 * @code: The KCI code of the command.
 * @data: The pointer of the data to be sent.
 * @size: The size of the data.
 *
 * Return: Returns error number if failed.
 */
static int gxp_kci_send_cmd_with_data(struct gxp_kci *gkci, u16 code, const void *data, size_t size)
{
	struct gcip_kci_command_element cmd = {
		.code = code,
	};
	struct gxp_mapped_resource buf;
	int ret;

	if (gxp_mcu_mem_alloc_data(gkci->mcu, &buf, size))
		return -ENOSPC;

	memcpy(buf.vaddr, data, size);
	cmd.dma.address = buf.daddr;
	cmd.dma.size = size;

	ret = gxp_kci_send_cmd(gkci->mbx, &cmd);

	gxp_mcu_mem_free_data(gkci->mcu, &buf);

	return ret;
}

int gxp_kci_init(struct gxp_mcu *mcu)
{
	struct gxp_dev *gxp = mcu->gxp;
	struct gxp_kci *gkci = &mcu->kci;
	struct gxp_mailbox_args mbx_args = {
		.type = GXP_MBOX_TYPE_KCI,
		.ops = &mbx_ops,
		.queue_wrap_bit = KCI_CIRCULAR_QUEUE_WRAP_BIT,
		.cmd_elem_size = sizeof(struct gcip_kci_command_element),
		.resp_elem_size = sizeof(struct gcip_kci_response_element),
		.data = gkci,
	};

	gkci->gxp = gxp;
	gkci->mcu = mcu;
	gkci->mbx = gxp_mailbox_alloc(gxp->mailbox_mgr, NULL, 0, KCI_MAILBOX_ID,
				      &mbx_args);
	if (IS_ERR(gkci->mbx))
		return PTR_ERR(gkci->mbx);

	gkci->enable_rkci_ack = true;
	init_rwsem(&gkci->enable_rkci_ack_lock);

	return 0;
}

int gxp_kci_reinit(struct gxp_kci *gkci)
{
	struct gxp_mailbox *mailbox = gkci->mbx;

	gxp_mailbox_reinit(mailbox);
	gcip_kci_reinit(mailbox->mbx_impl.gcip_kci);

	return 0;
}

void gxp_kci_enable_irq_handler(struct gxp_kci *gkci)
{
	gxp_mailbox_enable_irq_handler(gkci->mbx);
}

void gxp_kci_disable_irq_handler(struct gxp_kci *gkci)
{
	gxp_mailbox_disable_irq_handler(gkci->mbx);
}

void gxp_kci_enable_rkci_ack(struct gxp_kci *gkci)
{
	down_write(&gkci->enable_rkci_ack_lock);
	gkci->enable_rkci_ack = true;
	up_write(&gkci->enable_rkci_ack_lock);
}

void gxp_kci_disable_rkci_ack(struct gxp_kci *gkci)
{
	down_write(&gkci->enable_rkci_ack_lock);
	gkci->enable_rkci_ack = false;
	up_write(&gkci->enable_rkci_ack_lock);
}

void gxp_kci_cancel_work_queues(struct gxp_kci *gkci)
{
	if (gkci->mbx)
		gcip_kci_cancel_work_queues(gkci->mbx->mbx_impl.gcip_kci);
}

void gxp_kci_exit(struct gxp_kci *gkci)
{
	if (IS_GXP_TEST && (!gkci || !gkci->mbx))
		return;
	gxp_mailbox_release(gkci->gxp->mailbox_mgr, NULL, 0, gkci->mbx);
	gkci->mbx = NULL;
}

enum gcip_fw_flavor gxp_kci_fw_info(struct gxp_kci *gkci,
				    struct gcip_fw_info *fw_info)
{
	struct gxp_dev *gxp = gkci->gxp;
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_EXCHANGE_INFO,
		.dma = {
			.address = 0,
			.size = 0,
			.flags =
				(GXP_INTERFACE_VERSION_MAJOR << GXP_INTERFACE_VERSION_MAJOR_SHIFT) |
				GXP_INTERFACE_VERSION_MINOR,
		},
	};
	enum gcip_fw_flavor flavor = GCIP_FW_FLAVOR_UNKNOWN;
	struct gxp_mapped_resource buf;
	int ret;

	buf.paddr = 0;
	ret = gxp_mcu_mem_alloc_data(gkci->mcu, &buf, sizeof(*fw_info));
	/* If allocation failed still try handshake without full fw_info */
	if (ret) {
		dev_warn(gxp->dev, "%s: error setting up fw info buffer: %d",
			 __func__, ret);
		memset(fw_info, 0, sizeof(*fw_info));
	} else {
		memset(buf.vaddr, 0, sizeof(*fw_info));
		cmd.dma.address = buf.daddr;
		cmd.dma.size = sizeof(*fw_info);
	}

	ret = gxp_kci_send_cmd(gkci->mbx, &cmd);
	if (buf.paddr) {
		memcpy(fw_info, buf.vaddr, sizeof(*fw_info));
		gxp_mcu_mem_free_data(gkci->mcu, &buf);
	}

	if (ret == GCIP_KCI_ERROR_OK) {
		switch (fw_info->fw_flavor) {
		case GCIP_FW_FLAVOR_BL1:
		case GCIP_FW_FLAVOR_SYSTEST:
		case GCIP_FW_FLAVOR_PROD_DEFAULT:
		case GCIP_FW_FLAVOR_CUSTOM:
			flavor = fw_info->fw_flavor;
			break;
		default:
			dev_dbg(gxp->dev, "unrecognized fw flavor %#x\n",
				fw_info->fw_flavor);
		}
	} else {
		dev_dbg(gxp->dev, "firmware flavor query returns %d\n", ret);
		if (ret < 0)
			flavor = ret;
		else
			flavor = -EIO;
	}

	return flavor;
}

int gxp_kci_update_usage(struct gxp_kci *gkci)
{
	struct gxp_power_manager *power_mgr = gkci->gxp->power_mgr;
	struct gxp_mcu_firmware *fw = &gkci->mcu->fw;
	int ret = -EAGAIN;

	/* Quick return if device is already powered down. */
	if (power_mgr->curr_state == AUR_OFF ||
	    !gxp_lpm_is_powered(gkci->gxp, CORE_TO_PSM(GXP_REG_MCU_ID)))
		return -EAGAIN;

	/*
	 * Lockout change in f/w load/unload status during usage update.
	 * Skip usage update if the firmware is being updated now or is not
	 * valid.
	 */
	if (!mutex_trylock(&fw->lock))
		return -EAGAIN;

	if (fw->status != GCIP_FW_VALID)
		goto fw_unlock;

	/*
	 * This function may run in a worker that is being canceled when the
	 * device is powering down, and the power down code holds the PM lock.
	 * Using trylock to prevent cancel_work_sync() waiting forever.
	 */
	if (!mutex_trylock(&power_mgr->pm_lock))
		goto fw_unlock;

	if (power_mgr->curr_state != AUR_OFF &&
	    gxp_lpm_is_powered(gkci->gxp, CORE_TO_PSM(GXP_REG_MCU_ID)))
		ret = gxp_kci_update_usage_locked(gkci);
	mutex_unlock(&power_mgr->pm_lock);

fw_unlock:
	mutex_unlock(&fw->lock);

	return ret;
}

void gxp_kci_update_usage_async(struct gxp_kci *gkci)
{
	gcip_kci_update_usage_async(gkci->mbx->mbx_impl.gcip_kci);
}

int gxp_kci_update_usage_locked(struct gxp_kci *gkci)
{
	struct gxp_dev *gxp = gkci->gxp;
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_GET_USAGE_V2,
		.dma = {
			.address = 0,
			.size = 0,
			.flags = GCIP_USAGE_STATS_V2,
		},
	};
	struct gxp_mapped_resource buf;
	int ret;

	if (!gkci || !gkci->mbx)
		return -ENODEV;

	ret = gxp_mcu_mem_alloc_data(gkci->mcu, &buf, GXP_MCU_USAGE_BUFFER_SIZE);
	if (ret) {
		dev_warn_once(gxp->dev, "Failed to allocate usage buffer");
		return -ENOMEM;
	}

retry_v1:
	if (gxp->usage_stats && gxp->usage_stats->ustats.version == GCIP_USAGE_STATS_V1)
		cmd.code = GCIP_KCI_CODE_GET_USAGE_V1;

	cmd.dma.address = buf.daddr;
	cmd.dma.size = GXP_MCU_USAGE_BUFFER_SIZE;
	memset(buf.vaddr, 0, sizeof(struct gcip_usage_stats_header));
	ret = gxp_kci_send_cmd(gkci->mbx, &cmd);

	if (ret == GCIP_KCI_ERROR_UNIMPLEMENTED || ret == GCIP_KCI_ERROR_UNAVAILABLE) {
		if (gxp->usage_stats && gxp->usage_stats->ustats.version != GCIP_USAGE_STATS_V1) {
			gxp->usage_stats->ustats.version = GCIP_USAGE_STATS_V1;
			goto retry_v1;
		}
		dev_dbg(gxp->dev, "Firmware does not report usage");
	} else if (ret == GCIP_KCI_ERROR_OK) {
		gxp_usage_stats_process_buffer(gxp, buf.vaddr);
	} else if (ret != -ETIMEDOUT) {
		dev_warn_once(gxp->dev, "Failed to send GET_USAGE KCI, ret=%d", ret);
	}

	gxp_mcu_mem_free_data(gkci->mcu, &buf);

	return ret;
}

int gxp_kci_map_mcu_log_buffer(struct gcip_telemetry_kci_args *args)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_MAP_LOG_BUFFER,
		.dma = {
			.address = args->addr,
			.size = args->size,
		},
	};

	return gcip_kci_send_cmd(args->kci, &cmd);
}

int gxp_kci_map_mcu_trace_buffer(struct gcip_telemetry_kci_args *args)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_MAP_TRACE_BUFFER,
		.dma = {
			.address = args->addr,
			.size = args->size,
		},
	};

	return gcip_kci_send_cmd(args->kci, &cmd);
}

int gxp_kci_shutdown(struct gxp_kci *gkci)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_SHUTDOWN,
	};

	if (!gkci || !gkci->mbx)
		return -ENODEV;

	return gxp_kci_send_cmd(gkci->mbx, &cmd);
}

int gxp_kci_allocate_vmbox(struct gxp_kci *gkci, u32 client_id, u8 num_cores,
			   u8 slice_index, bool first_open)
{
	const struct gxp_kci_allocate_vmbox_detail detail = { .client_id = client_id,
							      .num_cores = num_cores,
							      .slice_index = slice_index,
							      .first_open = first_open };

	return gxp_kci_send_cmd_with_data(gkci, GCIP_KCI_CODE_ALLOCATE_VMBOX, &detail,
					  sizeof(detail));
}

int gxp_kci_release_vmbox(struct gxp_kci *gkci, u32 client_id)
{
	const struct gxp_kci_release_vmbox_detail detail = { .client_id = client_id };

	return gxp_kci_send_cmd_with_data(gkci, GCIP_KCI_CODE_RELEASE_VMBOX, &detail,
					  sizeof(detail));
}

int gxp_kci_link_unlink_offload_vmbox(
	struct gxp_kci *gkci, u32 client_id, u32 offload_client_id,
	enum gcip_kci_offload_chip_type offload_chip_type, bool link)
{
	u16 code = link ? GCIP_KCI_CODE_LINK_OFFLOAD_VMBOX : GCIP_KCI_CODE_UNLINK_OFFLOAD_VMBOX;
	const struct gxp_kci_link_unlink_offload_vmbox_detail detail = {
		.client_id = client_id,
		.offload_client_id = offload_client_id,
		.offload_chip_type = offload_chip_type
	};

	return gxp_kci_send_cmd_with_data(gkci, code, &detail, sizeof(detail));
}

int gxp_kci_notify_throttling(struct gxp_kci *gkci, u32 rate)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_NOTIFY_THROTTLING,
		.dma = {
			.flags = rate,
		},
	};

	if (!gkci || !gkci->mbx)
		return -ENODEV;

	return gxp_kci_send_cmd(gkci->mbx, &cmd);
}

void gxp_kci_resp_rkci_ack(struct gxp_kci *gkci,
			   struct gcip_kci_response_element *rkci_cmd)
{
	struct gcip_kci_command_element cmd = {
		.seq = rkci_cmd->seq,
		.code = GCIP_KCI_CODE_RKCI_ACK,
	};
	struct gxp_dev *gxp = gkci->gxp;
	int ret;

	down_read(&gkci->enable_rkci_ack_lock);

	if (!gkci->enable_rkci_ack) {
		dev_warn_ratelimited(gxp->dev, "Skip sending RKCI ACK");
		goto out;
	}

	ret = gxp_kci_send_cmd(gkci->mbx, &cmd);
	if (ret)
		dev_err(gxp->dev, "failed to send rkci resp %llu (%d)",
			rkci_cmd->seq, ret);
out:
	up_read(&gkci->enable_rkci_ack_lock);
}

int gxp_kci_set_device_properties(struct gxp_kci *gkci,
				  struct gxp_dev_prop *dev_prop)
{
	int ret = 0;

	mutex_lock(&dev_prop->lock);
	if (!dev_prop->initialized)
		goto out;

	ret = gxp_kci_send_cmd_with_data(gkci, GCIP_KCI_CODE_SET_DEVICE_PROPERTIES,
					 &dev_prop->opaque, sizeof(dev_prop->opaque));

out:
	mutex_unlock(&dev_prop->lock);
	return ret;
}

int gxp_kci_fault_injection(struct gcip_fault_inject *injection)
{
	return gxp_kci_send_cmd_with_data(injection->kci_data, GCIP_KCI_CODE_FAULT_INJECTION,
					  injection->opaque, sizeof(injection->opaque));
}

int gxp_kci_set_freq_limits(struct gxp_kci *gkci, u32 min_freq, u32 max_freq)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_SET_FREQ_LIMITS,
		.dma = {
			.size = min_freq,
			.flags = max_freq,
		},
	};
	return gxp_kci_send_cmd(gkci->mbx, &cmd);
}

int gxp_kci_thermal_control(struct gxp_kci *gkci, bool enable)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_THERMAL_CONTROL,
		.dma = {
			.flags = (u32)enable,
		},
	};
	return gxp_kci_send_cmd(gkci->mbx, &cmd);
}
