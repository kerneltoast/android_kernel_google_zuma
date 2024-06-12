// SPDX-License-Identifier: GPL-2.0

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

#include "qm35.h"
#include "hsspi_uci.h"

struct uci_packet *uci_packet_alloc(u16 length)
{
	struct uci_packet *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return NULL;

	if (hsspi_init_block(&p->blk, length)) {
		kfree(p);
		return NULL;
	}

	p->data = p->blk.data;
	p->length = p->blk.length;
	return p;
}

void uci_packet_free(struct uci_packet *p)
{
	hsspi_deinit_block(&p->blk);
	kfree(p);
}

static int uci_registered(struct hsspi_layer *layer)
{
	return 0;
}

static void clear_rx_list(struct uci_layer *uci)
{
	struct uci_packet *p;

	mutex_lock(&uci->lock);

	while (!list_empty(&uci->rx_list)) {
		p = list_first_entry(&uci->rx_list, struct uci_packet, list);

		list_del(&p->list);

		uci_packet_free(p);
	}

	mutex_unlock(&uci->lock);

	wake_up_interruptible(&uci->wq);
}

static void uci_unregistered(struct hsspi_layer *hlayer)
{
	struct uci_layer *uci = container_of(hlayer, struct uci_layer, hlayer);

	clear_rx_list(uci);
}

static struct hsspi_block *uci_get(struct hsspi_layer *hlayer, u16 length)
{
	struct uci_packet *p;

	p = uci_packet_alloc(length);
	if (!p)
		return NULL;

	return &p->blk;
}

static void uci_sent(struct hsspi_layer *hlayer, struct hsspi_block *blk,
		     int status)
{
	struct uci_packet *p = container_of(blk, struct uci_packet, blk);

	p->status = status;
	complete(p->write_done);
}

#define UCI_CONTROL_PACKET_PAYLOAD_SIZE_LOCATION (3)

static size_t get_payload_size_from_header(const u8 *header)
{
	bool is_data_packet = ((header[0] >> 5) & 0x07) == 0;
	bool is_control_packet = !is_data_packet && ((header[0] >> 7) == 0);

	if (is_control_packet)
		return header[UCI_CONTROL_PACKET_PAYLOAD_SIZE_LOCATION];

	return (header[3] << 8) | header[2];
}

#define UCI_PACKET_HEADER_SIZE (4)

static void uci_received(struct hsspi_layer *hlayer, struct hsspi_block *blk,
			 int status)
{
	struct uci_layer *uci = container_of(hlayer, struct uci_layer, hlayer);
	struct uci_packet *p = container_of(blk, struct uci_packet, blk);

	if (status)
		uci_packet_free(p);
	else {
		struct uci_packet *next;
		size_t readn = 0;
		size_t payload_size;

		while (1) {
			if (blk->length - readn < UCI_PACKET_HEADER_SIZE)
				// Incomplete UCI header
				break;

			payload_size = get_payload_size_from_header(
				(u8 *)blk->data + readn);

			if (blk->length - readn <=
			    UCI_PACKET_HEADER_SIZE + payload_size)
				// blk contains no additional packet
				break;

			next = kzalloc(sizeof(*next), GFP_KERNEL);
			if (!next)
				break;

			next->data = p->blk.data + readn;
			next->length = UCI_PACKET_HEADER_SIZE + payload_size;

			readn += next->length;

			mutex_lock(&uci->lock);
			list_add_tail(&next->list, &uci->rx_list);
			mutex_unlock(&uci->lock);
		}

		p->data = p->blk.data + readn;
		p->length = p->blk.length - readn;

		mutex_lock(&uci->lock);
		list_add_tail(&p->list, &uci->rx_list);
		mutex_unlock(&uci->lock);

		wake_up_interruptible(&uci->wq);
	}
}

static const struct hsspi_layer_ops uci_ops = {
	.registered = uci_registered,
	.unregistered = uci_unregistered,
	.get = uci_get,
	.received = uci_received,
	.sent = uci_sent,
};

int uci_layer_init(struct uci_layer *uci)
{
	uci->hlayer.name = "UCI";
	uci->hlayer.id = UL_UCI_APP;
	uci->hlayer.ops = &uci_ops;

	INIT_LIST_HEAD(&uci->rx_list);
	mutex_init(&uci->lock);
	init_waitqueue_head(&uci->wq);
	return 0;
}

void uci_layer_deinit(struct uci_layer *uci)
{
	clear_rx_list(uci);
}

bool uci_layer_has_data_available(struct uci_layer *uci)
{
	bool ret;

	mutex_lock(&uci->lock);
	ret = !list_empty(&uci->rx_list);
	mutex_unlock(&uci->lock);
	return ret;
}

struct uci_packet *uci_layer_read(struct uci_layer *uci, size_t max_size,
				  bool non_blocking)
{
	struct uci_packet *p;
	int ret;

	if (!non_blocking) {
		ret = wait_event_interruptible(
			uci->wq, uci_layer_has_data_available(uci));
		if (ret)
			return ERR_PTR(ret);
	}

	mutex_lock(&uci->lock);
	p = list_first_entry_or_null(&uci->rx_list, struct uci_packet, list);
	if (p) {
		if (p->length > max_size)
			p = ERR_PTR(-EMSGSIZE);
		else
			list_del(&p->list);
	} else
		p = ERR_PTR(-EAGAIN);

	mutex_unlock(&uci->lock);
	return p;
}
