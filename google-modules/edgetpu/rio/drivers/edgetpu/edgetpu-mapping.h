/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Records the mapped TPU IOVA in a device group.
 *
 * Copyright (C) 2019 Google, Inc.
 */
#ifndef __EDGETPU_MAPPING_H__
#define __EDGETPU_MAPPING_H__

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-map-ops.h>
#include <linux/iommu.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include <gcip/gcip-iommu.h>

#include "edgetpu-internal.h"
#include "edgetpu-mmu.h"

struct edgetpu_mapping_root {
	struct rb_root rb;
	struct mutex lock;
	size_t count;
};

struct edgetpu_mapping {
	struct gcip_iommu_mapping *gcip_mapping;
	struct rb_node node;
	u64 host_address;
	edgetpu_map_flag_t flags; /* the flag passed by the runtime */
	u32 mmu_flags;
	/* Private data set by whom created this mapping. */
	void *priv;
	/*
	 * This callback will be called when the mappings in
	 * edgetpu_mapping_root are wiped out, i.e. in edgetpu_mapping_clear().
	 * Release/un-map allocated TPU address in this callback.
	 *
	 * The lock of edgetpu_mapping_root is held when calling this callback.
	 *
	 * This callback is called after @map is unlinked from the RB tree, it's
	 * safe to free @map here.
	 *
	 * Note: edgetpu_mapping_unlink() will NOT call this callback.
	 *
	 * This field is mandatory.
	 */
	void (*release)(struct edgetpu_mapping *map);
	/*
	 * Callback for showing the map.
	 *
	 * The lock of edgetpu_mapping_root is held when calling this callback.
	 *
	 * This callback is optional. If this callback is not set, the mapping
	 * will be skipped on showing.
	 */
	void (*show)(struct edgetpu_mapping *map, struct seq_file *s);
};

static inline void edgetpu_mapping_lock(struct edgetpu_mapping_root *root)
{
	mutex_lock(&root->lock);
}

static inline void edgetpu_mapping_unlock(struct edgetpu_mapping_root *root)
{
	mutex_unlock(&root->lock);
}

/* Initializes the mapping structure. */
void edgetpu_mapping_init(struct edgetpu_mapping_root *mappings);

/*
 * Inserts @map into @mappings.
 *
 * Returns 0 on success.
 * Returns -EBUSY if the map already exists.
 */
int edgetpu_mapping_add(struct edgetpu_mapping_root *mappings,
			struct edgetpu_mapping *map);
/*
 * Finds the mapping previously added with edgetpu_mapping_add().
 *
 * Caller holds the mappings lock.
 *
 * Returns NULL if the mapping is not found.
 */
struct edgetpu_mapping *
edgetpu_mapping_find_locked(struct edgetpu_mapping_root *mappings,
			    tpu_addr_t iova);

/*
 * Removes @map from @mappings.
 *
 * Caller holds the mappings lock.
 */
void edgetpu_mapping_unlink(struct edgetpu_mapping_root *mappings,
			    struct edgetpu_mapping *map);

/*
 * Returns the first map in @mappings.
 *
 * Caller holds the mappings lock.
 *
 * Returns NULL if @mappings is empty.
 */
struct edgetpu_mapping *
edgetpu_mapping_first_locked(struct edgetpu_mapping_root *mappings);

/*
 * Clears added mappings.
 */
void edgetpu_mapping_clear(struct edgetpu_mapping_root *mappings);

/* dump mappings to seq file @s */
void edgetpu_mappings_show(struct edgetpu_mapping_root *mappings,
			   struct seq_file *s);

/* Returns gcip map flags based on @mmu_flags and @dir */
static inline u64 mmu_flag_to_gcip_flags(u32 mmu_flags, enum dma_data_direction dir)
{
	u64 gcip_map_flags = 0;

	if (mmu_flags & EDGETPU_MMU_COHERENT)
		gcip_map_flags = GCIP_MAP_FLAGS_DMA_COHERENT_TO_FLAGS(true);
	gcip_map_flags |= GCIP_MAP_FLAGS_DMA_DIRECTION_TO_FLAGS(dir);
	return gcip_map_flags;
}

/*
 * Return total size of mappings under the supplied root.
 * @restrict32: only count mappings restricted to 32-bit CPU-accessible IOVA space.
 */
size_t edgetpu_mappings_total_size(struct edgetpu_mapping_root *mappings, bool restrict32);

/*
 * Returns the gcip_map_flags encoded from edgetpu_dma_flags and dma_attrs.
 * If @adjust_dir is true, the dma data direction will be adjusted with edgetpu_host_dma_dir.
 */
u64 edgetpu_mappings_encode_gcip_map_flags(edgetpu_map_flag_t flags, unsigned long dma_attrs,
					   bool adjust_dir);

/*
 * Returns the mapping struct that contains @iova (within the @mappings tree), or NULL if none.
 * Caller must hold the group->host_mappings lock.
 */
struct edgetpu_mapping *
edgetpu_mapping_find_iova_range(struct edgetpu_mapping_root *mappings, tpu_addr_t iova);

#endif /* __EDGETPU_MAPPING_H__ */
