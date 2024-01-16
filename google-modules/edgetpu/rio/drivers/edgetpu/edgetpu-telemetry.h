/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Edge TPU ML accelerator telemetry: logging and tracing.
 *
 * Copyright (C) 2019-2020 Google, Inc.
 */
#ifndef __EDGETPU_TELEMETRY_H__
#define __EDGETPU_TELEMETRY_H__

#include <linux/mm_types.h>
#include <linux/seq_file.h>

#include <gcip/gcip-telemetry.h>

#include "edgetpu-internal.h"
#include "edgetpu-kci.h"

/* Buffer size must be a power of 2 */
#define EDGETPU_TELEMETRY_LOG_BUFFER_SIZE (16 * 4096)
#define EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE (64 * 4096)

struct edgetpu_telemetry_ctx {
	struct gcip_telemetry log;
	struct edgetpu_coherent_mem log_mem;
	struct gcip_telemetry trace;
	struct edgetpu_coherent_mem trace_mem;
};

#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
/*
 * Allocates resources needed for @etdev->telemetry.
 *
 * Returns 0 on success, or a negative errno on error.
 */
int edgetpu_telemetry_init(struct edgetpu_dev *etdev,
			   struct edgetpu_coherent_mem *log_mem,
			   struct edgetpu_coherent_mem *trace_mem);

/*
 * Disable the telemetry if enabled, release resources allocated in init().
 */
void edgetpu_telemetry_exit(struct edgetpu_dev *etdev);

/*
 * Sends the KCI commands about telemetry buffers to the device.
 *
 * Returns the code of KCI response, or a negative errno on error.
 */
int edgetpu_telemetry_kci(struct edgetpu_dev *etdev);

/*
 * Sets the eventfd to notify the runtime when an IRQ is sent from the device.
 *
 * Returns 0 on success, or a negative errno on error.
 */
int edgetpu_telemetry_set_event(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				u32 eventfd);
/* Removes previously set event. */
void edgetpu_telemetry_unset_event(struct edgetpu_dev *etdev, enum gcip_telemetry_type type);

/* Checks telemetries and signals eventfd if needed. */
void edgetpu_telemetry_irq_handler(struct edgetpu_dev *etdev);

/* debugfs mappings dump */
void edgetpu_telemetry_mappings_show(struct edgetpu_dev *etdev,
				     struct seq_file *s);

/* Map telemetry buffer into user space. */
int edgetpu_mmap_telemetry_buffer(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				  struct vm_area_struct *vma, int core_id);
void edgetpu_telemetry_inc_mmap_count(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				      int core_id);
void edgetpu_telemetry_dec_mmap_count(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				      int core_id);
#else
static inline
int edgetpu_telemetry_init(struct edgetpu_dev *etdev,
			   struct edgetpu_coherent_mem *log_mem,
			   struct edgetpu_coherent_mem *trace_mem)
{
	return 0;
}
static inline
void edgetpu_telemetry_exit(struct edgetpu_dev *etdev)
{
}
static inline
int edgetpu_telemetry_kci(struct edgetpu_dev *etdev)
{
	return 0;
}
static inline
int edgetpu_telemetry_set_event(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				u32 eventfd)
{
	return 0;
}
static inline
void edgetpu_telemetry_unset_event(struct edgetpu_dev *etdev, enum gcip_telemetry_type type)
{
}
static inline
void edgetpu_telemetry_irq_handler(struct edgetpu_dev *etdev)
{
}
static inline
void edgetpu_telemetry_mappings_show(struct edgetpu_dev *etdev,
				     struct seq_file *s)
{
}
static inline
int edgetpu_mmap_telemetry_buffer(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				  struct vm_area_struct *vma, int core_id)
{
	return -ENODEV;
}
static inline
void edgetpu_telemetry_inc_mmap_count(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				      int core_id)
{
}
static inline
void edgetpu_telemetry_dec_mmap_count(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				      int core_id)
{
}
#endif

#endif /* __EDGETPU_TELEMETRY_H__ */
