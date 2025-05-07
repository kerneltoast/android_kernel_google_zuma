// SPDX-License-Identifier: GPL-2.0-only
/*
 * The protocol of the additional_info between the kernel driver and the firmware.
 * Its implementation will be replaced with litebuf in the future.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/align.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "edgetpu-ikv-additional-info.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"

#define ADDITIONAL_INFO_ALIGN SZ_16

/*
 * Calculates an aligned start offset of the field which is expected to be start at @offset with
 * @size of buffer. If the end offset is already aligned, the returned offset will be the same
 * with @offset. Otherwise, a padded start offset will be returned.
 */
static uint32_t edgetpu_ikv_additional_info_align_offset(uint32_t offset, uint32_t size)
{
	uint32_t end = offset + size, aligned;

	aligned = ALIGN(end, ADDITIONAL_INFO_ALIGN);

	return offset + (aligned - end);
}

/* Fills the header part of the additional_info. */
static void
edgetpu_ikv_additional_info_fill_header(struct edgetpu_ikv_additional_info_header *header)
{
	header->identifier = 0;
	header->version = 0;
	header->root_offset = edgetpu_ikv_additional_info_align_offset(
		sizeof(*header), sizeof(struct edgetpu_ikv_additional_info_root));
}

/* Fills the root part of the additional info. */
static void edgetpu_ikv_additional_info_fill_root(struct edgetpu_ikv_additional_info_root *root,
						  uint32_t root_offset, uint32_t in_fences_count,
						  uint32_t out_fences_count, uint32_t timeout_ms,
						  uint32_t runtime_data_size)
{
	uint32_t in_fences_count_b = sizeof(uint16_t) * in_fences_count;
	uint32_t out_fences_count_b = sizeof(uint16_t) * out_fences_count;

	root->object_size = sizeof(*root);
	root->in_fences_offset =
		edgetpu_ikv_additional_info_align_offset(sizeof(*root), in_fences_count_b);
	root->in_fences_count = in_fences_count;
	root->out_fences_offset = edgetpu_ikv_additional_info_align_offset(
		root->in_fences_offset + in_fences_count_b, out_fences_count_b);
	root->out_fences_count = out_fences_count;
	root->timeout_ms = timeout_ms;
	root->runtime_data_offset = edgetpu_ikv_additional_info_align_offset(
		root->out_fences_offset + out_fences_count_b, runtime_data_size);
	root->runtime_data_size = runtime_data_size;
}

void edgetpu_ikv_additional_info_fill(struct edgetpu_ikv_additional_info *info, uint16_t *in_fences,
				      uint32_t in_fences_count, uint16_t *out_fences,
				      uint32_t out_fences_count, uint32_t timeout_ms,
				      uint8_t *runtime_data, uint32_t runtime_data_size)
{
	edgetpu_ikv_additional_info_fill_header(&info->header);
	edgetpu_ikv_additional_info_fill_root(&info->root, info->header.root_offset,
					      in_fences_count, out_fences_count, timeout_ms,
					      runtime_data_size);
	info->in_fences = in_fences;
	info->out_fences = out_fences;
	info->runtime_data = runtime_data;
}

ssize_t edgetpu_ikv_additional_info_alloc_and_copy(struct edgetpu_dev *etdev,
						   struct edgetpu_ikv_additional_info *info,
						   struct edgetpu_coherent_mem *mem)
{
	ssize_t size = info->header.root_offset + info->root.runtime_data_offset +
		       info->root.runtime_data_size;
	int ret;

	ret = edgetpu_iremap_alloc(etdev, size, mem);
	if (ret) {
		etdev_err(etdev, "Failed to allocate additional info: %d", ret);
		return ret;
	}

	/* Copy header. */
	memcpy(mem->vaddr, &info->header, sizeof(info->header));

	/* Copy root. */
	memcpy(mem->vaddr + info->header.root_offset, &info->root, sizeof(info->root));

	/* Copy in_fences. */
	if (info->root.in_fences_count)
		memcpy(mem->vaddr + info->header.root_offset + info->root.in_fences_offset,
		       info->in_fences, sizeof(uint16_t) * info->root.in_fences_count);

	/* Copy out_fences. */
	if (info->root.out_fences_count)
		memcpy(mem->vaddr + info->header.root_offset + info->root.out_fences_offset,
		       info->out_fences, sizeof(uint16_t) * info->root.out_fences_count);

	/* Copy runtime-defined additional info. */
	if (info->root.runtime_data_size)
		memcpy(mem->vaddr + info->header.root_offset + info->root.runtime_data_offset,
		       info->runtime_data, info->root.runtime_data_size);

	return size;
}
