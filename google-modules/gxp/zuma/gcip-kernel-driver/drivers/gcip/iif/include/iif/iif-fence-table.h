/* SPDX-License-Identifier: GPL-2.0-only */
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

#ifndef __IIF_IIF_FENCE_TABLE_H__
#define __IIF_IIF_FENCE_TABLE_H__

#include <linux/types.h>

#include <iif/iif-shared.h>

#define IIF_CLEAR_LSB(b) ((b) & ((b) - 1))

/*
 * Iterate IPs in a bitwise IP list. (See enum iif_ip_type)
 *
 * ips (input): The bitwise IP list whose each bit represents an IP.
 * ip (output): An IP which is set in @ips (enum iif_ip_type).
 * tmp (output): Temporary variable to iterate @ips (int).
 */
#define for_each_ip(ips, ip, tmp) \
	for (tmp = ips, ip = __ffs(tmp); tmp; tmp = IIF_CLEAR_LSB(tmp), ip = __ffs(tmp))

/*
 * Iterates the type of IPs waiting on the fence of @fence_id.
 *
 * fence_table (input): Pointer to the fence table.
 * fence_id (input): ID of the fence to get IPs waiting on it.
 * waiting_ip (output): Type of IP waiting on the fence (enum iif_ip_type).
 * tmp (output): Temporary variable to iterate the wait table entry (int).
 */
#define for_each_waiting_ip(fence_table, fence_id, waiting_ip, tmp) \
	for_each_ip(iif_fence_table_get_waiting_ip(fence_table, fence_id), waiting_ip, tmp)

/* The fence table which will be shared with the firmware side. */
struct iif_fence_table {
	struct iif_wait_table_entry *wait_table;
	struct iif_signal_table_entry *signal_table;
};

/*
 * Parses the fence table region from the device tree and map it to @fence_table.
 *
 * Returns 0 if succeeded. If it fails in mapping the table, returns -ENODEV.
 */
int iif_fence_table_init(const struct device_node *np, struct iif_fence_table *fence_table);

/**
 * iif_fence_table_init_single_shot_fence_entry() - Initializes the entry of @fence_id in the fence
 *                                                  table for single-shot fence.
 * @fence_table: The fence table object.
 * @fence_id: The fence ID.
 * @total_signalers: The total number of signalers.
 * @waiters: The bitwise value where each bit represents an IP.
 *
 * Since this function will be called only when the fence is initialized, we don't need any locks
 * to protect the entry.
 */
void iif_fence_table_init_single_shot_fence_entry(struct iif_fence_table *fence_table,
						  unsigned int fence_id,
						  unsigned int total_signalers, u8 waiters);

/**
 * iif_fence_table_init_reusable_fence_entry() -  Initializes the entry of @fence_id in the fence
 *                                                table for reusable fence.
 * @fence_table: The fence table object.
 * @fence_id: The fence ID.
 * @timeout: The timeout.
 * @waiters: The bitwise value where each bit represents an IP.
 *
 * Since this function will be called only when the fence is initialized, we don't need any locks
 * to protect the entry.
 */
void iif_fence_table_init_reusable_fence_entry(struct iif_fence_table *fence_table,
					       unsigned int fence_id, u16 timeout, u8 waiters);

/*
 * Sets waiting IP bit of the wait table entry of @fence_id.
 *
 * Since this function will be called by the `iif_fence_submit_waiter` function which protects the
 * entry by itself with holding its lock, we don't have to hold any locks here.
 */
void iif_fence_table_set_waiting_ip(struct iif_fence_table *fence_table, unsigned int fence_id,
				    enum iif_ip_type ip);

/* Gets the bitwise value where each bit represents a waiter IP. (See enum iif_ip_type) */
u8 iif_fence_table_get_waiting_ip(struct iif_fence_table *fence_table, unsigned int fence_id);

/**
 * iif_fence_table_set_sync_point() - Add a sync point to the fence table.
 * @fence_id: The fence ID.
 * @sync_point_index: The index of a sync point to set. (0 based, max 2)
 * @start_timeline: The timeline value that starts notifying waiters.
 * @count: How many times waiters will be notified once the fence reached @start_timeline.
 *
 * If @count is `IIF_WAIT_TABLE_SYNC_POINT_COUNT_ALL`, the waiters will be notified for every
 * timeline increase from @start_timeline.
 */
void iif_fence_table_set_sync_point(struct iif_fence_table *fence_table, unsigned int fence_id,
				    u8 sync_point_index, u8 start_timeline, u8 count);

/**
 * iif_fence_table_get_sync_point() - Returns the sync point registered at @sync_point_index index.
 * @fence_id: The fence ID.
 * @sync_point_index: The index of a sync point to set. (0 based, max 2)
 * @start_timeline: The pointer to get the start timeline value.
 * @count: The pointer to get the count value.
 */
void iif_fence_table_get_sync_point(struct iif_fence_table *fence_table, unsigned int fence_id,
				    u8 sync_point_index, u8 *start_timeline, u8 *count);

/*
 * Sets the number of remaining signalers to the signal table entry of @fence_id.
 *
 * This function should be called when either
 * - the signaler of the fence is AP, or
 * - the signaler is an IP but the IP is under the situation that it can't update the table by
 *   itself.
 *
 * Since this function will be called by the `iif_fence_signal{_with_status}` function which
 * protects the entry by itself with holding its lock, we don't have to hold any locks here.
 */
void iif_fence_table_set_remaining_signals(struct iif_fence_table *fence_table,
					   unsigned int fence_id, unsigned int remaining_signalers);

/*
 * Gets the number of remaining signalers from the signal table entry of @fence_id.
 *
 * This function should be called when either
 * - the signaler of the fence is AP, or
 * - the signaler is an IP but the IP is under the situation that it can't update the table by
 *   itself.
 *
 * Since this function will be called by the `iif_fence_signal{_with_status}` function which
 * protects the entry by itself with holding its lock, we don't have to hold any locks here.
 */
unsigned int iif_fence_table_get_remaining_signals(struct iif_fence_table *fence_table,
						   unsigned int fence_id);

/**
 * iif_fence_table_inc_timeline() - Increases the timeline value of @fence_id fence.
 * @fence_table: The fence table object.
 * @fence_id: The fence ID.
 */
void iif_fence_table_inc_timeline(struct iif_fence_table *fence_table, unsigned int fence_id);

/**
 * iif_fence_table_inc_timeline() - Gets the current timeline value of @fence_id fence.
 * @fence_table: The fence table object.
 * @fence_id: The fence ID.
 */
unsigned int iif_fence_table_get_timeline(struct iif_fence_table *fence_table,
					  unsigned int fence_id);

/**
 * iif_fence_table_get_timeout() - Gets the timeout value of @fence_id fence.
 * @fence_table: The fence table object.
 * @fence_id: The fence ID.
 */
unsigned int iif_fence_table_get_timeout(struct iif_fence_table *fence_table,
					 unsigned int fence_id);

/*
 * Sets the fence flag to the signal table entry of @fence_id.
 * See `enum iif_flag_bits` to understand meaning of each bit of @flags.
 *
 * This function should be called when either
 * - the signaler of the fence is AP, or
 * - the signaler is an IP but the IP is under the situation that it can't update the table by
 *   itself.
 *
 * Since this function will be called by the `iif_fence_signal{_with_status}` function which
 * protects the entry by itself with holding its lock, we don't have to hold any locks here.
 */
void iif_fence_table_set_flag(struct iif_fence_table *fence_table, unsigned int fence_id, u8 flag);

/*
 * Gets the fence flag from the signal table entry of @fence_id.
 *
 * This function should be called when either
 * - the signaler of the fence is AP, or
 * - the signaler is an IP but the IP is under the situation that it can't update the table by
 *   itself.
 *
 * Since this function will be called by the `iif_fence_signal{_with_status}` function which
 * protects the entry by itself with holding its lock, we don't have to hold any locks here.
 */
u8 iif_fence_table_get_flag(struct iif_fence_table *fence_table, unsigned int fence_id);

#endif /* __IIF_IIF_FENCE_TABLE_H__ */
