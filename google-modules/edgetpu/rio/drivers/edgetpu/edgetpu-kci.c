// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel Control Interface, implements the protocol between AP kernel and TPU
 * firmware.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/bits.h>
#include <linux/circ_buf.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h> /* memcpy */

#include <gcip/gcip-firmware.h>
#include <gcip/gcip-memory.h>
#include <gcip/gcip-telemetry.h>
#include <gcip/gcip-usage-stats.h>

#include "edgetpu-config.h"
#include "edgetpu-debug.h"
#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mmu.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-usage-stats.h"

/* the index of mailbox for kernel should always be zero */
#define KERNEL_MAILBOX_INDEX 0

/* size of queue for KCI mailbox */
#define QUEUE_SIZE CIRC_QUEUE_MAX_SIZE(CIRC_QUEUE_WRAP_BIT)

/* Timeout for KCI responses from the firmware (milliseconds) */
#ifdef EDGETPU_KCI_TIMEOUT

#define KCI_TIMEOUT EDGETPU_KCI_TIMEOUT

#elif IS_ENABLED(CONFIG_EDGETPU_TEST) && !IS_ENABLED(CONFIG_DEBUG_KMEMLEAK)
/* fake-firmware could respond in a short time */
#define KCI_TIMEOUT	(200)
#else
/* Wait for up to 1 second for FW to respond. */
#define KCI_TIMEOUT	(1000)
#endif

static inline int check_etdev_state(struct edgetpu_kci *etkci, char *opstring)
{
	int ret = edgetpu_get_state_errno_locked(etkci->mailbox->etdev);

	if (ret)
		etdev_err(etkci->mailbox->etdev, "%s failed: device state %u (%d)",
			  opstring, etkci->mailbox->etdev->state, ret);
	return ret;
}

static int edgetpu_kci_alloc_queue(struct edgetpu_dev *etdev, struct edgetpu_mailbox *mailbox,
				   enum gcip_mailbox_queue_type type, struct gcip_memory *mem)
{
	u32 queue_size = QUEUE_SIZE;
	u32 size = queue_size * gcip_kci_queue_element_size(type);
	int ret;

	ret = edgetpu_iremap_alloc(etdev, size, mem);
	if (ret)
		return ret;

	ret = edgetpu_mailbox_set_queue(mailbox, type, mem->dma_addr, queue_size);
	if (ret) {
		etdev_err(etdev, "failed to set mailbox queue: %d", ret);
		edgetpu_iremap_free(etdev, mem);
		return ret;
	}

	return 0;
}

static void edgetpu_kci_free_queue(struct edgetpu_dev *etdev, struct gcip_memory *mem)
{
	edgetpu_iremap_free(etdev, mem);
}

/* IRQ handler of KCI mailbox. */
static void edgetpu_kci_handle_irq(struct edgetpu_mailbox *mailbox)
{
	struct edgetpu_kci *etkci = mailbox->internal.etkci;

	gcip_kci_handle_irq(etkci->kci);
}

/* Callback functions for struct gcip_kci. */

static inline u32 edgetpu_kci_get_cmd_queue_head(struct gcip_kci *kci)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	return EDGETPU_MAILBOX_CMD_QUEUE_READ(mailbox, head);
}

static inline u32 edgetpu_kci_get_cmd_queue_tail(struct gcip_kci *kci)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	return mailbox->cmd_queue_tail;
}

static inline void edgetpu_kci_inc_cmd_queue_tail(struct gcip_kci *kci, u32 inc)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	edgetpu_mailbox_inc_cmd_queue_tail(mailbox, inc);
}

static inline u32 edgetpu_kci_get_resp_queue_size(struct gcip_kci *kci)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	return mailbox->resp_queue_size;
}

static inline u32 edgetpu_kci_get_resp_queue_head(struct gcip_kci *kci)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	return mailbox->resp_queue_head;
}

static inline u32 edgetpu_kci_get_resp_queue_tail(struct gcip_kci *kci)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	return EDGETPU_MAILBOX_RESP_QUEUE_READ_SYNC(mailbox, tail);
}

static inline void edgetpu_kci_inc_resp_queue_head(struct gcip_kci *kci, u32 inc)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	edgetpu_mailbox_inc_resp_queue_head(mailbox, inc);
}

/* Handle one incoming request from firmware. */
static void edgetpu_reverse_kci_handle_response(struct gcip_kci *kci,
						struct gcip_kci_response_element *resp)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);
	struct edgetpu_dev *etdev = mailbox->etdev;

	/*
	 * GCIP_RKCI_CLIENT_FATAL_ERROR_NOTIFY does not have chip-specific behavior, despite
	 * being labeled as non-generic.
	 */
	if (resp->code <= GCIP_RKCI_CHIP_CODE_LAST &&
	    resp->code != GCIP_RKCI_CLIENT_FATAL_ERROR_NOTIFY) {
		edgetpu_soc_handle_reverse_kci(etdev, resp);
		return;
	}

	switch (resp->code) {
	case GCIP_RKCI_CLIENT_FATAL_ERROR_NOTIFY:
		edgetpu_handle_client_fatal_error_notify(etdev, resp->rkci_value2);
		break;
	case GCIP_RKCI_CLIENT_INACTIVITY_TIMEOUT:
		edgetpu_handle_client_inactivity_timeout(etdev, resp->rkci_value2);
		break;
	case GCIP_RKCI_FIRMWARE_CRASH:
		edgetpu_handle_firmware_crash(etdev, (enum gcip_fw_crash_type)resp->rkci_value2);
		break;
	case GCIP_RKCI_JOB_LOCKUP:
		edgetpu_handle_job_lockup(etdev, resp->rkci_value2);
		break;
#if EDGETPU_HAS_FW_DEBUG
	case GCIP_RKCI_DEBUG_ASYNC_RESP:
		edgetpu_fw_debug_resp_ready(etdev, resp->rkci_value1, resp->rkci_value2);
		break;
#endif
	default:
		etdev_warn(etdev, "Unrecognized RKCI request: %#x\n", resp->code);
	}
}

static int edgetpu_kci_update_usage_wrapper(struct gcip_kci *kci)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	return edgetpu_kci_update_usage(mailbox->etdev);
}

static inline void edgetpu_kci_trigger_doorbell(struct gcip_kci *kci,
						enum gcip_kci_doorbell_reason reason)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	if (reason == GCIP_KCI_PUSH_CMD)
		EDGETPU_MAILBOX_CMD_QUEUE_WRITE_SYNC(mailbox, doorbell_set, 1);
	else if (reason == GCIP_KCI_CONSUME_RESP)
		EDGETPU_MAILBOX_CMD_QUEUE_WRITE(mailbox, doorbell_set, 1);
}

static inline bool edgetpu_kci_is_block_off(struct gcip_kci *kci)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	return edgetpu_soc_pm_is_block_off(mailbox->etdev);
}

static void edgetpu_kci_on_error(struct gcip_kci *kci, int err)
{
	struct edgetpu_mailbox *mailbox = gcip_kci_get_data(kci);

	if (err == -ETIMEDOUT)
		edgetpu_debug_dump_cpu_regs(mailbox->etdev);
}

static const struct gcip_kci_ops kci_ops = {
	.get_cmd_queue_head = edgetpu_kci_get_cmd_queue_head,
	.get_cmd_queue_tail = edgetpu_kci_get_cmd_queue_tail,
	.inc_cmd_queue_tail = edgetpu_kci_inc_cmd_queue_tail,
	.get_resp_queue_size = edgetpu_kci_get_resp_queue_size,
	.get_resp_queue_head = edgetpu_kci_get_resp_queue_head,
	.get_resp_queue_tail = edgetpu_kci_get_resp_queue_tail,
	.inc_resp_queue_head = edgetpu_kci_inc_resp_queue_head,
	.trigger_doorbell = edgetpu_kci_trigger_doorbell,
	.reverse_kci_handle_response = edgetpu_reverse_kci_handle_response,
	.update_usage = edgetpu_kci_update_usage_wrapper,
	.is_block_off = edgetpu_kci_is_block_off,
	.on_error = edgetpu_kci_on_error,
};

int edgetpu_kci_init(struct edgetpu_mailbox_manager *mgr, struct edgetpu_kci *etkci)
{
	struct edgetpu_mailbox *mailbox = edgetpu_mailbox_kci(mgr);
	struct gcip_memory *cmd_queue_mem = &etkci->cmd_queue_mem;
	struct gcip_memory *resp_queue_mem = &etkci->resp_queue_mem;
	struct gcip_kci_args args = {
		.dev = mgr->etdev->dev,
		.queue_wrap_bit = CIRC_QUEUE_WRAP_BIT,
		.rkci_buffer_size = REVERSE_KCI_BUFFER_SIZE,
		.timeout = KCI_TIMEOUT,
		.ops = &kci_ops,
		.data = mailbox,
	};
	int ret;

	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (!etkci->kci) {
		etkci->kci = devm_kzalloc(mgr->etdev->dev, sizeof(*etkci->kci), GFP_KERNEL);
		if (!etkci->kci) {
			ret = -ENOMEM;
			goto err_free_mailbox;
		}
	}

	ret = edgetpu_kci_alloc_queue(mgr->etdev, mailbox, GCIP_MAILBOX_CMD_QUEUE, cmd_queue_mem);
	if (ret)
		goto err_free_mailbox;

	etdev_dbg(mgr->etdev, "%s: cmdq kva=%pK dma=%pad", __func__, cmd_queue_mem->virt_addr,
		  &cmd_queue_mem->dma_addr);

	ret = edgetpu_kci_alloc_queue(mgr->etdev, mailbox, GCIP_MAILBOX_RESP_QUEUE, resp_queue_mem);
	if (ret)
		goto err_free_cmd_queue;

	etdev_dbg(mgr->etdev, "%s: rspq kva=%pK dma=%pad", __func__, resp_queue_mem->virt_addr,
		  &resp_queue_mem->dma_addr);

	mailbox->handle_irq = edgetpu_kci_handle_irq;
	mailbox->internal.etkci = etkci;

	args.cmd_queue = cmd_queue_mem->virt_addr;
	args.resp_queue = resp_queue_mem->virt_addr;
	ret = gcip_kci_init(etkci->kci, &args);
	if (ret)
		goto err_free_resp_queue;

	etkci->mailbox = mailbox;
	edgetpu_mailbox_enable(mailbox);

	return 0;

err_free_resp_queue:
	edgetpu_kci_free_queue(mgr->etdev, resp_queue_mem);
err_free_cmd_queue:
	edgetpu_kci_free_queue(mgr->etdev, cmd_queue_mem);
err_free_mailbox:
	kfree(mailbox);
	return ret;
}

int edgetpu_kci_reinit(struct edgetpu_kci *etkci)
{
	struct edgetpu_mailbox *mailbox = etkci->mailbox;
	struct edgetpu_mailbox_manager *mgr;
	struct gcip_memory *cmd_queue_mem = &etkci->cmd_queue_mem;
	struct gcip_memory *resp_queue_mem = &etkci->resp_queue_mem;
	unsigned long flags;
	int ret;

	if (!mailbox)
		return -ENODEV;

	ret = edgetpu_mailbox_set_queue(mailbox, GCIP_MAILBOX_CMD_QUEUE, cmd_queue_mem->dma_addr,
					QUEUE_SIZE);
	if (ret)
		return ret;

	ret = edgetpu_mailbox_set_queue(mailbox, GCIP_MAILBOX_RESP_QUEUE, resp_queue_mem->dma_addr,
					QUEUE_SIZE);
	if (ret)
		return ret;

	mgr = mailbox->etdev->mailbox_manager;
	/* Restore KCI irq handler */
	write_lock_irqsave(&mgr->mailboxes_lock, flags);
	mailbox->handle_irq = edgetpu_kci_handle_irq;
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);

	edgetpu_mailbox_init_doorbells(mailbox);
	edgetpu_mailbox_enable(mailbox);

	return 0;
}

void edgetpu_kci_cancel_work_queues(struct edgetpu_kci *etkci)
{
	struct edgetpu_mailbox_manager *mgr;
	struct edgetpu_mailbox *mailbox = etkci->mailbox;
	unsigned long flags;

	if (mailbox) {
		mgr = mailbox->etdev->mailbox_manager;
		/* Remove IRQ handler to stop responding to interrupts */
		write_lock_irqsave(&mgr->mailboxes_lock, flags);
		mailbox->handle_irq = NULL;
		write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
	}
	gcip_kci_cancel_work_queues(etkci->kci);
}

void edgetpu_kci_release(struct edgetpu_dev *etdev, struct edgetpu_kci *etkci)
{
	if (!etkci || !etkci->kci)
		return;

	edgetpu_kci_cancel_work_queues(etkci);

	gcip_kci_release(etkci->kci);

	edgetpu_kci_free_queue(etdev, &etkci->cmd_queue_mem);
	edgetpu_kci_free_queue(etdev, &etkci->resp_queue_mem);

	etkci->mailbox = NULL;
}

static int edgetpu_kci_send_cmd_with_data(struct edgetpu_kci *etkci,
					  struct gcip_kci_command_element *cmd, const void *data,
					  size_t size)
{
	struct edgetpu_mailbox *mailbox = etkci->mailbox;
	struct edgetpu_dev *etdev = mailbox->etdev;
	struct gcip_memory mem;
	int ret;

	ret = edgetpu_iremap_alloc(etdev, size, &mem);
	if (ret)
		return ret;
	memcpy(mem.virt_addr, data, size);

	etdev_dbg(etdev, "%s: map kva=%pK dma=%pad", __func__, mem.virt_addr, &mem.dma_addr);

	cmd->dma.address = mem.dma_addr;
	cmd->dma.size = size;
	ret = gcip_kci_send_cmd(etkci->kci, cmd);
	edgetpu_iremap_free(etdev, &mem);
	etdev_dbg(etdev, "%s: unmap kva=%pK dma=%pad", __func__, mem.virt_addr, &mem.dma_addr);

	return ret;
}

int edgetpu_kci_map_log_buffer(const struct gcip_telemetry_kci_args *args)
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

int edgetpu_kci_map_trace_buffer(const struct gcip_telemetry_kci_args *args)
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

enum gcip_fw_flavor edgetpu_kci_fw_info(struct edgetpu_kci *etkci, struct gcip_fw_info *fw_info)
{
	struct edgetpu_mailbox *mailbox = etkci->mailbox;
	struct edgetpu_dev *etdev = mailbox->etdev;
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_EXCHANGE_INFO,
		.dma = {
			.address = 0,
			.size = 0,
		},
	};
	struct gcip_memory mem;
	struct gcip_kci_response_element resp;
	enum gcip_fw_flavor flavor = GCIP_FW_FLAVOR_UNKNOWN;
	int ret;

	ret = edgetpu_iremap_alloc(etdev, sizeof(*fw_info), &mem);

	/* If allocation failed still try handshake without full fw_info */
	if (ret) {
		etdev_warn(etdev, "error setting up fw info buffer: %d", ret);
		memset(fw_info, 0, sizeof(*fw_info));
	} else {
		memset(mem.virt_addr, 0, sizeof(*fw_info));
		cmd.dma.address = mem.dma_addr;
		cmd.dma.size = sizeof(*fw_info);
	}

	ret = gcip_kci_send_cmd_return_resp(etkci->kci, &cmd, &resp);
	if (cmd.dma.address) {
		memcpy(fw_info, mem.virt_addr, sizeof(*fw_info));
		edgetpu_iremap_free(etdev, &mem);
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
			etdev_dbg(etdev, "unrecognized fw flavor %#x\n",
				  fw_info->fw_flavor);
		}
	} else {
		etdev_dbg(etdev, "firmware flavor query returns %d\n", ret);
		if (ret < 0)
			flavor = ret;
		else
			flavor = -EIO;
	}

	return flavor;
}

int edgetpu_kci_update_usage(struct edgetpu_dev *etdev)
{
	int ret = -EAGAIN;

	/* Quick return if device is already powered down. */
	if (!edgetpu_pm_is_powered(etdev))
		return -EAGAIN;

	/*
	 * Lockout change in f/w load/unload status during usage update.
	 * Skip usage update if the firmware is being updated now or is not
	 * valid.
	 */
	if (!edgetpu_firmware_trylock(etdev))
		return -EAGAIN;

	if (edgetpu_firmware_status_locked(etdev) != GCIP_FW_VALID)
		goto fw_unlock;

	/* Test firmware doesn't implement usage stats, skip and return no error. */
	if (edgetpu_firmware_get_flavor(etdev) == GCIP_FW_FLAVOR_SYSTEST) {
		ret = 0;
		goto fw_unlock;
	}

	/*
	 * This function may run in a worker that is being canceled when the device is powering
	 * down, and the power down code holds the PM lock.
	 * Using trylock to prevent cancel_work_sync() waiting forever.
	 */
	if (!edgetpu_pm_trylock(etdev))
		goto fw_unlock;

	if (edgetpu_pm_is_powered(etdev))
		ret = edgetpu_kci_update_usage_locked(etdev);

	edgetpu_pm_unlock(etdev);

fw_unlock:
	edgetpu_firmware_unlock(etdev);

	if (ret)
		etdev_warn_once(etdev, "get firmware usage stats failed: %d", ret);
	return ret;
}

int edgetpu_kci_update_usage_locked(struct edgetpu_dev *etdev)
{
#define EDGETPU_USAGE_BUFFER_SIZE	4096
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_GET_USAGE_V2,
		.dma = {
			.address = 0,
			.size = 0,
			.flags = EDGETPU_USAGE_METRIC_VERSION,
		},
	};
	struct gcip_memory mem;
	struct gcip_kci_response_element resp;
	int ret;

	ret = edgetpu_iremap_alloc(etdev, EDGETPU_USAGE_BUFFER_SIZE, &mem);

	if (ret) {
		etdev_warn_once(etdev, "failed to allocate usage buffer");
		return ret;
	}

	cmd.dma.address = mem.dma_addr;
	cmd.dma.size = EDGETPU_USAGE_BUFFER_SIZE;
	memset(mem.virt_addr, 0, sizeof(struct gcip_usage_stats_header));
	ret = gcip_kci_send_cmd_return_resp(etdev->etkci->kci, &cmd, &resp);

	if (ret == GCIP_KCI_ERROR_UNIMPLEMENTED || ret == GCIP_KCI_ERROR_UNAVAILABLE) {
		etdev_dbg(etdev, "firmware does not report usage\n");
	} else if (ret == GCIP_KCI_ERROR_OK) {
		edgetpu_usage_stats_process_buffer(etdev, mem.virt_addr);
	} else if (ret != -ETIMEDOUT) {
		etdev_warn_once(etdev, "fw usage stats error %d", ret);
	}

	edgetpu_iremap_free(etdev, &mem);

	return ret;
}

/* debugfs mappings dump */
void edgetpu_kci_mappings_show(struct edgetpu_dev *etdev, struct seq_file *s)
{
	struct edgetpu_kci *etkci = etdev->etkci;
	struct gcip_memory *cmd_queue_mem = &etkci->cmd_queue_mem;
	struct gcip_memory *resp_queue_mem = &etkci->resp_queue_mem;
	struct edgetpu_iommu_domain *default_etdomain = edgetpu_mmu_default_domain(etdev);

	seq_printf(s, "kci pasid %u:\n", default_etdomain->pasid);
	seq_printf(s, "  %pad %lu cmdq\n", &cmd_queue_mem->dma_addr,
		   DIV_ROUND_UP(QUEUE_SIZE * gcip_kci_queue_element_size(GCIP_MAILBOX_CMD_QUEUE),
				PAGE_SIZE));
	seq_printf(s, "  %pad %lu rspq\n", &resp_queue_mem->dma_addr,
		   DIV_ROUND_UP(QUEUE_SIZE * gcip_kci_queue_element_size(GCIP_MAILBOX_RESP_QUEUE),
				PAGE_SIZE));
	edgetpu_telemetry_mappings_show(etdev, s);
	edgetpu_firmware_mappings_show(etdev, s);
}

int edgetpu_kci_shutdown(struct edgetpu_kci *etkci)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_SHUTDOWN,
	};

	return gcip_kci_send_cmd(etkci->kci, &cmd);
}

int edgetpu_kci_get_debug_dump(struct edgetpu_kci *etkci, tpu_addr_t tpu_addr, size_t size,
			       bool init_buffer)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_GET_DEBUG_DUMP,
		.dma = {
			.address = tpu_addr,
			.size = size,
			.flags = init_buffer,
		},
	};

	return gcip_kci_send_cmd(etkci->kci, &cmd);
}

int edgetpu_kci_open_device(struct edgetpu_kci *etkci, u32 mailbox_map, u32 client_priv, s16 vcid,
			    bool first_open)
{
	const struct edgetpu_kci_open_device_detail detail = {
		.client_priv = client_priv,
		.vcid = vcid,
		.flags = (mailbox_map << 1) | first_open,
	};
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_OPEN_DEVICE,
		.dma = {
			.flags = mailbox_map,
		},
	};
	int ret;

	ret = check_etdev_state(etkci, "open device");
	if (ret)
		return ret;
	if (vcid < 0)
		return gcip_kci_send_cmd(etkci->kci, &cmd);

	return edgetpu_kci_send_cmd_with_data(etkci, &cmd, &detail, sizeof(detail));
}

int edgetpu_kci_close_device(struct edgetpu_kci *etkci, u32 mailbox_map)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_CLOSE_DEVICE,
		.dma = {
			.flags = mailbox_map,
		},
	};
	int ret;

	ret = check_etdev_state(etkci, "close device");
	if (ret)
		return ret;
	return gcip_kci_send_cmd(etkci->kci, &cmd);
}

int edgetpu_kci_notify_throttling(struct edgetpu_dev *etdev, u32 level)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_NOTIFY_THROTTLING,
		.dma = {
			.flags = level,
		},
	};

	return gcip_kci_send_cmd(etdev->etkci->kci, &cmd);
}

int edgetpu_kci_block_bus_speed_control(struct edgetpu_dev *etdev, bool block)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_BLOCK_BUS_SPEED_CONTROL,
		.dma = {
			.flags = (u32) block,
		},
	};

	return gcip_kci_send_cmd(etdev->etkci->kci, &cmd);
}

int edgetpu_kci_allocate_vmbox(struct edgetpu_kci *etkci, u32 client_id, u8 slice_index,
			       bool first_open, bool first_party)
{
	struct edgetpu_kci_allocate_vmbox_detail detail = {
		.client_id = client_id,
		.slice_index = slice_index,
		.first_open = first_open,
		.first_party = first_party,
	};
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_ALLOCATE_VMBOX,
	};
	int ret;

	ret = check_etdev_state(etkci, "allocate vmbox");
	if (ret)
		return ret;

	return edgetpu_kci_send_cmd_with_data(etkci, &cmd, &detail, sizeof(detail));
}

int edgetpu_kci_release_vmbox(struct edgetpu_kci *etkci, u32 client_id)
{
	struct edgetpu_kci_release_vmbox_detail detail = {
		.client_id = client_id,
	};
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_RELEASE_VMBOX,
	};
	int ret;

	ret = check_etdev_state(etkci, "release vmbox");
	if (ret)
		return ret;

	return edgetpu_kci_send_cmd_with_data(etkci, &cmd, &detail, sizeof(detail));
}

int edgetpu_kci_firmware_tracing_level(void *data, unsigned long level, unsigned long *active_level)
{
	struct edgetpu_dev *etdev = data;
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_FIRMWARE_TRACING_LEVEL,
		.dma = {
			.flags = (u32)level,
		},
	};
	struct gcip_kci_response_element resp;
	int ret;

	ret = gcip_kci_send_cmd_return_resp(etdev->etkci->kci, &cmd, &resp);
	if (ret == GCIP_KCI_ERROR_OK)
		*active_level = resp.retval;

	return ret;
}

int edgetpu_kci_thermal_control(struct edgetpu_dev *etdev, bool enable)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_THERMAL_CONTROL,
		.dma = {
			.flags = (u32)enable,
		},
	};

	return gcip_kci_send_cmd(etdev->etkci->kci, &cmd);
}

int edgetpu_kci_set_device_properties(struct edgetpu_kci *etkci, struct edgetpu_dev_prop *dev_prop)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_SET_DEVICE_PROPERTIES,
	};
	int ret = 0;

	/* set_device_properties not enabled in production b/405471390 */
	if (!IS_ENABLED(CONFIG_EDGETPU_TEST))
		return -EIO;

	mutex_lock(&dev_prop->lock);
	if (!dev_prop->initialized)
		goto out;

	ret = edgetpu_kci_send_cmd_with_data(etkci, &cmd, &dev_prop->opaque,
					     sizeof(dev_prop->opaque));

out:
	mutex_unlock(&dev_prop->lock);
	return ret;
}

int edgetpu_kci_set_freq_limits(struct edgetpu_kci *etkci, u32 min_freq, u32 max_freq)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_SET_FREQ_LIMITS,
		.dma = {
			.size = min_freq,
			.flags = max_freq,
		},
	};

	return gcip_kci_send_cmd(etkci->kci, &cmd);
}

int edgetpu_kci_resp_rkci_ack(struct edgetpu_dev *etdev, struct gcip_kci_response_element *rkci_cmd)
{
	struct gcip_kci_command_element cmd = {
		.seq = rkci_cmd->seq,
		.code = GCIP_KCI_CODE_RKCI_ACK,
	};

	return gcip_kci_send_cmd(etdev->etkci->kci, &cmd);
}

bool edgetpu_kci_flush_rkci(struct edgetpu_dev *etdev)
{
	return flush_work(&etdev->etkci->kci->rkci.work);
}

int edgetpu_kci_fault_injection(struct gcip_fault_inject *injection)
{
	struct edgetpu_kci *etkci = injection->kci_data;
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_FAULT_INJECTION,
	};

	return edgetpu_kci_send_cmd_with_data(etkci, &cmd, injection->opaque,
					      sizeof(injection->opaque));
}

int edgetpu_kci_bcl_mitigation(struct edgetpu_kci *etkci,
			       struct edgetpu_kci_bcl_mitigation_config *config)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_BCL_MITIGATION,
		.dma = {
			.flags = 0x0,
		},
	};

	return edgetpu_kci_send_cmd_with_data(etkci, &cmd, config, sizeof(*config));
}

#if EDGETPU_HAS_FW_DEBUG
int edgetpu_kci_fw_debug_cmd(struct edgetpu_dev *etdev, dma_addr_t daddr, size_t count)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_DEBUG_CMD,
	};
	struct gcip_kci_response_element resp;
	int ret;

	cmd.dma.address = daddr;
	cmd.dma.size = count;
	ret = gcip_kci_send_cmd_return_resp(etdev->etkci->kci, &cmd, &resp);
	if (ret == GCIP_KCI_ERROR_OK)
		edgetpu_fw_debug_resp_ready(etdev, 0, resp.retval);
	return ret;
}

int edgetpu_kci_fw_debug_reset(struct edgetpu_dev *etdev)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_DEBUG_RESET,
	};
	struct gcip_kci_response_element resp;
	int ret;

	ret = gcip_kci_send_cmd_return_resp(etdev->etkci->kci, &cmd, &resp);
	return ret;
}

void edgetpu_kci_fw_send_debug_init(struct edgetpu_dev *etdev, dma_addr_t daddr, size_t count)
{
	struct gcip_kci_command_element cmd = {
		.code = GCIP_KCI_CODE_DEBUG_INIT,
	};

	cmd.dma.address = daddr;
	cmd.dma.size = count;
	gcip_kci_send_cmd(etdev->etkci->kci, &cmd);
}
#endif /* EDGETPU_HAS_FW_DEBUG */
