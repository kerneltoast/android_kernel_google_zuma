// SPDX-License-Identifier: GPL-2.0
/*
 * Implements utilities for virtual device group of EdgeTPU.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/cred.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/eventfd.h>
#include <linux/iommu.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#include "edgetpu-async.h"
#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-dmabuf.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"
#include "mm-backport.h"

/* Records the mapping and other fields needed for a host buffer mapping */
struct edgetpu_host_map {
	struct edgetpu_mapping map;
};

/*
 * A helper structure for the return value of find_sg_to_sync().
 */
struct sglist_to_sync {
	struct scatterlist *sg;
	int nelems;
	/*
	 * The SG that has its length modified by find_sg_to_sync().
	 * Can be NULL, which means no SG's length was modified.
	 */
	struct scatterlist *last_sg;
	/*
	 * find_sg_to_sync() will temporarily change the length of @last_sg.
	 * This is used to restore the length.
	 */
	unsigned int orig_length;
};

static int edgetpu_group_activate_external_mailbox(struct edgetpu_device_group *group)
{
	if (!group->ext_mailbox)
		return 0;
	edgetpu_mailbox_reinit_external_mailbox(group);
	return edgetpu_mailbox_activate_external_mailbox(group);
}

/*
 * Activates the VII mailbox @group owns.
 *
 * Caller holds group->lock.
 */
static int edgetpu_group_activate(struct edgetpu_device_group *group)
{
	u8 mailbox_id;
	int ret;

	if (edgetpu_group_mailbox_detached_locked(group))
		return 0;

	mailbox_id = edgetpu_group_context_id_locked(group);
	edgetpu_soc_activate_context(group->etdev, mailbox_id);
	ret = edgetpu_mailbox_activate(group->etdev, mailbox_id, group->mbox_attr.client_priv,
				       group->vcid, !group->activated);
	if (ret) {
		etdev_err(group->etdev, "activate mailbox for VCID %d failed with %d", group->vcid,
			  ret);
	} else {
		group->activated = true;
		edgetpu_sw_wdt_inc_active_ref(group->etdev);
	}
	atomic_inc(&group->etdev->job_count);
	return ret;
}

static void edgetpu_group_deactivate_external_mailbox(struct edgetpu_device_group *group)
{
	edgetpu_mailbox_deactivate_external_mailbox(group);
	edgetpu_mailbox_disable_external_mailbox(group);
}

/*
 * Deactivates the VII mailbox @group owns.
 *
 * Caller holds group->lock.
 */
static void edgetpu_group_deactivate(struct edgetpu_device_group *group)
{
	u8 mailbox_id;

	if (edgetpu_group_mailbox_detached_locked(group))
		return;
	edgetpu_sw_wdt_dec_active_ref(group->etdev);
	mailbox_id = edgetpu_group_context_id_locked(group);
	edgetpu_mailbox_deactivate(group->etdev, mailbox_id);
	/*
	 * Deactivate the context to prevent speculative accesses from being issued to a disabled
	 * context.
	 */
	edgetpu_soc_deactivate_context(group->etdev, mailbox_id);
}

/*
 * Handle KCI chores for device group disband.
 *
 * send KCI CLOSE_DEVICE to the device (and GET_USAGE to update usage stats).
 *
 * Caller holds group->lock.
 */
static void edgetpu_device_group_kci_leave(struct edgetpu_device_group *group)
{
	edgetpu_kci_update_usage_async(group->etdev->etkci);
	/*
	 * Theoretically we don't need to check @dev_inaccessible here.
	 * @dev_inaccessible is true implies the client has wakelock count zero, under such case
	 * edgetpu_mailbox_deactivate() has been called on releasing the wakelock and therefore this
	 * edgetpu_group_deactivate() call won't send any KCI.
	 * Still have a check here in case this function does CSR programming other than calling
	 * edgetpu_mailbox_deactivate() someday.
	 */
	if (!group->dev_inaccessible)
		edgetpu_group_deactivate(group);
}

/*
 * Asynchronously sends a JOIN_GROUP KCI command to the @group device.
 *
 * Caller holds group->lock.
 */
static int
edgetpu_device_group_kci_finalized(struct edgetpu_device_group *group)
{
	return edgetpu_group_activate(group);
}

/*
 * Does attach domain, init VII, and set @group->context_id without checking
 * @group->mailbox_detachable and whether the mailbox is attached.
 *
 * Caller holds @group->lock.
 */
static int do_attach_mailbox_locked(struct edgetpu_device_group *group)
{
	int ret;

	ret = edgetpu_mmu_attach_domain(group->etdev, group->etdomain);
	if (ret)
		return ret;
	ret = edgetpu_mailbox_init_vii(&group->vii, group);
	if (ret) {
		edgetpu_mmu_detach_domain(group->etdev, group->etdomain);
		return ret;
	}
	group->context_id = group->vii.mailbox->mailbox_id;
	return 0;
}

/*
 * Does detach domain, remove VII, and invalidate @group->context_id without
 * checking @group->mailbox_detachable and whether the mailbox is detached.
 *
 * Caller holds @group->lock.
 */
static void do_detach_mailbox_locked(struct edgetpu_device_group *group)
{
	edgetpu_mailbox_remove_vii(&group->vii);
	edgetpu_mmu_detach_domain(group->etdev, group->etdomain);
	if (group->etdomain->token != EDGETPU_DOMAIN_TOKEN_END)
		group->context_id =
			EDGETPU_CONTEXT_DOMAIN_TOKEN | group->etdomain->token;
	else
		group->context_id = EDGETPU_CONTEXT_INVALID;
}

static inline bool is_finalized_or_errored(struct edgetpu_device_group *group)
{
	return edgetpu_device_group_is_finalized(group) ||
	       edgetpu_device_group_is_errored(group);
}

int edgetpu_group_set_eventfd(struct edgetpu_device_group *group, uint event_id,
			      int eventfd)
{
	struct eventfd_ctx *ctx = eventfd_ctx_fdget(eventfd);
	ulong flags;

	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (event_id >= EDGETPU_EVENT_COUNT)
		return -EINVAL;

	write_lock_irqsave(&group->events.lock, flags);
	if (group->events.eventfds[event_id])
		eventfd_ctx_put(group->events.eventfds[event_id]);
	group->events.eventfds[event_id] = ctx;
	write_unlock_irqrestore(&group->events.lock, flags);
	return 0;
}

void edgetpu_group_unset_eventfd(struct edgetpu_device_group *group,
				 uint event_id)
{
	ulong flags;

	if (event_id >= EDGETPU_EVENT_COUNT)
		return;

	write_lock_irqsave(&group->events.lock, flags);
	if (group->events.eventfds[event_id])
		eventfd_ctx_put(group->events.eventfds[event_id]);
	group->events.eventfds[event_id] = NULL;
	write_unlock_irqrestore(&group->events.lock, flags);
}

static void edgetpu_group_clear_events(struct edgetpu_device_group *group)
{
	int event_id;
	ulong flags;

	write_lock_irqsave(&group->events.lock, flags);
	for (event_id = 0; event_id < EDGETPU_EVENT_COUNT; event_id++) {
		if (group->events.eventfds[event_id])
			eventfd_ctx_put(group->events.eventfds[event_id]);
		group->events.eventfds[event_id] = NULL;
	}
	write_unlock_irqrestore(&group->events.lock, flags);
}

void edgetpu_group_notify(struct edgetpu_device_group *group, uint event_id)
{
	if (event_id >= EDGETPU_EVENT_COUNT)
		return;

	etdev_dbg(group->etdev, "%s: group %u id=%u", __func__,
		  group->workload_id, event_id);
	read_lock(&group->events.lock);
	if (group->events.eventfds[event_id])
		eventfd_signal(group->events.eventfds[event_id], 1);
	read_unlock(&group->events.lock);
}

/*
 * Releases all resources the group allocated and mark the group as disbanded.
 *
 * release VII mailboxes, buffer mappings, etc.
 *
 * The lock of group must be held.
 */
static void edgetpu_device_group_release(struct edgetpu_device_group *group)
{
	edgetpu_group_clear_events(group);
	if (is_finalized_or_errored(group)) {
		edgetpu_device_group_kci_leave(group);
		/*
		 * Mappings clear should be performed after had a handshake with
		 * the firmware.
		 */
		edgetpu_mappings_clear_group(group);
		edgetpu_mailbox_external_disable_free_locked(group);
		edgetpu_mailbox_remove_vii(&group->vii);
	}
	if (group->etdomain) {
		edgetpu_mmu_detach_domain(group->etdev, group->etdomain);
		edgetpu_mmu_free_domain(group->etdev, group->etdomain);
	}
	/* Signal any unsignaled dma fences owned by the group with an error. */
	edgetpu_sync_fence_group_shutdown(group);
	group->status = EDGETPU_DEVICE_GROUP_DISBANDED;
}

/*
 * Inserts @group to the list @etdev->groups.
 *
 * Returns 0 on success.
 * Returns -EAGAIN if group join is currently disabled.
 */
static int edgetpu_dev_add_group(struct edgetpu_dev *etdev,
				 struct edgetpu_device_group *group)
{
	struct edgetpu_list_group *l = kmalloc(sizeof(*l), GFP_KERNEL);
	int ret;

	if (!l)
		return -ENOMEM;
	mutex_lock(&etdev->groups_lock);
	if (etdev->group_join_lockout) {
		ret = -EAGAIN;
		goto error_unlock;
	}
	if (group->etdev == etdev) {
		u32 vcid_pool = etdev->vcid_pool;

#ifdef EDGETPU_VCID_EXTRA_PARTITION
		if (group->mbox_attr.partition_type != EDGETPU_PARTITION_EXTRA)
			vcid_pool &= ~BIT(EDGETPU_VCID_EXTRA_PARTITION);
		else
			vcid_pool &= BIT(EDGETPU_VCID_EXTRA_PARTITION);
#endif
		if (!vcid_pool) {
			ret = -EBUSY;
			goto error_unlock;
		}
		group->vcid = ffs(vcid_pool) - 1;
		etdev->vcid_pool &= ~BIT(group->vcid);
	}
	l->grp = edgetpu_device_group_get(group);
	list_add_tail(&l->list, &etdev->groups);
	etdev->n_groups++;

	mutex_unlock(&etdev->groups_lock);
	return 0;

error_unlock:
	mutex_unlock(&etdev->groups_lock);
	kfree(l);
	return ret;
}

void edgetpu_device_group_put(struct edgetpu_device_group *group)
{
	if (!group)
		return;
	if (refcount_dec_and_test(&group->ref_count))
		kfree(group);
}

/* caller must hold @etdev->groups_lock. */
static bool edgetpu_in_any_group_locked(struct edgetpu_dev *etdev)
{
	return etdev->n_groups;
}

void edgetpu_device_group_leave(struct edgetpu_client *client)
{
	struct edgetpu_device_group *group;
	struct edgetpu_list_group *l;

	mutex_lock(&client->group_lock);
	group = client->group;
	if (!group) {
		mutex_unlock(&client->group_lock);
		return;
	}

	mutex_lock(&group->lock);
	edgetpu_device_group_release(group);
	edgetpu_client_put(group->client);
	edgetpu_device_group_put(client->group);
	client->group = NULL;
	mutex_unlock(&group->lock);
	mutex_unlock(&client->group_lock);
	/* remove the group from the client device */
	mutex_lock(&client->etdev->groups_lock);
	list_for_each_entry(l, &client->etdev->groups, list) {
		if (l->grp == group) {
			if (group->etdev == client->etdev)
				client->etdev->vcid_pool |= BIT(group->vcid);
			list_del(&l->list);
			edgetpu_device_group_put(l->grp);
			kfree(l);
			client->etdev->n_groups--;
			break;
		}
	}
	mutex_unlock(&client->etdev->groups_lock);
}

static int edgetpu_device_group_add(struct edgetpu_device_group *group,
				    struct edgetpu_client *client)
{
	int ret = 0;

	mutex_lock(&client->group_lock);
	if (client->group) {
		mutex_unlock(&client->group_lock);
		return -EINVAL;
	}

	mutex_lock(&group->lock);
	if (group->client) {
		ret = -EINVAL;
		goto out;
	}
	if (!edgetpu_device_group_is_waiting(group)) {
		ret = -EINVAL;
		goto out;
	}

	ret = edgetpu_dev_add_group(client->etdev, group);
	if (ret)
		goto out;

	group->client = edgetpu_client_get(client);
	client->group = edgetpu_device_group_get(group);
	etdev_dbg(client->etdev, "%s: added group %u", __func__,
		  group->workload_id);

out:
	mutex_unlock(&group->lock);
	mutex_unlock(&client->group_lock);
	return ret;
}

struct edgetpu_device_group *
edgetpu_device_group_alloc(struct edgetpu_client *client,
			   const struct edgetpu_mailbox_attr *attr)
{
	static uint cur_workload_id;
	int ret;
	struct edgetpu_device_group *group;
	struct edgetpu_iommu_domain *etdomain;

	ret = edgetpu_mailbox_validate_attr(attr);
	if (ret)
		goto error;
	/*
	 * The client already belongs to a group.
	 * It's safe not to take client->group_lock as
	 * edgetpu_device_group_add() will fail if there is race.
	 */
	if (client->group) {
		ret = -EINVAL;
		goto error;
	}

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group) {
		ret = -ENOMEM;
		goto error;
	}

	refcount_set(&group->ref_count, 1);
	group->workload_id = cur_workload_id++;
	group->status = EDGETPU_DEVICE_GROUP_WAITING;
	group->etdev = client->etdev;
	group->vii.etdev = client->etdev;
	mutex_init(&group->lock);
	rwlock_init(&group->events.lock);
	INIT_LIST_HEAD(&group->dma_fence_list);
	edgetpu_mapping_init(&group->host_mappings);
	edgetpu_mapping_init(&group->dmabuf_mappings);
	group->mbox_attr = *attr;
	if (attr->priority & EDGETPU_PRIORITY_DETACHABLE)
		group->mailbox_detachable = true;

	etdomain = edgetpu_mmu_alloc_domain(group->etdev);
	if (!etdomain) {
		ret = -ENOMEM;
		goto error_put_group;
	}
	group->etdomain = etdomain;
	if (etdomain->token != EDGETPU_DOMAIN_TOKEN_END)
		group->context_id =
			EDGETPU_CONTEXT_DOMAIN_TOKEN | etdomain->token;
	else
		group->context_id = EDGETPU_CONTEXT_INVALID;

	/* adds @client as the only member */
	ret = edgetpu_device_group_add(group, client);
	if (ret) {
		etdev_dbg(group->etdev, "%s: group %u add failed ret=%d",
			  __func__, group->workload_id, ret);
		goto error_free_mmu_domain;
	}
	return group;

error_free_mmu_domain:
	edgetpu_mmu_free_domain(group->etdev, group->etdomain);
 error_put_group:
	edgetpu_device_group_put(group);
error:
	return ERR_PTR(ret);
}

int edgetpu_device_group_finalize(struct edgetpu_device_group *group)
{
	int ret = 0;
	bool mailbox_attached = false;

	mutex_lock(&group->lock);
	/* do nothing if the group is finalized */
	if (is_finalized_or_errored(group))
		goto err_unlock;

	if (!edgetpu_device_group_is_waiting(group)) {
		etdev_err(group->etdev, "finalize group is not waiting");
		ret = -EINVAL;
		goto err_unlock;
	}

	/*
	 * Initialize VII mailbox if
	 * 1. mailbox is non-detachable: VII is assigned and has the same life
	 *    cycle as a finalized @group, or
	 * 2. has non-zero wakelock reference counter: VII should be ready to
	 *    use after group is finalized.
	 */
	if (!group->mailbox_detachable ||
	    edgetpu_wakelock_count_locked(group->client->wakelock)) {
		mailbox_attached = true;
		ret = do_attach_mailbox_locked(group);
		if (ret) {
			etdev_err(group->etdev,
				  "finalize attach mailbox failed: %d", ret);
			goto err_unlock;
		}
	}

	/* send KCI only if the device is powered on */
	if (edgetpu_wakelock_count_locked(group->client->wakelock)) {
		ret = edgetpu_device_group_kci_finalized(group);
		if (ret)
			goto err_remove_detach_mailbox;
	}

	group->status = EDGETPU_DEVICE_GROUP_FINALIZED;

	mutex_unlock(&group->lock);
	return 0;

err_remove_detach_mailbox:
	if (mailbox_attached)
		do_detach_mailbox_locked(group);
err_unlock:
	mutex_unlock(&group->lock);
	return ret;
}

bool edgetpu_in_any_group(struct edgetpu_dev *etdev)
{
	bool ret;

	mutex_lock(&etdev->groups_lock);
	ret = edgetpu_in_any_group_locked(etdev);
	mutex_unlock(&etdev->groups_lock);
	return ret;
}

bool edgetpu_set_group_join_lockout(struct edgetpu_dev *etdev, bool lockout)
{
	bool ret = true;

	mutex_lock(&etdev->groups_lock);
	if (lockout && edgetpu_in_any_group_locked(etdev))
		ret = false;
	else
		etdev->group_join_lockout = lockout;
	mutex_unlock(&etdev->groups_lock);
	return ret;
}

/*
 * Unmap a mapping specified by @map. Unmaps from IOMMU and unpins pages,
 * frees mapping node, which is invalid upon return.
 *
 * Caller locks group->host_mappings.
 */
static void edgetpu_unmap_node(struct edgetpu_mapping *map)
{
	struct edgetpu_device_group *group = map->priv;
	enum edgetpu_context_id ctx_id = edgetpu_group_context_id_locked(group);
	struct edgetpu_host_map *hmap =
		container_of(map, struct edgetpu_host_map, map);
	struct sg_page_iter sg_iter;

	etdev_dbg(group->etdev, "%s: %u: iova=%#llx", __func__,
		  group->workload_id, map->device_address);

	if (map->device_address)
		edgetpu_mmu_unmap(group->etdev, map, ctx_id);

	for_each_sg_page(map->sgt.sgl, &sg_iter, map->sgt.orig_nents, 0) {
		struct page *page = sg_page_iter_page(&sg_iter);

		if (map->dir == DMA_FROM_DEVICE ||
		    map->dir == DMA_BIDIRECTIONAL)
			set_page_dirty(page);

		unpin_user_page(page);
	}

	sg_free_table(&map->sgt);
	edgetpu_device_group_put(map->priv);
	kfree(hmap);
}

static void edgetpu_host_map_show(struct edgetpu_mapping *map,
				  struct seq_file *s)
{
	struct scatterlist *sg;
	int i;
	size_t cur_offset = 0;

	for_each_sg(map->sgt.sgl, sg, map->sgt.nents, i) {
		dma_addr_t phys_addr = sg_phys(sg);
		dma_addr_t dma_addr = sg_dma_address(sg);

		seq_printf(s, "%#llx %lu %s %#llx %pap %pad\n",
			   map->device_address + cur_offset,
			   DIV_ROUND_UP(sg_dma_len(sg), PAGE_SIZE),
			   edgetpu_dma_dir_rw_s(map->dir),
			   map->host_address + cur_offset, &phys_addr,
			   &dma_addr);
		cur_offset += sg_dma_len(sg);
	}
}

size_t edgetpu_group_mappings_total_size(struct edgetpu_device_group *group)
{
	return edgetpu_mappings_total_size(&group->host_mappings) +
		edgetpu_mappings_total_size(&group->dmabuf_mappings);
}

/*
 * Pins the user-space address @arg->host_address and returns the pinned pages.
 * @pnum_pages is set to the number of pages.
 *
 * Returns -errno if failed on pinning @size bytes.
 */
static struct page **edgetpu_pin_user_pages(struct edgetpu_device_group *group,
					    struct edgetpu_map_ioctl *arg,
					    uint *pnum_pages, bool *preadonly)
{
	u64 host_addr = untagged_addr(arg->host_address);
	u64 size = arg->size;
	uint num_pages;
	ulong offset;
	struct edgetpu_dev *etdev = group->etdev;
	struct page **pages;
	int i;
	int ret;
	struct vm_area_struct *vma;
	struct vm_area_struct **vmas;
	unsigned int foll_flags = FOLL_LONGTERM | FOLL_WRITE;

	if (size == 0)
		return ERR_PTR(-EINVAL);
	if (!access_ok((const void *)host_addr, size)) {
		etdev_err(etdev, "invalid address range in buffer map request");
		return ERR_PTR(-EFAULT);
	}
	offset = host_addr & (PAGE_SIZE - 1);
	num_pages = DIV_ROUND_UP((size + offset), PAGE_SIZE);
	if (num_pages * PAGE_SIZE < size + offset)
		return ERR_PTR(-EINVAL);
	etdev_dbg(etdev, "%s: hostaddr=%#llx pages=%u", __func__, host_addr, num_pages);
	/*
	 * "num_pages" is decided from user-space arguments, don't show warnings
	 * when facing malicious input.
	 */
	pages = kvmalloc((num_pages * sizeof(*pages)), GFP_KERNEL | __GFP_NOWARN);
	if (!pages) {
		etdev_err(etdev, "out of memory allocating pages (%lu bytes)",
			  num_pages * sizeof(*pages));
		return ERR_PTR(-ENOMEM);
	}
	/*
	 * The host pages might be read-only and could fail if we attempt to pin
	 * it with FOLL_WRITE.
	 * default to read/write if find_extend_vma returns NULL
	 */
	mmap_read_lock(current->mm);
	vma = find_extend_vma(current->mm, host_addr & PAGE_MASK);
	if (vma && !(vma->vm_flags & VM_WRITE)) {
		foll_flags &= ~FOLL_WRITE;
		*preadonly = true;
	} else {
		*preadonly = false;
	}
	mmap_read_unlock(current->mm);

	/* Try fast call first, in case it's actually faster. */
	ret = pin_user_pages_fast(host_addr & PAGE_MASK, num_pages, foll_flags,
				  pages);
	if (ret == num_pages) {
		*pnum_pages = num_pages;
		return pages;
	}
	if (ret == -EFAULT && !*preadonly) {
		foll_flags &= ~FOLL_WRITE;
		*preadonly = true;
		ret = pin_user_pages_fast(host_addr & PAGE_MASK, num_pages,
					  foll_flags, pages);
	}
	if (ret < 0) {
		etdev_dbg(etdev, "pin_user_pages failed %u:%pK-%u: %d",
			  group->workload_id, (void *)host_addr, num_pages,
			  ret);
		if (ret == -EFAULT)
			etdev_err(etdev,
				  "bad address locking %u pages for %s",
				  num_pages, *preadonly ? "read" : "write");
		if (ret != -ENOMEM) {
			num_pages = 0;
			goto error;
		}
	}
	etdev_dbg(etdev,
		  "pin_user_pages_fast error %u:%pK npages=%u ret=%d",
		  group->workload_id, (void *)host_addr, num_pages,
		  ret);
	/* Unpin any partial mapping and start over again. */
	for (i = 0; i < ret; i++)
		unpin_user_page(pages[i]);

	/* Allocate our own vmas array non-contiguous. */
	vmas = kvmalloc((num_pages * sizeof(*vmas)), GFP_KERNEL | __GFP_NOWARN);
	if (!vmas) {
		etdev_err(etdev, "out of memory allocating vmas (%lu bytes)",
			  num_pages * sizeof(*pages));
		kvfree(pages);
		return ERR_PTR(-ENOMEM);
	}
	mmap_read_lock(current->mm);
	ret = pin_user_pages(host_addr & PAGE_MASK, num_pages, foll_flags,
			     pages, vmas);
	mmap_read_unlock(current->mm);
	kvfree(vmas);
	if (ret < 0) {
		etdev_dbg(etdev, "pin_user_pages failed %u:%pK-%u: %d",
			  group->workload_id, (void *)host_addr, num_pages,
			  ret);
		if (ret == -ENOMEM)
			etdev_err(etdev,
				  "system out of memory locking %u pages",
				  num_pages);
		num_pages = 0;
		goto error;
	}
	if (ret < num_pages) {
		etdev_dbg(etdev,
			  "pin_user_pages partial %u:%pK npages=%u pinned=%d",
			  group->workload_id, (void *)host_addr, num_pages,
			  ret);
		etdev_err(etdev, "can only lock %u of %u pages requested",
			  (unsigned int)ret, num_pages);
		num_pages = ret;
		ret = -EFAULT;
		goto error;
	}

	*pnum_pages = num_pages;
	return pages;

error:
	for (i = 0; i < num_pages; i++)
		unpin_user_page(pages[i]);
	kvfree(pages);

	return ERR_PTR(ret);
}

/*
 * Allocates an edgetpu_host_map with the user-space address @host_addr.
 */
static struct edgetpu_host_map *
alloc_mapping_from_useraddr(struct edgetpu_device_group *group, u64 host_addr,
			    edgetpu_map_flag_t flags, struct page **pages,
			    uint num_pages)
{
	struct edgetpu_dev *etdev = group->etdev;
	struct edgetpu_host_map *hmap;
	int ret;

	hmap = kzalloc(sizeof(*hmap), GFP_KERNEL);
	if (!hmap) {
		ret = -ENOMEM;
		goto error;
	}

	hmap->map.host_address = host_addr;
	hmap->map.dir = map_flag_to_host_dma_dir(flags);
	hmap->map.priv = edgetpu_device_group_get(group);
	hmap->map.release = edgetpu_unmap_node;
	hmap->map.show = edgetpu_host_map_show;
	hmap->map.flags = flags;
	hmap->map.dma_attrs = map_to_dma_attr(flags, true);

	ret = sg_alloc_table_from_pages(&hmap->map.sgt, pages, num_pages, 0,
					num_pages * PAGE_SIZE,
					GFP_KERNEL);
	if (ret) {
		etdev_dbg(etdev,
			  "%s: sg_alloc_table_from_pages failed %u:%pK-%u: %d",
			  __func__, group->workload_id,
			  (void *)host_addr, num_pages, ret);
		goto error_free_sgt;
	}

	return hmap;

error_free_sgt:
	/*
	 * Starting from kernel version 5.10, the caller must call sg_free_table
	 * to clean up any leftover allocations if sg_alloc_table_from_pages
	 * returns non-0 for failures. Calling sg_free_table is also fine with
	 * older kernel versions since sg_free_table handles this properly.
	 */
	sg_free_table(&hmap->map.sgt);
error:
	if (hmap) {
		edgetpu_device_group_put(hmap->map.priv);
		kfree(hmap);
	}

	return ERR_PTR(ret);
}

/*
 * Finds the scatterlist covering range [start, end).
 *
 * The found SG and number of elements will be stored in @sglist.
 *
 * To ensure the returned SG list strictly locates in range [start, end), the
 * last SG's length is shrunk. Therefore caller must call
 * restore_sg_after_sync(@sglist) after the DMA sync is performed.
 *
 * @sglist->nelems == 0 means the target range exceeds the whole SG table.
 */
static void find_sg_to_sync(const struct sg_table *sgt, u64 start, u64 end,
			    struct sglist_to_sync *sglist)
{
	struct scatterlist *sg;
	size_t cur_offset = 0;
	int i;

	sglist->sg = NULL;
	sglist->nelems = 0;
	sglist->last_sg = NULL;
	if (unlikely(end == 0))
		return;
	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
		if (cur_offset <= start && start < cur_offset + sg->length)
			sglist->sg = sg;
		if (sglist->sg)
			++sglist->nelems;
		cur_offset += sg->length;
		if (end <= cur_offset) {
			sglist->last_sg = sg;
			sglist->orig_length = sg->length;
			/*
			 * To let the returned SG list have exact length as
			 * [start, end).
			 */
			sg->length -= cur_offset - end;
			break;
		}
	}
}

static void restore_sg_after_sync(struct sglist_to_sync *sglist)
{
	if (!sglist->last_sg)
		return;
	sglist->last_sg->length = sglist->orig_length;
}

/*
 * Performs DMA sync of the mapping with region [offset, offset + size).
 *
 * Caller holds mapping's lock, to prevent @hmap being modified / removed by
 * other processes.
 */
static int group_sync_host_map(struct edgetpu_device_group *group,
			       struct edgetpu_host_map *hmap, u64 offset,
			       u64 size, enum dma_data_direction dir,
			       bool for_cpu)
{
	const u64 end = offset + size;
	typeof(dma_sync_sg_for_cpu) *sync =
		for_cpu ? dma_sync_sg_for_cpu : dma_sync_sg_for_device;
	struct sg_table *sgt;
	struct sglist_to_sync sglist;

	sgt = &hmap->map.sgt;
	find_sg_to_sync(sgt, offset, end, &sglist);
	if (!sglist.nelems)
		return -EINVAL;

	sync(group->etdev->dev, sglist.sg, sglist.nelems, dir);
	restore_sg_after_sync(&sglist);
	return 0;
}

int edgetpu_device_group_map(struct edgetpu_device_group *group,
			     struct edgetpu_map_ioctl *arg)
{
	uint num_pages = 0;
	struct page **pages;
	int ret = -EINVAL;
	u64 host_addr = arg->host_address;
	edgetpu_map_flag_t flags = arg->flags;
	struct edgetpu_host_map *hmap;
	struct edgetpu_mapping *map = NULL;
	enum edgetpu_context_id context_id;
	const u32 mmu_flags = map_to_mmu_flags(flags) | EDGETPU_MMU_HOST;
	int i;
	bool readonly;
	tpu_addr_t tpu_addr;

	if (!valid_dma_direction(flags & EDGETPU_MAP_DIR_MASK))
		return -EINVAL;
	/* Pin user pages before holding any lock. */
	pages = edgetpu_pin_user_pages(group, arg, &num_pages, &readonly);
	if (IS_ERR(pages))
		return PTR_ERR(pages);
	/* If the host pages are read-only, fallback to use DMA_TO_DEVICE. */
	if (readonly) {
		flags &= ~EDGETPU_MAP_DIR_MASK;
		flags |= EDGETPU_MAP_DMA_TO_DEVICE;
	}

	mutex_lock(&group->lock);
	context_id = edgetpu_group_context_id_locked(group);
	if (!edgetpu_device_group_is_finalized(group)) {
		ret = edgetpu_group_errno(group);
		goto error;
	}
	hmap = alloc_mapping_from_useraddr(group, host_addr, flags, pages,
					   num_pages);
	if (IS_ERR(hmap)) {
		ret = PTR_ERR(hmap);
		goto error;
	}

	map = &hmap->map;
	ret = edgetpu_mmu_map(group->etdev, map, context_id, mmu_flags);
	if (ret)
		goto error;
	map->map_size = arg->size;
	/*
	 * @map can be freed (by another thread) once it's added to the mappings, record the address
	 * before that.
	 */
	tpu_addr = map->device_address;
	ret = edgetpu_mapping_add(&group->host_mappings, map);
	if (ret) {
		etdev_dbg(group->etdev, "duplicate mapping %u:%#llx",
			  group->workload_id, tpu_addr);
		goto error;
	}

	mutex_unlock(&group->lock);
	arg->device_address = tpu_addr;
	kvfree(pages);
	return 0;

error:
	if (map) {
		edgetpu_mapping_lock(&group->host_mappings);
		/* this will free @hmap */
		edgetpu_unmap_node(map);
		edgetpu_mapping_unlock(&group->host_mappings);
	} else {
		/* revert edgetpu_pin_user_pages() */
		for (i = 0; i < num_pages; i++)
			unpin_user_page(pages[i]);
	}
	mutex_unlock(&group->lock);
	kvfree(pages);
	return ret;
}

int edgetpu_device_group_unmap(struct edgetpu_device_group *group,
			       tpu_addr_t tpu_addr, edgetpu_map_flag_t flags)
{
	struct edgetpu_mapping *map;
	int ret = 0;

	mutex_lock(&group->lock);
	if (!is_finalized_or_errored(group)) {
		ret = -EINVAL;
		goto unlock_group;
	}

	edgetpu_mapping_lock(&group->host_mappings);
	map = edgetpu_mapping_find_locked(&group->host_mappings, tpu_addr);
	if (!map) {
		edgetpu_mapping_unlock(&group->host_mappings);
		etdev_dbg(group->etdev,
			  "%s: mapping not found for workload %u: %#llx",
			  __func__, group->workload_id, tpu_addr);

		ret = -EINVAL;
		goto unlock_group;
	}

	edgetpu_mapping_unlink(&group->host_mappings, map);
	map->dma_attrs = map_to_dma_attr(flags, false);
	edgetpu_unmap_node(map);
	edgetpu_mapping_unlock(&group->host_mappings);
unlock_group:
	mutex_unlock(&group->lock);
	return ret;
}

int edgetpu_device_group_sync_buffer(struct edgetpu_device_group *group,
				     const struct edgetpu_sync_ioctl *arg)
{
	struct edgetpu_mapping *map;
	int ret = 0;
	tpu_addr_t tpu_addr = arg->device_address;
	/*
	 * Sync operations don't care the data correctness of prefetch by TPU CPU if they mean to
	 * sync FROM_DEVICE only, so @dir here doesn't need to be wrapped with host_dma_dir().
	 */
	enum dma_data_direction dir = arg->flags & EDGETPU_MAP_DIR_MASK;
	struct edgetpu_host_map *hmap;

	if (!valid_dma_direction(dir))
		return -EINVAL;
	/* invalid if size == 0 or overflow */
	if (arg->offset + arg->size <= arg->offset)
		return -EINVAL;

	mutex_lock(&group->lock);
	if (!edgetpu_device_group_is_finalized(group)) {
		ret = edgetpu_group_errno(group);
		goto unlock_group;
	}

	edgetpu_mapping_lock(&group->host_mappings);
	map = edgetpu_mapping_find_locked(&group->host_mappings, tpu_addr);
	if (!map) {
		ret = -EINVAL;
		goto unlock_mapping;
	}

	hmap = container_of(map, struct edgetpu_host_map, map);
	ret = group_sync_host_map(group, hmap, arg->offset, arg->size, dir,
				  arg->flags & EDGETPU_SYNC_FOR_CPU);
unlock_mapping:
	edgetpu_mapping_unlock(&group->host_mappings);
unlock_group:
	mutex_unlock(&group->lock);
	return ret;
}

void edgetpu_mappings_clear_group(struct edgetpu_device_group *group)
{
	edgetpu_mapping_clear(&group->host_mappings);
	edgetpu_mapping_clear(&group->dmabuf_mappings);
}

void edgetpu_group_mappings_show(struct edgetpu_device_group *group,
				 struct seq_file *s)
{
	enum edgetpu_context_id context =
		edgetpu_group_context_id_locked(group);

	seq_printf(s, "group %u", group->workload_id);
	switch (group->status) {
	case EDGETPU_DEVICE_GROUP_WAITING:
	case EDGETPU_DEVICE_GROUP_FINALIZED:
		break;
	case EDGETPU_DEVICE_GROUP_ERRORED:
		seq_puts(s, " (errored)");
		break;
	case EDGETPU_DEVICE_GROUP_DISBANDED:
		seq_puts(s, ": disbanded\n");
		return;
	}

	if (context == EDGETPU_CONTEXT_INVALID)
		seq_puts(s, " context (none):\n");
	else if (context & EDGETPU_CONTEXT_DOMAIN_TOKEN)
		seq_printf(s, " context detached %#x:\n",
			   context & ~(EDGETPU_CONTEXT_DOMAIN_TOKEN));
	else
		seq_printf(s, " context mbox %d:\n", context);

	if (group->host_mappings.count) {
		seq_printf(s, "host buffer mappings (%zd):\n",
			   group->host_mappings.count);
		edgetpu_mappings_show(&group->host_mappings, s);
	}
	if (group->dmabuf_mappings.count) {
		seq_printf(s, "dma-buf buffer mappings (%zd):\n",
			   group->dmabuf_mappings.count);
		edgetpu_mappings_show(&group->dmabuf_mappings, s);
	}

	if (group->vii.cmd_queue_mem.vaddr) {
		seq_puts(s, "VII queues:\n");
		seq_printf(s, "  %#llx %lu cmdq %#llx %pad\n",
			   group->vii.cmd_queue_mem.tpu_addr,
			   DIV_ROUND_UP(group->vii.cmd_queue_mem.size,
					PAGE_SIZE),
			   group->vii.cmd_queue_mem.host_addr,
			   &group->vii.cmd_queue_mem.dma_addr);
		seq_printf(s, "  %#llx %lu rspq %#llx %pad\n",
			   group->vii.resp_queue_mem.tpu_addr,
			   DIV_ROUND_UP(group->vii.resp_queue_mem.size,
					PAGE_SIZE),
			   group->vii.resp_queue_mem.host_addr,
			   &group->vii.resp_queue_mem.dma_addr);
	}
}

int edgetpu_mmap_csr(struct edgetpu_device_group *group,
		     struct vm_area_struct *vma, bool is_external)
{
	struct edgetpu_dev *etdev = group->etdev;
	int ret = 0;
	ulong phys_base, vma_size, map_size;

	if (is_external && !uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return -EPERM;

	mutex_lock(&group->lock);
	if (!edgetpu_group_finalized_and_attached(group)) {
		ret = edgetpu_group_errno(group);
		goto out;
	}

	if (is_external && (!group->ext_mailbox || !group->ext_mailbox->descriptors)) {
		ret = -ENOENT;
		goto out;
	}

	vma_size = vma->vm_end - vma->vm_start;
	map_size = min(vma_size, USERSPACE_CSR_SIZE);
	if (is_external)
		phys_base = etdev->regs.phys +
			    group->ext_mailbox->descriptors[0].mailbox->cmd_queue_csr_base;
	else
		phys_base = etdev->regs.phys + group->vii.mailbox->cmd_queue_csr_base;
	ret = io_remap_pfn_range(vma, vma->vm_start, phys_base >> PAGE_SHIFT,
				 map_size, vma->vm_page_prot);
	if (ret)
		etdev_dbg(etdev, "Error remapping PFN range: %d", ret);

out:
	mutex_unlock(&group->lock);
	return ret;
}

int edgetpu_mmap_queue(struct edgetpu_device_group *group, enum gcip_mailbox_queue_type type,
		       struct vm_area_struct *vma, bool is_external)
{
	struct edgetpu_dev *etdev = group->etdev;
	int ret = 0;
	edgetpu_queue_mem *queue_mem;

	if (is_external && !uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return -EPERM;

	mutex_lock(&group->lock);
	if (!edgetpu_group_finalized_and_attached(group)) {
		ret = edgetpu_group_errno(group);
		goto out;
	}

	if (is_external && (!group->ext_mailbox || !group->ext_mailbox->descriptors)) {
		ret = -ENOENT;
		goto out;
	}

	if (type == GCIP_MAILBOX_CMD_QUEUE) {
		if (is_external)
			queue_mem = &(group->ext_mailbox->descriptors[0].cmd_queue_mem);
		else
			queue_mem = &(group->vii.cmd_queue_mem);
	} else {
		if (is_external)
			queue_mem = &(group->ext_mailbox->descriptors[0].resp_queue_mem);
		else
			queue_mem = &(group->vii.resp_queue_mem);
	}

	if (!queue_mem->vaddr) {
		ret = -ENXIO;
		goto out;
	}

	ret = edgetpu_iremap_mmap(etdev, vma, queue_mem);
	if (!ret)
		queue_mem->host_addr = vma->vm_start;

out:
	mutex_unlock(&group->lock);
	return ret;
}

/*
 * Set @group status as errored, set the error mask, and notify the runtime of
 * the fatal error event on the group.
 */
void edgetpu_group_fatal_error_notify(struct edgetpu_device_group *group,
				      uint error_mask)
{
	etdev_dbg(group->etdev, "notify group %u error %#x",
		  group->workload_id, error_mask);
	mutex_lock(&group->lock);
	/*
	 * Only finalized groups may have handshake with the FW, mark
	 * them as errored.
	 */
	if (edgetpu_device_group_is_finalized(group))
		group->status = EDGETPU_DEVICE_GROUP_ERRORED;
	group->fatal_errors |= error_mask;
	mutex_unlock(&group->lock);
	edgetpu_group_notify(group, EDGETPU_EVENT_FATAL_ERROR);
}

/*
 * For each group active on @etdev: set the group status as errored, set the
 * error mask, and notify the runtime of the fatal error event.
 */
void edgetpu_fatal_error_notify(struct edgetpu_dev *etdev, uint error_mask)
{
	size_t i, num_groups = 0;
	struct edgetpu_device_group *group;
	struct edgetpu_device_group **groups;
	struct edgetpu_list_group *g;

	mutex_lock(&etdev->groups_lock);
	groups = kmalloc_array(etdev->n_groups, sizeof(*groups), GFP_KERNEL);
	if (unlikely(!groups)) {
		/*
		 * Just give up setting status in this case, this only happens
		 * when the system is OOM.
		 */
		mutex_unlock(&etdev->groups_lock);
		return;
	}
	/*
	 * Fetch the groups into an array to set the group status without
	 * holding @etdev->groups_lock. To prevent the potential deadlock that
	 * edgetpu_device_group_add() holds group->lock then etdev->groups_lock.
	 */
	etdev_for_each_group(etdev, g, group) {
		if (edgetpu_device_group_is_disbanded(group))
			continue;
		groups[num_groups++] = edgetpu_device_group_get(group);
	}
	mutex_unlock(&etdev->groups_lock);
	for (i = 0; i < num_groups; i++) {
		edgetpu_group_fatal_error_notify(groups[i], error_mask);
		edgetpu_device_group_put(groups[i]);
	}
	kfree(groups);
}

uint edgetpu_group_get_fatal_errors(struct edgetpu_device_group *group)
{
	uint fatal_errors;

	mutex_lock(&group->lock);
	fatal_errors = edgetpu_group_get_fatal_errors_locked(group);
	mutex_unlock(&group->lock);
	return fatal_errors;
}

void edgetpu_group_detach_mailbox_locked(struct edgetpu_device_group *group)
{
	if (!group->mailbox_detachable)
		return;
	if (edgetpu_group_mailbox_detached_locked(group))
		return;
	do_detach_mailbox_locked(group);
}

void edgetpu_group_close_and_detach_mailbox(struct edgetpu_device_group *group)
{
	mutex_lock(&group->lock);
	/*
	 * Only a finalized group may have mailbox attached.
	 *
	 * Detaching mailbox for an errored group is also fine.
	 */
	if (is_finalized_or_errored(group)) {
		edgetpu_group_deactivate(group);
		edgetpu_group_detach_mailbox_locked(group);
		edgetpu_group_deactivate_external_mailbox(group);
	}
	mutex_unlock(&group->lock);
}

int edgetpu_group_attach_mailbox_locked(struct edgetpu_device_group *group)
{
	if (!group->mailbox_detachable)
		return 0;
	if (!edgetpu_group_mailbox_detached_locked(group))
		return 0;
	return do_attach_mailbox_locked(group);
}

int edgetpu_group_attach_and_open_mailbox(struct edgetpu_device_group *group)
{
	int ret = 0;

	mutex_lock(&group->lock);
	/*
	 * Only attaching mailbox for finalized groups.
	 * Don't attach mailbox for errored groups.
	 */
	if (!edgetpu_device_group_is_finalized(group))
		goto out_unlock;
	ret = edgetpu_group_attach_mailbox_locked(group);
	if (ret)
		goto out_unlock;
	ret = edgetpu_group_activate(group);
	if (ret)
		goto error_detach;
	ret = edgetpu_group_activate_external_mailbox(group);
	if (!ret)
		goto out_unlock;

	edgetpu_group_deactivate(group);
error_detach:
	edgetpu_group_detach_mailbox_locked(group);
out_unlock:
	mutex_unlock(&group->lock);
	return ret;
}

/*
 * Return the group with id @vcid for device @etdev, with a reference held
 * on the group (must call edgetpu_device_group_put when done), or NULL if
 * no group with that VCID is found.
 */
static struct edgetpu_device_group *get_group_by_vcid(
	struct edgetpu_dev *etdev, u16 vcid)
{
	struct edgetpu_device_group *group = NULL;
	struct edgetpu_device_group *tgroup;
	struct edgetpu_list_group *g;

	mutex_lock(&etdev->groups_lock);
	etdev_for_each_group(etdev, g, tgroup) {
		if (tgroup->vcid == vcid) {
			group = edgetpu_device_group_get(tgroup);
			break;
		}
	}
	mutex_unlock(&etdev->groups_lock);
	return group;
}

void edgetpu_handle_job_lockup(struct edgetpu_dev *etdev, u16 vcid)
{
	struct edgetpu_device_group *group;

	etdev_err(etdev, "firmware-detected job lockup on VCID %u",
		  vcid);
	group = get_group_by_vcid(etdev, vcid);
	if (!group) {
		etdev_warn(etdev, "VCID %u group not found", vcid);
		return;
	}
	edgetpu_group_fatal_error_notify(group, EDGETPU_ERROR_RUNTIME_TIMEOUT);
	edgetpu_device_group_put(group);
}
