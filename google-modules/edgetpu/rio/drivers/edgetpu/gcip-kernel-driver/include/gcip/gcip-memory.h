/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The definition of gcip_memory.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __GCIP_MEMORY_H__
#define __GCIP_MEMORY_H__

#include <linux/types.h>

/**
 * struct gcip_memory - The object to record the mapping of the memory.
 * @virt_addr: The kernel virtual address of the memory.
 * @phys_addr: The physical address of the memory.
 * @dma_addr: The device DMA address of the memory.
 * @host_addr: The host address of the memory.
 * @size: The size of the memory.
 */
struct gcip_memory {
	void *virt_addr;
	phys_addr_t phys_addr;
	dma_addr_t dma_addr;
	u64 host_addr;
	size_t size;
};

#endif /* __GCIP_MEMORY_H__ */
