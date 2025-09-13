// SPDX-License-Identifier: GPL-2.0-only
/*
 * The manager of inter-IP fences.
 *
 * It manages the pool of fence IDs. The IIF driver device will initialize a manager and each IP
 * driver will fetch the manager from the IIF device.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#define pr_fmt(fmt) "iif: " fmt

#include <linux/atomic.h>
#include <linux/container_of.h>
#include <linux/dma-fence.h>
#include <linux/export.h>
#include <linux/hashtable.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/of.h>
#include <linux/rwsem.h>
#include <linux/slab.h>

#include <iif/iif-direct.h>
#include <iif/iif-fence-table.h>
#include <iif/iif-fence.h>
#include <iif/iif-manager.h>
#include <iif/iif-shared.h>
#include <iif/iif.h>

static int iif_manager_set_direct_fence_ops(struct iif_manager *mgr, struct iif_fence *iif)
{
	if (!mgr->direct_fence_ops)
		return -EOPNOTSUPP;

	iif->fence_ops = mgr->direct_fence_ops;
	iif->driver_data = mgr;

	return 0;
}

static int iif_manager_set_sync_unit_fence_ops(struct iif_manager *mgr, struct iif_fence *iif)
{
	int ret = 0;

	down_read(&mgr->fence_ops_sema);

	if (!mgr->fence_ops) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	iif->fence_ops = mgr->fence_ops;
	iif->driver_data = mgr->driver_data;
out:
	up_read(&mgr->fence_ops_sema);

	return ret;
}

static void iif_manager_destroy(struct kref *kref)
{
	struct iif_manager *mgr = container_of(kref, struct iif_manager, kref);

	ida_destroy(&mgr->idp);
	kfree(mgr);
}

/* Validates @ops. Returns true if operators are valid. */
static bool iif_manager_validate_ops(const struct iif_manager_fence_ops *ops)
{
	return ops->sync_unit_name && ops->fence_create && ops->fence_retire && ops->fence_signal &&
	       ops->add_poll_cb && ops->remove_poll_cb;
}

struct iif_manager *iif_manager_init(const struct device_node *np)
{
	struct iif_manager *mgr;
	int ret;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return ERR_PTR(-ENOMEM);

	ret = iif_fence_table_init(np, &mgr->fence_table);
	/*
	 * The IIF driver can support direct fences only if it succeeds in initializing the fence
	 * table.
	 */
	if (!ret)
		mgr->direct_fence_ops = &iif_direct_fence_ops;

	kref_init(&mgr->kref);
	ida_init(&mgr->idp);
	init_rwsem(&mgr->ops_sema);
	init_rwsem(&mgr->fence_ops_sema);

	mgr->dma_fence_context = dma_fence_context_alloc(1);
	atomic64_set(&mgr->dma_fence_seqno, 0);
	hash_init(mgr->id_to_fence);
	rwlock_init(&mgr->id_to_fence_lock);

	return mgr;
}

struct iif_manager *iif_manager_get(struct iif_manager *mgr)
{
	kref_get(&mgr->kref);
	return mgr;
}

void iif_manager_put(struct iif_manager *mgr)
{
	kref_put(&mgr->kref, iif_manager_destroy);
}

int iif_manager_register_ops(struct iif_manager *mgr, enum iif_ip_type ip,
			     const struct iif_manager_ops *ops, void *data)
{
	if (!ops || !ops->fence_unblocked || ip >= IIF_IP_NUM)
		return -EINVAL;

	down_write(&mgr->ops_sema);

	mgr->ops[ip] = ops;
	mgr->data[ip] = data;

	up_write(&mgr->ops_sema);

	return 0;
}

void iif_manager_unregister_ops(struct iif_manager *mgr, enum iif_ip_type ip)
{
	if (ip >= IIF_IP_NUM)
		return;

	down_write(&mgr->ops_sema);

	mgr->ops[ip] = NULL;
	mgr->data[ip] = NULL;

	up_write(&mgr->ops_sema);
}

int iif_manager_register_fence_ops(struct iif_manager *mgr, const struct iif_manager_fence_ops *ops,
				   void *data)
{
	int ret = 0;

	if (!iif_manager_validate_ops(ops))
		return -EINVAL;

	down_write(&mgr->fence_ops_sema);

	if (mgr->fence_ops) {
		ret = -EPERM;
		goto out;
	}

	mgr->fence_ops = ops;
	mgr->driver_data = data;
out:
	up_write(&mgr->fence_ops_sema);

	return ret;
}

void iif_manager_unregister_fence_ops(struct iif_manager *mgr)
{
	down_write(&mgr->fence_ops_sema);

	mgr->fence_ops = NULL;
	mgr->driver_data = NULL;

	up_write(&mgr->fence_ops_sema);
}

int iif_manager_set_fence_ops(struct iif_manager *mgr, struct iif_fence *iif,
			      const struct iif_fence_params *params)
{
	if (params->flags & IIF_FLAGS_DIRECT)
		return iif_manager_set_direct_fence_ops(mgr, iif);
	return iif_manager_set_sync_unit_fence_ops(mgr, iif);
}

void iif_manager_unset_fence_ops(struct iif_manager *mgr, struct iif_fence *iif)
{
	if (!iif->fence_ops)
		return;

	iif->fence_ops = NULL;
	iif->driver_data = NULL;
}

int iif_manager_acquire_block_wakelock(struct iif_manager *mgr, enum iif_ip_type ip)
{
	int ret = 0;

	if (ip >= IIF_IP_NUM)
		return -EINVAL;

	down_read(&mgr->ops_sema);

	if (mgr->ops[ip] && mgr->ops[ip]->acquire_block_wakelock)
		ret = mgr->ops[ip]->acquire_block_wakelock(mgr->data[ip]);

	up_read(&mgr->ops_sema);

	return ret;
}

void iif_manager_release_block_wakelock(struct iif_manager *mgr, enum iif_ip_type ip)
{
	if (ip >= IIF_IP_NUM)
		return;

	down_read(&mgr->ops_sema);

	if (mgr->ops[ip] && mgr->ops[ip]->release_block_wakelock)
		mgr->ops[ip]->release_block_wakelock(mgr->data[ip]);

	up_read(&mgr->ops_sema);
}

void iif_manager_broadcast_fence_unblocked(struct iif_manager *mgr, struct iif_fence *fence)
{
	enum iif_ip_type ip;
	unsigned int tmp;

	down_read(&mgr->ops_sema);

	for_each_waiting_ip(&mgr->fence_table, fence->id, ip, tmp) {
		if (!mgr->ops[ip] || !mgr->ops[ip]->fence_unblocked) {
			pr_warn("IP driver hasn't registered fence_unblocked, ip=%d\n", ip);
			continue;
		}
		mgr->ops[ip]->fence_unblocked(fence, mgr->data[ip]);
	}

	up_read(&mgr->ops_sema);
}

void iif_manager_add_fence_to_hlist(struct iif_manager *mgr, struct iif_fence *fence)
{
	unsigned long flags;

	write_lock_irqsave(&mgr->id_to_fence_lock, flags);
	hash_add(mgr->id_to_fence, &fence->id_to_fence_node, fence->id);
	write_unlock_irqrestore(&mgr->id_to_fence_lock, flags);
}

void iif_manager_remove_fence_from_hlist(struct iif_manager *mgr, struct iif_fence *fence)
{
	unsigned long flags;

	write_lock_irqsave(&mgr->id_to_fence_lock, flags);
	hash_del(&fence->id_to_fence_node);
	write_unlock_irqrestore(&mgr->id_to_fence_lock, flags);
}

struct iif_fence *iif_manager_get_fence_from_id(struct iif_manager *mgr, int id)
{
	struct iif_fence *fence;
	unsigned long flags;

	read_lock_irqsave(&mgr->id_to_fence_lock, flags);

	hash_for_each_possible(mgr->id_to_fence, fence, id_to_fence_node, id) {
		if (fence->id == id) {
			iif_fence_get(fence);
			read_unlock_irqrestore(&mgr->id_to_fence_lock, flags);
			return fence;
		}
	}

	read_unlock_irqrestore(&mgr->id_to_fence_lock, flags);

	return NULL;
}
