/*
 * Google LWIS Transaction Processor
 *
 * Copyright (c) 2019 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LWIS_TRANSACTION_H_
#define LWIS_TRANSACTION_H_

#include "lwis_commands.h"

/* LWIS forward declarations */
struct lwis_device;
struct lwis_client;
struct lwis_fence;

/*
 * Transaction entry. Each entry belongs to two queues:
 * 1) Event list: Transactions are sorted by event IDs. This is to search for
 *    the appropriate transactions to trigger.
 * 2) Process queue: When it's time to process, the transaction will be put
 *    into a queue.
 */
struct lwis_transaction {
	struct lwis_transaction_info_v2 info;
	struct lwis_transaction_response_header *resp;
	struct list_head event_list_node;
	struct list_head process_queue_node;
	struct hlist_node pending_map_node;
	int signaled_count;
	/*
	 * Flag used for level trigger conditions, indicating the transaction
	 * should be queued right after creation
	 */
	bool queue_immediately;
	/*
	 * temporary variables to add support for mixing events and fences in
	 * trigger_condition. Will be removed and refactor into an union soon.
	 */
	bool is_weak_transaction;
	int64_t id;
	/* List of fences's fp that's referenced by the transaction */
	int num_trigger_fences;
	struct file *trigger_fence_fps[LWIS_TRIGGER_NODES_MAX_NUM];
	/* Parameters for completion fences */
	struct list_head completion_fence_list;
	/* Precondition fence file pointer */
	struct file *precondition_fence_fp;
	/*
	 * If the transaction has more entries to process than the transaction_process_limit
	 * for the processing device, then this will save the number of entries that are
	 * remaining to be processed after a given transaction process cycle
	 */
	int remaining_entries_to_process;
	/*
	 * Starting read buffer pointer is set to the last read location when the transaction
	 * process limit has reached. During the next run for the transaction, this pointer
	 * will be referred to correctly point to the read buffer for the run.
	 */
	uint8_t *starting_read_buf;
	/* The timestamp of the transaction trigger event */
	int64_t triggered_event_timestamp;
};

/*
 * For debugging purposes, keeps track of the transaction information, as
 * well as the time it executes and the time it took to execute.
 */
struct lwis_transaction_history {
	struct lwis_transaction_info_v2 info;
	int64_t process_timestamp;
	int64_t process_duration_ns;
};

struct lwis_transaction_event_list {
	int64_t event_id;
	struct list_head list;
	struct hlist_node node;
};

struct lwis_pending_transaction_id {
	int64_t id;
	struct list_head list_node;
};

int lwis_transaction_init(struct lwis_client *client);
int lwis_transaction_clear(struct lwis_client *client);
int lwis_transaction_client_flush(struct lwis_client *client);
int lwis_transaction_client_cleanup(struct lwis_client *client);

int lwis_transaction_event_trigger(struct lwis_client *client, int64_t event_id,
				   int64_t event_counter, int64_t event_timestamp,
				   struct list_head *pending_events);
void lwis_transaction_fence_trigger(struct lwis_client *client, struct lwis_fence *fence,
				    struct list_head *transaction_list);

int lwis_transaction_cancel(struct lwis_client *client, int64_t id);

void lwis_transaction_free(struct lwis_device *lwis_dev, struct lwis_transaction **ptransaction);

/*
 * Expects lwis_client->transaction_lock to be acquired before calling
 * the following functions. */
int lwis_transaction_submit_locked(struct lwis_client *client,
				   struct lwis_transaction *transaction);
int lwis_transaction_replace_locked(struct lwis_client *client,
				    struct lwis_transaction *transaction);

int lwis_trigger_event_add_weak_transaction(struct lwis_client *client, int64_t transaction_id,
					    int64_t event_id, int32_t precondition_fence_fd);

void lwis_process_transactions_in_queue(struct lwis_client *client);

#endif /* LWIS_TRANSACTION_H_ */
