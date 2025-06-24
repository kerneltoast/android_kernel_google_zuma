/*
 * Copyright 2021 Qorvo US, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
 *
 * This file is provided under the Apache License 2.0, or the
 * GNU General Public License v2.0.
 *
 */
#ifndef __QMROM_ERROR_H__
#define __QMROM_ERROR_H__

#define SPI_ERR_BASE -8000
#define PEG_ERR_BASE -9000
#define SPI_PROTO_ERR_BASE -10000

#define IS_PTR_ERROR(ptr) (((intptr_t)ptr) <= 0)
#define PTR2ERROR(ptr) ((int)((intptr_t)ptr))
#define ERROR2PTR(error) ((void *)((intptr_t)error))

#endif /* __QMROM_ERROR_H__ */