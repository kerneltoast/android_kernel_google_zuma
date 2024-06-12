/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS I2C Bus Manager Scheduler
 *
 * Copyright 2023 Google LLC.
 */

#ifndef LWIS_I2C_SCHED_H_
#define LWIS_I2C_SCHED_H_

#include "lwis_device.h"

struct lwis_i2c_process_queue;

/* lwis_i2c_process_request:
 * This maintains the node to identify the devices that
 * have a request to be processed on a given I2C bus */
struct lwis_i2c_process_request {
	struct lwis_client *requesting_client;
	struct list_head request_node;
};

bool lwis_i2c_process_request_queue_is_empty(struct lwis_i2c_process_queue *process_queue);

void lwis_i2c_process_request_queue_initialize(struct lwis_i2c_process_queue *process_queue);

void lwis_i2c_process_request_queue_destroy(struct lwis_i2c_process_queue *process_queue);

#endif /* LWIS_I2C_SCHED_H_ */