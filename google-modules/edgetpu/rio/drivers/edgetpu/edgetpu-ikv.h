/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Virtual Inference Interface, implements the protocol between AP kernel and TPU firmware.
 *
 * Copyright (C) 2023-2025 Google LLC
 */

#ifndef __EDGETPU_IKV_H__
#define __EDGETPU_IKV_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-mailbox.h>
#include <gcip/gcip-memory.h>
#include <iif/iif-fence.h>

#include "edgetpu-device-group.h"
#include "edgetpu-ikv-additional-info.h"
#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"

#ifdef EDGETPU_IKV_TIMEOUT
#define IKV_TIMEOUT	EDGETPU_IKV_TIMEOUT
#elif IS_ENABLED(CONFIG_EDGETPU_TEST)
/* fake-firmware could respond in a short time */
#define IKV_TIMEOUT	(2000)
#else
/* Wait for up to 2 minutes for FW to respond. */
#define IKV_TIMEOUT	(120000)
#endif

struct edgetpu_ikv_response {
	struct edgetpu_ikv *etikv;
	struct list_head list_entry;
	/* Pointer to the VII response packet. */
	void *resp;
	/*
	 * The queue this response will be added to when it has been submitted to the mailbox.
	 *
	 * Access to this queue must be protected by `queue_lock`.
	 */
	struct list_head *pending_queue;
	/*
	 * The queue this response will be added to when it has arrived.
	 *
	 * Access to this queue must be protected by `queue_lock`.
	 */
	struct list_head *dest_queue;
	/*
	 * Indicates whether this response has already been handled (either prepared for a client or
	 * marked as timedout).
	 * This flag is used to detect and handle races between response arrival and timeout.
	 *
	 * Accessing this value must be done while holding `queue_lock`.
	 */
	bool processed;
	/*
	 * Lock to synchronize arrival, timeout, and consumption of this response.
	 *
	 * Protects `pending_queue`, `dest_queue` and `processed`.
	 */
	spinlock_t *queue_lock;
	/*
	 * Mailbox awaiter this response was delivered in.
	 * Must be released with `gcip_mailbox_release_awaiter()` after this response has been
	 * processed. Doing so will also free this response.
	 */
	struct gcip_mailbox_resp_awaiter *awaiter;
	/*
	 * Saves the client-provided sequence number so it can be used when returning the response
	 * to the client.
	 *
	 * This is necessary because the command sequence number is overridden with a
	 * Kernel-generated sequence number while in the mailbox queue. This prevents clients from
	 * using conflicting numbers (e.g. Client A and Client B both send commands with seq=3).
	 */
	u64 client_seq;
	/*
	 * A group to notify with the EDGETPU_EVENT_RESPDATA event when this response arrives.
	 */
	struct edgetpu_device_group *group_to_notify;
	/* Fences which the command is waiting on. */
	struct gcip_fence_array *in_fence_array;
	/* Fences to signal on timeout or completion. */
	struct gcip_fence_array *out_fence_array;
	/*
	 * Pointer to an IIF being used as a proxy for any DMA in-fences for the command.
	 * Saved here so the proxying thread can be stopped when this response arrives or is
	 * canceled.
	 */
	struct iif_fence *iif_dma_fence;
	/* The coherent buffer for the additional_info to be shared with the firmware. */
	struct gcip_memory additional_info;
	/* Callback to clean-up any data allocated for this command. */
	void (*release_callback)(void *data);
	void *release_data;
};

struct edgetpu_ikv {
	struct edgetpu_dev *etdev;

	/* Interface for managing sending/receiving messages via the mailbox queues. */
	struct gcip_mailbox *mbx_protocol;
	/* Interface for accessing the mailbox hardware and the values in their data registers. */
	struct edgetpu_mailbox *mbx_hardware;

	struct gcip_memory cmd_queue_mem;
	struct mutex cmd_queue_lock;
	struct gcip_memory resp_queue_mem;
	spinlock_t resp_queue_lock;
	unsigned long resp_queue_lock_flags;

	/*
	 * Wait queue used by gcip-mailbox for storing pending commands, should the command queue
	 * ever be full. In practice, credit enforcement prevents the queue from ever overflowing.
	 */
	wait_queue_head_t pending_commands;

	/* Whether in-kernel VII is supported. If false, VII is routed through user-space. */
	bool enabled;

	/*
	 * Timeout for a command, once it has been enqueued.
	 * Set during `edgetpu_ikv_init()` then never changes. Can be customized with the module
	 * param `user_ikv_timeout`.
	 */
	unsigned int command_timeout_ms;
};

/*
 * Initializes a VII object.
 *
 * Will request a mailbox from @mgr and allocate cmd/resp queues.
 */
int edgetpu_ikv_init(struct edgetpu_mailbox_manager *mgr, struct edgetpu_ikv *etikv);

/*
 * Re-initializes the initialized VII object.
 *
 * This function is used when the TPU device is reset, it re-programs CSRs related to the VII
 * mailbox.
 *
 * Returns 0 on success, -errno on error.
 */
int edgetpu_ikv_reinit(struct edgetpu_ikv *etikv);

/*
 * Releases resources allocated by @etikv
 *
 * Note: must be invoked after the VII interrupt is disabled and before the @etikv pointer is
 * released.
 */
void edgetpu_ikv_release(struct edgetpu_dev *etdev, struct edgetpu_ikv *etikv);

/*
 * Sends a VII command
 *
 * The command will be executed asynchronously, pushing a pending response into @pending_queue and
 * moving it into @ready_queue when it arrives.
 *
 * @queue_lock will be acquired then released during this call, and will be acquired asynchronously
 * when the response arrives or times-out, so that it can be moved between queues.
 *
 * If @in_fence_array contains in-kernel fences (DMA fences) and at least one of them are not yet
 * signaled, a new thread will be created to wait on @in_fence_array before sending the command.
 * If the fences are inter-IP fences, the command will be submitted to the firmware right away.
 *
 * The fences in @out_fence_array will be signaled when this command's corresponding response
 * arrives, or errored if the command is otherwise errored/canceled.
 *
 * Before freeing either queue, their owner must first:
 * 1) Set the `processed` flag on all responses in the @pending_queue
 * 2) Release @queue_lock (so the next step can proceed)
 * 3) Cancel all responses in @pending_queue with `gcip_mailbox_cancel_awaiter()`
 * 4) Release all responses in both queues with `gcip_mailbox_release_awaiter()`
 *
 * @release_callback will be called, with @release_data as an argument, immediately before the
 * command's edgetpu_ikv_response is released. This can be used to release any resources that were
 * allocated to support the command. If this function returns an error, @release_callback will not
 * be called.
 *
 * Returns 0 on success, -errno on error.
 */
int edgetpu_ikv_send_cmd(struct edgetpu_ikv *etikv, void *cmd, struct list_head *pending_queue,
			 struct list_head *ready_queue, spinlock_t *queue_lock,
			 struct edgetpu_device_group *group_to_notify,
			 struct gcip_fence_array *in_fence_array,
			 struct gcip_fence_array *out_fence_array, struct iif_fence *iif_dma_fence,
			 struct edgetpu_ikv_additional_info *additional_info,
			 void (*release_callback)(void *), void *release_data);

/*
 * Process all responses currently in the VII response queue.
 *
 * By the time this function returns, all valid responses will have been added to their ready_queue
 * and invalid responses (e.g. those with an invalid sequence number or client_id) will be dropped.
 *
 * This function should not be called from an interrupt context, as it will wait for the response
 * queue spinlock if another thread is already processing responses.
 *
 * The caller of this function must not hold any response queue_locks (see edgetpu_ikv_send_cmd())
 * or @etikv's resp_queue_lock, as they may be acquired during response processing.
 */
void edgetpu_ikv_flush_responses(struct edgetpu_ikv *etikv);

/*
 * Cancels all pending IKV commands of @group.
 *
 * This function should be called when both conditions are true:
 *   (a) the client won't send commands any more commands.
 *   (b) the MCU won't return any more responses for @group.
 * For example,
 *  - @group released the block wakelock and the `RELEASE_VMBOX` KCI has been sent to the MCU.
 *  - The MCU crashed and @group had been invalidated.
 *
 * Also, before calling this function, it is recommended to call the `edgetpu_ikv_flush_responses()`
 * function to prevent a race condition canceling pending commands whose responses have already
 * arrived from the MCU, but not consumed yet. Otherwise, from the MCU perspective, the commands
 * were processed successfully, but from the kernel/runtime perspective, those commands can be
 * considered as canceled.
 */
void edgetpu_ikv_cancel(struct edgetpu_device_group *group, int reason);

/*
 * Notifies the firmware of the unblock of the @fence_id inter-IP fence. This function works
 * asynchronously.
 *
 * Note that this function will be called when the fence has been unblocked and the IIF driver calls
 * the unblocked callback.
 */
void edgetpu_ikv_send_iif_unblock_notification(struct edgetpu_ikv *etikv, int fence_id);

#endif /* __EDGETPU_IKV_H__*/
