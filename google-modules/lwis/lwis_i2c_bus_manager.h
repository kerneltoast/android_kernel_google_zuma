
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS I2C Bus Manager
 *
 * Copyright 2023 Google LLC.
 */

#ifndef LWIS_I2C_BUS_MANAGER_H_
#define LWIS_I2C_BUS_MANAGER_H_

#include "lwis_device.h"
#include "lwis_util.h"
#include "lwis_periodic_io.h"
#include "lwis_transaction.h"

/* enum lwis_i2c_device_priority_level:
 * Defines the I2C device priority level
 * in which the requests will be executed
 */
enum lwis_i2c_device_priority_level {
	I2C_DEVICE_HIGH_PRIORITY,
	I2C_DEVICE_MEDIUM_PRIORITY,
	I2C_DEVICE_LOW_PRIORITY,
	I2C_MAX_PRIORITY_LEVELS
};

/* enum lwis_i2c_client_connection:
 * Defines the I2C client connection status
 * being requested
 */
enum lwis_i2c_client_connection { I2C_CLIENT_CONNECT, I2C_CLIENT_DISCONNECT };

struct lwis_i2c_device;

/* struct lwis_i2c_bus_manager_list:
 * Holds I2C bus manager list */
struct lwis_i2c_bus_manager_list {
	struct list_head i2c_bus_manager_list_head;
};

/* struct lwis_i2c_bus_manager_identifier:
 * Holds a pointer to the I2C bus manager */
struct lwis_i2c_bus_manager_identifier {
	struct list_head i2c_bus_manager_list_node;
	struct lwis_i2c_bus_manager *i2c_bus_manager;
	int i2c_bus_manager_handle;
};

/* lwis_i2c_process_queue:
 * This maintains the process queue for a given I2C bus.
 * This is a collection of process request nodes that identify
 * the lwis device requests in order they were queued.
 * The scheduler is set to operate requests in a
 * first in-first out manner, starting and updating the head
 * and working towards the tail end. */
struct lwis_i2c_process_queue {
	/* Head node for the process queue */
	struct list_head head;
	/* Total number of devices that are queued to be processed */
	int number_of_nodes;
};

/*
 *  struct lwis_i2c_bus_manager
 *  This defines the main attributes for I2C Bus Manager.
 */
struct lwis_i2c_bus_manager {
	/* Unique identifier for this I2C bus manager */
	int i2c_bus_id;
	/* Name of I2C Bus manager corresponds to the name of the I2C Bus */
	char i2c_bus_name[LWIS_MAX_NAME_STRING_LEN];
	/* Lock to control access to bus transfers */
	struct mutex i2c_bus_lock;
	/* Lock to control access to the I2C process queue for this bus */
	struct mutex i2c_process_queue_lock;
	/* I2C Bus thread priority */
	u32 i2c_bus_thread_priority;
	/* Worker thread */
	struct kthread_worker i2c_bus_worker;
	struct task_struct *i2c_bus_worker_thread;
	/* Queue of all I2C devices that have data to transfer in their process queues */
	struct lwis_i2c_process_queue i2c_bus_process_queue[I2C_MAX_PRIORITY_LEVELS];
	/* List of I2C devices using this bus */
	struct list_head i2c_connected_devices;
	/* Total number of physically connected devices to the bus
	 * This count is set while probe/unprobe sequence */
	int number_of_connected_devices;
};

/* This maintains the structure to identify the connected devices to a given I2C bus.
 * This will be used to guard the bus against processing any illegal device entries */
struct lwis_i2c_connected_device {
	struct lwis_device *connected_device;
	struct list_head connected_device_node;
};

void lwis_i2c_bus_manager_lock_i2c_bus(struct lwis_device *lwis_dev);

void lwis_i2c_bus_manager_unlock_i2c_bus(struct lwis_device *lwis_dev);

struct lwis_i2c_bus_manager *lwis_i2c_bus_manager_get(struct lwis_device *lwis_dev);

int lwis_i2c_bus_manager_create(struct lwis_i2c_device *i2c_dev);

void lwis_i2c_bus_manager_disconnect(struct lwis_device *lwis_dev);

void lwis_i2c_bus_manager_process_worker_queue(struct lwis_client *client);

void lwis_i2c_bus_manager_flush_i2c_worker(struct lwis_device *lwis_dev);

void lwis_i2c_bus_manager_list_initialize(void);

void lwis_i2c_bus_manager_list_deinitialize(void);

int lwis_i2c_bus_manager_connect_client(struct lwis_client *connecting_client);

void lwis_i2c_bus_manager_disconnect_client(struct lwis_client *disconnecting_client);

#endif /* LWIS_I2C_BUS_MANAGER_H */
