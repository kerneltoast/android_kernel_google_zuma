/*
 * Copyright 2021 Qorvo US, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
 *
 * This file is provided under the Apache License 2.0, or the
 * GNU General Public License v2.0.
 *
 */
#ifndef __QMROM_UTILS_H__
#define __QMROM_UTILS_H__

#ifndef __KERNEL__
#include <stdlib.h>
#include <stdint.h>

#ifndef __linux__
extern void usleep(unsigned int us);
#else
#include <unistd.h>
#endif

#define qmrom_msleep(ms)           \
	do {                       \
		usleep(ms * 1000); \
	} while (0)

#define qmrom_alloc(ptr, size)         \
	do {                           \
		ptr = calloc(1, size); \
	} while (0)

#define qmrom_free free

#define qmrom_data_dma_able(d) true
#else

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>

#define qmrom_msleep(ms)                            \
	do {                                        \
		usleep_range(ms * 1000, ms * 1000); \
	} while (0)

#define qmrom_alloc(ptr, size)                             \
	do {                                               \
		ptr = kzalloc(size, GFP_KERNEL | GFP_DMA); \
	} while (0)

#define qmrom_free kfree

#define qmrom_data_dma_able(d) !is_vmalloc_addr(d)
#endif
#endif /* __QMROM_UTILS_H__ */
