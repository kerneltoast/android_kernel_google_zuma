// SPDX-License-Identifier: GPL-2.0
/*
 * Records and maintains the mapped TPU IOVA in a device group.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>

#include "edgetpu-internal.h"
#include "edgetpu-mapping.h"

/*
 * Compare @map->device_address with @iova.
 *
 * Returns -1, 0, 1 if @map->device_address is "less than", "equal to", or
 * "larger than" @iova, respectively.
 */
static int compare(struct edgetpu_mapping *map, tpu_addr_t iova)
{
	if (map->gcip_mapping->device_address != iova) {
		if (map->gcip_mapping->device_address < iova)
			return -1;
		else
			return 1;
	}
	return 0;
}

void edgetpu_mapping_init(struct edgetpu_mapping_root *mappings)
{
	mappings->rb = RB_ROOT;
	mappings->count = 0;
	mutex_init(&mappings->lock);
}

int edgetpu_mapping_add(struct edgetpu_mapping_root *mappings,
			struct edgetpu_mapping *map)
{
	struct rb_node **new;
	struct rb_node *parent = NULL;
	int ret = -EBUSY;

	if (!map->release)
		return -EINVAL;
	edgetpu_mapping_lock(mappings);
	new = &mappings->rb.rb_node;

	while (*new) {
		struct edgetpu_mapping *this =
			container_of(*new, struct edgetpu_mapping, node);
		const int cmp = compare(this, map->gcip_mapping->device_address);

		parent = *new;
		if (cmp > 0)
			new = &((*new)->rb_left);
		else if (cmp < 0)
			new = &((*new)->rb_right);
		else
			goto out;
	}

	rb_link_node(&map->node, parent, new);
	rb_insert_color(&map->node, &mappings->rb);
	mappings->count++;
	ret = 0;

out:
	edgetpu_mapping_unlock(mappings);
	return ret;
}

struct edgetpu_mapping *
edgetpu_mapping_find_locked(struct edgetpu_mapping_root *mappings,
			    tpu_addr_t iova)
{
	struct rb_node *node = mappings->rb.rb_node;

	while (node) {
		struct edgetpu_mapping *map =
			container_of(node, struct edgetpu_mapping, node);
		const int cmp = compare(map, iova);

		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			return map;
	}

	return NULL;
}

void edgetpu_mapping_unlink(struct edgetpu_mapping_root *mappings,
			    struct edgetpu_mapping *map)
{
	rb_erase(&map->node, &mappings->rb);
	mappings->count--;
}

struct edgetpu_mapping *
edgetpu_mapping_first_locked(struct edgetpu_mapping_root *mappings)
{
	struct rb_node *node = rb_first(&mappings->rb);

	if (!node)
		return NULL;
	return container_of(node, struct edgetpu_mapping, node);
}

void edgetpu_mapping_clear(struct edgetpu_mapping_root *mappings)
{
	struct edgetpu_mapping *map;

	edgetpu_mapping_lock(mappings);

	for (map = edgetpu_mapping_first_locked(mappings); map;
	     map = edgetpu_mapping_first_locked(mappings)) {
		edgetpu_mapping_unlink(mappings, map);
		map->release(map);
	}

	edgetpu_mapping_unlock(mappings);
}

void edgetpu_mappings_show(struct edgetpu_mapping_root *mappings,
			   struct seq_file *s)
{
	struct rb_node *node;

	edgetpu_mapping_lock(mappings);

	for (node = rb_first(&mappings->rb); node; node = rb_next(node)) {
		struct edgetpu_mapping *map =
			container_of(node, struct edgetpu_mapping, node);

		if (map->show)
			map->show(map, s);
	}

	edgetpu_mapping_unlock(mappings);
}

size_t edgetpu_mappings_total_size(struct edgetpu_mapping_root *mappings, bool restrict32)
{
	struct rb_node *node;
	size_t total = 0;

	edgetpu_mapping_lock(mappings);

	for (node = rb_first(&mappings->rb); node; node = rb_next(node)) {
		struct edgetpu_mapping *map =
			container_of(node, struct edgetpu_mapping, node);

		if (restrict32 && (map->flags & EDGETPU_MAP_CPU_NONACCESSIBLE))
			continue;

		total += map->gcip_mapping->size;
	}

	edgetpu_mapping_unlock(mappings);
	return total;
}

u64 edgetpu_mappings_encode_gcip_map_flags(edgetpu_map_flag_t flags, unsigned long dma_attrs,
					   bool adjust_dir)
{
	enum dma_data_direction dir = flags & EDGETPU_MAP_DIR_MASK;
	bool coherent = flags & EDGETPU_MAP_COHERENT;
	bool restrict_iova = !(flags & EDGETPU_MAP_CPU_NONACCESSIBLE);

	if (adjust_dir)
		dir = edgetpu_host_dma_dir(dir);

	return gcip_iommu_encode_gcip_map_flags(dir, coherent, dma_attrs, restrict_iova);
}

/*
 * Compare the range of device addresses for @map with @iova.
 *
 * Returns -1, 0, 1 if the @map address range is "less than", "equal to", or "greater than" the
 * iova, respectively.
 */
static int iova_in_mapping(struct edgetpu_mapping *map, tpu_addr_t iova)
{
	if (map->gcip_mapping->device_address + map->gcip_mapping->size <= iova)
		return -1;
	if (iova < map->gcip_mapping->device_address)
		return 1;
	return 0;
}

struct edgetpu_mapping *
edgetpu_mapping_find_iova_range(struct edgetpu_mapping_root *mappings, tpu_addr_t iova)
{
	struct rb_node *node = mappings->rb.rb_node;

	while (node) {
		struct edgetpu_mapping *map =
			container_of(node, struct edgetpu_mapping, node);
		const int cmp = iova_in_mapping(map, iova);

		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			return map;
	}

	return NULL;
}
