// SPDX-License-Identifier: GPL-2.0
/*
 * Rio Edge TPU ML accelerator device host support.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/irqreturn.h>
#include <linux/uaccess.h>

#include "edgetpu-config.h"
#include "edgetpu-debug-dump.h"
#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-telemetry.h"
#include "mobile-pm.h"
#include "rio-platform.h"

static irqreturn_t rio_mailbox_handle_irq(struct edgetpu_dev *etdev, int irq)
{
	struct edgetpu_mailbox *mailbox;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mailbox_manager *mgr = etdev->mailbox_manager;
	uint i;

	if (!mgr)
		return IRQ_NONE;
	for (i = 0; i < etmdev->n_irq; i++)
		if (etmdev->irq[i] == irq)
			break;
	if (i == etmdev->n_irq)
		return IRQ_NONE;
	read_lock(&mgr->mailboxes_lock);
	mailbox = mgr->mailboxes[i];
	if (!mailbox)
		goto out;
	if (!EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, doorbell_status))
		goto out;
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, doorbell_clear, 1);
	etdev_dbg(mgr->etdev, "mbox %u resp doorbell irq tail=%u\n", i,
		  EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, tail));
	if (mailbox->handle_irq)
		mailbox->handle_irq(mailbox);
out:
	read_unlock(&mgr->mailboxes_lock);
	return IRQ_HANDLED;
}

irqreturn_t edgetpu_chip_irq_handler(int irq, void *arg)
{
	struct edgetpu_dev *etdev = arg;

	edgetpu_telemetry_irq_handler(etdev);
	edgetpu_debug_dump_resp_handler(etdev);
	return rio_mailbox_handle_irq(etdev, irq);
}

void edgetpu_chip_init(struct edgetpu_dev *etdev)
{
}

void edgetpu_chip_exit(struct edgetpu_dev *etdev)
{
}

void edgetpu_mark_probe_fail(struct edgetpu_dev *etdev)
{
}

int edgetpu_chip_get_ext_mailbox_index(u32 mbox_type, u32 *start, u32 *end)
{
	switch (mbox_type) {
	case EDGETPU_EXTERNAL_MAILBOX_TYPE_DSP:
		*start = RIO_EXT_DSP_MAILBOX_START;
		*end = RIO_EXT_DSP_MAILBOX_END;
		return 0;
	default:
		return -ENOENT;
	}
}
