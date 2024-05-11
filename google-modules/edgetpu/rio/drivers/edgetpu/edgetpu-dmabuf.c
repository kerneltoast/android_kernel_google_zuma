// SPDX-License-Identifier: GPL-2.0
/*
 * EdgeTPU support for dma-buf.
 *
 * Copyright (C) 2020 Google, Inc.
 */

#include <linux/debugfs.h>
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/dma-fence.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>
#include <linux/time64.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include <gcip/gcip-dma-fence.h>

#include "edgetpu-device-group.h"
#include "edgetpu-dmabuf.h"
#include "edgetpu-internal.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"
#include "edgetpu.h"

#define to_etfence(gfence) container_of(gfence, struct edgetpu_dma_fence, gfence)

/*
 * Records the mapping and other fields needed for mapping a dma-buf to a device
 * group.
 */
struct edgetpu_dmabuf_map {
	struct edgetpu_mapping map;
	u64 size; /* size of this mapping in bytes */
	u32 mmu_flags;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attachment;
	/* SG table returned by dma_buf_map_attachment(). Records default domain mapping. */
	struct sg_table *dma_sgt;
};

/*
 * edgetpu implementation of DMA fence
 *
 * @gfence:		GCIP DMA fence
 * @group:		owning device group
 * @group_list:		list of DMA fences owned by the same group
 */
struct edgetpu_dma_fence {
	struct gcip_dma_fence gfence;
	struct edgetpu_device_group *group;
	struct list_head group_list;
};

static const struct dma_fence_ops edgetpu_dma_fence_ops;

/*
 * Maps @dmap->map into its owning device group's context.
 *
 * Caller holds @group->lock.
 */
static int etdev_map_dmabuf(struct edgetpu_dev *etdev, struct edgetpu_dmabuf_map *dmap)
{
	struct edgetpu_device_group *group = dmap->map.priv;
	const enum edgetpu_context_id ctx_id =
		edgetpu_group_context_id_locked(group);

	return edgetpu_mmu_map(etdev, &dmap->map, ctx_id, dmap->mmu_flags);
}

/*
 * Reverts etdev_map_dmabuf().
 *
 * Caller holds @group->lock.
 */
static void etdev_unmap_dmabuf(struct edgetpu_dev *etdev, struct edgetpu_dmabuf_map *dmap)
{
	struct edgetpu_device_group *group = dmap->map.priv;
	const enum edgetpu_context_id ctx_id =
		edgetpu_group_context_id_locked(group);

	edgetpu_mmu_unmap(etdev, &dmap->map, ctx_id);
}

/*
 * Clean resources recorded in @dmap.
 *
 * Caller holds the lock of group (map->priv) and ensures the group is in
 * the finalized state.
 */
static void dmabuf_map_callback_release(struct edgetpu_mapping *map)
{
	struct edgetpu_dmabuf_map *dmap =
		container_of(map, struct edgetpu_dmabuf_map, map);
	struct edgetpu_device_group *group = map->priv;
	const enum dma_data_direction dir = map->dir;

	if (dmap->map.device_address)
		etdev_unmap_dmabuf(group->etdev, dmap);
	sg_free_table(&dmap->map.sgt);
	if (dmap->dma_sgt)
		dma_buf_unmap_attachment(dmap->attachment, dmap->dma_sgt, dir);
	if (dmap->attachment)
		dma_buf_detach(dmap->dmabuf, dmap->attachment);
	dma_buf_put(dmap->dmabuf);
	edgetpu_device_group_put(group);
	kfree(dmap);
}

static void entry_show_dma_addrs(struct edgetpu_dmabuf_map *dmap, struct seq_file *s)
{
	struct sg_table *sgt = &dmap->map.sgt;

	if (sgt->nents == 1) {
		seq_printf(s, "%pad\n", &sg_dma_address(sgt->sgl));
	} else {
		uint i;
		struct scatterlist *sg = sgt->sgl;

		seq_puts(s, "[");
		for (i = 0; i < sgt->nents; i++) {
			if (i)
				seq_puts(s, ", ");
			seq_printf(s, "%pad", &sg_dma_address(sg));
			sg = sg_next(sg);
		}
		seq_puts(s, "]\n");
	}
}

static void dmabuf_map_callback_show(struct edgetpu_mapping *map, struct seq_file *s)
{
	struct edgetpu_dmabuf_map *dmap =
		container_of(map, struct edgetpu_dmabuf_map, map);

	seq_printf(s, "  <%s> iova=%#llx (default=%#llx) pages=%llu %s", dmap->dmabuf->exp_name,
		   map->device_address, sg_dma_address(dmap->dma_sgt->sgl),
		   DIV_ROUND_UP(dmap->size, PAGE_SIZE), edgetpu_dma_dir_rw_s(map->dir));
	seq_puts(s, " dma=");
	entry_show_dma_addrs(dmap, s);
}

/*
 * Allocates and properly sets fields of an edgetpu_dmabuf_map.
 *
 * Caller holds group->lock and checks @group is finalized.
 *
 * Returns the pointer on success, or NULL on failure.
 */
static struct edgetpu_dmabuf_map *
alloc_dmabuf_map(struct edgetpu_device_group *group, edgetpu_map_flag_t flags)
{
	struct edgetpu_dmabuf_map *dmap = kzalloc(sizeof(*dmap), GFP_KERNEL);
	struct edgetpu_mapping *map;

	if (!dmap)
		return NULL;
	dmap->mmu_flags = map_to_mmu_flags(flags) | EDGETPU_MMU_DMABUF;
	map = &dmap->map;
	map->flags = flags;
	map->dir = flags & EDGETPU_MAP_DIR_MASK;
	map->release = dmabuf_map_callback_release;
	map->show = dmabuf_map_callback_show;
	map->priv = edgetpu_device_group_get(group);
	return dmap;
}

/*
 * Duplicates @sgt in region [0, @size) to @out.
 * Only duplicates the "page" parts in @sgt, DMA addresses and lengths are not
 * considered.
 */
static int dup_sgt_in_region(struct sg_table *sgt, u64 size, struct sg_table *out)
{
	uint n = 0;
	u64 cur_offset = 0;
	struct scatterlist *sg, *new_sg;
	int i;
	int ret;

	/* calculate the number of sg covered */
	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
		size_t pg_len = sg->length + sg->offset;

		n++;
		if (size <= cur_offset + pg_len)
			break;
		cur_offset += pg_len;
	}
	ret = sg_alloc_table(out, n, GFP_KERNEL);
	if (ret)
		return ret;
	cur_offset = 0;
	new_sg = out->sgl;
	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
		size_t pg_len = sg->length + sg->offset;
		struct page *page = sg_page(sg);
		unsigned int len = pg_len;
		u64 remain_size = size - cur_offset;

		if (remain_size < pg_len)
			len -= pg_len - remain_size;
		sg_set_page(new_sg, page, len, 0);
		new_sg = sg_next(new_sg);

		if (size <= cur_offset + pg_len)
			break;
		cur_offset += pg_len;
	}
	return 0;
}

/*
 * Performs dma_buf_attach + dma_buf_map_attachment of @dmabuf to @etdev, and
 * sets @dmap per the attaching result.
 *
 * Fields of @dmap will be set on success.
 */
static int etdev_attach_dmabuf_to_mapping(struct edgetpu_dev *etdev, struct dma_buf *dmabuf,
					  struct edgetpu_dmabuf_map *dmap, u64 size,
					  enum dma_data_direction dir)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	int ret;

	attachment = dma_buf_attach(dmabuf, etdev->dev);
	if (IS_ERR(attachment))
		return PTR_ERR(attachment);
	sgt = dma_buf_map_attachment(attachment, dir);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_detach;
	}
	dmap->attachment = attachment;
	dmap->dma_sgt = sgt;
	ret = dup_sgt_in_region(dmap->dma_sgt, size, &dmap->map.sgt);
	if (ret)
		goto err_unmap;

	return 0;

err_unmap:
	dma_buf_unmap_attachment(attachment, sgt, dir);
err_detach:
	dma_buf_detach(dmabuf, attachment);
	dmap->dma_sgt = NULL;
	dmap->attachment = NULL;
	return ret;
}

int edgetpu_map_dmabuf(struct edgetpu_device_group *group,
		       struct edgetpu_map_dmabuf_ioctl *arg)
{
	int ret = -EINVAL;
	struct dma_buf *dmabuf;
	edgetpu_map_flag_t flags = arg->flags;
	u64 size;
	const enum dma_data_direction dir = map_flag_to_host_dma_dir(flags);
	struct edgetpu_dmabuf_map *dmap;

	if (!valid_dma_direction(dir)) {
		etdev_dbg(group->etdev, "%s: invalid direction %d\n", __func__, dir);
		return -EINVAL;
	}
	dmabuf = dma_buf_get(arg->dmabuf_fd);
	if (IS_ERR(dmabuf)) {
		etdev_dbg(group->etdev, "%s: dma_buf_get returns %ld\n",
			  __func__, PTR_ERR(dmabuf));
		return PTR_ERR(dmabuf);
	}

	mutex_lock(&group->lock);
	if (!edgetpu_device_group_is_finalized(group)) {
		ret = edgetpu_group_errno(group);
		etdev_dbg(group->etdev,
			  "%s: edgetpu_device_group_is_finalized returns %d\n",
			  __func__, ret);
		goto err_unlock_group;
	}

	dmap = alloc_dmabuf_map(group, flags);
	if (!dmap) {
		ret = -ENOMEM;
		goto err_unlock_group;
	}

	get_dma_buf(dmabuf);
	dmap->dmabuf = dmabuf;
	dmap->map.map_size = dmap->size = size = dmabuf->size;
	ret = etdev_attach_dmabuf_to_mapping(group->etdev, dmabuf, dmap, size, dir);
	if (ret) {
		etdev_dbg(group->etdev,
			  "%s: etdev_attach_dmabuf_to_entry returns %d\n",
			  __func__, ret);
		goto err_release_map;
	}
	ret = etdev_map_dmabuf(group->etdev, dmap);
	if (ret) {
		etdev_dbg(group->etdev,
			  "%s: etdev_map_dmabuf returns %d\n",
			  __func__, ret);
		goto err_release_map;
	}
	ret = edgetpu_mapping_add(&group->dmabuf_mappings, &dmap->map);
	if (ret) {
		etdev_dbg(group->etdev, "%s: edgetpu_mapping_add returns %d\n",
			  __func__, ret);
		goto err_release_map;
	}
	arg->device_address = dmap->map.device_address;
	mutex_unlock(&group->lock);
	dma_buf_put(dmabuf);
	return 0;

err_release_map:
	dmabuf_map_callback_release(&dmap->map);
err_unlock_group:
	mutex_unlock(&group->lock);
	dma_buf_put(dmabuf);

	return ret;
}

int edgetpu_unmap_dmabuf(struct edgetpu_device_group *group,
			 tpu_addr_t tpu_addr)
{
	struct edgetpu_mapping_root *mappings = &group->dmabuf_mappings;
	struct edgetpu_mapping *map;
	int ret = -EINVAL;

	mutex_lock(&group->lock);
	/* allows unmapping on errored groups */
	if (!edgetpu_device_group_is_finalized(group) && !edgetpu_device_group_is_errored(group)) {
		ret = -EINVAL;
		goto out_unlock;
	}
	edgetpu_mapping_lock(mappings);
	map = edgetpu_mapping_find_locked(mappings, tpu_addr);
	if (!map) {
		edgetpu_mapping_unlock(mappings);
		goto out_unlock;
	}
	edgetpu_mapping_unlink(mappings, map);
	edgetpu_mapping_unlock(mappings);
	map->release(map);
	ret = 0;
out_unlock:
	mutex_unlock(&group->lock);
	return ret;
}

int edgetpu_sync_fence_manager_create(struct edgetpu_dev *etdev)
{
	struct gcip_dma_fence_manager *gfence_mgr = gcip_dma_fence_manager_create(etdev->dev);

	if (IS_ERR(gfence_mgr))
		return PTR_ERR(gfence_mgr);

	etdev->gfence_mgr = gfence_mgr;

	return 0;
}

static const char *edgetpu_dma_fence_get_driver_name(struct dma_fence *fence)
{
	return "edgetpu";
}

static void edgetpu_dma_fence_release(struct dma_fence *fence)
{
	struct gcip_dma_fence *gfence = to_gcip_fence(fence);
	struct edgetpu_dma_fence *etfence = to_etfence(gfence);
	struct edgetpu_device_group *group = etfence->group;

	mutex_lock(&group->lock);
	list_del(&etfence->group_list);
	mutex_unlock(&group->lock);
	/* Release this fence's reference on the owning group. */
	edgetpu_device_group_put(group);
	gcip_dma_fence_exit(gfence);
	kfree(etfence);
}

static const struct dma_fence_ops edgetpu_dma_fence_ops = {
	.get_driver_name = edgetpu_dma_fence_get_driver_name,
	.get_timeline_name = gcip_dma_fence_get_timeline_name,
	.wait = dma_fence_default_wait,
	.enable_signaling = gcip_dma_fence_always_true,
	.release = edgetpu_dma_fence_release,
};

static int edgetpu_dma_fence_after_init(struct gcip_dma_fence *gfence)
{
	struct edgetpu_dma_fence *etfence = to_etfence(gfence);
	struct edgetpu_device_group *group = etfence->group;

	mutex_lock(&group->lock);
	list_add_tail(&etfence->group_list, &group->dma_fence_list);
	mutex_unlock(&group->lock);

	return 0;
}

int edgetpu_sync_fence_create(struct edgetpu_dev *etdev, struct edgetpu_device_group *group,
			      struct edgetpu_create_sync_fence_data *datap)
{
	struct gcip_dma_fence_data data = {
		.timeline_name = datap->timeline_name,
		.ops = &edgetpu_dma_fence_ops,
		.seqno = datap->seqno,
		.after_init = edgetpu_dma_fence_after_init,
	};
	struct edgetpu_dma_fence *etfence = kzalloc(sizeof(*etfence), GFP_KERNEL);
	int ret;

	if (!etfence)
		return -ENOMEM;

	INIT_LIST_HEAD(&etfence->group_list);
	etfence->group = edgetpu_device_group_get(group);

	ret = gcip_dma_fence_init(etdev->gfence_mgr, &etfence->gfence, &data);
	if (!ret)
		datap->fence = data.fence;

	/*
	 * We don't need to kfree(etfence) on error because that's called in
	 * edgetpu_dma_fence_release.
	 */

	return ret;
}

int edgetpu_sync_fence_signal(struct edgetpu_signal_sync_fence_data *datap)
{
	return gcip_dma_fence_signal(datap->fence, datap->error, false);
}

/* Caller holds group lock. */
void edgetpu_sync_fence_group_shutdown(struct edgetpu_device_group *group)
{
	struct list_head *pos;
	int ret;

	lockdep_assert_held(&group->lock);
	list_for_each(pos, &group->dma_fence_list) {
		struct edgetpu_dma_fence *etfence =
			container_of(pos, struct edgetpu_dma_fence, group_list);

		ret = gcip_dma_fenceptr_signal(&etfence->gfence, -EPIPE, true);
		if (ret) {
			struct dma_fence *fence = &etfence->gfence.fence;

			etdev_warn(group->etdev, "error %d signaling fence %s-%s %llu-%llu", ret,
				   fence->ops->get_driver_name(fence),
				   fence->ops->get_timeline_name(fence), fence->context,
				   fence->seqno);
		}
	}
}

int edgetpu_sync_fence_status(struct edgetpu_sync_fence_status *datap)
{
	return gcip_dma_fence_status(datap->fence, &datap->status);
}

int edgetpu_sync_fence_debugfs_show(struct seq_file *s, void *unused)
{
	struct edgetpu_dev *etdev = s->private;
	struct gcip_dma_fence *gfence;
	unsigned long flags;

	GCIP_DMA_FENCE_LIST_LOCK(etdev->gfence_mgr, flags);
	gcip_for_each_fence(etdev->gfence_mgr, gfence) {
		struct edgetpu_dma_fence *etfence = to_etfence(gfence);

		gcip_dma_fence_show(gfence, s);
		seq_printf(s, " group=%u\n", etfence->group->workload_id);
	}
	GCIP_DMA_FENCE_LIST_UNLOCK(etdev->gfence_mgr, flags);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,16,0)
MODULE_IMPORT_NS(DMA_BUF);
#endif
