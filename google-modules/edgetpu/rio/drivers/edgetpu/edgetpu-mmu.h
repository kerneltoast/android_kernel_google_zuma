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

#define EDGETPU_MMU_COHERENT		(1 << 0)

/* max number of allocated domains is: 1 for default + EDGETPU_NUM_VCIDS */
#define EDGETPU_DOMAIN_TOKEN_END	(1 + EDGETPU_NUM_VCIDS)
#define EDGETPU_DOMAIN_TOKEN_INVALID	(EDGETPU_DOMAIN_TOKEN_END + 1)
struct edgetpu_iommu_domain {
	/*
	 * IOMMU PASID, set by edgetpu_mmu_attach_domain().
	 * This field should be set as IOMMU_PASID_INVALID in
	 * edgetpu_mmu_detach_domain().
	 */
	uint pasid;
	struct gcip_iommu_domain *gdomain;
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
	return (flags & EDGETPU_MAP_COHERENT) ? EDGETPU_MMU_COHERENT : 0;
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

int edgetpu_mmu_attach(struct edgetpu_dev *dev);
void edgetpu_mmu_detach(struct edgetpu_dev *dev);

/**
 * Maps TPU IOVA @iova to @sgt.
 * @sgt: the sg table presents the list of pages.
 *
 * Description: Request TPU to map @iova to the pages presented by @sgt.
 *
 * Returns 0 on success, -errno on error.
 */
int edgetpu_mmu_map_iova_sgt(struct edgetpu_dev *etdev, tpu_addr_t iova, struct sg_table *sgt,
			     enum dma_data_direction dir, u32 mmu_flags,
			     struct edgetpu_iommu_domain *etdomain);
void edgetpu_mmu_unmap_iova_sgt_attrs(struct edgetpu_dev *etdev, tpu_addr_t iova,
				      struct sg_table *sgt, enum dma_data_direction dir,
				      struct edgetpu_iommu_domain *etdomain, unsigned long attrs);
#define edgetpu_mmu_unmap_iova_sgt(e, i, s, d, c)                              \
	edgetpu_mmu_unmap_iova_sgt_attrs(e, i, s, d, c, 0)

/**
 * Add an IOVA translation to the chip MMU/IOMMU.
 * @iova: I/O virtual address (TPU VA) to map to paddr.
 * @paddr: Physical/next-stage target address to which iova is to be mapped.
 * @size: size of the mapping in bytes.
 * @gcip_map_flags: GCIP IOMMU mapping API flags.
 * @etdomain: the IOMMU domain to add the translation to.
 *
 * Description: Add a mapping from iova -> paddr to the MMU for the chip.
 * paddr can be considered a physical address from the TPU's viewpoint, but
 * may actually be another IOVA for another IOMMU downstream of the chip MMU.
 *
 * Note: for chipsets with edgetpu_mmu_alloc() support, @iova passed to this
 * function must be either allocated from edgetpu_mmu_alloc() or reserved by
 * edgetpu_mmu_reserve().
 */
int edgetpu_mmu_add_translation(struct edgetpu_dev *etdev, unsigned long iova, phys_addr_t paddr,
				size_t size, u64 gcip_map_flags,
				struct edgetpu_iommu_domain *etdomain);

/* Remove a translation added by edgetpu_mmu_add_translation. */
void edgetpu_mmu_remove_translation(struct edgetpu_dev *etdev, unsigned long iova, size_t size,
				    struct edgetpu_iommu_domain *etdomain);

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
bool edgetpu_mmu_is_domain_default_domain(struct edgetpu_dev *etdev,
					  struct edgetpu_iommu_domain *etdomain);

/*
 * Returns the domain attached for a given PASID.
 *
 * If AUX domains are not supported, this function always returns the default domain.
 *
 * Returns NULL if @pasid is invalid or AUX domains are supported but @pasid is not attached.
 */
struct edgetpu_iommu_domain *edgetpu_mmu_domain_for_pasid(struct edgetpu_dev *etdev, uint pasid);

/*
 * Returns the default IOMMU domain used for kernel mappings.
 */
static inline struct edgetpu_iommu_domain *edgetpu_mmu_default_domain(struct edgetpu_dev *etdev)
{
	return edgetpu_mmu_domain_for_pasid(etdev, 0);
}

static inline bool edgetpu_mmu_domain_detached(const struct edgetpu_iommu_domain *etdomain)
{
	return !etdomain || etdomain->pasid == IOMMU_PASID_INVALID;
}

#endif /* __EDGETPU_MMU_H__ */
