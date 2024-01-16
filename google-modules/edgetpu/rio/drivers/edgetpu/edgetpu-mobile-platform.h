/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common platform interfaces for mobile TPU chips.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __EDGETPU_MOBILE_PLATFORM_H__
#define __EDGETPU_MOBILE_PLATFORM_H__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "edgetpu-config.h"
#include "edgetpu-internal.h"
#include "mobile-debug-dump.h"

/*
 * Log and trace buffers at the beginning of the remapped region,
 * pool memory afterwards.
 */
#define EDGETPU_POOL_MEM_OFFSET                                                                    \
	((EDGETPU_TELEMETRY_LOG_BUFFER_SIZE + EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE) *               \
	 EDGETPU_NUM_CORES)

#define to_mobile_dev(etdev) container_of(etdev, struct edgetpu_mobile_platform_dev, edgetpu_dev)

struct edgetpu_mobile_platform_pwr {
	struct dentry *debugfs_dir;
	struct mutex policy_lock;
	enum edgetpu_pwr_state curr_policy;
	struct mutex state_lock;
	u64 min_state;
	u64 requested_state;

	/* LPM callbacks, NULL for chips without LPM */
	int (*lpm_up)(struct edgetpu_dev *etdev);
	void (*lpm_down)(struct edgetpu_dev *etdev);

	/* Block shutdown status callback, may be NULL */
	bool (*is_block_down)(struct edgetpu_dev *etdev);

	/* After firmware is started on power up */
	void (*post_fw_start)(struct edgetpu_dev *etdev);
};

struct edgetpu_mobile_fw_ctx {
	/* SG table for NS firmware context region mappings. */
	struct sg_table *sgt;
	/* DMA address of the NS firmware context region. */
	dma_addr_t daddr;
	/* Size of the NS firmware context region. */
	size_t size;
	/* List of all NS firmware context region mappings for the device. */
	struct list_head list;
};

struct edgetpu_mobile_platform_dev {
	/* Generic edgetpu device */
	struct edgetpu_dev edgetpu_dev;
	/* Common mobile platform power interface */
	struct edgetpu_mobile_platform_pwr platform_pwr;
	/* Physical address of the firmware image */
	phys_addr_t fw_region_paddr;
	/* Size of the firmware region */
	size_t fw_region_size;
	/* TPU address from which the TPU CPU can access data in the remapped region */
	tpu_addr_t remapped_data_addr;
	/* Size of remapped DRAM data region */
	size_t remapped_data_size;
	/* Virtual address of the memory region shared with firmware */
	void *shared_mem_vaddr;
	/* Physical address of the memory region shared with firmware */
	phys_addr_t shared_mem_paddr;
	/* Size of the shared memory region size */
	size_t shared_mem_size;
	/* List of all NS firmware context region mappings for the device. */
	struct list_head fw_ctx_list;
	/* Lock to protect @fw_ctx_list. */
	struct mutex fw_ctx_list_lock;
	/*
	 * Pointer to GSA device for firmware authentication.
	 * May be NULL if the chip does not support firmware authentication
	 */
	struct device *gsa_dev;
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
	/* Coherent log buffer */
	struct edgetpu_coherent_mem *log_mem;
	/* Coherent trace buffer */
	struct edgetpu_coherent_mem *trace_mem;
#endif
	/* subsystem coredump info struct */
	struct mobile_sscd_info sscd_info;
	/* Protects TZ Mailbox client pointer */
	struct mutex tz_mailbox_lock;
	/* TZ mailbox client */
	struct edgetpu_client *secure_client;

	/* Length of @irq */
	int n_irq;
	/* Array of IRQ numbers */
	int *irq;

	/* callbacks for chip-dependent implementations */

	/*
	 * Called when common device probing procedure is done.
	 *
	 * Return a non-zero value can fail the probe procedure.
	 *
	 * This callback is optional.
	 */
	int (*after_probe)(struct edgetpu_mobile_platform_dev *etmdev);
};

/* Sets up telemetry buffer address and size. */
void edgetpu_mobile_set_telemetry_mem(struct edgetpu_mobile_platform_dev *etmdev);

#endif /* __EDGETPU_MOBILE_PLATFORM_H__ */
