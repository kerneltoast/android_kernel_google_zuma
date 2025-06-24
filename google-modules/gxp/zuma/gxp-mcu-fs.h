/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common file system operations for devices with MCU support.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_MCU_FS_H__
#define __GXP_MCU_FS_H__

#include <linux/fs.h>
#include <linux/mm_types.h>

/**
 * gxp_mcu_ioctl() - Handles ioctl calls that are meaningful for devices with
 * MCU support.
 *
 * Return:
 * * -ENOTTY    - The call is not handled - either the command is unrecognized
 *                or the driver is running in direct mode.
 * * Otherwise  - Returned by individual command handlers.
 */
long gxp_mcu_ioctl(struct file *file, uint cmd, ulong arg);

/**
 * gxp_mcu_mmap() - Handles mmap calls that are meaningful for devices with
 * MCU support.
 *
 * Return:
 * * -EOPNOTSUPP - The call is not handled - either the offset is unrecognized
 *                 or the driver is running in direct mode.
 * * Otherwise   - Returned by individual command handlers.
 */
int gxp_mcu_mmap(struct file *file, struct vm_area_struct *vma);

#endif /* __GXP_MCU_FS_H__ */
