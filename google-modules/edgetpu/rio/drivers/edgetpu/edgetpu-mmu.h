/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Edge TPU MMU API.
 *
 * Copyright (C) 2020 Google, Inc.
 */
#ifndef __EDGETPU_MMU_H__
#define __EDGETPU_MMU_H__

#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/scatterlist.h>
#include <linux/version.h>

#include <gcip/gcip-iommu.h>

#include "edgetpu-internal.h"
#include "edgetpu.h"

/* flags for MMU operations */

/* Whether the TPU address allocated must be accessible to the control cluster. */
#define EDGETPU_MMU_CC_ACCESS		(0 << 0)
#define EDGETPU_MMU_CC_NO_ACCESS	(1 << 0)
/* The memory will be mapped to host DRAM or dma-buf. */
#define EDGETPU_MMU_HOST		(0 << 1)
#define EDGETPU_MMU_DMABUF		(1 << 1)

#define EDGETPU_MMU_COHERENT		(1 << 2)

/*
 * The max possible value of token is (EDGETPU_DOMAIN_TOKEN_END - 1), which
 * shouldn't equal or exceed the bit mask EDGETPU_CONTEXT_DOMAIN_TOKEN.
 */
#define EDGETPU_DOMAIN_TOKEN_END EDGETPU_CONTEXT_DOMAIN_TOKEN
struct edgetpu_iommu_domain {
	/*
	 * IOMMU PASID, set by edgetpu_mmu_attach_domain().
	 * This field should be set as IOMMU_PASID_INVALID in
	 * edgetpu_mmu_detach_domain().
	 */
	uint pasid;
	struct gcip_iommu_domain *gdomain;
	/*
	 * A token set by edgetpu_mmu_alloc_domain(). See the description of
	 * edgetpu_mmu_add_translation() about @context_id for more details.
	 */
	int token;
};

/*
 * Return the DMA direction to use for the host DMA API call to map a buffer.
 * Normally DMA buffers "only written" by the device (so far as the TPU runtime
 * is concerned) would be mapped write-only to the host IOMMU.  However, our
 * TPU CPU may perform cache line fills and possibly prefetches from the buffer
 * being written to.  Map write-only buffers bi-directional.
 */
static inline enum dma_data_direction
edgetpu_host_dma_dir(enum dma_data_direction target_dir)
{
	switch (target_dir) {
	case DMA_FROM_DEVICE:
		return DMA_BIDIRECTIONAL;
	default:
		return target_dir;
	}
}

static inline enum dma_data_direction map_flag_to_host_dma_dir(edgetpu_map_flag_t flags)
{
	return edgetpu_host_dma_dir(flags & EDGETPU_MAP_DIR_MASK);
}

static inline u32 map_to_mmu_flags(edgetpu_map_flag_t flags)
{
	u32 ret = 0;

	ret |= (flags & EDGETPU_MAP_CPU_NONACCESSIBLE) ? EDGETPU_MMU_CC_NO_ACCESS :
							 EDGETPU_MMU_CC_ACCESS;
	ret |= (flags & EDGETPU_MAP_COHERENT) ? EDGETPU_MMU_COHERENT : 0;
	return ret;
}

/* To be compatible with Linux kernel without this flag. */
#ifndef DMA_ATTR_PBHA_PROT
#define DMA_ATTR_PBHA_PROT(x) 0
#endif
#ifndef IOMMU_PBHA_PROT
#define IOMMU_PBHA_PROT(x) 0
#endif
/* fetch the value of PBHA in map flags */
#define EDGEPTU_MAP_PBHA_VALUE(flags)                                          \
	((flags >> EDGETPU_MAP_ATTR_PBHA_SHIFT) & EDGETPU_MAP_ATTR_PBHA_MASK)
/*
 * Converts edgetpu map flag to DMA attr.
 *
 * Ignore EDGETPU_MAP_SKIP_CPU_SYNC if @map = true
 */
static inline unsigned long map_to_dma_attr(edgetpu_map_flag_t flags, bool map)
{
	unsigned long attr = 0;

	if (!map && flags & EDGETPU_MAP_SKIP_CPU_SYNC)
		attr = DMA_ATTR_SKIP_CPU_SYNC;
	attr |= DMA_ATTR_PBHA_PROT(EDGEPTU_MAP_PBHA_VALUE(flags));

	return attr;
}

int edgetpu_mmu_attach(struct edgetpu_dev *dev, void *mmu_info);
void edgetpu_mmu_detach(struct edgetpu_dev *dev);

int edgetpu_mmu_map(struct edgetpu_dev *dev, struct edgetpu_mapping *map,
		    enum edgetpu_context_id context_id, u32 mmu_flags);
void edgetpu_mmu_unmap(struct edgetpu_dev *dev, struct edgetpu_mapping *map,
		       enum edgetpu_context_id context_id);

int edgetpu_mmu_map_sgt(struct edgetpu_dev *etdev, struct sg_table *sgt,
			enum edgetpu_context_id context_id, enum dma_data_direction dir,
			unsigned long dma_attrs, u32 mmu_flags);
void edgetpu_mmu_unmap_sgt(struct edgetpu_dev *etdev, struct sg_table *sgt,
			   enum edgetpu_context_id context_id, enum dma_data_direction dir,
			   unsigned long dma_attrs, u32 mmu_flags);

/**
 * Maps TPU IOVA @iova to @sgt.
 * @sgt: the sg table presents the list of pages.
 *
 * Description: Request TPU to map @iova to the pages presented by @sgt.
 *
 * Returns 0 on success, -errno on error.
 */
int edgetpu_mmu_map_iova_sgt(struct edgetpu_dev *etdev, tpu_addr_t iova,
			     struct sg_table *sgt, enum dma_data_direction dir,
			     u32 mmu_flags, enum edgetpu_context_id context_id);
void edgetpu_mmu_unmap_iova_sgt_attrs(struct edgetpu_dev *etdev,
				      tpu_addr_t iova, struct sg_table *sgt,
				      enum dma_data_direction dir,
				      enum edgetpu_context_id context_id,
				      unsigned long attrs);
#define edgetpu_mmu_unmap_iova_sgt(e, i, s, d, c)                              \
	edgetpu_mmu_unmap_iova_sgt_attrs(e, i, s, d, c, 0)

/**
 * Add an IOVA translation to the chip MMU/IOMMU.
 * @iova: I/O virtual address (TPU VA) to map to paddr.
 * @paddr: Physical/next-stage target address to which iova is to be mapped.
 * @size: size of the mapping in bytes.
 * @prot: IOMMU API protections to use for the mapping.
 * @context_id: generic context ID for the mapping.
 *
 * Description: Add a mapping from iova -> paddr to the MMU for the chip.
 * paddr can be considered a physical address from the TPU's viewpoint, but
 * may actually be another IOVA for another IOMMU downstream of the chip MMU.
 *
 * For chipsets with IOMMU AUX domain support, @context_id can be used to
 * specify a detached IOMMU domain by value
 * (EDGETPU_CONTEXT_DOMAIN_TOKEN | @token), where @token is the one returned by
 * edgetpu_mmu_alloc_domain(). This description holds for all APIs in this file
 * with @context_id as a parameter.
 */
int edgetpu_mmu_add_translation(struct edgetpu_dev *etdev, unsigned long iova,
				phys_addr_t paddr, size_t size, int prot,
				enum edgetpu_context_id context_id);

/* Remove a translation added by edgetpu_mmu_add_translation. */
void edgetpu_mmu_remove_translation(struct edgetpu_dev *etdev,
				    unsigned long iova, size_t size,
				    enum edgetpu_context_id context_id);

/*
 * Allocates a IOMMU domain.
 *
 * The newly allocated domain would have @pasid equal IOMMU_PASID_INVALID, use
 * edgetpu_mmu_attach_domain() to acquire a valid PASID.
 *
 * If the chipset doesn't need to drive the domain AUX feature, a valid
 * pointer shall be returned with @etdomain->pasid == IOMMU_PASID_INVALID.
 *
 * Returns NULL on error.
 */
struct edgetpu_iommu_domain *
edgetpu_mmu_alloc_domain(struct edgetpu_dev *etdev);

/* Frees the domain previously allocated by edgetpu_mmu_alloc_domain(). */
void edgetpu_mmu_free_domain(struct edgetpu_dev *etdev,
			     struct edgetpu_iommu_domain *etdomain);

/*
 * Attaches the domain to the MMU device.
 *
 * If the chipset doesn't need to drive the domain AUX feature, this function
 * should return 0 without setting @etdomain->pasid.
 *
 * When success, 0 is returned and @etdomain->pasid is set.
 * Returns -errno on error.
 */
int edgetpu_mmu_attach_domain(struct edgetpu_dev *etdev,
			      struct edgetpu_iommu_domain *etdomain);

/* Detaches the domain from the MMU device. */
void edgetpu_mmu_detach_domain(struct edgetpu_dev *etdev,
			       struct edgetpu_iommu_domain *etdomain);

/* TODO(b/281459896) Make domain comparisons internal to edgetpu-mmu.h */
/*
 * Returns whether mappings for a given context exist in the default domain.
 *
 * If a context represented by @ctx_id has been assigned the default IOMMU domain, either uniquely,
 * or because AUX domains are not supported, this function returns true.
 *
 * This can be used to determine if a buffer which is already mapped for the default domain (such as
 * coherent buffers or dma-bufs) needs to be remapped for specifically for the context.
 */
bool edgetpu_mmu_is_context_using_default_domain(struct edgetpu_dev *etdev,
						 enum edgetpu_context_id ctx_id);

#endif /* __EDGETPU_MMU_H__ */
