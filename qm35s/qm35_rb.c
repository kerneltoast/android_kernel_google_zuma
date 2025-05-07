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
 * QM35 Ringbuffer
 */

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "qm35_rb.h"

static bool __rb_can_pop(struct rb *rb, uint32_t *tail)
{
	// if tail equals with the head index, no data can be popped
	return *tail != rb->head;
}

bool rb_can_pop(struct rb *rb)
{
	bool can_pop = false;

	mutex_lock(&rb->lock);
	can_pop = __rb_can_pop(rb, &rb->rdtail);
	mutex_unlock(&rb->lock);

	return can_pop;
}

static rb_entry_size_t __rb_next_size(struct rb *rb, uint32_t *tail)
{
	rb_entry_size_t next_packet_size = 0;

	// check if something is available to pop from the ring buffer
	if (!__rb_can_pop(rb, tail))
		return 0;

	// read next packet size
	memcpy(&next_packet_size, rb->buf + *tail, sizeof(next_packet_size));

	if (next_packet_size == 0) {
		if (*tail == 0)
			return 0;
		// if the next packet size is 0x00, it is an
		// indication that we hit the end marker so we should
		// start reading from the beginning
		*tail = 0;
		memcpy(&next_packet_size, rb->buf + *tail,
		       sizeof(next_packet_size));
	}

	return next_packet_size;
}

rb_entry_size_t rb_next_size(struct rb *rb)
{
	rb_entry_size_t next_packet_size = 0;

	mutex_lock(&rb->lock);
	next_packet_size = __rb_next_size(rb, &rb->rdtail);
	mutex_unlock(&rb->lock);

	return next_packet_size;
}

static char *__rb_pop(struct rb *rb, rb_entry_size_t *len, uint32_t *tail)
{
	char *trace;

	*len = __rb_next_size(rb, tail);

	if (*len == 0)
		return 0;

	// advance ptr with sizeof(next_entry_size)
	*tail += sizeof(*len);

	// allocate memory for data
	trace = kmalloc(*len, GFP_KERNEL);

	// get the data
	memcpy(trace, rb->buf + *tail, *len);
	*tail += *len;

	return trace;
}

char *rb_pop(struct rb *rb, rb_entry_size_t *len)
{
	char *entry;

	mutex_lock(&rb->lock);
	entry = __rb_pop(rb, len, &rb->rdtail);
	mutex_unlock(&rb->lock);

	return entry;
}

static bool __rb_skip(struct rb *rb)
{
	rb_entry_size_t next_entry_size;

	// read next packet size
	memcpy(&next_entry_size, rb->buf + rb->tail, sizeof(next_entry_size));

	if (next_entry_size == 0) {
		if (rb->tail == 0)
			return false;
		// if the next packet size is 0x00, it is an
		// indication that we hit the end marker so we should
		// start reading from the beginning
		rb->tail = 0;
		memcpy(&next_entry_size, rb->buf + rb->tail,
		       sizeof(next_entry_size));
	}

	// advance ptr with sizeof(next_entry_size)
	rb->tail += sizeof(next_entry_size) + next_entry_size;

	return true;
}

int rb_push(struct rb *rb, const char *data, rb_entry_size_t len)
{
	rb_entry_size_t entry_size;
	uint32_t available_to_end;

	// doesn't make sense to push a packet with the payload len 0.
	if (len == 0)
		return 1;

	mutex_lock(&rb->lock);

	// calculate how much data we want to store
	// we add the size of the trace with the size of the size of the trace
	// because we want to store the size as well for reading
	entry_size = sizeof(len) + len;

	// available data to the end of the ring buffer
	available_to_end = rb->size - rb->head;

	// we check that by writing the buffer still has 2 bytes left
	// to write the buffer end marker 0x0000 if not, we just add
	// the marker and start from beginning
	if (available_to_end <= entry_size + sizeof(rb_entry_size_t)) {
		memset(rb->buf + rb->head, 0, sizeof(rb_entry_size_t));
		if (rb->tail >= rb->head)
			rb->tail = 0;
		if (rb->rdtail >= rb->head)
			rb->rdtail = 0;
		rb->head = 0;
	}

	// check if writing the new data will cause the head index to
	// overrun the tail index
	while ((rb->head <= rb->tail) && (rb->head + entry_size >= rb->tail)) {
		bool equal_tails = rb->tail == rb->rdtail;
		// need to adjust the tail to point to the next log
		// not overwritten
		bool skipped = __rb_skip(rb);

		if (equal_tails)
			rb->rdtail = rb->tail;

		if (!skipped)
			break;
	}

	// copy the size first
	memcpy(rb->buf + rb->head, &len, sizeof(len));
	// move the head by sizeof(trace->size) to be in place for copying the data
	rb->head += sizeof(len);
	// copy the data
	memcpy(rb->buf + rb->head, data, len);
	// update the head ptr to the next write
	rb->head += len;

	mutex_unlock(&rb->lock);

	return 0;
}

void rb_reset(struct rb *rb)
{
	mutex_lock(&rb->lock);
	rb->rdtail = rb->tail;
	mutex_unlock(&rb->lock);
}

int rb_init(struct rb *rb, uint32_t size)
{
	rb->buf = kmalloc(size, GFP_KERNEL);
	if (!rb->buf)
		return -ENOMEM;

	mutex_init(&rb->lock);
	rb->head = 0;
	rb->tail = 0;
	rb->rdtail = 0;
	rb->size = size;

	return 0;
}

void rb_deinit(struct rb *rb)
{
	kfree(rb->buf);
}
