/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip-dependent configuration for mailbox.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __RIO_CONFIG_MAILBOX_H__
#define __RIO_CONFIG_MAILBOX_H__

#include <linux/types.h> /* u32 */

#define EDGETPU_NUM_VII_MAILBOXES 15
/* 7 VII contexts mappable via SysMMU VID */
#define EDGETPU_NUM_USE_VII_MAILBOXES 7
#define EDGETPU_NUM_EXT_MAILBOXES 3
#define EDGETPU_NUM_MAILBOXES (EDGETPU_NUM_VII_MAILBOXES + EDGETPU_NUM_EXT_MAILBOXES + 1)
/*
 * Mailbox index layout in mailbox manager is like:
 * ---------------------------------------------
 * | KCI X 1 |   VII(s) X 15  | EXT_DSP(s) X 3  |
 * ---------------------------------------------
 */
#define RIO_EXT_DSP_MAILBOX_START (EDGETPU_NUM_VII_MAILBOXES + 1)
#define RIO_EXT_DSP_MAILBOX_END (EDGETPU_NUM_EXT_MAILBOXES + RIO_EXT_DSP_MAILBOX_START - 1)

#define RIO_CSR_MBOX3_CONTEXT_ENABLE 0x30000 /* starting kernel mb */
#define EDGETPU_MBOX_CSRS_SIZE 0x2000 /* CSR size of each mailbox */

#define RIO_CSR_MBOX_CMD_QUEUE_DOORBELL_SET_OFFSET 0x1000
#define RIO_CSR_MBOX_RESP_QUEUE_DOORBELL_SET_OFFSET 0x1800
#define EDGETPU_MBOX_BASE RIO_CSR_MBOX3_CONTEXT_ENABLE

static inline u32 edgetpu_mailbox_get_context_csr_base(u32 index)
{
	return EDGETPU_MBOX_BASE + index * EDGETPU_MBOX_CSRS_SIZE;
}

static inline u32 edgetpu_mailbox_get_cmd_queue_csr_base(u32 index)
{
	return edgetpu_mailbox_get_context_csr_base(index) +
	       RIO_CSR_MBOX_CMD_QUEUE_DOORBELL_SET_OFFSET;
}

static inline u32 edgetpu_mailbox_get_resp_queue_csr_base(u32 index)
{
	return edgetpu_mailbox_get_context_csr_base(index) +
	       RIO_CSR_MBOX_RESP_QUEUE_DOORBELL_SET_OFFSET;
}

#endif /* __RIO_CONFIG_MAILBOX_H__ */
