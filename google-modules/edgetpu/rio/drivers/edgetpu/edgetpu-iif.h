/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support Inter-IP Fences.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __EDGETPU_IIF_H__
#define __EDGETPU_IIF_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <gcip/gcip-mailbox.h>
#include <gcip/gcip-memory.h>

#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"

struct edgetpu_iif {
	struct edgetpu_dev *etdev;

	struct iif_manager *iif_mgr;
	struct device *iif_dev;

	/* Interface for managing sending/receiving messages via the mailbox queues. */
	struct gcip_mailbox *mbx_protocol;
	/* Interface for accessing the mailbox hardware and the values in their data registers. */
	struct edgetpu_mailbox *mbx_hardware;
	struct gcip_memory cmd_queue_mem;
	struct mutex cmd_queue_lock;

	/*
	 * Fields used to ensure pending signal commands are flushed when firmware resets.
	 *
	 * When edgetpu_iif_send_unblock_notification() is called it must check if @is_flushing
	 * has been set, and exit immediately if so. Otherwise, it increments @pending_signals.
	 * If @is_flushing becomes set while a thread is waiting for @cmd_queue_lock or space in
	 * the command queue, it should return an error immediately rather than enqueueing its
	 * command.
	 *
	 * When firmware resets, @is_flushing will be set and the reset code will wait until
	 * @pending_signals is 0 before proceeding.
	 *
	 * @flush_lock protects both @is_flushing and @pending_signals.
	 */
	bool is_flushing;
	unsigned int pending_signals;
	spinlock_t flush_lock;

	/* The work sending IIF unblock notification to the firmware. */
	struct work_struct unblocked_work;
	/* The list of unblocked IIF. */
	struct list_head unblocked_list;
	/* Protects @unblocked_list. */
	spinlock_t unblocked_lock;
};

/*
 * Initializes an IIF object.
 *
 * Initializes the IIF mailbox (if supported), obtain references to the system @iif_mgr and
 * @iif_dev, and start @unblocked_work.
 */
int edgetpu_iif_init(struct edgetpu_mailbox_manager *mgr, struct edgetpu_iif *etiif);

/*
 * Releases resources allocated by @etiif.
 *
 * Since @unblocked_work calls `edgetpu_pm_get()` and this function will stop @unblocked_work, this
 * function cannot be called in a thread holding the power management lock.
 *
 * The IIF mailbox can be reset or flushed while holding the power management lock by calling
 * `edgetpu_iif_reinit_mailbox()` or `edgetpu_iif_release_mailbox()` respectively.
 */
void edgetpu_iif_release(struct edgetpu_iif *etiif);

/*
 * Initialize the IIF mailbox if supported.
 *
 * If the platform does not support an IIF mailbox, this function returns success immediately.
 */
int edgetpu_iif_init_mailbox(struct edgetpu_mailbox_manager *mgr, struct edgetpu_iif *etiif);

/*
 * Releases resources used by the IIF mailbox.
 *
 * This function is meant to flush any pending IIF mailbox commands and de-allocate the mailbox's
 * queues and other resources. The rest of @etiif's state will be unaffected.
 */
void edgetpu_iif_release_mailbox(struct edgetpu_iif *etiif);

/*
 * Re-initializes the initialized IIF object.
 *
 * This function is used when the TPU device is reset, it re-programs CSRs related to the IIF
 * mailbox.
 *
 * Returns 0 on success, -errno on error.
 */
int edgetpu_iif_reinit_mailbox(struct edgetpu_iif *etiif);


/*
 * Notifies the firmware of the unblock of the @fence_id inter-IP fence.
 *
 * Note that this function will be called when the fence has been unblocked and the IIF driver calls
 * the unblocked callback.
 *
 * This function MUST NOT be called from an interrupt context or a user-thread. It will block until
 * space is available in the IIF mailbox queue or a watchdog timer restarts firmware.
 */
void edgetpu_iif_send_unblock_notification(struct edgetpu_iif *etiif, int fence_id);

#endif /* __EDGETPU_IIF_H__*/
