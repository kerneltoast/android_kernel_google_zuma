/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GXP IOVAs. The list of addresses for fixed device-side IOVAs
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __CALLISTO_IOVA_H__
#define __CALLISTO_IOVA_H__

/* IOVAs from system firmware's view */
#define GXP_IOVA_MAILBOX(_x_)           (0x18390000 + (_x_) * 0x00020000)
#define GXP_IOVA_EXT_TPU_MBX            (0x1A050000)
#define GXP_IOVA_FIRMWARE(_x_)          (0xFA000000 + (_x_) * 0x00100000)
#define GXP_IOVA_SHARED_BUFFER          (0xFA3A8000)
#define GXP_IOVA_PRIV_FW_DATA           (0xFA500000)
#define GXP_IOVA_TPU_MBX_BUFFER(_x_)    (0xFE100000 + (_x_) * 0x00040000)

/* IOVAs for MCU firmware */
#define GXP_MCU_NS_MAILBOX(_x_) (0x2000000 + (_x_) * 0x1000)
#define GXP_IREMAP_CODE_BASE 0x10000000
#define GXP_IREMAP_CODE_SIZE 0x100000 /* 1MB */
/* offset from GXP_IREMAP_CODE_BASE */
#define GXP_IREMAP_SECURE_OFFSET GXP_IREMAP_CODE_SIZE
#define GXP_IREMAP_SECURE_SIZE 0x100000 /* 1MB */
#define GXP_IREMAP_DATA_OFFSET (GXP_IREMAP_SECURE_OFFSET + GXP_IREMAP_SECURE_SIZE)
#define GXP_IREMAP_DATA_SIZE 0x1FF000 /* (2MB - 4KB) */
/* Last 4K page of IREMAP_DATA */
#define GXP_MCU_BOOT_MODE_OFFSET (GXP_IREMAP_DATA_OFFSET + GXP_IREMAP_DATA_SIZE)
#define GXP_IREMAP_DYNAMIC_CODE_BASE 0x11000000

#endif /* __CALLISTO_IOVA_H__ */
