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

#include "hsspi_test.h"
#include "hsspi_uci.h"
#include <linux/printk.h>
#include <linux/delay.h>

int hsspi_test_registered(struct hsspi_layer *upper_layer);
void hsspi_test_unregistered(struct hsspi_layer *upper_layer);
struct hsspi_block *hsspi_test_get(struct hsspi_layer *upper_layer, u16 length);
void hsspi_test_received(struct hsspi_layer *upper_layer,
			 struct hsspi_block *blk, int status);
void hsspi_test_sent(struct hsspi_layer *upper_layer, struct hsspi_block *blk,
		     int status);

struct hsspi_layer_ops test_hsspi_layer_ops = {
	.registered = hsspi_test_registered,
	.unregistered = hsspi_test_unregistered,
	.get = hsspi_test_get,
	.received = hsspi_test_received,
	.sent = hsspi_test_sent,
};

struct hsspi_layer test_hsspi_layer = {
	.name = "hsspi_test_layer",
	.id = UL_TEST_HSSPI,
	.ops = &test_hsspi_layer_ops,
};

static struct hsspi *ghsspi;
int sleep_inter_frame_ms;
extern int test_sleep_after_ss_ready_us;

int hsspi_test_init(struct hsspi *hsspi)
{
	ghsspi = hsspi;
	return hsspi_register(hsspi, &test_hsspi_layer);
}

void hsspi_test_deinit(struct hsspi *hsspi)
{
	hsspi_unregister(hsspi, &test_hsspi_layer);
}

int hsspi_test_registered(struct hsspi_layer *upper_layer)
{
	return 0;
}

void hsspi_test_unregistered(struct hsspi_layer *upper_layer)
{
}

static int check_rx(const u8 *rx, int len)
{
	int idx = 0, err = 0;
	for (; idx < len; idx++) {
		if (rx[idx] != (idx & 0xff)) {
			pr_err("hsspi test: check_rx rx[%u] != %u\n", idx,
			       rx[idx]);
			print_hex_dump(KERN_DEBUG, "rx:", DUMP_PREFIX_ADDRESS,
				       16, 1, rx, len, false);
			err = -5963;
			break;
		}
	}
	return err;
}

struct hsspi_block *hsspi_test_get(struct hsspi_layer *layer, u16 length)
{
	struct hsspi_block *blk = kzalloc(sizeof(*blk) + length, GFP_KERNEL);
	if (blk) {
		blk->data = blk + 1;
		blk->size = length;
		blk->length = length;
	}
	return blk;
}

void hsspi_test_set_inter_frame_ms(int ms)
{
	sleep_inter_frame_ms = ms;
}

void hsspi_test_received(struct hsspi_layer *layer, struct hsspi_block *blk,
			 int status)
{
	static uint64_t bytes, msgs, errors, bytes0, msgs0, errors0;
	static time64_t last_perf_dump;
	uint32_t rem;
	int error = check_rx(blk->data, blk->length) ? 1 : 0;
	time64_t now;
	errors += error;

	if (!last_perf_dump) {
		last_perf_dump = ktime_get_seconds();
	}
	now = ktime_get_seconds();

	/* inject latencies between each message and between the check
	 * of ss-ready and the xfer.
	 * The test is expected to fail if
	 * sleep_inter_frame_ms > CONFIG_PM_RET_SLEEP_DELAY_US
	 */
	if (sleep_inter_frame_ms > 0) {
		static int delay_us = 0;
		test_sleep_after_ss_ready_us =
			sleep_inter_frame_ms * 1000 - delay_us;
		usleep_range(delay_us, delay_us + 1);
		delay_us += 100;
		if (delay_us > sleep_inter_frame_ms * 1000)
			delay_us = 0;
	} else {
		test_sleep_after_ss_ready_us = 0;
	}
	bytes += blk->length;
	msgs++;
	error |= hsspi_send(ghsspi, layer, blk);
	div_u64_rem(msgs, 100, &rem);
	if (error || (rem == 0))
		pr_info("hsspi test: bytes received %llu, msgs %llu, errors %llu\n",
			bytes, msgs, errors);
	if (now > last_perf_dump) {
		uint64_t dbytes = bytes >= bytes0 ? bytes - bytes0 :
						    ~0ULL - bytes0 + bytes;
		uint64_t dmsgs = msgs >= msgs0 ? msgs - msgs0 :
						 ~0ULL - msgs0 + msgs;
		uint64_t derrors = errors >= errors0 ? errors - errors0 :
						       ~0ULL - errors0 + errors;
		pr_info("hsspi test perfs: %llu B/s, %llu msgs/s, %llu errors/s\n",
			div_u64(dbytes, (now - last_perf_dump)),
			div_u64(dmsgs, (now - last_perf_dump)),
			div_u64(derrors, (now - last_perf_dump)));
		bytes0 = bytes;
		msgs0 = msgs;
		errors0 = errors;
		last_perf_dump = now;
	}
}

void hsspi_test_sent(struct hsspi_layer *layer, struct hsspi_block *blk,
		     int status)
{
	static uint64_t bytes, msgs, errors;
	uint32_t rem;
	errors += status ? 1 : 0;
	msgs++;
	bytes += blk->length;
	div_u64_rem(msgs, 100, &rem);
	if (status || (rem == 0))
		pr_info("hsspi test: bytes sent %llu, msgs %llu, errors %llu\n",
			bytes, msgs, errors);
	kfree(blk);
}
