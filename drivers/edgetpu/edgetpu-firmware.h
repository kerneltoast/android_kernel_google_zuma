/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EdgeTPU firmware loader.
 *
 * Copyright (C) 2020 Google, Inc.
 */
#ifndef __EDGETPU_FIRMWARE_H__
#define __EDGETPU_FIRMWARE_H__

#include <linux/seq_file.h>

#include <gcip/gcip-firmware.h>

#include "edgetpu-internal.h"

enum edgetpu_firmware_flags {
	/* Image is default firmware for the chip */
	FW_DEFAULT = 0x1,
};

struct edgetpu_firmware_private;

struct edgetpu_firmware {
	struct edgetpu_dev *etdev;
	struct edgetpu_firmware_private *p;
};

struct edgetpu_firmware_buffer {
	/*
	 * fields set by alloc_buffer() handler for using custom allocated
	 * buffer
	 *
	 * edgetpu_firmware framework also updates these fields when using
	 * shared firmware buffer.
	 */
	/*
	 * kernel VA, leave NULL to indicate edgetpu_firmware using shared
	 * firmware buffer.
	 */
	void *vaddr;
	size_t alloc_size;	/* allocated size of @vaddr in bytes */
	size_t used_size_align;	/* firmware size alignment in bytes */

	/* fields set by setup_buffer() handler */
	dma_addr_t dma_addr;	/* DMA handle for downstream IOMMU, if any */

	/* fields set by prepare_run() handler */

	/* fields modifiable by handlers */
	enum edgetpu_firmware_flags flags;

	/*
	 * fields set by edgetpu_firmware, don't modify the following fields in
	 * the handlers
	 */
	size_t used_size;	/* actual size of firmware image */
	const char *name;	/* the name of this firmware */
};

/*
 * Descriptor for loaded firmware, either in shared buffer mode or carveout mode
 * (non-shared, custom allocated memory).
 */
struct edgetpu_firmware_desc {
	/*
	 * Mode independent buffer information. This is either passed into or
	 * updated by handlers.
	 */
	struct edgetpu_firmware_buffer buf;
	/*
	 * Shared firmware buffer when we're using shared buffer mode. This
	 * pointer to keep and release the reference count on unloading this
	 * shared firmware buffer.
	 *
	 * This is NULL when firmware is loaded in carveout mode.
	 */
	struct edgetpu_shared_fw_buffer *shared_buf;
};

struct edgetpu_firmware_chip_data {
	/* Name of default firmware image for this chip. */
	const char *default_firmware_name;

	/*
	 * Chip handlers called by common firmware processing.
	 * Each handler returns 0 to indicate success, non-zero value to
	 * indicate error.
	 */
	int (*after_create)(struct edgetpu_firmware *et_fw);
	/*
	 * Release resource used in platform specific implementation,
	 * including stopping firmware.
	 */
	void (*before_destroy)(struct edgetpu_firmware *et_fw);
	/*
	 * Allocate a buffer for loading firmware and filling out the
	 * information into @fw_buf before running. See comments of
	 * edgetpu_firmware_buffer for the details of each field.
	 *
	 * This is invoked for each run.
	 */
	int (*alloc_buffer)(struct edgetpu_firmware *et_fw,
			    struct edgetpu_firmware_buffer *fw_buf);
	/*
	 * Free the buffer allocated by alloc_buffer() handler after running.
	 * See comments of edgetpu_firmware_buffer for the details of each
	 * field.
	 *
	 * This is invoked for each run.
	 */
	void (*free_buffer)(struct edgetpu_firmware *et_fw,
			    struct edgetpu_firmware_buffer *fw_buf);
	/*
	 * Setup for an allocated host buffer, mainly for dma mapping,
	 * for loading firmware and filling out the information into
	 * @fw_buf before running. See comments of
	 * edgetpu_firmware_buffer for the details of each
	 * field.
	 *
	 * This is invoked for each run.
	 */
	int (*setup_buffer)(struct edgetpu_firmware *et_fw,
			    struct edgetpu_firmware_buffer *fw_buf);
	/*
	 * Platform-specific handling after firmware loaded, before running
	 * the firmware, such as validating the firmware or resetting the
	 * processor.
	 */
	int (*prepare_run)(struct edgetpu_firmware *et_fw,
			   struct edgetpu_firmware_buffer *fw_buf);

	/*
	 * Optional platform-specific handler to restart an already loaded
	 * firmware.
	 */
	int (*restart)(struct edgetpu_firmware *et_fw, bool force_reset);
};

/*
 * Returns the chip-specific IOVA where the firmware is mapped.
 *
 * Debug purpose only.
 */
unsigned long edgetpu_chip_firmware_iova(struct edgetpu_dev *etdev);

/*
 * Load and run firmware.
 * @name: the name passed into underlying request_firmware API
 * @flags: edgetpu_firmware_flags for the image
 * Used internally by the sysfs load interface and by unit tests.
 */
int edgetpu_firmware_run(struct edgetpu_dev *etdev, const char *name,
			 enum edgetpu_firmware_flags flags);

/* Load and run the default firmware name for the chip. */
int edgetpu_firmware_run_default(struct edgetpu_dev *etdev);

/* Runs default firmware for the chip, caller holds FW/PM locks */
int edgetpu_firmware_run_default_locked(struct edgetpu_dev *etdev);

/*
 * Private data set and used by handlers. It is expected to
 * allocate and set the data on after_create() and release on
 * before_destroy().
 */
void edgetpu_firmware_set_data(struct edgetpu_firmware *et_fw, void *data);
void *edgetpu_firmware_get_data(struct edgetpu_firmware *et_fw);

int edgetpu_firmware_create(struct edgetpu_dev *etdev,
			    const struct edgetpu_firmware_chip_data *chip_fw);
void edgetpu_firmware_destroy(struct edgetpu_dev *etdev);
void edgetpu_firmware_mappings_show(struct edgetpu_dev *etdev,
				    struct seq_file *s);

/*
 * These functions grab and release the internal firmware lock and must be used
 * before calling the helper functions suffixed with _locked below.
 */

int edgetpu_firmware_lock(struct edgetpu_dev *etdev);
int edgetpu_firmware_trylock(struct edgetpu_dev *etdev);
void edgetpu_firmware_unlock(struct edgetpu_dev *etdev);

/* Returns whether the firmware loading work is ongoing. */
bool edgetpu_firmware_is_loading(struct edgetpu_dev *etdev);

/*
 * Returns the state of the firmware image currently loaded for this device.
 * Caller must hold firmware lock.
 */
enum gcip_fw_status edgetpu_firmware_status_locked(struct edgetpu_dev *etdev);

/* Caller must hold firmware lock. For unit tests. */
void edgetpu_firmware_set_status_locked(struct edgetpu_dev *etdev, enum gcip_fw_status status);

/*
 * Restarts the last firmware image loaded
 * Intended for power managed devices to re-run the firmware without a full
 * reload from the file system.
 * Optionally, force a CPU reset to recover from a bad firmware state.
 */
int edgetpu_firmware_restart_locked(struct edgetpu_dev *etdev,
				    bool force_reset);

/* Called on software watchdog timeout to restart firmware. */
void edgetpu_firmware_watchdog_restart(struct edgetpu_dev *etdev);

/* Returns the current firmware image name. */
ssize_t edgetpu_firmware_get_name(struct edgetpu_dev *etdev, char *buf,
				  size_t buflen);

/* Returns the changelist ID of the image loaded on the device. */
uint32_t edgetpu_firmware_get_cl(struct edgetpu_firmware *et_fw);

/* Returns the build time of the image in seconds since 1970. */
uint64_t edgetpu_firmware_get_build_time(struct edgetpu_firmware *et_fw);

#endif /* __EDGETPU_FIRMWARE_H__ */
