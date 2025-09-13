/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EdgeTPU firmware loader.
 *
 * Copyright (C) 2020-2022,2024 Google, Inc.
 */
#ifndef __EDGETPU_FIRMWARE_H__
#define __EDGETPU_FIRMWARE_H__

#include <linux/bits.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>

#include <gcip/gcip-firmware.h>
#include <gcip/gcip-image-config.h>

#include "edgetpu-internal.h"
#include "edgetpu-mmu.h"

#define MAX_IOMMU_MAPPINGS 23
#define MAX_NS_IOMMU_MAPPINGS 5

#define EDGETPU_FW_HEADER_SIZE SZ_4K

/* Value of magic field above: 'TPUF' as a 32-bit LE int */
#define EDGETPU_FW_MAGIC	0x46555054

/*
 * Instruction remap registers make carveout memory appear at address
 * 0x10000000 from the TPU CPU perspective
 */
#define EDGETPU_INSTRUCTION_REMAP_BASE		0x10000000

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
 * Default address from which the TPU CPU can access data in the remapped region.
 * Data in remapped DRAM starts after firmware code and internal data.
 */
#define EDGETPU_DEFAULT_REMAPPED_DATA_ADDR                                                         \
	(EDGETPU_INSTRUCTION_REMAP_BASE + EDGETPU_DEFAULT_FW_LIMIT)

/* Firmware client_id fields. */
#define CLIENT_ID_REALM		GENMASK(31, 30)
#define CLIENT_ID_VM		GENMASK(29, 16)
#define CLIENT_ID_PASID		GENMASK(15, 0)

/* Firmware client_id realm IDs. */
#define CLIENT_REALM_NS		0	/* kHostVmId  */

/*
 * Load and run firmware.
 * @name: the name passed into underlying request_firmware API
 * Used internally by the sysfs load interface and by unit tests.
 */
int edgetpu_firmware_run(struct edgetpu_dev *etdev, const char *name);

/* Load and run the default firmware name for the chip. */
int edgetpu_firmware_run_default(struct edgetpu_dev *etdev);

/* Runs default firmware for the chip, caller holds FW/PM locks */
int edgetpu_firmware_run_default_locked(struct edgetpu_dev *etdev);

void edgetpu_firmware_set_img_cfg_parser(struct edgetpu_firmware *et_fw,
					 struct gcip_image_config_parser *parser);
struct gcip_image_config_parser *
edgetpu_firmware_get_img_cfg_parser(struct edgetpu_firmware *et_fw);

/*
 * Creates the firmware loader for device.
 * Must be called after the firmware region is setup via edgetpu_firmware_setup_fw_region.
 * @etdev: the device for which to create the firmware loader.
 */
int edgetpu_firmware_create(struct edgetpu_dev *etdev);

void edgetpu_firmware_destroy(struct edgetpu_dev *etdev);
void edgetpu_firmware_mappings_show(struct edgetpu_dev *etdev,
				    struct seq_file *s);

/*
 * These functions grab and release the internal firmware lock and must be used before calling the
 * helper functions suffixed with _locked below.
 *
 * Lock ordering note: this lock can be taken with the pm_lock held.  For code that needs both
 * locks, hold pm_lock first and then take the firmware state lock (aka internal firmware lock).
 * Most non-PM code should not require both locks held simultaneously; a pm_get(), then lock
 * firmware state and perform the firmware operations, followed by a firmware unlock and pm_put()
 * should be more common.
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

/*
 * Called on software watchdog timeout or crash/lockup recovery to restart firmware.
 * @in_powerdown: if true we're in the power down sequence and must leave power state alone,
 *    we are called to restart firmware so it can execute its graceful shutdown sequence.
 */
void edgetpu_firmware_watchdog_restart(struct edgetpu_dev *etdev, bool in_powerdown);

/* Returns the current firmware image name. */
ssize_t edgetpu_firmware_get_name(struct edgetpu_dev *etdev, char *buf,
				  size_t buflen);

/* Returns the changelist ID of the image loaded on the device. */
uint32_t edgetpu_firmware_get_cl(struct edgetpu_firmware *et_fw);

/* Returns the build time of the image in seconds since 1970. */
uint64_t edgetpu_firmware_get_build_time(struct edgetpu_firmware *et_fw);

/* Returns the flavor of the firmware image. */
enum gcip_fw_flavor edgetpu_firmware_get_flavor(struct edgetpu_dev *etdev);

/* Establish "shared" mappings from the firmware image config at domain attach time. */
void edgetpu_firmware_shared_mappings_context_map(struct edgetpu_dev *etdev,
						  struct edgetpu_iommu_domain *etdomain);

/* Unmap "shared" mappings from the firmware image config at domain detach time. */
void edgetpu_firmware_shared_mappings_context_unmap(struct edgetpu_dev *etdev,
						    struct edgetpu_iommu_domain *etdomain);

/*
 * Assert or release the reset signal of the TPU's CPU
 * Depending on privilege level, this may be by a direct register write
 * or a call into GSA.
 */
int edgetpu_firmware_reset_cpu(struct edgetpu_dev *etdev, bool assert_reset);

/*
 * Setup firmware region carveout and iremap pool for device.
 * Allocates device firmware private data.  Must be called before edgetpu_firmware_create.
 *
 * @etdev: device for which to setup firmware region.
 * @fw_region_paddr: phys addr of firmware region (as from device tree)
 */
int edgetpu_firmware_setup_fw_region(struct edgetpu_dev *etdev, phys_addr_t fw_region_paddr);

/* Cleanup firmware region carveout and iremap pool, free firmware private data. */
void edgetpu_firmware_cleanup_fw_region(struct edgetpu_dev *etdev);

/* Return KVA of FW shared data area. */
void *edgetpu_firmware_shared_data_vaddr(struct edgetpu_dev *etdev);

/* Return device DMA addr of FW shared data area. */
dma_addr_t edgetpu_firmware_shared_data_daddr(struct edgetpu_dev *etdev);

/* Return phys addr of FW shared data area. */
phys_addr_t edgetpu_firmware_shared_data_paddr(struct edgetpu_dev *etdev);

/* Return size of FW shared data area. */
size_t edgetpu_firmware_shared_data_size(struct edgetpu_dev *etdev);

/* Return phys addr of FW remap region start. */
phys_addr_t edgetpu_firmware_fw_region_paddr(struct edgetpu_dev *etdev);

/* Return size of entire FW remap region. */
size_t edgetpu_firmware_fw_region_size(struct edgetpu_dev *etdev);

#if IS_ENABLED(CONFIG_EDGETPU_TEST)
/* Used by unit tests to set a mocked GSA device. */
void edgetpu_firmware_set_fake_gsa_dev(struct edgetpu_dev *etdev, struct device *gsa_dev);

/* Return the gcip_fault_inject from the private firmware data for the device. */
struct gcip_fault_inject *edgetpu_firmware_get_fault_inject(struct edgetpu_dev *etdev);
#endif

/* Return true if @error_mask implies that the firmware is not responding. */
static inline bool edgetpu_firmware_is_not_responding(uint error_mask)
{
	return error_mask == EDGETPU_ERROR_FW_CRASH || error_mask == EDGETPU_ERROR_WATCHDOG_TIMEOUT;
}

#endif /* __EDGETPU_FIRMWARE_H__ */
