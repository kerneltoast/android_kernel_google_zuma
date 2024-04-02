/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Include all configuration files for Callisto.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __CALLISTO_CONFIG_H__
#define __CALLISTO_CONFIG_H__

#include <linux/sizes.h>

#define GXP_DRIVER_NAME "gxp_callisto"
#define DSP_FIRMWARE_DEFAULT_PREFIX "gxp_callisto_fw_core"
#define GXP_DEFAULT_MCU_FIRMWARE "google/gxp-callisto.fw"

/*
 * From soc/gs/include/dt-bindings/clock/zuma.h
 *   #define ACPM_DVFS_AUR 0x0B040013
 */
#define AUR_DVFS_DOMAIN 19

#define GXP_NUM_CORES 3
/* three for cores, one for KCI, and one for UCI */
#define GXP_NUM_MAILBOXES (GXP_NUM_CORES + 2)
/* Indexes of the mailbox reg in device tree */
#define KCI_MAILBOX_ID (GXP_NUM_CORES)
#define UCI_MAILBOX_ID (GXP_NUM_CORES + 1)

/* three for cores, one for MCU */
#define GXP_NUM_WAKEUP_DOORBELLS (GXP_NUM_CORES + 1)

/* The total size of the configuration region. */
#define GXP_SHARED_BUFFER_SIZE SZ_512K
/* Size of slice per VD. */
#define GXP_SHARED_SLICE_SIZE SZ_32K
/* The max number of active virtual devices. */
#define GXP_NUM_SHARED_SLICES 7

/*
 * Can be coherent with AP
 *
 * Linux IOMMU-DMA APIs optimise cache operations based on "dma-coherent"
 * property in DT. Handle "dma-coherent" property in driver itself instead of
 * specifying in DT so as to support both coherent and non-coherent buffers.
 */
#define GXP_IS_DMA_COHERENT

/* HW watchdog */
#define GXP_WDG_DT_IRQ_INDEX 5
#define GXP_WDG_ENABLE_BIT 0
#define GXP_WDG_INT_CLEAR_BIT 5

#include "config-pwr-state.h"
#include "context.h"
#include "csrs.h"
#include "iova.h"
#include "lpm.h"
#include "mailbox-regs.h"

#endif /* __CALLISTO_CONFIG_H__ */
