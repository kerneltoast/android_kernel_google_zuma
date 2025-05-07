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
#include <linux/module.h>
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
 * Clean resources recorded in @dmap.
 *
 * Caller holds the lock of group (map->priv) and ensures the group is in
 * the finalized state.
 */
static void dmabuf_mapping_destroy(struct edgetpu_mapping *mapping)
{
	struct edgetpu_device_group *group = mapping->priv;

	gcip_iommu_mapping_unmap(mapping->gcip_mapping);
	edgetpu_device_group_put(group);
	kfree(mapping);
}

static void dmabuf_map_callback_show(struct edgetpu_mapping *map, struct seq_file *s)
{
	gcip_iommu_dmabuf_map_show(map->gcip_mapping, s);
}

/**
 * dmabuf_mapping_create() - Maps the DMA buffer and creates the corresponding mapping object.
 * @group: The group that the DMA buffer belongs to.
 * @fd: The file descriptor of the DMA buffer.
 * @flags: The flags used to map the DMA buffer.
 *
 * Return: The pointer of the target mapping object or an error pointer on failure.
 */
static struct edgetpu_mapping *dmabuf_mapping_create(struct edgetpu_device_group *group, int fd,
						     edgetpu_map_flag_t flags)
{
	struct edgetpu_mapping *mapping;
	struct edgetpu_iommu_domain *etdomain;
	struct dma_buf *dmabuf;
	int ret;
	u64 gcip_map_flags = edgetpu_mappings_encode_gcip_map_flags(flags, 0, false);

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return ERR_CAST(dmabuf);

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		ret = -ENOMEM;
		goto err_dma_buf_put;
	}

	mapping->flags = flags;
	mapping->mmu_flags = map_to_mmu_flags(flags);
	mapping->priv = edgetpu_device_group_get(group);
	mapping->release = dmabuf_mapping_destroy;
	mapping->show = dmabuf_map_callback_show;

	mutex_lock(&group->lock);
	if (!edgetpu_device_group_is_finalized(group)) {
		ret = edgetpu_group_errno(group);
		etdev_dbg(group->etdev,
			  "%s: edgetpu_device_group_is_finalized returns %d\n",
			  __func__, ret);
		mutex_unlock(&group->lock);
		goto err_device_group_put;
	}
	etdomain = edgetpu_group_domain_locked(group);

	mapping->gcip_mapping =
		gcip_iommu_domain_map_dma_buf(etdomain->gdomain, dmabuf, gcip_map_flags);
	mutex_unlock(&group->lock);
	if (IS_ERR(mapping->gcip_mapping)) {
		ret = PTR_ERR(mapping->gcip_mapping);
		etdev_dbg(group->etdev, "%s: gcip_iommu_domain_map_dma_buf returns %d\n", __func__,
			  ret);
		goto err_device_group_put;
	}

	dma_buf_put(dmabuf);

	return mapping;

err_device_group_put:
	edgetpu_device_group_put(group);
	kfree(mapping);
err_dma_buf_put:
	dma_buf_put(dmabuf);
	return ERR_PTR(ret);
}

int edgetpu_map_dmabuf(struct edgetpu_device_group *group, struct edgetpu_map_dmabuf_ioctl *arg)
{
	struct edgetpu_mapping *mapping;
	int ret;

	mapping = dmabuf_mapping_create(group, arg->dmabuf_fd, arg->flags);
	if (IS_ERR(mapping)) {
		ret = PTR_ERR(mapping);
		etdev_dbg(group->etdev, "%s: dmabuf_mapping_create returns %d\n", __func__, ret);
		return ret;
	}

	/* Save address before add to mapping tree, after which another thread can free it. */
	arg->device_address = mapping->gcip_mapping->device_address;
	ret = edgetpu_mapping_add(&group->dmabuf_mappings, mapping);
	if (ret) {
		etdev_dbg(group->etdev, "%s: edgetpu_mapping_add returns %d\n",
			  __func__, ret);
		goto err_destroy_mapping;
	}

	return 0;

err_destroy_mapping:
	dmabuf_mapping_destroy(mapping);
	return ret;
}

int edgetpu_unmap_dmabuf(struct edgetpu_device_group *group, tpu_addr_t tpu_addr)
{
	struct edgetpu_mapping_root *mappings = &group->dmabuf_mappings;
	struct edgetpu_mapping *map;

	edgetpu_mapping_lock(mappings);
	map = edgetpu_mapping_find_locked(mappings, tpu_addr);
	if (!map) {
		edgetpu_mapping_unlock(mappings);
		etdev_err(group->etdev, "unmap group=%u tpu_addr=%pad not found",
			  group->workload_id, &tpu_addr);
		return -EINVAL;
	}
	edgetpu_mapping_unlink(mappings, map);
	edgetpu_mapping_unlock(mappings);
	map->release(map);
	return 0;
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

MODULE_IMPORT_NS(DMA_BUF);
