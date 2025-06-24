/* SPDX-License-Identifier: GPL-2.0 */

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

#ifndef __HSSPI_LOG_H__
#define __HSSPI_LOG_H__

#include "hsspi.h"
#include "debug.h"
#include "qm35_rb.h"

struct log_packet {
	struct hsspi_block blk;
	struct completion *write_done;
};

struct log_layer {
	struct hsspi_layer hlayer;
	uint8_t log_modules_count;
	struct log_module *log_modules;
	struct rb rb;
	bool enabled;
};

/**
 * log_packet_alloc() - Allocate an LOG packet
 * @length: length of the LOG packet
 *
 * Allocate an LOG packet that can be used by the HSSPI driver in
 * order to send or receive an LOG packet.
 *
 * Return: a newly allocated &struct log_packet or NULL
 */
struct log_packet *log_packet_alloc(u16 length);

/**
 * log_packet_free() - Free an LOG packet
 * @p: pointer to the &struct log_packet to free
 *
 */
void log_packet_free(struct log_packet *p);

int log_layer_init(struct log_layer *log, struct debug *debug);
void log_layer_deinit(struct log_layer *log);

#endif // __HSSPI_LOG_H__
