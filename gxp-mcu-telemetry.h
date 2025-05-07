/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GXP MCU telemetry support
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_MCU_TELEMETRY_H__
#define __GXP_MCU_TELEMETRY_H__

#include <linux/workqueue.h>

#include <gcip/gcip-telemetry.h>

#include "gxp-internal.h"

/* Buffer size must be a power of 2 */
#define GXP_MCU_TELEMETRY_LOG_BUFFER_SIZE (16 * 4096)
#define GXP_MCU_TELEMETRY_TRACE_BUFFER_SIZE (64 * 4096)

struct gxp_mcu;

struct gxp_mcu_telemetry_ctx {
	struct gcip_telemetry log;
	struct gxp_mapped_resource log_mem;
	struct gcip_telemetry trace;
	struct gxp_mapped_resource trace_mem;
};

/*
 * Allocates resources needed for @mcu->telemetry.
 *
 * Returns 0 on success, or a negative errno on error.
 */
int gxp_mcu_telemetry_init(struct gxp_mcu *mcu);

/*
 * Disable the MCU telemetry if enabled, release resources allocated in init().
 */
void gxp_mcu_telemetry_exit(struct gxp_mcu *mcu);

/* Interrupt handler. */
void gxp_mcu_telemetry_irq_handler(struct gxp_mcu *mcu);

/*
 * Sends the KCI commands about MCU telemetry buffers to the device.
 *
 * Returns the code of KCI response, or a negative errno on error.
 */
int gxp_mcu_telemetry_kci(struct gxp_mcu *mcu);

/*
 * Sets the eventfd to notify the runtime when an IRQ is sent from the device.
 *
 * Returns 0 on success, or a negative errno on error.
 */
int gxp_mcu_telemetry_register_eventfd(struct gxp_mcu *mcu,
				       enum gcip_telemetry_type type,
				       u32 eventfd);
/* Removes previously set event. */
int gxp_mcu_telemetry_unregister_eventfd(struct gxp_mcu *mcu,
					 enum gcip_telemetry_type type);

/* Maps MCU telemetry buffer into user space. */
int gxp_mcu_telemetry_mmap_buffer(struct gxp_mcu *mcu,
				  enum gcip_telemetry_type type,
				  struct vm_area_struct *vma);

/*
 * Increments or decrements the refcount count of private data of telemetry mapping.
 * @vma must be mmap'ed by the `gxp_mcu_telemetry_mmap_buffer` function above.
 */
void gxp_mcu_telemetry_vma_get(struct vm_area_struct *vma);
void gxp_mcu_telemetry_vma_put(struct vm_area_struct *vma);

#endif /* __GXP_MCU_TELEMETRY_H__ */
