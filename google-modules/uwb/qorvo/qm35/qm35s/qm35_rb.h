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
 * QM35 Ringbuffer
 */

#ifndef __QM35_RB_H__
#define __QM35_RB_H__

#include <linux/types.h>

typedef uint16_t rb_entry_size_t;

struct rb {
	uint8_t *buf;
	uint32_t size;
	uint32_t head;
	uint32_t tail;
	uint32_t rdtail;
	struct mutex lock;
};

bool rb_can_pop(struct rb *rb);
rb_entry_size_t rb_next_size(struct rb *rb);

char *rb_pop(struct rb *rb, rb_entry_size_t *len);
int rb_push(struct rb *rb, const char *data, rb_entry_size_t len);
void rb_reset(struct rb *rb);

int rb_init(struct rb *rb, uint32_t size);
void rb_deinit(struct rb *rb);

#endif // __QM35_RB_H__
