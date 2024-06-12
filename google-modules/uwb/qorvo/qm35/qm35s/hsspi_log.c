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
 * QM35 LOG layer HSSPI Protocol
 */

#include <qmrom.h>

#include "qm35.h"
#include "hsspi_log.h"

#define LOG_CID_TRACE_NTF 0x0000
#define LOG_CID_SET_LOG_LVL 0x0001
#define LOG_CID_GET_LOG_LVL 0x0002
#define LOG_CID_GET_LOG_SRC 0x0003

#define TRACE_RB_SIZE 0xFFFFF // 1MB

struct __packed log_packet_hdr {
	uint16_t cmd_id;
	uint16_t b_size;
};

struct __packed log_level_set_cmd {
	struct log_packet_hdr hdr;
	uint8_t id;
	uint8_t lvl;
};

struct __packed log_level_get_cmd {
	struct log_packet_hdr hdr;
	uint8_t id;
};

struct log_packet *log_packet_alloc(u16 length)
{
	struct log_packet *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return NULL;

	if (hsspi_init_block(&p->blk, length)) {
		kfree(p);
		return NULL;
	}
	return p;
}

void log_packet_free(struct log_packet *p)
{
	hsspi_deinit_block(&p->blk);
	kfree(p);
}

static struct log_packet *encode_get_log_sources_packet(void)
{
	struct log_packet *p;
	struct log_packet_hdr msg;

	p = log_packet_alloc(sizeof(msg));
	if (!p)
		return p;

	msg.cmd_id = LOG_CID_GET_LOG_SRC;
	msg.b_size = 0;

	memcpy(p->blk.data, &msg, sizeof(msg));

	return p;
}

static struct log_packet *encode_set_log_level_packet(uint8_t id, uint8_t lvl)
{
	struct log_packet *p;
	struct log_level_set_cmd cmd;

	p = log_packet_alloc(sizeof(cmd));
	if (!p)
		return p;

	cmd.hdr.cmd_id = LOG_CID_SET_LOG_LVL;
	cmd.hdr.b_size = sizeof(cmd) - sizeof(cmd.hdr);
	cmd.id = id;
	cmd.lvl = lvl;

	memcpy(p->blk.data, &cmd, sizeof(cmd));

	return p;
}

static struct log_packet *encode_get_log_level_packet(uint8_t id)
{
	struct log_packet *p;
	struct log_level_get_cmd cmd;

	p = log_packet_alloc(sizeof(cmd));
	if (!p)
		return p;

	cmd.hdr.cmd_id = LOG_CID_GET_LOG_LVL;
	cmd.hdr.b_size = sizeof(cmd) - sizeof(cmd.hdr);
	cmd.id = id;

	memcpy(p->blk.data, &cmd, sizeof(cmd));

	return p;
}

static int parse_log_sources_response(struct log_layer *layer, uint8_t *data,
				      uint16_t data_size)
{
	struct qm35_ctx *qm35_hdl;
	int idx = 0;
	uint16_t current_len = 0;

	qm35_hdl = container_of(layer, struct qm35_ctx, log_layer);

	layer->log_modules_count = *data++;

	kfree(layer->log_modules);

	layer->log_modules = kcalloc(layer->log_modules_count,
				     sizeof(struct log_module), GFP_KERNEL);
	if (!layer->log_modules)
		return 1;

	for (idx = 0; idx < layer->log_modules_count; idx++) {
		layer->log_modules[idx].debug = &qm35_hdl->debug;
		layer->log_modules[idx].id = *data++;
		layer->log_modules[idx].lvl = *data++;
		current_len = strlen((char *)data) + 1;
		if (current_len > sizeof(layer->log_modules[idx].name)) {
			const char *error_msg = "log name error";
			pr_err("qm35: log module name bigger than allocated buffer: current_len = %d bytes\n",
			       current_len);
			strlcpy(layer->log_modules[idx].name, error_msg,
				sizeof(layer->log_modules[idx].name));
		} else
			strcpy(layer->log_modules[idx].name, (char *)data);
		data += current_len;

		debug_create_module_entry(&qm35_hdl->debug,
					  &layer->log_modules[idx]);
	}

	return 0;
}

static int parse_get_log_lvl_response(struct log_layer *layer, uint8_t *data,
				      uint16_t data_size)
{
	uint8_t src_id, src_lvl;
	int idx = 0;

	src_id = *data++;
	src_lvl = *data;

	for (idx = 0; idx < layer->log_modules_count; idx++) {
		if (layer->log_modules[idx].id == src_id) {
			layer->log_modules[idx].lvl = src_lvl;

			if (layer->log_modules[idx].read_done)
				complete(layer->log_modules[idx].read_done);
		}
	}

	return 0;
}

static int log_registered(struct hsspi_layer *hlayer)
{
	return 0;
}

static void log_unregistered(struct hsspi_layer *hlayer)
{
	;
}

static struct hsspi_block *log_get(struct hsspi_layer *hlayer, u16 length)
{
	struct log_packet *p;

	p = log_packet_alloc(length);
	if (!p)
		return NULL;

	return &p->blk;
}

static void log_received(struct hsspi_layer *hlayer, struct hsspi_block *blk,
			 int status)
{
	struct log_layer *layer;
	struct log_packet_hdr hdr;
	uint8_t *body;
	struct qm35_ctx *qm35_hdl;
	struct log_packet *p;

	p = container_of(blk, struct log_packet, blk);

	if (status)
		goto out;

	layer = container_of(hlayer, struct log_layer, hlayer);
	qm35_hdl = container_of(layer, struct qm35_ctx, log_layer);

	if (blk->length < sizeof(struct log_packet_hdr)) {
		pr_err("qm35: log packet header too small: %d bytes\n",
		       blk->length);
		goto out;
	}

	memcpy(&hdr, blk->data, sizeof(struct log_packet_hdr));
	body = blk->data + sizeof(struct log_packet_hdr);

	if (blk->length < sizeof(struct log_packet_hdr) + hdr.b_size) {
		pr_err("qm35: incomplete log packet: %d/%d bytes\n",
		       blk->length, hdr.b_size);
		goto out;
	}
	if (qm35_hdl->log_qm_traces)
		pr_info("qm35_log: %.*s\n", hdr.b_size - 2, body);

	switch (hdr.cmd_id) {
	case LOG_CID_TRACE_NTF:
		rb_push(&layer->rb, (const char *)body, hdr.b_size);
		debug_new_trace_available(&qm35_hdl->debug);
		break;
	case LOG_CID_SET_LOG_LVL:
		break;
	case LOG_CID_GET_LOG_LVL:
		parse_get_log_lvl_response(layer, body, hdr.b_size);
		break;
	case LOG_CID_GET_LOG_SRC:
		parse_log_sources_response(layer, body, hdr.b_size);
		break;
	default:
		break;
	}
out:
	log_packet_free(p);
}

static void log_sent(struct hsspi_layer *hlayer, struct hsspi_block *blk,
		     int status)
{
	struct log_packet *p = container_of(blk, struct log_packet, blk);

	log_packet_free(p);
}

static const struct hsspi_layer_ops log_ops = {
	.registered = log_registered,
	.unregistered = log_unregistered,
	.get = log_get,
	.received = log_received,
	.sent = log_sent,
};

static void log_enable_set(struct debug *dbg, int enable)
{
	struct qm35_ctx *qm35_hdl;
	struct log_packet *p;
	int ret;

	// TODO: what happens if enable is false?
	//    if (!enable)
	//        return;
	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	if (qm35_hdl->log_layer.enabled) {
		pr_warn("qm35: logging already enabled\n");
		return;
	}

	p = encode_get_log_sources_packet();
	if (!p) {
		pr_err("failed to encode get log sources packet\n");
		return;
	}

	ret = hsspi_send(&qm35_hdl->hsspi, &qm35_hdl->log_layer.hlayer,
			 &p->blk);

	if (ret) {
		pr_err("failed to send spi packet\n");
		log_packet_free(p);
	} else {
		qm35_hdl->log_layer.enabled = enable;
	}
}

static int log_enable_get(struct debug *dbg)
{
	struct qm35_ctx *qm35_hdl;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	return qm35_hdl->log_layer.enabled;
}

static void log_level_set(struct debug *dbg, struct log_module *log_module,
			  int lvl)
{
	struct qm35_ctx *qm35_hdl;
	struct log_packet *p;
	int ret;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	p = encode_set_log_level_packet(log_module->id, lvl);
	if (!p) {
		pr_err("failed to encode set log level packet\n");
		return;
	}

	ret = hsspi_send(&qm35_hdl->hsspi, &qm35_hdl->log_layer.hlayer,
			 &p->blk);
	if (ret) {
		pr_err("failed to send spi packet\n");
		log_packet_free(p);
	}
}

static int log_level_get(struct debug *dbg, struct log_module *log_module)
{
	struct qm35_ctx *qm35_hdl;
	struct log_packet *p;
	int ret = 0;
	DECLARE_COMPLETION_ONSTACK(comp);

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	p = encode_get_log_level_packet(log_module->id);
	if (!p) {
		pr_err("failed to encode get log level packet\n");
		return 0;
	}

	log_module->read_done = &comp;

	ret = hsspi_send(&qm35_hdl->hsspi, &qm35_hdl->log_layer.hlayer,
			 &p->blk);
	if (ret) {
		pr_err("failed to send spi packet\n");
		log_packet_free(p);
		return 0;
	}

	wait_for_completion(log_module->read_done);

	return log_module->lvl;
}

static char *log_trace_get_next(struct debug *dbg, rb_entry_size_t *len)
{
	struct qm35_ctx *qm35_hdl;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	return rb_pop(&qm35_hdl->log_layer.rb, len);
}

static rb_entry_size_t log_trace_get_next_size(struct debug *dbg)
{
	struct qm35_ctx *qm35_hdl;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	return rb_next_size(&qm35_hdl->log_layer.rb);
}

static bool log_trace_next_avail(struct debug *dbg)
{
	struct qm35_ctx *qm35_hdl;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	return rb_can_pop(&qm35_hdl->log_layer.rb);
}

static void log_trace_reset(struct debug *dbg)
{
	struct qm35_ctx *qm35_hdl;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	rb_reset(&qm35_hdl->log_layer.rb);
}

static int get_dev_id(struct debug *dbg, uint16_t *dev_id)
{
	struct qm35_ctx *qm35_hdl;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	return qm_get_dev_id(qm35_hdl, dev_id);
}

static int get_soc_id(struct debug *dbg, uint8_t *soc_id)
{
	struct qm35_ctx *qm35_hdl;

	qm35_hdl = container_of(dbg, struct qm35_ctx, debug);

	return qm_get_soc_id(qm35_hdl, soc_id);
}

static const struct debug_trace_ops debug_trace_ops = {
	.enable_set = log_enable_set,
	.enable_get = log_enable_get,
	.level_set = log_level_set,
	.level_get = log_level_get,
	.trace_get_next = log_trace_get_next,
	.trace_get_next_size = log_trace_get_next_size,
	.trace_next_avail = log_trace_next_avail,
	.trace_reset = log_trace_reset,
	.get_dev_id = get_dev_id,
	.get_soc_id = get_soc_id,
};

int log_layer_init(struct log_layer *log, struct debug *debug)
{
	int ret;

	ret = rb_init(&log->rb, TRACE_RB_SIZE);
	if (ret)
		return ret;

	log->hlayer.name = "QM35 LOG";
	log->hlayer.id = UL_LOG;
	log->hlayer.ops = &log_ops;
	log->enabled = false;
	log->log_modules = NULL;
	log->log_modules_count = 0;

	debug->trace_ops = &debug_trace_ops;

	return 0;
}

void log_layer_deinit(struct log_layer *log)
{
	kfree(log->log_modules);
	rb_deinit(&log->rb);
}
