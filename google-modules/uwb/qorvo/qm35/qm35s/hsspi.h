/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file is part of the QM35 UCI stack for linux.
 *
 * Copyright (c) 2021 Qorvo US, Inc.
 *
 * This software is provided under the GNU General Public License, version 2
 * (GPLv2), as well as under a Qorvo commercial license.
 *
 * You may choose to use this software under the terms of the GPLv2 License,
 * version 2 ("GPLv2"), as published by the Free Software Foundation.
 * You should have received a copy of the GPLv2 along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * This program is distributed under the GPLv2 in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GPLv2 for more
 * details.
 *
 * If you cannot meet the requirements of the GPLv2, you may not use this
 * software for any purpose without first obtaining a commercial license from
 * Qorvo.
 * Please contact Qorvo to inquire about licensing terms.
 *
 * QM35 HSSPI Protocol
 */

#ifndef __HSSPI_H__
#define __HSSPI_H__

#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>

enum { UL_RESERVED,
       UL_BOOT_FLASH,
       UL_UCI_APP,
       UL_COREDUMP,
       UL_LOG,
       UL_TEST_HSSPI,
       UL_MAX_IDX };

struct stc_header {
	u8 flags;
	u8 ul;
	u16 length;
} __packed;

enum hsspi_work_type {
	HSSPI_WORK_TX = 0,
	HSSPI_WORK_COMPLETION,
};

/**
 * struct hsspi_block - Memory block used by the HSSPI.
 * @data: pointer to some memory
 * @length: requested length of the data
 * @size: size of the data (could be greater than length)
 *
 * This structure represents the memory used by the HSSPI driver for
 * sending or receiving message. Upper layer must provides the HSSPI
 * driver a way to allocate such structure for reception and uses this
 * structure for sending function.
 *
 * The goal here is to prevent useless copy.
 */
struct hsspi_block {
	void *data;
	u16 length;
	u16 size;
};

struct hsspi_layer;

/**
 * struct hsspi_layer_ops - Upper layer operations.
 *
 * @registered: Called when this upper layer is registered.
 * @unregistered: Called when unregistered.
 *
 * @get: Called when the HSSPI driver need some memory for
 * reception. This &struct hsspi_block will be give back to the upper
 * layer in the received callback.
 *
 * @received: Called when the HSSPI driver received some data for this
 * upper layer. In case of error, status is used to notify the upper
 * layer.
 *
 * @sent: Called when a &struct hsspi_block is sent by the HSSPI
 * driver. In case of error, status is used to notify the upper layer.
 *
 * Operation needed to be implemented by an upper layer. All ops are
 * called by the HSSPI driver and are mandatory.
 */
struct hsspi_layer_ops {
	int (*registered)(struct hsspi_layer *upper_layer);
	void (*unregistered)(struct hsspi_layer *upper_layer);

	struct hsspi_block *(*get)(struct hsspi_layer *upper_layer, u16 length);
	void (*received)(struct hsspi_layer *upper_layer,
			 struct hsspi_block *blk, int status);
	void (*sent)(struct hsspi_layer *upper_layer, struct hsspi_block *blk,
		     int status);
};

/**
 * struct hsspi_layer - HSSPI upper layer.
 * @name: Name of this upper layer.
 * @id: id (ul used in the STC header) of this upper layer
 * @ops: &struct hsspi_layer_ops
 *
 * Basic upper layer structure. Inherit from it to implement a
 * concrete upper layer.
 */
struct hsspi_layer {
	char *name;
	u8 id;
	const struct hsspi_layer_ops *ops;
};

enum hsspi_flags {
	HSSPI_FLAGS_SS_IRQ = 0,
	HSSPI_FLAGS_SS_READY = 1,
	HSSPI_FLAGS_SS_BUSY = 2,
	HSSPI_FLAGS_MAX = 3,
};

enum hsspi_state {
	HSSPI_RUNNING = 0,
	HSSPI_ERROR = 1,
	HSSPI_STOPPED = 2,
};

/**
 * struct hsspi - HSSPI driver.
 *
 * Some things need to be refine:
 * 1. a better way to disable/enable ss_irq or ss_ready GPIOs
 * 2. be able to change spi speed on the fly (for flashing purpose)
 *
 * Actually this structure should be abstract.
 *
 */
struct hsspi {
	spinlock_t lock; /* protect work_list, layers and state */
	spinlock_t flags_lock; /* protect flags */
	struct list_head work_list;
	struct hsspi_layer *layers[UL_MAX_IDX];
	enum hsspi_state state;

	DECLARE_BITMAP(flags, HSSPI_FLAGS_MAX);
	struct wait_queue_head wq;
	struct wait_queue_head wq_ready;
	struct task_struct *thread;

	// re-enable SS_IRQ
	void (*odw_cleared)(struct hsspi *hsspi);

	// wakeup QM35
	void (*wakeup)(struct hsspi *hsspi);

	// reset QM35
	void (*reset_qm35)(struct hsspi *hsspi);

	struct spi_device *spi;

	struct stc_header *host, *soc;
	ktime_t next_cs_active_time;

	struct gpio_desc *gpio_ss_rdy;
	struct gpio_desc *gpio_exton;

	volatile bool waiting_ss_rdy;
};

/**
 * hsspi_init() - Initialiaze the HSSPI
 * @hsspi: pointer to a &struct hsspi
 * @spi: pointer to the &struct spi_device used by the HSSPI for SPI
 * transmission
 *
 * Initialize the HSSPI structure.
 *
 * Return: 0 if no error or -errno.
 *
 */
int hsspi_init(struct hsspi *hsspi, struct spi_device *spi);
void hsspi_set_gpios(struct hsspi *hsspi, struct gpio_desc *gpio_ss_rdy,
		     struct gpio_desc *gpio_exton);

/**
 * hsspi_deinit() - Initialiaze the HSSPI
 * @hsspi: pointer to a &struct hsspi
 *
 * Deinitialize the HSSPI structure.
 *
 * Return: 0 if no error or -errno
 */
int hsspi_deinit(struct hsspi *hsspi);

/**
 * hsspi_register() - Register an upper layer
 * @hsspi: pointer to a &struct hsspi
 * @layer: pointer to a &struct hsspi_layer
 *
 * Register an upper layer.
 *
 * Return: 0 if no error or -errno.
 *
 */
int hsspi_register(struct hsspi *hsspi, struct hsspi_layer *layer);

/**
 * hsspi_unregister() - Unregister an upper layer
 * @hsspi: pointer to a &struct hsspi
 * @layer: pointer to a &struct hsspi_layer
 *
 * Unregister an upper layer.
 *
 * Return: 0 if no error or -errno.
 *
 */
int hsspi_unregister(struct hsspi *hsspi, struct hsspi_layer *layer);

/**
 * hsspi_set_spi_slave_ready() - tell the hsspi that the ss_ready is active
 * @hsspi: pointer to a &struct hsspi
 *
 * This function is called in the driver's init. It notices the
 * HSSPI driver that the QM is ready for transfer.
 */
void hsspi_set_spi_slave_ready(struct hsspi *hsspi);

/**
 * hsspi_set_spi_slave_ready_irq() - tell the hsspi that the ss_ready is active
 * in irq context,
 * @hsspi: pointer to a &struct hsspi
 *
 * This function is called in the ss_ready irq handler. It notices the
 * HSSPI driver that the QM is ready for transfer.
 */
void hsspi_set_spi_slave_ready_irq(struct hsspi *hsspi);

/**
 * hsspi_clear_spi_slave_ready() - tell the hsspi that the ss_ready has
 * been lowered meaning that the fw is busy or asleep,
 * @hsspi: pointer to a &struct hsspi
 */
void hsspi_clear_spi_slave_ready(struct hsspi *hsspi);

/**
 * hsspi_set_spi_slave_busy() - tell the hsspi that the ss_ready has
 * not been lowered and raised again meaning that the fw is busy,
 * @hsspi: pointer to a &struct hsspi
 */
void hsspi_set_spi_slave_busy(struct hsspi *hsspi);

/**
 * hsspi_clear_spi_slave_busy() - tell the hsspi that the ss_ready has
 * been lowered and raised again meaning that the fw is not busy anymore,
 * @hsspi: pointer to a &struct hsspi
 *
 * This function is called in the ss_ready irq handler. It notices the
 * HSSPI driver that the QM has acknowledged the last SPI xfer.
 */
void hsspi_clear_spi_slave_busy(struct hsspi *hsspi);

/**
 * hsspi_set_output_data_waiting() - tell the hsspi that the ss_irq is active
 * @hsspi: pointer to a &struct hsspi
 *
 * This function is called in the ss_irq irq handler. It notices the
 * HSSPI dirver that the QM has some date to outpput.
 *
 * The HSSPI must work with or without the ss_irq gpio. The current
 * implementation is far from ideal regarding this requirement.
 *
 * W/o the gpio we should send repeatly some 0-length write transfer
 * in order to check for ODW flag in SOC STC header.
 */
void hsspi_set_output_data_waiting(struct hsspi *hsspi);

/**
 * hsspi_init_block() - allocate a block data that suits HSSPI.
 *
 * @blk: point to a &struct hsspi_block
 * @length: block length
 *
 * Return: 0 or -ENOMEM on error
 */
int hsspi_init_block(struct hsspi_block *blk, u16 length);

/**
 * hsspi_deinit_block() - deallocate a block data.
 *
 * @blk: point to a &struct hsspi_block
 *
 */
void hsspi_deinit_block(struct hsspi_block *blk);

/**
 * hsspi_send() - send a &struct hsspi_block for a &struct hsspi_layer
 * layer.
 *
 * @hsspi: pointer to a &struct hsspi
 * @layer: pointer to a &struct hsspi_layer
 * @blk: pointer to a &struct hsspi_block
 *
 * Send the block `blk` of the upper layer `layer` on the `hsspi`
 * driver.
 *
 * Return: 0 if no error or -errno.
 *
 */
int hsspi_send(struct hsspi *hsspi, struct hsspi_layer *layer,
	       struct hsspi_block *blk);

/**
 * hsspi_start() - start the HSSPI
 *
 * @hsspi: pointer to a &struct hsspi
 *
 */
void hsspi_start(struct hsspi *hsspi);

/**
 * hsspi_stop() - stop the HSSPI
 *
 * @hsspi: pointer to a &struct hsspi
 *
 */
void hsspi_stop(struct hsspi *hsspi);

#endif // __HSSPI_H__
