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

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <linux/fs.h>
#include <linux/mutex.h>
#include "qm35_rb.h"

struct debug;
struct log_module;

struct debug_trace_ops {
	void (*enable_set)(struct debug *dbg, int enable);
	int (*enable_get)(struct debug *dbg);
	void (*level_set)(struct debug *dbg, struct log_module *log_module,
			  int lvl);
	int (*level_get)(struct debug *dbg, struct log_module *log_module);
	char *(*trace_get_next)(struct debug *dbg, rb_entry_size_t *len);
	rb_entry_size_t (*trace_get_next_size)(struct debug *dbg);
	bool (*trace_next_avail)(struct debug *dbg);
	void (*trace_reset)(struct debug *dbg);
	int (*get_dev_id)(struct debug *dbg, uint16_t *dev_id);
	int (*get_soc_id)(struct debug *dbg, uint8_t *soc_id);
};

struct debug_coredump_ops {
	char *(*coredump_get)(struct debug *dbg, size_t *len);
	int (*coredump_force)(struct debug *dbg);
};

struct debug {
	struct dentry *root_dir;
	struct dentry *fw_dir;
	struct dentry *chip_dir;
	const struct debug_trace_ops *trace_ops;
	const struct debug_coredump_ops *coredump_ops;
	struct wait_queue_head wq;
	struct file *pv_filp;
	struct mutex pv_filp_lock;
	struct firmware *certificate;
};

// TODO move this from here to a commom place for both log layer and debug
struct log_module {
	uint8_t id;
	uint8_t lvl;
	char name[64];
	struct completion *read_done;
	struct debug *debug;
};

int debug_init_root(struct debug *debug, struct dentry *root);
int debug_init(struct debug *debug);
void debug_deinit(struct debug *debug);

int debug_create_module_entry(struct debug *debug,
			      struct log_module *log_module);

void debug_new_trace_available(struct debug *debug);

void debug_soc_info_available(struct debug *debug);

#endif // __DEBUG_H__
