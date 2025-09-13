/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines the data structures which can be shared by kernel drivers, firmware and user-space.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __IIF_SHARED_H__
#define __IIF_SHARED_H__

/*
 * This file is shared with firmware and user-space where has no Linux kernel sources. Therefore,
 * we should branch the headers according to whether we are going to build this file as kernel
 * drivers or not.
 */
#ifndef __KERNEL__
#include <stdint.h>
#else /* !__KERNEL__ */
#include <linux/types.h>
#endif /* __KERNEL__ */

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif /* __packed */

/*
 * The max number of fences can be created per IP.
 * Increasing this value needs to increase the size of fence table.
 */
#define IIF_NUM_FENCES_PER_IP 1024

/* The number of sync points per fence. */
#define IIF_NUM_SYNC_POINTS 3

/* Notifies waiters for every single fence signal. */
#define IIF_WAIT_TABLE_SYNC_POINT_COUNT_ALL (0xFF)

/* The maximum of the timeline. */
#define IIF_SIGNAL_TABLE_MAX_TIMELINE (0xFF)

/* Type of fence signaler. */
enum iif_fence_signaler_type {
	/* The fence will be signaled by an IP (or AP) when it completes a signaler command. */
	IIF_FENCE_SIGNALER_TYPE_IP,
	/* Unknown signaler type. */
	IIF_FENCE_SIGNALER_TYPE_UNKNOWN,
};

/* Type of fence. */
enum iif_fence_type {
	/*
	 * If the signaler is an IP, the fence will be unblocked once the fence has been signaled
	 * as the number of signalers times. Note that each signaler command will signal the fence
	 * once.
	 */
	IIF_FENCE_TYPE_SINGLE_SHOT,
	/*
	 * If the signaler is an IP, each signaler command can signal the fence multiple times
	 * (i.e., streaming command). Each signal will monotonically increase the timeline value of
	 * the fence and each waiter will be notified once or multiple times when the timeline value
	 * reaches the value they are waiting for. If the timeline exceeds the timeout, a timeout
	 * error will be propagated to the waiters.
	 */
	IIF_FENCE_TYPE_REUSABLE,
	/* Unknown fence type. */
	IIF_FENCE_TYPE_UNKNOWN,
};

/* Type of IPs. */
enum iif_ip_type {
	IIF_IP_DSP,
	IIF_IP_TPU,
	IIF_IP_GPU,
	IIF_IP_AP,
	IIF_IP_NUM,

	/* Reserve the number of IP type to expand the fence table easily in the future. */
	IIF_IP_RESERVED = 16,
};

/*
 * Bit location of each IIF flag in the signal table.
 * It will be set to the @flag field of the signal table per fence which has 1-byte size.
 */
enum iif_signal_table_flag_bits {
	/*
	 * If this flag is set, the fence has been signaled with an error at least once.
	 * The waiters shouldn't consider the fence as unblocked until the number of remaining
	 * signals becomes 0.
	 */
	IIF_SIGNAL_TABLE_FLAG_ERROR_BIT,
};

/* Sync-point of reusable fences. */
struct iif_wait_table_reusable_sync_point {
	/* Marks BIT(0) to true if the fence is a reusable fence. */
	uint8_t start_timeline;
	/*
	 * Waiters will be notified @count times from @start_timeline.
	 *
	 * If it is `IIF_WAIT_TABLE_SYNC_POINT_COUNT_ALL`, waiters will be signaled for every
	 * single signal.
	 */
	uint8_t count;
} __packed;

/* Entry of the wait table. */
struct iif_wait_table_entry {
	/* The waiters waiting on the fence unblock. */
	uint8_t waiting_ips;
	/* Marks BIT(0) if the fence is a reusable fence. */
	uint8_t reusable;
	union {
		/* Sync points for reusable fence. */
		struct iif_wait_table_reusable_sync_point sync_points[IIF_NUM_SYNC_POINTS];
		/* Reserved. */
		uint8_t reserved[6];
	};
} __packed;

/* Entry of the signal table. */
struct iif_signal_table_entry {
	union {
		/*
		 * The number of remaining signals to unblock the fence. (Single-shot)
		 *
		 * If it becomes 0, it means that the fence has been unblocked. Note that the
		 * waiters should investigate @flag to confirm that if there was a fence error or
		 * not.
		 */
		uint16_t remaining_signals;
		/*
		 * The timeline value of the fence. (Reusable)
		 *
		 * It will be increased per fence signal. If it reaches a certain value that waiters
		 * are waiting for, those waiters will be notified.
		 */
		uint8_t timeline;
	};
	/*
	 * The flag of the fence.
	 * See `enum iif_signal_table_flag_bits` to understand the meaning of each bit.
	 */
	uint8_t flag;
	union {
		/* Reserved. (Single-shot) */
		uint8_t reserved_0;
		/*
		 * The timeout value. (Resuable)
		 *
		 * If the fence is a reusable fence and the timeline value reaches it, the fence
		 * should be timedout.
		 */
		uint8_t timeout;
	};
	/* Reserved. */
	uint8_t reserved_1[4];
} __packed;

#endif /* __IIF_SHARED_H__ */
