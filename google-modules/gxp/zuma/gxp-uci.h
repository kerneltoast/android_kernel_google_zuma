/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GXP user command interface.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_UCI_H__
#define __GXP_UCI_H__

#include <linux/kthread.h>

#include <gcip/gcip-mailbox.h>

#include "gxp-client.h"
#include "gxp-internal.h"
#include "gxp-mailbox.h"
#include "gxp-vd.h"

#define UCI_RESOURCE_ID 0

struct gxp_mcu;

/* Command/Response Structures */

/* Size of `gxp_uci_type` should be u8 to match FW */
enum gxp_uci_type {
	CORE_COMMAND = 0,
	WAKELOCK_COMMAND = 1,
} __packed;

struct gxp_uci_wakelock_command_params {
	/* DVFS operating point of DSP cores */
	uint8_t dsp_operating_point;
	/* DVFS operating point of memory */
	uint8_t memory_operating_point;
};

struct gxp_uci_core_command_params {
	/* iova address of the app command */
	uint64_t address;
	/* size of the app command */
	uint32_t size;
	/* number of dsp cores required for this command */
	uint8_t num_cores;
	/* DVFS operating point of DSP cores */
	uint8_t dsp_operating_point;
	/* DVFS operating point of memory */
	uint8_t memory_operating_point;
};

struct gxp_uci_command {
	/* sequence number, should match the corresponding response */
	uint64_t seq;
	/* unique ID for each client that identifies client VM & security realm */
	uint32_t client_id;
	/* type of the command */
	enum gxp_uci_type type;
	/* hint for which core the job should be assigned to */
	uint8_t core_id;
	/* reserved field */
	uint8_t reserved[2];
	/* All possible command parameters */
	union {
		struct gxp_uci_core_command_params core_command_params;
		struct gxp_uci_wakelock_command_params wakelock_command_params;
		uint8_t opaque[48];
	};
};

struct gxp_uci_response {
	/* sequence number, should match the corresponding command */
	uint64_t seq;
	/* unique ID for each client that identifies client VM & security realm*/
	uint32_t client_id;
	/* status code that tells the success or error. */
	uint16_t code;
	/* reserved field */
	uint8_t reserved[2];
	uint8_t opaque[16];
};

/*
 * Wrapper struct for responses consumed by a thread other than the one which
 * sent the command.
 */
struct gxp_uci_async_response {
	/*
	 * List entry which will be inserted to the waiting queue of the vd.
	 * It will be pushed into the waiting queue when the response is sent.
	 * (i.e, the `gxp_uci_send_command` function is called)
	 * It will be poped when the response is consumed by the vd.
	 */
	struct list_head wait_list_entry;
	/*
	 * List entry which will be inserted to the dest_queue of the vd.
	 * It will be pushed into the dest_queue when the response is arrived or timed out.
	 * It will be poped when the response is consumed by the vd.
	 */
	struct list_head dest_list_entry;
	/* Stores the response. */
	struct gxp_uci_response resp;
	struct gxp_uci *uci;
	/* Queue where to be removed from once it is complete or timed out. */
	struct list_head *wait_queue;
	/* Queue to add the response to once it is complete or timed out. */
	struct list_head *dest_queue;
	/*
	 * The lock that protects queues pointed to by `dest_queue` and `wait_queue`.
	 * The mailbox code also uses this lock to protect changes to the `wait_queue` pointer
	 * itself when processing this response.
	 */
	spinlock_t *queue_lock;
	/* Queue of clients to notify when this response is processed. */
	wait_queue_head_t *dest_queue_waitq;
	/* gxp_eventfd to signal when the response completes. May be NULL. */
	struct gxp_eventfd *eventfd;
	/* The request was sent from this virtual device. */
	struct gxp_virtual_device *vd;
	/* Handles arrival, timeout of async response. */
	struct gcip_mailbox_resp_awaiter *awaiter;
	/* Status of the response. */
	enum gxp_response_status status;
};

struct gxp_uci_wait_list {
	struct list_head list;
	struct gxp_uci_response *resp;
	bool is_async;
};

struct gxp_uci {
	struct gxp_dev *gxp;
	struct gxp_mcu *mcu;
	struct gxp_mailbox *mbx;
	struct gxp_mapped_resource cmd_queue_mem;
	struct gxp_mapped_resource resp_queue_mem;
	struct gxp_mapped_resource descriptor_mem;
};

/* UCI APIs */

/**
 * gxp_uci_init() - API for initializing GXP UCI in MCU, should only be
 * called while initializing MCU
 * @mcu: The MCU that UCI communicate with
 *
 * Return:
 * * 0       - Initialization finished successfully
 * * -ENOMEM - Cannot get memory to finish init.
 */
int gxp_uci_init(struct gxp_mcu *mcu);

/**
 * gxp_uci_exit() - API for releasing the UCI mailbox of MCU.
 * @uci: The UCI to be released
 */
void gxp_uci_exit(struct gxp_uci *uci);

/*
 * gxp_uci_send_command() - API for sending @cmd to MCU firmware, and
 * registering @resp_queue to put the response in after MCU firmware handle the
 * command.
 *
 * Returns 0 on success, a negative errno on failure.
 */
int gxp_uci_send_command(struct gxp_uci *uci, struct gxp_virtual_device *vd,
			 struct gxp_uci_command *cmd,
			 struct list_head *wait_queue,
			 struct list_head *resp_queue, spinlock_t *queue_lock,
			 wait_queue_head_t *queue_waitq,
			 struct gxp_eventfd *eventfd);

/*
 * gxp_uci_wait_async_response() - API for waiting and fetching a response from
 * MCU firmware.
 *
 * Returns 0 on success, a negative errno on failure.
 */
int gxp_uci_wait_async_response(struct mailbox_resp_queue *uci_resp_queue,
				u64 *resp_seq, u16 *error_code, u8 *opaque);

#endif /* __GXP_UCI_H__ */
