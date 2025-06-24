/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip-dependent configuration for TPU CPU.
 *
 * Copyright (C) 2021-2022 Google LLC
 */

#ifndef __RIO_CONFIG_TPU_CPU_H__
#define __RIO_CONFIG_TPU_CPU_H__

#define EDGETPU_REG_RESET_CONTROL                       0x190018
#define EDGETPU_REG_INSTRUCTION_REMAP_CONTROL           0x190070
#define EDGETPU_REG_INSTRUCTION_REMAP_BASE              0x190080
#define EDGETPU_REG_INSTRUCTION_REMAP_LIMIT             0x190090
#define EDGETPU_REG_INSTRUCTION_REMAP_NEW_BASE          0x1900a0
#define EDGETPU_REG_INSTRUCTION_REMAP_SECURITY          0x1900b0
#define EDGETPU_REG_SECURITY                            0x190060
#define EDGETPU_REG_LPM_CONTROL                         0x1D0020

/* funcApbSlaves_debugApbSlaves_apbaddr_dbg_0, 1 base addresses */
#define EDGETPU_REG_EXTERNAL_DEBUG_0_BASE		0x210000
#define EDGETPU_REG_EXTERNAL_DEBUG_1_BASE		0x310000

/* CSR offsets within external debug 0,1 */
#define EDGETPU_REG_EXTERNAL_DEBUG_PROGRAM_COUNTER      0x00a0
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_ACCESS          0x0fb0
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_STATUS          0x0fb4
#define EDGETPU_REG_EXTERNAL_DEBUG_AUTHSTATUS           0x0fb8
#define EDGETPU_REG_EXTERNAL_DEBUG_OS_LOCK_ACCESS       0x0300
#define EDGETPU_REG_EXTERNAL_DEBUG_PROCESSOR_STATUS     0x0314

#endif /* __RIO_CONFIG_TPU_CPU_H__ */
