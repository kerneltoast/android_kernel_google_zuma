// SPDX-License-Identifier: GPL-2.0

/*
 * This file is part of the QM35 UCI stack for linux.
 *
 * Copyright (c) 2022 Qorvo US, Inc.
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
 * QM35 COREDUMP layer HSSPI Protocol
 */

#include "qm35.h"
#include "hsspi_coredump.h"
#include "qm35-sscd.h"

#define COREDUMP_HEADER_NTF 0x00
#define COREDUMP_BODY_NTF 0x01
#define COREDUMP_RCV_STATUS 0x02
#define COREDUMP_FORCE_CMD 0x03

#define COREDUMP_RCV_NACK 0x00
#define COREDUMP_RCV_ACK 0x01

#define COREDUMP_RCV_TIMER_TIMEOUT_S 2

struct __packed coredump_common_hdr {
	uint8_t cmd_id;
};

struct __packed coredump_hdr_ntf {
	uint32_t size;
	uint16_t crc;
};

struct __packed coredump_rcv_status {
	uint8_t ack;
};

struct coredump_packet *coredump_packet_alloc(u16 length)
{
	struct coredump_packet *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return NULL;

	if (hsspi_init_block(&p->blk, length)) {
		kfree(p);
		return NULL;
	}
	return p;
}

void coredump_packet_free(struct coredump_packet *p)
{
	hsspi_deinit_block(&p->blk);
	kfree(p);
}

static int coredump_send_rcv_status(struct coredump_layer *layer, uint8_t ack)
{
	struct coredump_packet *p;
	struct coredump_common_hdr hdr;
	struct coredump_rcv_status rcv;
	struct qm35_ctx *qm35_hdl;

	pr_info("qm35: coredump: sending status %s\n",
		layer->coredump_status == COREDUMP_RCV_ACK ? "ACK" : "NACK");

	qm35_hdl = container_of(layer, struct qm35_ctx, coredump_layer);

	p = coredump_packet_alloc(sizeof(hdr) + sizeof(rcv));
	if (!p)
		return -ENOMEM;

	hdr.cmd_id = COREDUMP_RCV_STATUS;
	rcv.ack = ack;

	memcpy(p->blk.data, &hdr, sizeof(hdr));
	memcpy(p->blk.data + sizeof(hdr), &rcv, sizeof(rcv));

	return hsspi_send(&qm35_hdl->hsspi, &qm35_hdl->coredump_layer.hlayer,
			  &p->blk);
}

static uint16_t coredump_get_checksum(struct coredump_layer *layer)
{
	uint32_t idx;
	uint16_t crc = 0;

	for (idx = 0; idx < layer->coredump_data_wr_idx; idx++) {
		uint8_t *val;

		val = layer->coredump_data + idx;
		crc += *val;
	}

	return crc;
}

static void corredump_on_expired_timer(struct timer_list *timer)
{
	struct coredump_layer *layer =
		container_of(timer, struct coredump_layer, timer);

	pr_warn("qm35: coredump receive timer expired\n");

	coredump_send_rcv_status(layer, layer->coredump_status);
}

static int coredump_registered(struct hsspi_layer *hlayer)
{
	return 0;
}

static void coredump_unregistered(struct hsspi_layer *hlayer)
{
	;
}

static struct hsspi_block *coredump_get(struct hsspi_layer *hlayer, u16 length)
{
	struct coredump_packet *p;

	p = coredump_packet_alloc(length);
	if (!p)
		return NULL;

	return &p->blk;
}

static void coredump_header_ntf_received(struct coredump_layer *layer,
					 struct coredump_hdr_ntf chn)
{
	void *data;

	pr_info("qm35: coredump: receiving coredump with len: %d and crc: 0x%x\n",
		chn.size, chn.crc);

	layer->coredump_data_wr_idx = 0;
	layer->coredump_size = chn.size;
	layer->coredump_crc = chn.crc;
	layer->coredump_status = COREDUMP_RCV_NACK;

	data = krealloc(layer->coredump_data, layer->coredump_size, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(data)) {
		layer->coredump_data = NULL;
		layer->coredump_size = 0;
		layer->coredump_crc = 0;
		pr_err("qm35: failed to allocate coredump mem: %px\n", data);
	} else {
		layer->coredump_data = data;
	}
}

static int coredump_body_ntf_received(struct coredump_layer *layer,
				      uint8_t *cch_body, uint16_t cch_body_size)
{
	if (!layer->coredump_data) {
		pr_err("qm35: failed to save coredump, mem not allocated\n");
		return 1;
	}

	if (cch_body_size + layer->coredump_data_wr_idx >
	    layer->coredump_size) {
		pr_err("qm35: failed to save coredump, mem overflow: max size: %d, wr_idx: %d, cd size: %d\n",
		       layer->coredump_size, layer->coredump_data_wr_idx,
		       cch_body_size);
		return 1;
	}

	memcpy(layer->coredump_data + layer->coredump_data_wr_idx, cch_body,
	       cch_body_size);
	layer->coredump_data_wr_idx += cch_body_size;

	return 0;
}

static void coredump_received(struct hsspi_layer *hlayer,
			      struct hsspi_block *blk, int status)
{
	struct coredump_common_hdr cch;
	struct coredump_hdr_ntf chn;
	uint8_t *cch_body;
	uint16_t cch_body_size;

	struct coredump_packet *packet =
		container_of(blk, struct coredump_packet, blk);

	struct coredump_layer *layer =
		container_of(hlayer, struct coredump_layer, hlayer);

	struct qm35_ctx *qm35_hdl =
		container_of(layer, struct qm35_ctx, coredump_layer);

	if (status)
		goto out;

	if (blk->length < sizeof(struct coredump_common_hdr)) {
		pr_err("qm35: coredump packet header too small: %d bytes\n",
		       blk->length);
		goto out;
	}

	del_timer_sync(&layer->timer);

	memcpy(&cch, blk->data, sizeof(cch));
	cch_body = blk->data + sizeof(cch);
	cch_body_size = blk->length - sizeof(cch);

	switch (cch.cmd_id) {
	case COREDUMP_HEADER_NTF:
		if (cch_body_size < sizeof(chn)) {
			pr_err("qm35: coredump packet header ntf too small: %d bytes\n",
			       cch_body_size);
			break;
		}

		memcpy(&chn, cch_body, sizeof(chn));
		coredump_header_ntf_received(layer, chn);
		break;

	case COREDUMP_BODY_NTF:
		pr_debug(
			"qm35: coredump: saving coredump data with len: %d [%d/%d]\n",
			cch_body_size,
			layer->coredump_data_wr_idx + cch_body_size,
			layer->coredump_size);

		if (coredump_body_ntf_received(layer, cch_body, cch_body_size))
			break;

		if (layer->coredump_data_wr_idx == layer->coredump_size) {
			uint16_t crc = coredump_get_checksum(layer);

			pr_info("qm35: coredump: calculated crc: 0x%x, header crc: 0x%x\n",
				crc, layer->coredump_crc);

			if (crc == layer->coredump_crc)
				layer->coredump_status = COREDUMP_RCV_ACK;

			if (qm35_hdl->sscd)
				qm35_report_coredump(qm35_hdl);

			coredump_send_rcv_status(layer, layer->coredump_status);

			break;
		}

		mod_timer(&layer->timer,
			  jiffies + COREDUMP_RCV_TIMER_TIMEOUT_S * HZ);

		break;

	default:
		pr_err("qm35: coredump: wrong cmd id received: 0x%x\n",
		       cch.cmd_id);
		break;
	}

out:

	coredump_packet_free(packet);
}

static void coredump_sent(struct hsspi_layer *hlayer, struct hsspi_block *blk,
			  int status)
{
	struct coredump_packet *buf =
		container_of(blk, struct coredump_packet, blk);

	coredump_packet_free(buf);
}

static const struct hsspi_layer_ops coredump_ops = {
	.registered = coredump_registered,
	.unregistered = coredump_unregistered,
	.get = coredump_get,
	.received = coredump_received,
	.sent = coredump_sent,
};

char *debug_coredump_get(struct debug *dbg, size_t *len)
{
	char *data;
	struct qm35_ctx *qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	*len = qm35_hdl->coredump_layer.coredump_data_wr_idx;
	data = qm35_hdl->coredump_layer.coredump_data;

	return data;
}

int debug_coredump_force(struct debug *dbg)
{
	struct coredump_packet *p;
	struct coredump_common_hdr hdr = { .cmd_id = COREDUMP_FORCE_CMD };
	struct qm35_ctx *qm35_hdl;

	pr_info("qm35: force coredump");

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	p = coredump_packet_alloc(sizeof(hdr));
	if (!p)
		return -ENOMEM;

	memcpy(p->blk.data, &hdr, sizeof(hdr));

	return hsspi_send(&qm35_hdl->hsspi, &qm35_hdl->coredump_layer.hlayer,
			  &p->blk);
}

static const struct debug_coredump_ops debug_coredump_ops = {
	.coredump_get = debug_coredump_get,
	.coredump_force = debug_coredump_force,
};

int coredump_layer_init(struct coredump_layer *layer, struct debug *debug)
{
	layer->hlayer.name = "QM35 COREDUMP";
	layer->hlayer.id = UL_COREDUMP;
	layer->hlayer.ops = &coredump_ops;

	layer->coredump_data = NULL;
	layer->coredump_data_wr_idx = 0;
	layer->coredump_size = 0;
	layer->coredump_crc = 0;
	layer->coredump_status = 0;
	timer_setup(&layer->timer, corredump_on_expired_timer, 0);

	debug->coredump_ops = &debug_coredump_ops;

	return 0;
}

void coredump_layer_deinit(struct coredump_layer *layer)
{
	del_timer_sync(&layer->timer);
	kfree(layer->coredump_data);
}
