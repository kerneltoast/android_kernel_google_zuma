// SPDX-License-Identifier: GPL-2.0-only
/*
 * Interface to utilize IIF fence tables, the wait table and the signal table. Both tables will have
 * one entry per fence ID.
 *
 * - Wait table: Describes which IPs are waiting on each fence. This table will be written by the
 *               kernel driver only.
 *
 * - Signal table: Describes how many signals are remaining to unblock each fence. This table will
 *                 be initialized by the kernel driver and each signaler IP will update it.
 *
 * Copyright (C) 2023-2025 Google LLC
 */

#define pr_fmt(fmt) "iif: " fmt

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <iif/iif-fence-table.h>
#include <iif/iif-shared.h>

#define IIF_FENCE_WAIT_TABLE_PROP_NAME "iif-fence-wait-table-region"
#define IIF_FENCE_SIGNAL_TABLE_PROP_NAME "iif-fence-signal-table-region"

static int iif_fence_table_get_resource(const struct device_node *np, const char *name,
					struct resource *r)
{
	int ret;
	struct device_node *iif_np;

	iif_np = of_parse_phandle(np, name, 0);
	if (IS_ERR_OR_NULL(iif_np))
		return -ENODEV;

	ret = of_address_to_resource(iif_np, 0, r);
	of_node_put(iif_np);

	return ret;
}

static int iif_fence_wait_table_init(const struct device_node *np,
				     struct iif_fence_table *fence_table)
{
	struct resource r;
	size_t table_size;
	void *vaddr;
	int ret;

	ret = iif_fence_table_get_resource(np, IIF_FENCE_WAIT_TABLE_PROP_NAME, &r);
	if (ret) {
		pr_err("Failed to get the fence wait-table region\n");
		return ret;
	}

	table_size = IIF_IP_RESERVED * IIF_NUM_FENCES_PER_IP * sizeof(*fence_table->wait_table);

	if (resource_size(&r) < table_size) {
		pr_err("Unsufficient fence wait-table space in device tree\n");
		return -EINVAL;
	}

	vaddr = memremap(r.start, resource_size(&r), MEMREMAP_WC);
	if (IS_ERR_OR_NULL(vaddr)) {
		pr_err("Failed to map the fence wait-table region\n");
		return -ENODEV;
	}

	fence_table->wait_table = vaddr;

	return 0;
}

static int iif_fence_signal_table_init(const struct device_node *np,
				       struct iif_fence_table *fence_table)
{
	struct resource r;
	size_t table_size;
	void *vaddr;
	int ret;

	ret = iif_fence_table_get_resource(np, IIF_FENCE_SIGNAL_TABLE_PROP_NAME, &r);
	if (ret) {
		pr_err("Failed to get the fence signal-table region\n");
		return ret;
	}

	table_size = IIF_IP_RESERVED * IIF_NUM_FENCES_PER_IP * sizeof(*fence_table->signal_table);

	if (resource_size(&r) < table_size) {
		pr_err("Unsufficient fence signal-table space in device tree\n");
		return -EINVAL;
	}

	vaddr = memremap(r.start, resource_size(&r), MEMREMAP_WC);
	if (IS_ERR_OR_NULL(vaddr)) {
		pr_err("Failed to map the fence signal-table region\n");
		return -ENODEV;
	}

	fence_table->signal_table = vaddr;

	return 0;
}

int iif_fence_table_init(const struct device_node *np, struct iif_fence_table *fence_table)
{
	int ret;

	ret = iif_fence_wait_table_init(np, fence_table);
	if (ret)
		return ret;

	ret = iif_fence_signal_table_init(np, fence_table);

	return ret;
}

void iif_fence_table_init_single_shot_fence_entry(struct iif_fence_table *fence_table,
						  unsigned int fence_id,
						  unsigned int total_signalers, u8 waiters)
{
	fence_table->wait_table[fence_id].waiting_ips = waiters;
	fence_table->wait_table[fence_id].reusable = 0;
	fence_table->signal_table[fence_id].remaining_signals = total_signalers;
	fence_table->signal_table[fence_id].flag = 0;
}

void iif_fence_table_init_reusable_fence_entry(struct iif_fence_table *fence_table,
					       unsigned int fence_id, u16 timeout, u8 waiters)
{
	fence_table->wait_table[fence_id].waiting_ips = waiters;
	fence_table->wait_table[fence_id].reusable = BIT(0);
	memset(fence_table->wait_table[fence_id].sync_points, 0,
	       sizeof(fence_table->wait_table[fence_id].sync_points));

	fence_table->signal_table[fence_id].timeline = 0;
	fence_table->signal_table[fence_id].flag = 0;
	fence_table->signal_table[fence_id].timeout = timeout;
}

void iif_fence_table_set_waiting_ip(struct iif_fence_table *fence_table, unsigned int fence_id,
				    enum iif_ip_type ip)
{
	fence_table->wait_table[fence_id].waiting_ips |= BIT(ip);
}

u8 iif_fence_table_get_waiting_ip(struct iif_fence_table *fence_table, unsigned int fence_id)
{
	return fence_table->wait_table[fence_id].waiting_ips;
}

void iif_fence_table_set_sync_point(struct iif_fence_table *fence_table, unsigned int fence_id,
				    u8 sync_point_index, u8 start_timeline, u8 count)
{
	struct iif_wait_table_reusable_sync_point *sync_point;

	if (sync_point_index >= IIF_NUM_SYNC_POINTS)
		return;

	sync_point = &fence_table->wait_table[fence_id].sync_points[sync_point_index];

	sync_point->start_timeline = start_timeline;
	sync_point->count = count;
}

void iif_fence_table_get_sync_point(struct iif_fence_table *fence_table, unsigned int fence_id,
				    u8 sync_point_index, u8 *start_timeline, u8 *count)
{
	struct iif_wait_table_reusable_sync_point *sync_point;

	if (sync_point_index >= IIF_NUM_SYNC_POINTS)
		return;

	sync_point = &fence_table->wait_table[fence_id].sync_points[sync_point_index];

	*start_timeline = sync_point->start_timeline;
	*count = sync_point->count;
}

void iif_fence_table_set_remaining_signals(struct iif_fence_table *fence_table,
					   unsigned int fence_id, unsigned int remaining_signalers)
{
	/*
	 * If the signaler is an IP and it becomes faulty which will let the IP driver to update
	 * @fence->propagate to true, there can be a race condition that the IP already signaled the
	 * fence and updated the fence table right before it crashes. Therefore, we should check the
	 * value in the fence table first to see whether the fence was signaled more times by the IP
	 * compared to the kernel perspective. If it was, we should ignore updating the table.
	 */
	if (fence_table->signal_table[fence_id].remaining_signals > remaining_signalers)
		fence_table->signal_table[fence_id].remaining_signals = remaining_signalers;
}

unsigned int iif_fence_table_get_remaining_signals(struct iif_fence_table *fence_table,
						   unsigned int fence_id)
{
	return fence_table->signal_table[fence_id].remaining_signals;
}

void iif_fence_table_inc_timeline(struct iif_fence_table *fence_table, unsigned int fence_id)
{
	fence_table->signal_table[fence_id].timeline++;
}

unsigned int iif_fence_table_get_timeline(struct iif_fence_table *fence_table,
					  unsigned int fence_id)
{
	return fence_table->signal_table[fence_id].timeline;
}

unsigned int iif_fence_table_get_timeout(struct iif_fence_table *fence_table, unsigned int fence_id)
{
	return fence_table->signal_table[fence_id].timeout;
}

void iif_fence_table_set_flag(struct iif_fence_table *fence_table, unsigned int fence_id, u8 flag)
{
	fence_table->signal_table[fence_id].flag = flag;
}

u8 iif_fence_table_get_flag(struct iif_fence_table *fence_table, unsigned int fence_id)
{
	return fence_table->signal_table[fence_id].flag;
}
