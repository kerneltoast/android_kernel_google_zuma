// SPDX-License-Identifier: GPL-2.0
/*
 * Implements utilities for virtual device group of EdgeTPU.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/eventfd.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/refcount.h>
#include <linux/rwsem.h>
#include <linux/scatterlist.h>
#include <linux/sched/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time64.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-iommu.h>
#include <gcip/gcip-memory.h>
#include <iif/iif-dma-fence.h>

#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-dmabuf.h"
#include "edgetpu-firmware.h"
#include "edgetpu-iif.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-wakelock.h"
#include "edgetpu-vii-packet.h"
#include "edgetpu.h"

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
 * Caller holds group->lock for writing.
 */
static int edgetpu_group_activate(struct edgetpu_device_group *group)
{
	struct edgetpu_iommu_domain *etdomain;
	int ret;

	if (edgetpu_group_mailbox_detached_locked(group))
		return 0;

	/* Activate the mailbox whose index == the assigned PASID */
	etdomain = edgetpu_group_domain_locked(group);
	edgetpu_soc_activate_context(group->etdev, etdomain->pasid);
	ret = edgetpu_mailbox_activate_vii(group->etdev, etdomain->pasid,
					   group->mbox_attr.client_priv, group->vcid,
					   !group->activated);
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
 * Caller holds group->lock for writing.
 */
static void edgetpu_group_deactivate(struct edgetpu_device_group *group)
{
	struct edgetpu_iommu_domain *etdomain;

	if (edgetpu_group_mailbox_detached_locked(group))
		return;
	edgetpu_sw_wdt_dec_active_ref(group->etdev);
	etdomain = edgetpu_group_domain_locked(group);
	edgetpu_mailbox_deactivate_vii(group->etdev, etdomain->pasid);
	/*
	 * Deactivate the context to prevent speculative accesses from being issued to a disabled
	 * context.
	 */
	edgetpu_soc_deactivate_context(group->etdev, etdomain->pasid);
}

/*
 * Handle KCI chores for device group disband.
 *
 * send KCI CLOSE_DEVICE to the device (and GET_USAGE to update usage stats).
 *
 * Caller holds group->lock for writing.
 */
static void edgetpu_device_group_kci_deactivate(struct edgetpu_device_group *group)
{
	edgetpu_kci_update_usage_async(group->etdev->etkci);
	/*
	 * Theoretically we don't need to check @dev_inaccessible here.
	 * @dev_inaccessible is true implies the client has wakelock count zero, under such case
	 * edgetpu_mailbox_deactivate_vii() has been called on releasing the wakelock and therefore
	 * this edgetpu_group_deactivate() call won't send any KCI.
	 * Still have a check here in case this function does CSR programming other than calling
	 * edgetpu_mailbox_deactivate_vii() someday.
	 */
	if (!group->dev_inaccessible)
		edgetpu_group_deactivate(group);
}

/*
 * Asynchronously sends a JOIN_GROUP KCI command to the @group device.
 *
 * Caller holds group->lock for writing.
 */
static int
edgetpu_device_group_kci_finalized(struct edgetpu_device_group *group)
{
	return edgetpu_group_activate(group);
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

	if (event_id >= EDGETPU_EVENT_COUNT) {
		eventfd_ctx_put(ctx);
		return -EINVAL;
	}

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

struct pending_command_task {
	struct list_head list_entry;
	struct task_struct *task;
};

static void edgetpu_group_clear_pending_commands(struct edgetpu_device_group *group)
{
	struct list_head *cur, *nxt;
	struct pending_command_task *pending_task;
	unsigned long flags;

	spin_lock_irqsave(&group->pending_cmd_tasks_lock, flags);
	group->is_clearing_pending_commands = true;
	spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);

	/*
	 * With @group->lock held for writing and @group->is_clearing_pending_commands set, there
	 * will be no more additions or deletions from @group->pending_cmd_tasks respectively so it
	 * can be iterated over without holding @group->pending_cmd_tasks.
	 */
	list_for_each_safe(cur, nxt, &group->pending_cmd_tasks) {
		pending_task = container_of(cur, struct pending_command_task, list_entry);
		/*
		 * kthread_stop() will wake the task and wait for it to exit.
		 * If the task is already waiting on a dma_fence, this will interrupt the wait
		 * and cause the task to exit immediately.
		 *
		 * If the task has not started waiting on its fence by the time this call occurs,
		 * then this call will have to wait for the fence to timeout before it returns.
		 */
		kthread_stop(pending_task->task);
		list_del(&pending_task->list_entry);
		kfree(pending_task);
	}
}

static void edgetpu_group_clear_responses(struct edgetpu_device_group *group)
{
	struct edgetpu_ikv_response *cur, *nxt;
	unsigned long flags;
	LIST_HEAD(pending_ikv_resps);

	spin_lock_irqsave(&group->ikv_resp_lock, flags);

	/*
	 * Setting all pending responses as `processed` indicates that any processing or timeout
	 * threads currently waiting on `ikv_resp_lock` should exit immediately when unblocked.
	 *
	 * This ensures no other threads will access pending_ikv_resps or ready_ikv_resps.
	 */
	list_for_each_entry(cur, &group->pending_ikv_resps, list_entry) {
		cur->processed = true;
	}

	list_replace_init(&group->pending_ikv_resps, &pending_ikv_resps);

	/*
	 * It's necessary to release the group's ikv_resp_lock, so that any pending timeouts can
	 * proceed during calls to `gcip_mailbox_cancel_awaiter()` below.
	 */
	spin_unlock_irqrestore(&group->ikv_resp_lock, flags);

	/*
	 * Free all responses that were still pending.
	 *
	 * With the group being released (preventing new commands) and all existing responses marked
	 * as processed, no other threads will modify `pending_ikv_resps`.
	 */
	list_for_each_entry_safe(cur, nxt, &pending_ikv_resps, list_entry) {
		if (cur->iif_dma_fence) {
			iif_dma_fence_stop(cur->iif_dma_fence);
			iif_fence_put(cur->iif_dma_fence);
		}
		gcip_fence_array_waited_async(cur->in_fence_array, IIF_IP_TPU);
		gcip_fence_array_put_async(cur->out_fence_array);
		gcip_fence_array_put_async(cur->in_fence_array);
		gcip_mailbox_cancel_awaiter(cur->awaiter);
		gcip_mailbox_release_awaiter(cur->awaiter);
	}

	spin_lock_irqsave(&group->ikv_resp_lock, flags);

	/*
	 * Free all responses that were ready for consumption.
	 *
	 * Now that all pending response awaiters have been cancelled and additional pending
	 * responses will not be created due to the group being released, it is guaranteed no more
	 * responses will be added to `ready_ikv_resps`.
	 */
	list_for_each_entry_safe(cur, nxt, &group->ready_ikv_resps, list_entry) {
		list_del(&cur->list_entry);
		/*
		 * Clean-up the mailbox protocol's async response structure.
		 * This will also free the edgetpu_ikv_response.
		 */
		gcip_mailbox_release_awaiter(cur->awaiter);
	}

	spin_unlock_irqrestore(&group->ikv_resp_lock, flags);
}

void edgetpu_group_notify(struct edgetpu_device_group *group, uint event_id)
{
	unsigned long flags;

	if (event_id >= EDGETPU_EVENT_COUNT)
		return;

	etdev_dbg(group->etdev, "%s: group %u id=%u", __func__,
		  group->group_id, event_id);
	read_lock_irqsave(&group->events.lock, flags);
	if (group->events.eventfds[event_id])
		eventfd_signal(group->events.eventfds[event_id], 1);
	read_unlock_irqrestore(&group->events.lock, flags);
}

/*
 * Releases all resources the group allocated and mark the group as disbanded.
 *
 * release VII mailboxes, buffer mappings, etc.
 *
 * The lock of group must be held for writing.
 */
static void edgetpu_device_group_release(struct edgetpu_device_group *group)
{
	lockdep_assert_held(&group->lock);

	edgetpu_group_clear_events(group);
	if (is_finalized_or_errored(group)) {
		edgetpu_group_clear_pending_commands(group);
		edgetpu_device_group_kci_deactivate(group);
		/*
		 * Mappings and responses cannot be cleared until the device_group has been closed
		 * via KCI. This ensures firmware will not attempt to access any resoucres freed by
		 * the command's `release_callback` or memory which has been unmapped.
		 */
		edgetpu_group_clear_responses(group);
		edgetpu_mappings_clear_group(group);
		edgetpu_mailbox_external_disable_free_locked(group);
		edgetpu_mailbox_remove_vii(&group->vii);
	}
	/* etdomain is freed after group->lock is dropped to avoid deadlock b/348298955. */
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

		if (group->mbox_attr.partition_type_high == EDGETPU_PARTITION_EXTRA)
			vcid_pool &= BIT(EDGETPU_VCID_EXTRA_PARTITION_HIGH);
		else if (group->mbox_attr.partition_type == EDGETPU_PARTITION_EXTRA)
			vcid_pool &= BIT(EDGETPU_VCID_EXTRA_PARTITION);
		else
			vcid_pool &= ~(BIT(EDGETPU_VCID_EXTRA_PARTITION) |
				       BIT(EDGETPU_VCID_EXTRA_PARTITION_HIGH));
		if (!vcid_pool) {
			etdev_err(etdev, "%s client slot unavailable (%u active groups)\n",
				  group->mbox_attr.partition_type_high == EDGETPU_PARTITION_EXTRA ||
				  group->mbox_attr.partition_type == EDGETPU_PARTITION_EXTRA ?
				  "extra" : "normal", etdev->n_groups);
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

void edgetpu_device_group_disband(struct edgetpu_client *client)
{
	struct edgetpu_device_group *group;
	struct edgetpu_iommu_domain *etdomain;
	struct edgetpu_list_group *l;

	mutex_lock(&client->group_lock);
	group = client->group;
	if (!group) {
		mutex_unlock(&client->group_lock);
		return;
	}

	down_write(&group->lock);
	edgetpu_device_group_release(group);
	edgetpu_client_put(group->client);
	edgetpu_device_group_put(client->group);
	client->group = NULL;
	etdomain = group->etdomain;
	group->etdomain = NULL;
	up_write(&group->lock);
	mutex_unlock(&client->group_lock);

	/* Now that group->lock is released, free the etdomain b/348298955 */
	if (etdomain) {
		edgetpu_mmu_detach_domain(group->etdev, etdomain);
		edgetpu_mmu_free_domain(group->etdev, etdomain);
	}

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

/* Only called by edgetpu_device_group_create() for a new group in "waiting" status. */
static int edgetpu_device_group_add(struct edgetpu_device_group *group,
				    struct edgetpu_client *client)
{
	int ret = 0;

	mutex_lock(&client->group_lock);
	if (client->group) {
		mutex_unlock(&client->group_lock);
		return -EINVAL;
	}

	group->client = edgetpu_client_get(client);
	ret = edgetpu_dev_add_group(client->etdev, group);
	if (ret) {
		edgetpu_client_put(client);
		goto out;
	}

	client->group = edgetpu_device_group_get(group);
	etdev_dbg(client->etdev, "%s: added group %u", __func__,
		  group->group_id);

out:
	mutex_unlock(&client->group_lock);
	return ret;
}

struct edgetpu_device_group *
edgetpu_device_group_create(struct edgetpu_client *client, const struct edgetpu_mailbox_attr *attr)
{
	static uint cur_group_id;
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
	group->group_id = cur_group_id++;
	group->status = EDGETPU_DEVICE_GROUP_WAITING;
	group->etdev = client->etdev;
	group->vii.etdev = client->etdev;
	INIT_LIST_HEAD(&group->ready_ikv_resps);
	INIT_LIST_HEAD(&group->pending_ikv_resps);
	spin_lock_init(&group->ikv_resp_lock);
	atomic_set(&group->available_vii_credits, EDGETPU_NUM_VII_CREDITS_PER_CLIENT);
	init_rwsem(&group->lock);
	mutex_init(&group->mapping_lock);
	mutex_init(&group->vii_lock);
	rwlock_init(&group->events.lock);
	INIT_LIST_HEAD(&group->dma_fence_list);
	edgetpu_mapping_init(&group->host_mappings);
	edgetpu_mapping_init(&group->dmabuf_mappings);
	group->mbox_attr = *attr;
	INIT_LIST_HEAD(&group->pending_cmd_tasks);
	spin_lock_init(&group->pending_cmd_tasks_lock);
	group->is_clearing_pending_commands = false;
#if HAS_DETACHABLE_IOMMU_DOMAINS
	if (attr->priority & EDGETPU_PRIORITY_DETACHABLE)
		group->mailbox_detachable = true;
#endif

	etdomain = edgetpu_mmu_alloc_domain(group->etdev);
	if (!etdomain) {
		ret = -ENOMEM;
		goto error_put_group;
	}
	group->etdomain = etdomain;

	/* adds @client as the only member */
	ret = edgetpu_device_group_add(group, client);
	if (ret) {
		etdev_dbg(group->etdev, "%s: group %u add failed ret=%d",
			  __func__, group->group_id, ret);
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

	down_write(&group->lock);
	/* do nothing if the group is finalized */
	if (is_finalized_or_errored(group))
		goto err_unlock;

	if (!edgetpu_device_group_is_waiting(group)) {
		etdev_err(group->etdev, "finalize group is not waiting");
		ret = -EINVAL;
		goto err_unlock;
	}

	if (!group->mailbox_detachable) {
		ret = edgetpu_mmu_attach_domain(group->etdev, group->etdomain);
		if (ret) {
			etdev_err(group->etdev, "finalize attach domain failed: %d", ret);
			goto err_unlock;
		}
	}
	if (edgetpu_wakelock_count_locked(&group->client->wakelock)) {
		ret = edgetpu_group_attach_mailbox_locked(group);
		if (ret) {
			etdev_err(group->etdev, "finalize attach mailbox failed: %d", ret);
			goto err_detach_mmu_domain;
		}
	}

	/* send KCI only if the device is powered on */
	if (edgetpu_wakelock_count_locked(&group->client->wakelock)) {
		ret = edgetpu_device_group_kci_finalized(group);
		if (ret)
			goto err_remove_detach_mailbox;
	}

	group->status = EDGETPU_DEVICE_GROUP_FINALIZED;

	up_write(&group->lock);
	return 0;

err_remove_detach_mailbox:
	if (edgetpu_wakelock_count_locked(&group->client->wakelock))
		edgetpu_group_detach_mailbox_locked(group);

err_detach_mmu_domain:
	if (!group->mailbox_detachable)
		edgetpu_mmu_detach_domain(group->etdev, group->etdomain);

err_unlock:
	up_write(&group->lock);
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
static void buffer_mapping_destroy(struct edgetpu_mapping *map)
{
	struct edgetpu_device_group *group = map->priv;

	etdev_dbg(group->etdev, "%s: %u: iova=%pad", __func__, group->group_id,
		  &map->gcip_mapping->device_address);

	gcip_iommu_mapping_unmap(map->gcip_mapping);

	edgetpu_device_group_put(group);
	kfree(map);
}

static void edgetpu_host_map_show(struct edgetpu_mapping *map,
				  struct seq_file *s)
{
	struct scatterlist *sg;
	int i;
	size_t cur_offset = 0;

	/* Only 1 entry per mapped segment is shown, with the phys addr of the 1st segment. */
	for_each_sg(map->gcip_mapping->sgt->sgl, sg, map->gcip_mapping->sgt->nents, i) {
		dma_addr_t phys_addr = sg_phys(sg);
		dma_addr_t dma_addr = sg_dma_address(sg);

		seq_printf(s, "  %pad %lu %s %#llx %pap\n", &dma_addr,
			   DIV_ROUND_UP(sg_dma_len(sg), PAGE_SIZE),
			   edgetpu_dma_dir_rw_s(map->gcip_mapping->dir),
			   map->host_address + cur_offset, &phys_addr);
		cur_offset += sg_dma_len(sg);
	}
}

size_t edgetpu_group_mappings_total_size(struct edgetpu_device_group *group, bool restrict32)
{
	return edgetpu_mappings_total_size(&group->host_mappings, restrict32) +
		edgetpu_mappings_total_size(&group->dmabuf_mappings, restrict32);
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
 * Caller holds mapping's lock, to prevent @map being modified / removed by
 * other processes.
 */
static int group_sync_host_map(struct edgetpu_device_group *group, struct edgetpu_mapping *map,
			       u64 offset, u64 size, enum dma_data_direction dir, bool for_cpu)
{
	const u64 end = offset + size;
	typeof(dma_sync_sg_for_cpu) *sync =
		for_cpu ? dma_sync_sg_for_cpu : dma_sync_sg_for_device;
	struct sg_table *sgt;
	struct sglist_to_sync sglist;

	sgt = map->gcip_mapping->sgt;
	find_sg_to_sync(sgt, offset, end, &sglist);
	if (!sglist.nelems)
		return -EINVAL;

	sync(group->etdev->dev, sglist.sg, sglist.nelems, dir);
	restore_sg_after_sync(&sglist);
	return 0;
}

void edgetpu_device_group_log_map_error(struct edgetpu_device_group *group, size_t size,
					edgetpu_map_flag_t flags, int errorval)
{
	bool restrict32 = !(flags & EDGETPU_MAP_CPU_NONACCESSIBLE);
	size_t total = edgetpu_group_mappings_total_size(group, restrict32);

	etdev_err(group->etdev, "map %zuB (%d-bit) failed: %d (already mapped %zuB)", size,
		  restrict32 ? 32 : 36, errorval, total);
}

/**
 * buffer_mapping_create() - Maps the buffer and creates the corresponding mapping object.
 * @group: The group that the buffer belongs to.
 * @host_addr: The memory address of the buffer.
 * @size: The size of the buffer.
 * @flags: The flags used to map the buffer.
 *
 * Return: The pointer of the target mapping object or an error pointer on failure.
 */
static struct edgetpu_mapping *buffer_mapping_create(struct edgetpu_device_group *group,
						     u64 host_addr, u64 size,
						     edgetpu_map_flag_t flags)
{
	int ret = -EINVAL;
	struct edgetpu_mapping *map = NULL;
	struct edgetpu_iommu_domain *etdomain;
	unsigned long dma_attrs = map_to_dma_attr(flags, true);
	u64 gcip_map_flags;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map) {
		ret = -ENOMEM;
		goto err_ret;
	}

	map->host_address = host_addr;
	map->priv = edgetpu_device_group_get(group);
	map->release = buffer_mapping_destroy;
	map->show = edgetpu_host_map_show;
	map->flags = flags;

	down_read(&group->lock);
	mutex_lock(&group->mapping_lock);
	etdomain = edgetpu_group_domain_locked(group);
	if (!edgetpu_device_group_is_finalized(group)) {
		ret = edgetpu_group_errno(group);
		mutex_unlock(&group->mapping_lock);
		up_read(&group->lock);
		goto err_free_map;
	}
	gcip_map_flags = edgetpu_mappings_encode_gcip_map_flags(flags, dma_attrs, true);
	map->gcip_mapping = gcip_iommu_domain_map_buffer(etdomain->gdomain, host_addr, size,
							 gcip_map_flags, NULL);
	mutex_unlock(&group->mapping_lock);
	up_read(&group->lock);
	if (IS_ERR(map->gcip_mapping)) {
		ret = PTR_ERR(map->gcip_mapping);
		edgetpu_device_group_log_map_error(group, size, flags, ret);
		goto err_free_map;
	}

	return map;

err_free_map:
	kfree(map);
	edgetpu_device_group_put(group);
err_ret:
	return ERR_PTR(ret);
}

int edgetpu_device_group_map(struct edgetpu_device_group *group, struct edgetpu_map_ioctl *arg)
{
	int ret;
	struct edgetpu_mapping *map;
	tpu_addr_t tpu_addr;

	map = buffer_mapping_create(group, arg->host_address, arg->size, arg->flags);
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		return ret;
	}

	/*
	 * @map can be freed (by another thread) once it's added to the mappings, record the address
	 * before that.
	 */
	tpu_addr = map->gcip_mapping->device_address;
	ret = edgetpu_mapping_add(&group->host_mappings, map);
	if (ret) {
		etdev_dbg(group->etdev, "duplicate mapping %u:%pad", group->group_id, &tpu_addr);
		goto err_destroy_mapping;
	}

	arg->device_address = tpu_addr;

	return 0;

err_destroy_mapping:
	buffer_mapping_destroy(map);

	return ret;
}

int edgetpu_device_group_unmap(struct edgetpu_device_group *group,
			       tpu_addr_t tpu_addr, edgetpu_map_flag_t flags)
{
	struct edgetpu_mapping *map;

	edgetpu_mapping_lock(&group->host_mappings);
	map = edgetpu_mapping_find_locked(&group->host_mappings, tpu_addr);
	if (!map) {
		edgetpu_mapping_unlock(&group->host_mappings);
		etdev_dbg(group->etdev, "%s: mapping not found for workload %u: %pad", __func__,
			  group->group_id, &tpu_addr);
		return -EINVAL;
	}

	edgetpu_mapping_unlink(&group->host_mappings, map);

	if (flags & EDGETPU_MAP_SKIP_CPU_SYNC)
		map->gcip_mapping->gcip_map_flags |=
			edgetpu_mappings_encode_gcip_map_flags(0, DMA_ATTR_SKIP_CPU_SYNC, false);

	buffer_mapping_destroy(map);
	edgetpu_mapping_unlock(&group->host_mappings);
	return 0;
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

	if (!valid_dma_direction(dir))
		return -EINVAL;
	/* invalid if size == 0 or overflow */
	if (arg->offset + arg->size <= arg->offset)
		return -EINVAL;

	down_read(&group->lock);
	mutex_lock(&group->mapping_lock);
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

	ret = group_sync_host_map(group, map, arg->offset, arg->size, dir,
				  arg->flags & EDGETPU_SYNC_FOR_CPU);
unlock_mapping:
	edgetpu_mapping_unlock(&group->host_mappings);
unlock_group:
	mutex_unlock(&group->mapping_lock);
	up_read(&group->lock);
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
	struct edgetpu_iommu_domain *etdomain = edgetpu_group_domain_locked(group);

	seq_printf(s, "group %u", group->group_id);
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

	if (edgetpu_mmu_domain_detached(etdomain))
		seq_puts(s, " pasid detached:\n");
	else
		seq_printf(s, " pasid %u:\n", etdomain->pasid);

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

	if (group->vii.cmd_queue_mem.virt_addr) {
		seq_puts(s, "VII queues:\n");
		seq_printf(s, "  %pad %lu cmdq %#llx\n", &group->vii.cmd_queue_mem.dma_addr,
			   DIV_ROUND_UP(group->vii.cmd_queue_mem.size, PAGE_SIZE),
			   group->vii.cmd_queue_mem.host_addr);
		seq_printf(s, "  %pad %lu rspq %#llx\n", &group->vii.resp_queue_mem.dma_addr,
			   DIV_ROUND_UP(group->vii.resp_queue_mem.size, PAGE_SIZE),
			   group->vii.resp_queue_mem.host_addr);
	}
}

int edgetpu_device_group_send_vii_command(struct edgetpu_device_group *group, void *cmd,
					  struct gcip_fence_array *in_fence_array,
					  struct gcip_fence_array *out_fence_array,
					  struct iif_fence *iif_dma_fence,
					  struct edgetpu_ikv_additional_info *additional_info,
					  void (*release_callback)(void *), void *release_data)
{
	struct edgetpu_dev *etdev = group->etdev;
	struct edgetpu_iommu_domain *etdomain;
	int ret = edgetpu_pm_get_if_powered(etdev, true);

	if (ret) {
		etdev_err(etdev, "Unable to send VII command, TPU block is off");
		return ret;
	}

	down_read(&group->lock);
	mutex_lock(&group->vii_lock);
	if (!edgetpu_device_group_is_finalized(group) || edgetpu_device_group_is_errored(group)) {
		etdev_err(etdev, "Unable to send VII command, device group is %s",
			  edgetpu_device_group_is_errored(group) ? "errored" : "not finalized");
		ret = -EINVAL;
		goto unlock_group;
	}

	etdomain = edgetpu_group_domain_locked(group);
	if (!etdomain) {
		etdev_err(etdev, "Unable to send VII command, device group has no domain");
		ret = -EINVAL;
		goto unlock_group;
	}

	if (!atomic_add_unless(&group->available_vii_credits, -1, 0)) {
		ret = -EBUSY;
		goto unlock_group;
	}

	edgetpu_vii_command_set_client_id(etdev, cmd, etdomain->pasid);
	ret = edgetpu_ikv_send_cmd(etdev->etikv, cmd, &group->pending_ikv_resps,
				   &group->ready_ikv_resps, &group->ikv_resp_lock, group,
				   in_fence_array, out_fence_array, iif_dma_fence, additional_info,
				   release_callback, release_data);
	/* Refund credit if command failed to send. */
	if (ret)
		atomic_inc(&group->available_vii_credits);

unlock_group:
	mutex_unlock(&group->vii_lock);
	up_read(&group->lock);
	edgetpu_pm_put(etdev);
	return ret;
}

int edgetpu_device_group_get_vii_response(struct edgetpu_device_group *group, void *resp)
{
	struct edgetpu_ikv_response *ikv_resp;
	unsigned long flags;
	int ret = 0;

	down_read(&group->lock);
	mutex_lock(&group->vii_lock);
	if (!edgetpu_device_group_is_finalized(group) || edgetpu_device_group_is_errored(group)) {
		ret = -EINVAL;
		goto unlock_group;
	}

	spin_lock_irqsave(&group->ikv_resp_lock, flags);

	if (list_empty(&group->ready_ikv_resps)) {
		ret = -ENOENT;
		spin_unlock_irqrestore(&group->ikv_resp_lock, flags);
		goto unlock_group;
	}

	ikv_resp = list_first_entry(&group->ready_ikv_resps, typeof(struct edgetpu_ikv_response),
				    list_entry);
	list_del(&ikv_resp->list_entry);

	spin_unlock_irqrestore(&group->ikv_resp_lock, flags);

	memcpy(resp, ikv_resp->resp, edgetpu_vii_response_packet_size(group->etdev));
	/* This will also free `ikv_resp` */
	gcip_mailbox_release_awaiter(ikv_resp->awaiter);

unlock_group:
	mutex_unlock(&group->vii_lock);
	up_read(&group->lock);
	return ret;
}

int edgetpu_mmap_csr(struct edgetpu_device_group *group,
		     struct vm_area_struct *vma, bool is_external)
{
	struct edgetpu_dev *etdev = group->etdev;
	int ret = 0;
	ulong phys_base, vma_size, map_size;

	if (is_external && !uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return -EPERM;
	if (!is_external && etdev->mailbox_manager->use_ikv)
		return -EOPNOTSUPP;

	down_write(&group->lock);
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
	up_write(&group->lock);
	return ret;
}

int edgetpu_mmap_queue(struct edgetpu_device_group *group, enum gcip_mailbox_queue_type type,
		       struct vm_area_struct *vma, bool is_external)
{
	struct edgetpu_dev *etdev = group->etdev;
	int ret = 0;
	struct gcip_memory *queue_mem;

	if (is_external && !uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return -EPERM;
	if (!is_external && etdev->mailbox_manager->use_ikv)
		return -EOPNOTSUPP;

	down_write(&group->lock);
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

	if (!queue_mem->virt_addr) {
		ret = -ENXIO;
		goto out;
	}

	ret = edgetpu_iremap_mmap(etdev, vma, queue_mem);
	if (!ret)
		queue_mem->host_addr = vma->vm_start;

out:
	up_write(&group->lock);
	return ret;
}

/*
 * Set @group status as errored, set the error mask, and notify the runtime of
 * the fatal error event on the group.
 */
void edgetpu_group_fatal_error_notify(struct edgetpu_device_group *group,
				      uint error_mask)
{
	etdev_warn(group->etdev, "notify group %u error %#x", group->group_id, error_mask);
	down_write(&group->lock);
	/*
	 * Only finalized groups may have handshake with the FW, mark
	 * them as errored.
	 */
	if (edgetpu_device_group_is_finalized(group))
		group->status = EDGETPU_DEVICE_GROUP_ERRORED;
	group->fatal_errors |= error_mask;

	/*
	 * If the firmware is not able to return error responses, cancel all pending commands.
	 * Intentionally call this function while holding @group->lock to prevent any possible race
	 * conditions between canceling commands and the runtime submitting commands.
	 */
	if (edgetpu_firmware_is_not_responding(error_mask))
		edgetpu_ikv_cancel(group, error_mask);

	up_write(&group->lock);
	edgetpu_group_notify(group, EDGETPU_EVENT_FATAL_ERROR);
}

/*
 * For each group active on @etdev: set the group status as errored, set the
 * error mask, and notify the runtime of the fatal error event.
 */
void edgetpu_fatal_error_notify(struct edgetpu_dev *etdev, uint error_mask)
{
	struct edgetpu_device_group *group;
	struct edgetpu_list_group *g;

	/* Consume all arrived responses first before each group cancels pending commands. */
	if (edgetpu_firmware_is_not_responding(error_mask))
		edgetpu_ikv_flush_responses(etdev->etikv);

	mutex_lock(&etdev->groups_lock);
	etdev_for_each_group(etdev, g, group) {
		if (edgetpu_device_group_is_disbanded(group))
			continue;
		edgetpu_group_fatal_error_notify(group, error_mask);
	}
	mutex_unlock(&etdev->groups_lock);

	/* Flush any pending IIF signals. */
	edgetpu_iif_reinit_mailbox(etdev->etiif);
}

uint edgetpu_group_get_fatal_errors(struct edgetpu_device_group *group)
{
	uint fatal_errors;

	down_write(&group->lock);
	fatal_errors = edgetpu_group_get_fatal_errors_locked(group);
	up_write(&group->lock);
	return fatal_errors;
}

void edgetpu_group_detach_mailbox_locked(struct edgetpu_device_group *group)
{
	if (edgetpu_group_mailbox_detached_locked(group))
		return;

	edgetpu_mailbox_remove_vii(&group->vii);

	if (group->mailbox_detachable)
		edgetpu_mmu_detach_domain(group->etdev, group->etdomain);

	group->mailbox_attached = false;
}

void edgetpu_group_close_and_detach_mailbox(struct edgetpu_device_group *group)
{
	down_write(&group->lock);
	/*
	 * Only a finalized group may have mailbox attached.
	 *
	 * Detaching mailbox for an errored group is also fine.
	 */
	if (is_finalized_or_errored(group)) {
		edgetpu_group_deactivate(group);
		/*
		 * TODO(b/312575591) Flush pending reverse KCI traffic before detaching the mailbox.
		 * This is necessary since detaching the mailbox may change the group's domain's
		 * PASID, which some rKCI commands use to identify a client.
		 *
		 * The group must be unlocked in case the rKCI handlers need the lock. This is safe
		 * because this thread continues to hold the owning `client`'s lock, preventing any
		 * other threads from trying to reattach the mailbox via either the
		 * EDGETPU_FINALIZE_GROUP or EDGETPU_ACQUIRE_WAKE_LOCK ioctls.
		 */
		up_write(&group->lock);
		edgetpu_kci_flush_rkci(group->etdev);
		down_write(&group->lock);
		edgetpu_group_detach_mailbox_locked(group);
		edgetpu_group_deactivate_external_mailbox(group);
	}
	up_write(&group->lock);
}

int edgetpu_group_attach_mailbox_locked(struct edgetpu_device_group *group)
{
	int ret;

	if (!edgetpu_group_mailbox_detached_locked(group))
		return 0;

	if (group->mailbox_detachable) {
		ret = edgetpu_mmu_attach_domain(group->etdev, group->etdomain);
		if (ret)
			return ret;
	}

	ret = edgetpu_mailbox_init_vii(&group->vii, group);
	if (ret) {
		if (group->mailbox_detachable)
			edgetpu_mmu_detach_domain(group->etdev, group->etdomain);
		return ret;
	}

	group->mailbox_attached = true;

	return 0;
}

int edgetpu_group_attach_and_open_mailbox(struct edgetpu_device_group *group)
{
	int ret = 0;

	down_write(&group->lock);
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
	up_write(&group->lock);
	return ret;
}

/* TODO(b/312575591) Simplify this function when the JOB_LOCKUP rKCI switches to client_id. */
/*
 * Return the group with @id of the given @type for device @etdev, with a reference held on the
 * group (must call edgetpu_device_group_put when done), or NULL if no group with that @id is found.
 */
enum id_type {
	EDGETPU_ID_TYPE_CLIENT_ID,
	EDGETPU_ID_TYPE_VCID,
};
static struct edgetpu_device_group *get_group_by_id(struct edgetpu_dev *etdev, u32 id,
						    enum id_type type)
{
	struct edgetpu_device_group *group = NULL;
	struct edgetpu_device_group *tgroup;
	struct edgetpu_list_group *g;
	u32 tgroup_id;
	struct edgetpu_iommu_domain *etdomain __maybe_unused;

	mutex_lock(&etdev->groups_lock);
	etdev_for_each_group(etdev, g, tgroup) {
		switch (type) {
		case EDGETPU_ID_TYPE_CLIENT_ID:
			down_write(&tgroup->lock);
			etdomain = edgetpu_group_domain_locked(tgroup);
			if (!etdomain)
				tgroup_id = IOMMU_PASID_INVALID;
			else
				tgroup_id = etdomain->pasid;
			up_write(&tgroup->lock);
			break;
		case EDGETPU_ID_TYPE_VCID:
			tgroup_id = tgroup->vcid;
			break;
		}
		if (tgroup_id == id) {
			group = edgetpu_device_group_get(tgroup);
			break;
		}
	}
	mutex_unlock(&etdev->groups_lock);
	return group;
}

void edgetpu_handle_client_fatal_error_notify(struct edgetpu_dev *etdev, u32 client_id)
{
	u32 client_pasid = FIELD_GET(CLIENT_ID_PASID, client_id);
	u32 client_realm = FIELD_GET(CLIENT_ID_REALM, client_id);
	u32 client_vm = FIELD_GET(CLIENT_ID_VM, client_id);
	struct edgetpu_device_group *group;

	etdev_err(etdev, "firmware reported fatal error for realm %u vm %u pasid %u",
		  client_realm, client_vm, client_pasid);
	if (client_realm != CLIENT_REALM_NS)
		return;
	group = get_group_by_id(etdev, client_id, EDGETPU_ID_TYPE_CLIENT_ID);
	if (!group) {
		etdev_warn(etdev, "pasid %u group not found", client_pasid);
		return;
	}
	edgetpu_group_fatal_error_notify(group, EDGETPU_ERROR_CLIENT_CONTEXT_CRASH);
	edgetpu_device_group_put(group);
}

void edgetpu_handle_client_inactivity_timeout(struct edgetpu_dev *etdev, u32 client_id)
{
	u32 client_pasid = FIELD_GET(CLIENT_ID_PASID, client_id);
	u32 client_realm = FIELD_GET(CLIENT_ID_REALM, client_id);
	u32 client_vm = FIELD_GET(CLIENT_ID_VM, client_id);
	struct edgetpu_device_group *group;
	struct edgetpu_client *client;
	struct timespec64 wake_duration;

	etdev_warn(etdev, "firmware reported inactivity timeout for realm %u vm %u pasid %u",
		   client_realm, client_vm, client_pasid);
	if (client_realm != CLIENT_REALM_NS)
		return;
	group = get_group_by_id(etdev, client_id, EDGETPU_ID_TYPE_CLIENT_ID);
	if (!group) {
		etdev_warn(etdev, "pasid %u group not found", client_pasid);
		return;
	}

	client = group->client;
	wake_duration.tv_sec = 0;

	if (client->wakelock.req_count) {
		ktime_get_ts64(&wake_duration);
		wake_duration = timespec64_sub(wake_duration,
					       client->wakelock.current_acquire_timestamp);
	}

	etdev_warn(etdev, "group %u client pid %d tgid %d wake count=%d dur=%ld sec\n",
		   group->group_id, client->pid, client->tgid,
		   client->wakelock.req_count, (unsigned long)wake_duration.tv_sec);
	edgetpu_device_group_put(group);
}

void edgetpu_handle_job_lockup(struct edgetpu_dev *etdev, u16 vcid)
{
	struct edgetpu_device_group *group;

	etdev_err(etdev, "firmware-detected job lockup on VCID %u", vcid);
	group = get_group_by_id(etdev, vcid, EDGETPU_ID_TYPE_VCID);
	if (!group) {
		etdev_warn(etdev, "VCID %u group not found", vcid);
		return;
	}
	edgetpu_group_fatal_error_notify(group, EDGETPU_ERROR_RUNTIME_TIMEOUT);
	edgetpu_device_group_put(group);
}

int edgetpu_device_group_track_fence_task(struct edgetpu_device_group *group,
					  struct task_struct *task)
{
	struct pending_command_task *pending_task;
	unsigned long flags;

	pending_task = kzalloc(sizeof(*pending_task), GFP_KERNEL);
	if (!pending_task)
		return -ENOMEM;

	pending_task->task = task;

	spin_lock_irqsave(&group->pending_cmd_tasks_lock, flags);
	list_add_tail(&pending_task->list_entry, &group->pending_cmd_tasks);
	spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);

	return 0;
}

void edgetpu_device_group_untrack_fence_task(struct edgetpu_device_group *group,
					     struct task_struct *task)
{
	struct list_head *cur, *nxt;
	struct pending_command_task *pending_task;
	unsigned long flags;

	spin_lock_irqsave(&group->pending_cmd_tasks_lock, flags);

	if (group->is_clearing_pending_commands) {
		spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);
		/*
		 * Wait until the release handler has requested this task stop so it doesn't
		 * disappear out from under the release handler.
		 */
		while (!kthread_should_stop())
			msleep(20);
		return;
	}

	list_for_each_safe(cur, nxt, &group->pending_cmd_tasks) {
		pending_task = container_of(cur, struct pending_command_task, list_entry);
		if (pending_task->task == task) {
			list_del(&pending_task->list_entry);
			kfree(pending_task);
			goto out;
		}
	}

	etdev_err(group->etdev, "Attempt to untrack task which was not being tracked");

out:
	spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);
}

/*
 * Return the group with pasid @pasid for device @etdev, with a reference held on the group, or
 * NULL if no group with that pasid is found.
 */
static struct edgetpu_device_group *get_group_by_pasid(struct edgetpu_dev *etdev, uint pasid)
{
	struct edgetpu_device_group *group = NULL;
	struct edgetpu_device_group *tgroup;
	struct edgetpu_list_group *g;

	if (pasid == IOMMU_PASID_INVALID || !pasid)
		return NULL;

	mutex_lock(&etdev->groups_lock);
	etdev_for_each_group(etdev, g, tgroup) {
		if (tgroup->etdomain && tgroup->etdomain->pasid == pasid) {
			group = edgetpu_device_group_get(tgroup);
			break;
		}
	}
	mutex_unlock(&etdev->groups_lock);
	return group;
}

/*
 * Currently always returns a negative error to indicate caller should proceed with fault error
 * processing.
 */
int edgetpu_device_group_handle_fault(struct edgetpu_dev *etdev, u64 iova, uint pasid)
{
	struct edgetpu_device_group *group;
	struct edgetpu_mapping *map;

	group = get_group_by_pasid(etdev, pasid);
	if (!group) {
		etdev_dbg(etdev, "fault iova=%#llx pasid=%u group not found\n", iova, pasid);
		return -EIO;
	}

	edgetpu_mapping_lock(&group->host_mappings);
	map = edgetpu_mapping_find_iova_range(&group->host_mappings, iova);
	if (!map) {
		if (EDGETPU_REPORT_PAGE_FAULT_ERRORS)
			etdev_warn(etdev, "fault iova=%#llx pasid=%u not mapped\n", iova, pasid);
		else
			etdev_dbg(etdev, "fault iova=%#llx pasid=%u not mapped\n", iova, pasid);
		edgetpu_mapping_unlock(&group->host_mappings);
		edgetpu_device_group_put(group);
		return -EIO;
	}

	etdev_warn(etdev, "fault iova=%#llx pasid=%u mapped %s\n", iova, pasid,
		   edgetpu_dma_dir_rw_s(map->gcip_mapping->dir));
	edgetpu_mapping_unlock(&group->host_mappings);
	edgetpu_device_group_put(group);
	return -EIO;
}
