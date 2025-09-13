/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP telemetry: logging and tracing.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GCIP_TELEMETRY_H__
#define __GCIP_TELEMETRY_H__

#include <linux/device.h>
#include <linux/eventfd.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/rwlock_types.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <gcip/gcip-memory.h>

#define GCIP_TELEMETRY_NAME_LOG "telemetry_log"
#define GCIP_TELEMETRY_NAME_TRACE "telemetry_trace"

/* Log level codes used by gcip firmware. */
#define GCIP_FW_LOG_LEVEL_VERBOSE (2)
#define GCIP_FW_LOG_LEVEL_DEBUG (1)
#define GCIP_FW_LOG_LEVEL_INFO (0)
#define GCIP_FW_LOG_LEVEL_WARN (-1)
#define GCIP_FW_LOG_LEVEL_ERROR (-2)
#define GCIP_FW_LOG_LEVEL_FATAL (-3)

#define GCIP_FW_DMESG_LOG_LEVEL (GCIP_FW_LOG_LEVEL_INFO)

/* When log data arrives, recheck for more log data after this delay. */
#define GCIP_TELEMETRY_TYPE_LOG_RECHECK_DELAY 200 /* ms */

enum gcip_telemetry_state {
	GCIP_TELEMETRY_DISABLED = 0,
	GCIP_TELEMETRY_ENABLED = 1,
	GCIP_TELEMETRY_INVALID = -1,
};

/* To specify the target of operation. */
enum gcip_telemetry_type {
	GCIP_TELEMETRY_TYPE_LOG = 0,
	GCIP_TELEMETRY_TYPE_TRACE = 1,
	GCIP_TELEMETRY_TYPE_COUNT = 2,
};

struct gcip_telemetry_header {
	u32 head;
	u32 size;
	u32 reserved0[14]; /* Place head and tail into different cache lines */
	u32 tail;
	u32 entries_dropped; /* Number of entries dropped due to buffer full */
	u32 reserved1[14]; /* Pad to 128 bytes in total */
};

struct gcip_log_entry_header {
	s16 code;
	u16 length;
	u64 timestamp;
	u16 crc16;
} __packed;

struct gcip_telemetry {
	/* Device used for logging and memory allocation. */
	struct device *dev;

	/*
	 * State transitioning is to prevent racing in IRQ handlers. e.g. the interrupt comes when
	 * the kernel is releasing buffers.
	 */
	enum gcip_telemetry_state state;
	spinlock_t state_lock; /* protects state */

	struct gcip_telemetry_header *header;

	struct eventfd_ctx *ctx; /* signal this to notify the runtime */
	rwlock_t ctx_lock; /* protects ctx */
	const char *name; /* for debugging */

	struct work_struct work; /* worker for handling data */
	/* Fallback function to call for default log/trace handling. */
	void (*fallback_fn)(const struct gcip_telemetry *tel);
	struct mutex mmap_lock; /* protects mmapped_count */
	long mmapped_count; /* number of VMAs that are mapped to this telemetry buffer */
};

/**
 * struct gcip_telemetry_ctx - The object containing the telemetry contex.
 * @log: The gcip_telemetry for logging.
 * @trace: The gcip_telemetry for tracing.
 * @log_mem: The gcip_memory for logging.
 * @trace_mem: The gcip_memory for tracing.
 */
struct gcip_telemetry_ctx {
	struct gcip_telemetry log;
	struct gcip_telemetry trace;
	struct gcip_memory log_mem;
	struct gcip_memory trace_mem;
};

struct gcip_kci;

struct gcip_telemetry_kci_args {
	struct gcip_kci *kci;
	u64 addr;
	u32 size;
};

/**
 * gcip_telemetry_kci() -  Sends telemetry KCI through send kci callback.
 * @tel_ctx: The object holds the info of the telemetry buffer.
 * @type: The type of telemetry.
 * @send_kci: The callback function to send the KCI, which receives gcip_telemetry_kci_args and
 *            returns:
 *            0 - Success
 *            > 0 - Firmware error
 *            < 0 - Driver error
 * @kci: The pointer to the gcip_kci object to interact with gcip_kci APIs.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_telemetry_kci(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
		       int (*send_kci)(const struct gcip_telemetry_kci_args *),
		       struct gcip_kci *kci);

/**
 * gcip_telemetry_set_event() - Sets the eventfd for the given array of telemetries.
 * @tel_ctx: The gcip_telemetry_ctx to be set.
 * @type: The telemetry type to be set.
 * @eventfd: The evenfd to be set.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_telemetry_set_event(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
			     u32 eventfd);

/**
 * gcip_telemetry_unset_event() - Unsets the eventfd for the given array of telemetries.
 * @tel_ctx: The gcip_telemetry_ctx to be unset.
 * @type: The telemetry type to be set.
 */
void gcip_telemetry_unset_event(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type);

/**
 * gcip_telemetry_irq_handler() - The interrupt handler to schedule the worker when irq arrives.
 * @tel_ctx: The object holds the info of the telemetry buffer.
 * @type: The type of telemetry to be handled.
 */
void gcip_telemetry_irq_handler(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type);

/**
 * gcip_telemetry_mmap() - Mmaps the telemetry buffer.
 * @tel_ctx: The object holds the info of the telemetry buffer.
 * @type: The type of telemetry to be mmaped.
 * @vma: The struct holds the data to communicate with mm APIs.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_telemetry_mmap(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
			struct vm_area_struct *vma);

/*
 * Initializes struct gcip_telemetry.
 *
 * @vaddr: Virtual address of the queue buffer.
 * @size: Size of the queue buffer. Must be power of 2 and greater than the size of struct
 * gcip_telemetry_header.
 * @fallback_fn: Fallback function to call for default log/trace handling.
 */
int gcip_telemetry_init(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
			struct device *dev);

/* Exits and sets the telemetry state to GCIP_TELEMETRY_INVALID. */
void gcip_telemetry_exit(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type);

#endif /* __GCIP_TELEMETRY_H__ */
