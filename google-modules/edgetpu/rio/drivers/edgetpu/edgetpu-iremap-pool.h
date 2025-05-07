/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Lightweight gen_pool based allocator for memory that is placed at a specific
 * location in the TPU address space (such as a carveout memory)
 *
 * Copyright (C) 2020 Google, Inc.
 */
#ifndef __EDGETPU_IREMAP_POOL_H_
#define __EDGETPU_IREMAP_POOL_H_

#include "edgetpu-internal.h"
#include "edgetpu-mmu.h"

struct edgetpu_mempool {
	struct gen_pool *gen_pool;
	void *base_vaddr;
	dma_addr_t base_dma_addr;
	phys_addr_t base_phys_addr;
	size_t granule;
};

/*
 * Create a memory pool with the provided addresses.
 * The etdev->iremap_pool token will be set and used internally in the calls
 * below.
 */
int edgetpu_iremap_pool_create(struct edgetpu_dev *etdev, void *base_vaddr,
			       dma_addr_t base_dma_addr,
			       phys_addr_t base_phys_addr, size_t size,
			       size_t granule);

/* Release the resources allocated by the memory pool (if any) */
void edgetpu_iremap_pool_destroy(struct edgetpu_dev *etdev);

/*
 * Allocate memory from the instruction remap pool.
 */
int edgetpu_iremap_alloc(struct edgetpu_dev *etdev, size_t size, struct edgetpu_coherent_mem *mem);

/*
 * Free memory allocated by the function above.
 */
void edgetpu_iremap_free(struct edgetpu_dev *etdev, struct edgetpu_coherent_mem *mem);

/*
 * Map memory in the pool to user space.
 */
int edgetpu_iremap_mmap(struct edgetpu_dev *etdev, struct vm_area_struct *vma,
			struct edgetpu_coherent_mem *mem);

#endif /* __EDGETPU_IREMAP_POOL_H_ */
