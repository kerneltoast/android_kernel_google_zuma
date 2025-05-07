/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Module that defines structures and functions to retrieve debug dump segments
 * from edgetpu firmware.
 *
 * Copyright (C) 2020 Google, Inc.
 */
#ifndef __EDGETPU_DEBUG_H__
#define __EDGETPU_DEBUG_H__

#include <linux/completion.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/types.h>

struct edgetpu_dev;

/* Firmware debug service buffer IOVA and size. */
/* TODO(b/333625284): reserve firmware debug buffer IOVA range from DMA window used by DMA API */
#define FW_DEBUG_BUFFER_IOVA	0x18000000
#define FW_DEBUG_BUFFER_SIZE	(13 * SZ_1M)

/* Firmware debug service command/response memory. */
struct edgetpu_fw_debug_mem {
	/* Scatter-gather table for the non-contiguous buffer. */
	struct sg_table *sgt;
	/* Kernel VA of buffer start. */
	void *vaddr;
	/* If true, data in buffer is a fw response, will discard if not read before write. */
	bool resp_data_ready;
	/* Completion for firmware returned response data ready for reading. */
	struct completion rd_data_ready;
	/* Length of firmware buffer data ready for reading or writing. */
	size_t data_len;
	/* If true FW responded to last cmd saying response packet will be async via RKCI. */
	bool async_resp_pending;
};

#define DEBUG_DUMP_HOST_CONTRACT_VERSION 3

enum edgetpu_dump_type_bit_position {
	DUMP_TYPE_CRASH_REASON_BIT = 0,
	DUMP_TYPE_STATS_BIT = 1,
	DUMP_TYPE_TCM_BIT = 2,
	DUMP_TYPE_SRAM_BIT = 3,
	DUMP_TYPE_CPU_BIT = 4,
	DUMP_TYPE_CSRS_BIT = 5,

	DUMP_TYPE_KERNEL_ETDEV_BIT = 32,

	DUMP_TYPE_MAX_BIT = 63
};

enum edgetpu_dump_reason {
	DUMP_REASON_DEFAULT = 0,
	/* Host request reasons */
	DUMP_REASON_REQ_BY_USER = 1,

	/* FW side dump reasons */
	DUMP_REASON_FW_CHECKPOINT = 2,
	DUMP_REASON_RECOVERABLE_FAULT = 3,
	DUMP_REASON_UNRECOVERABLE_FAULT = 4,
	DUMP_REASON_NON_FATAL_CRASH = 5,
	DUMP_REASON_SW_WATCHDOG_TIMEOUT = 6,

	DUMP_REASON_NUM = 7
};

struct edgetpu_crash_reason {
	u64 code;	/* code that captures the reset reason */
};

struct edgetpu_debug_stats {
	u64 num_requests;	/* number of dump requests made to the tpu */
	u64 uptime;	/* time since boot up on the tpu */
	u64 current_context;	/* current task context */
};

struct edgetpu_dump_segment {
	u64 type;	/* type of the dump */
	u64 size;	/* size of the dump data */
	u64 src_addr; /* source of the dump on the CPU address map */
};

struct edgetpu_debug_dump {
	u64 magic;	/* word identifying the beginning of the dump info */
	u64 version;	/* host-firmware dump info contract version */
	u64 host_dump_available_to_read;	/* is new info available */
	u64 dump_reason;	/* Reason or context for debug dump */
	u64 reserved[2];
	u64 crash_reason_offset;	/* byte offset to crash reason */
	u64 crash_reason_size;	/* crash reason size */
	u64 debug_stats_offset;	/* byte offset to debug stats */
	u64 debug_stats_size;	/* crash reason size */
	u64 dump_segments_offset;	/* byte offset to dump segments */
	u64 dump_segments_num;	/* number of dump segments populated */
};

struct mobile_sscd_info {
	void *pdata; /* SSCD platform data */
	void *dev; /* SSCD platform device */
};

/*
 * Handle FW response data available. Data has been written to debug_mem by firmware.
 * @data_len is the number of bytes of data written.
 */
void edgetpu_fw_debug_resp_ready(struct edgetpu_dev *etdev, u32 data_len);

/* Generate a debug dump for reason @dump_reason. */
void edgetpu_debug_dump(struct edgetpu_dev *etdev, u64 dump_reason);

/* Dump the external debug TPU CPU registers. */
void edgetpu_debug_dump_cpu_regs(struct edgetpu_dev *etdev);

/* debugfs mappings dump */
void edgetpu_debug_dump_mappings_show(struct edgetpu_dev *etdev, struct seq_file *s);

/* Init debug features including debug dump. */
void edgetpu_debug_init(struct edgetpu_dev *etdev);

/* De-init debug features including debug dump. */
void edgetpu_debug_exit(struct edgetpu_dev *etdev);

#endif /* EDGETPU_DEBUG_H_ */
