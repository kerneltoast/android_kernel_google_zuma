/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Include all configuration files for Rio.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __RIO_CONFIG_H__
#define __RIO_CONFIG_H__

#define DRIVER_NAME "rio"

#define EDGETPU_NUM_CORES 2

#define EDGETPU_NUM_SSMTS 2

#define EDGETPU_MAX_STREAM_ID 64

/* 1 context per VII/group plus 1 for KCI */
#define EDGETPU_NCONTEXTS 16
/* Max number of virtual context IDs that can be allocated for one device. */
#define EDGETPU_NUM_VCIDS 16
/* Reserved VCID that uses the extra partition. */
#define EDGETPU_VCID_EXTRA_PARTITION 0

/* Pre-allocate 1 IOMMU domain per VCID */
#define EDGETPU_NUM_PREALLOCATED_DOMAINS EDGETPU_NUM_VCIDS

/* Number of TPU clusters for metrics handling. */
#define EDGETPU_TPU_CLUSTER_COUNT 3

/* Placeholder value */
#define EDGETPU_TZ_MAILBOX_ID 31

/* Default size limit of the area in remapped DRAM reserved for firmware code and internal data. */
#define EDGETPU_DEFAULT_FW_LIMIT 0x100000

/* Default size of remapped DRAM data region. */
#define EDGETPU_DEFAULT_REMAPPED_DATA_SIZE 0x100000

/*
 * Maximum size limit of the area in remapped DRAM reserved for firmware code and internal data.
 * The firmware image config may modify the split between code and data, but the total size of both
 * must be respected.
 */
#define EDGETPU_MAX_FW_LIMIT (EDGETPU_DEFAULT_FW_LIMIT + EDGETPU_DEFAULT_REMAPPED_DATA_SIZE)

/*
 * Instruction remap registers make carveout memory appear at address
 * 0x10000000 from the TPU CPU perspective
 */
#define EDGETPU_INSTRUCTION_REMAP_BASE		0x10000000

/*
 * Default address from which the TPU CPU can access data in the remapped region.
 * Data in remapped DRAM starts after firmware code and internal data.
 */
#define EDGETPU_DEFAULT_REMAPPED_DATA_ADDR                                                         \
	(EDGETPU_INSTRUCTION_REMAP_BASE + EDGETPU_DEFAULT_FW_LIMIT)

/*
 * Size of memory for FW accessible debug dump segments
 * TODO(b/208758697): verify whether this size is good
 */
#define EDGETPU_DEBUG_DUMP_MEM_SIZE 0x4E0000

#include "config-mailbox.h"
#include "config-pwr-state.h"
#include "config-tpu-cpu.h"
#include "csrs.h"

#endif /* __RIO_CONFIG_H__ */
