/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GXP MicroController Unit firmware management.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_MCU_FIRMWARE_H__
#define __GXP_MCU_FIRMWARE_H__

#include <linux/mutex.h>
#include <linux/workqueue.h>

#include <gcip/gcip-firmware.h>
#include <gcip/gcip-image-config.h>

#include "gxp-internal.h"

struct gxp_mcu_firmware_ns_buffer {
	/* SG table for NS firmware buffer mappings. */
	struct sg_table *sgt;
	/* DMA address of the NS firmware buffer. */
	dma_addr_t daddr;
	/* Size of the NS firmware buffer. */
	size_t size;
	/* List of NS firmware buffer mappings for the device. */
	struct list_head list;
};

struct gxp_mcu_firmware {
	struct gxp_dev *gxp;
	/* resource for MCU firmware image */
	struct gxp_mapped_resource image_buf;

	struct mutex lock; /* lock to protect fields below */
	enum gcip_fw_status status;
	struct gcip_fw_info fw_info;
	struct gcip_image_config_parser cfg_parser;
	bool is_secure;
	int crash_cnt;

	/* Worker to handle the MCU FW unrecoverable crash. */
	struct work_struct fw_crash_handler_work;
	/* The container of fault injection data. */
	struct gcip_fault_inject *fault_inject;
	/* List of all NS buffer mappings for the device. */
	struct list_head ns_buffer_list;
	/* Lock to protect @ns_buffer_list. */
	struct mutex ns_buffer_list_lock;
	/* The buffer of dynamic fw memory, which is only used in non-secure mode. */
	struct gxp_mcu_firmware_ns_buffer *dynamic_fw_buffer;
	/* The sanitizer enablement status for ASAN and UBSAN */
	int sanitizer_status;
};

/*
 * Initializes @mcu_fw.
 *
 * Returns 0 on success, a negative errno on failure.
 */
int gxp_mcu_firmware_init(struct gxp_dev *gxp, struct gxp_mcu_firmware *mcu_fw);
/* cleans up resources in @mcu_fw */
void gxp_mcu_firmware_exit(struct gxp_mcu_firmware *mcu_fw);

/*
 * Runs the MCU firmware. The firmware is ready to serve when this
 * call succeeds.
 *
 * Returns 0 on success, a negative errno on failure.
 */
int gxp_mcu_firmware_run(struct gxp_mcu_firmware *mcu_fw);

/*
 * Stops the running MCU firmware.
 */
void gxp_mcu_firmware_stop(struct gxp_mcu_firmware *mcu_fw);

/*
 * Send shutdown command to GSA.
 *
 * Returns firmware's shutdown status from GSA.
 */
int gxp_mcu_firmware_shutdown(struct gxp_mcu_firmware *mcu_fw);

/*
 * Loads MCU firmware into memories and parses the image config.
 *
 * Returns 0 on success, a negative errno on failure.
 */
int gxp_mcu_firmware_load(struct gxp_dev *gxp, char *fw_name,
			  const struct firmware **fw);

/*
 * Unloads MCU firmware.
 */
void gxp_mcu_firmware_unload(struct gxp_dev *gxp, const struct firmware *fw);

/*
 * Returns the pointer of MCU firmware associated with the GXP device object.
 *
 * This function is NOT implemented in gxp-mcu-firmware.c. Instead, it shall be
 * implemented in *-platform.c as a chip-dependent implementation.
 *
 * It's okay to not implement this function for chips without MCU support,
 * because in this case this function will never be used.
 */
struct gxp_mcu_firmware *gxp_mcu_firmware_of(struct gxp_dev *gxp);

/*
 * Handles the MCU firmware crash. It will handle the crash only when the @crash_type is
 * GCIP_FW_CRASH_UNRECOVERABLE_FAULT or GCIP_FW_CRASH_HW_WDG_TIMEOUT. Otherwise, it will ignore
 * that crash.
 *
 * This function will be called from the `gxp-kci.c` when GCIP_RKCI_FIRMWARE_CRASH RKCI is received
 * from the MCU firmware side or from the HW watchdog IRQ handler.
 */
void gxp_mcu_firmware_crash_handler(struct gxp_dev *gxp,
				    enum gcip_fw_crash_type crash_type);

/*
 * Waits for the MCU LPM transition to the PG state.
 *
 * Must be called with holding @mcu_fw->lock.
 *
 * @force: force MCU to boot in recovery mode and execute WFI so that it can
 *         go in PG state.
 * Returns true if MCU successfully transitioned to PG state, otherwise false.
 */
bool gxp_mcu_recovery_boot_shutdown(struct gxp_dev *gxp, bool force);

#endif /* __GXP_MCU_FIRMWARE_H__ */
