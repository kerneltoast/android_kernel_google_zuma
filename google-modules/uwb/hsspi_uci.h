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
 * QM35 UCI layer HSSPI Protocol
 */

#ifndef __HSSPI_UCI_H__
#define __HSSPI_UCI_H__

#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include "hsspi.h"

/**
 * struct uci_packet - UCI packet that implements a &struct hsspi_block.
 * @blk: &struct hsspi_block
 * @write_done: norify when the packet has been really send
 * @list: link with &struct uci_layer.rx_list
 * @status: status of the transfer
 */
struct uci_packet {
	struct hsspi_block blk;
	struct completion *write_done;
	struct list_head list;
	u8 *data;
	int length;
	int status;
};

/**
 * uci_packet_alloc() - Allocate an UCI packet
 * @length: length of the UCI packet
 *
 * Allocate an UCI packet that can be used by the HSSPI driver in
 * order to send or receive an UCI packet.
 *
 * Return: a newly allocated &struct uci_packet or NULL
 */
struct uci_packet *uci_packet_alloc(u16 length);

/**
 * uci_packet_free() - Free an UCI packet
 * @p: pointer to the &struct uci_packet to free
 *
 */
void uci_packet_free(struct uci_packet *p);

/**
 * struct uci_layer - Implement an HSSPI Layer
 * @hlayer: &struct hsspi_layer
 * @rx_list: list of received UCI packets
 * @lock: protect the &struct uci_layer.rx_list
 * @wq: notify when the &struct uci_layer.rx_list is not empty
 */
struct uci_layer {
	struct hsspi_layer hlayer;
	struct list_head rx_list;
	struct mutex lock;
	wait_queue_head_t wq;
};

/**
 * uci_layer_init() - Initialize UCI Layer
 *
 */
int uci_layer_init(struct uci_layer *uci);

/**
 * uci_layer_deinit() - Deinitialize UCI Layer
 *
 */
void uci_layer_deinit(struct uci_layer *uci);

/**
 * uci_layer_has_data_availal() - checks if the layer has some rx packets
 * @uci: pointer to &struct uci_layer
 *
 * Function that checks if the UCI layer has some data waiting to be read.
 *
 * Return: true if some data is available, false otherwise.
 */
bool uci_layer_has_data_available(struct uci_layer *uci);

/**
 * uci_layer_read() - get a packet from the rx_list
 * @uci: pointer to &struct uci_layer
 * @max_size: maximum size possible for the UCI packet
 * @non_blocking: true if non blocking, false otherwise
 *
 * This function returns an UCI packet if available in the
 * &struct uci_layer.rx_list. The max_size argument logic is due to the way
 * the /dev/uci is make. We should ensure that we return an entire
 * packet in uci_read, so we must get a packet only if the caller has
 * enougth room for it.
 *
 * Return: a &struct uci_packet if succeed,
 *         -EINTR if it was interrupted (in blocking mode),
 *         -ESMGSIZE if the available UCI packet is bigger than max_size,
 *         -EAGAIN if there is no available UCI packet (in non blocking mode)
 */
struct uci_packet *uci_layer_read(struct uci_layer *uci, size_t max_size,
				  bool non_blocking);

#endif // __HSSPI_UCI_H__
