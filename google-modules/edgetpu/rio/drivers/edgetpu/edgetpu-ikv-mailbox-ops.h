/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP Mailbox Ops for the in-kernel VII mailbox
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __EDGETPU_IKV_MAILBOX_OPS_H__
#define __EDGETPU_IKV_MAILBOX_OPS_H__

#include <gcip/gcip-mailbox.h>

#include "edgetpu-ikv.h"

/* Operators of the IKV mailbox. */
extern const struct gcip_mailbox_ops ikv_mailbox_ops;

/*
 * Helper function to prepare a ready response, move it to its destination queue, and signal
 * any waiters for the response.
 *
 * If @resp_code or @resp_retval are provided, the `code` and `retval` fields of @ikv_resp's
 * internal VII response will be overridden with the values they point to before the response
 * is placed in is destination queue.
 */
void edgetpu_ikv_process_response(struct edgetpu_ikv_response *ikv_resp, u16 *resp_code,
				  u64 *resp_retval, int fence_error);

#endif /* __EDGETPU_IKV_MAILBOX_OPS_H__ */
