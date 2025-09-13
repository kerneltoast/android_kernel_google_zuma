/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support for Inter-IP fences.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __GXP_IIF_H__
#define __GXP_IIF_H__

#include <linux/device.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <gcip/gcip-memory.h>

#include <iif/iif-manager.h>

#include "gxp-internal.h"
#include "gxp-mailbox.h"

#define IIF_CIRCULAR_QUEUE_WRAP_BIT BIT(15)

struct gxp_mcu;

struct gxp_iif {
	struct gxp_mcu *mcu;

	struct iif_manager *iif_mgr;
	struct device *iif_dev;

	bool use_iif_mbox;
	struct gxp_mailbox *mbx;
	struct gcip_memory cmd_queue_mem;
	struct gcip_memory descriptor_mem;

	/*
	 * If false, ensures that no IIF unblock signal commands get propagated to the MCU
	 * firmware.
	 */
	bool enable_iif_mbox;
	spinlock_t enable_iif_mbox_lock;

	/* The work sending IIF unblock notification to the firmware. */
	struct work_struct unblocked_work;
	/* The list of unblocked IIF. */
	struct list_head unblocked_list;
	/* Protects @unblocked_list. */
	spinlock_t unblocked_lock;
};

/*
 * Initializes a gxp_iif object.
 *
 * Will request a mailbox from @mgr, allocate cmd queues, and start @unblocked_work.
 */
int gxp_iif_init(struct gxp_mcu *mcu);

/*
 * Re-initializes the initialized gxp_iif object.
 *
 * This function is used when the Aurora MCU is reset, it re-programs CSRs related to the IIF
 * mailbox.
 *
 * Returns 0 on success, -errno on error.
 */
int gxp_iif_reinit(struct gxp_iif *giif);

/*
 * Releases resources allocated by @giif
 */
void gxp_iif_release(struct gxp_iif *giif);

/*
 * Notifies the firmware that the @fence_id inter-IP fence has been unblocked. This function works
 * asynchronously and will send a mailbox command to the firmware.
 *
 * Note that this function will be called when the fence has been unblocked and the IIF driver calls
 * the unblocked callback.
 *
 * This function MUST NOT be called from an interrupt context or a user-thread. It will block until
 * space is available in the IIF mailbox queue or a watchdog timer restarts firmware.
 */
void gxp_iif_send_unblock_notification(struct gxp_iif *giif, int fence_id);

/*
 * Enables the IIF mailbox.
 *
 * Enables the propagation of the IIF unblock signal command via IIF mailbox.
 */
void gxp_iif_enable_iif_mbox(struct gxp_iif *giif);

/*
 * Disables the IIF mailbox.
 *
 * Disables the IIF signal command propagation via IIF mailbox. It stops any further
 * scheduling of @giif->unblocked_work, synchronously cancels the pending works and clears
 * the @unblocked_list.
 */
void gxp_iif_disable_iif_mbox(struct gxp_iif *giif);

#endif /*  __GXP_IIF_H__  */
