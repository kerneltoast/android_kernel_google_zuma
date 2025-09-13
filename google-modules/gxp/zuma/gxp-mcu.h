/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Structures and helpers for managing GXP MicroController Unit.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_MCU_H__
#define __GXP_MCU_H__

#include <gcip/gcip-mem-pool.h>
#include <gcip/gcip-memory.h>

#include "gxp-iif.h"
#include "gxp-kci.h"
#include "gxp-mcu-firmware.h"
#include "gxp-mcu-telemetry.h"
#include "gxp-uci.h"

enum gxp_mcu_boot_mode {
	GXP_MCU_BOOT_MODE_NORMAL,
	GXP_MCU_BOOT_MODE_RECOVERY,
};

/* Configuration for the dump memory to be shared to the MCU. */
struct gxp_mcu_dump_config {
	/* Device address of the dump region. */
	uint32_t dump_dev_addr;
	/* Size of the dump region. */
	uint32_t dump_size;
	/* Reserved for future expansion. */
	uint32_t reserved[2];
};

struct gxp_dev;

struct gxp_mcu {
	struct gxp_dev *gxp;
	struct gxp_mcu_firmware fw;
	/* instruction remapped data region */
	struct gcip_mem_pool remap_data_pool;
	/* secure region (memory inaccessible by non-secure AP (us)) */
	struct gcip_mem_pool remap_secure_pool;
	struct gxp_uci uci;
	struct gxp_kci kci;
	struct gcip_telemetry_ctx telemetry;
	/* To manage IIF fences. */
	struct gxp_iif *giif;
};

/*
 * Initializes all fields in @mcu.
 *
 * Returns 0 on success, a negative errno on failure.
 */
int gxp_mcu_init(struct gxp_dev *gxp, struct gxp_mcu *mcu);
/* cleans up resources in @mcu */
void gxp_mcu_exit(struct gxp_mcu *mcu);
/*
 * Forcefully resets MCU without LPM transition.
 * @gxp: The GXP device to reset MCU.
 * @release_reset: If true, it will release reset bits and let MCU transit to RUN state. Set it as
 *                 false only when the block power cycle is needed without running MCU.
 *
 * Returns 0 on success, a negative errno on failure.
 */
int gxp_mcu_reset(struct gxp_dev *gxp, bool release_reset);
/*
 * Resets UCI and KCI mailbox CSRs.
 *
 * This function must be called before running MCU FW to prevent MCU FW draining them improperly.
 */
void gxp_mcu_reset_mailbox(struct gxp_mcu *mcu);
/*
 * A wrapper function to allocate memory from @mcu->remap_data_pool.
 *
 * Returns 0 on success, a negative errno otherwise.
 */
int gxp_mcu_mem_alloc_data(struct gxp_mcu *mcu, struct gcip_memory *mem, size_t size);
/*
 * Free memory allocated by gxp_mcu_mem_alloc_data().
 */
void gxp_mcu_mem_free_data(struct gxp_mcu *mcu, struct gcip_memory *mem);

/*
 * Returns the pointer of `struct gxp_mcu` associated with the GXP device object.
 *
 * This function is NOT implemented in gxp-mcu.c. Instead, it shall be implemented in
 * *-platform.c as a chip-dependent implementation.
 */
struct gxp_mcu *gxp_mcu_of(struct gxp_dev *gxp);

/*
 * Set boot mode for MCU.
 */
void gxp_mcu_set_boot_mode(struct gxp_mcu_firmware *mcu_fw, enum gxp_mcu_boot_mode mode);

/*
 * Set the MCU debug dump config region.
 */
void gxp_mcu_set_debug_dump_config(struct gxp_mcu_firmware *mcu_fw, struct gcip_memory *mem);

#endif /* __GXP_MCU_H__ */
