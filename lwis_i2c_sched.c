// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS I2C Bus Manager Scheduler
 *
 * Copyright 2023 Google LLC.
 */
#define pr_fmt(fmt) KBUILD_MODNAME "-i2c-sched: " fmt

#include "lwis_i2c_sched.h"
#include "lwis_i2c_bus_manager.h"

/*
 * lwis_i2c_process_request_queue_is_empty:
 * Checks if the I2C process request queue is empty
*/
bool lwis_i2c_process_request_queue_is_empty(struct lwis_i2c_process_queue *process_queue)
{
	return (!process_queue || (process_queue && (process_queue->number_of_nodes == 0)));
}

/*
 * lwis_i2c_process_request_queue_initialize:
 * Initializes the I2C process request queue for a given I2C Bus
*/
void lwis_i2c_process_request_queue_initialize(struct lwis_i2c_process_queue *process_queue)
{
	process_queue->number_of_nodes = 0;
	INIT_LIST_HEAD(&process_queue->head);
}

/*
 * lwis_i2c_process_request_queue_destroy:
 * Frees all the requests in the queue
*/
void lwis_i2c_process_request_queue_destroy(struct lwis_i2c_process_queue *process_queue)
{
	struct list_head *request;
	struct list_head *request_tmp;
	struct lwis_i2c_process_request *process_request;

	if (!process_queue)
		return;

	if (lwis_i2c_process_request_queue_is_empty(process_queue))
		return;

	list_for_each_safe (request, request_tmp, &process_queue->head) {
		process_request =
			list_entry(request, struct lwis_i2c_process_request, request_node);
		list_del(&process_request->request_node);
		process_request->requesting_client = NULL;
		kfree(process_request);
		process_queue->number_of_nodes--;
	}
}