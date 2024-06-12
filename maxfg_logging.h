/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC
 */

#ifndef MAXFG_LOGGING_H_
#define MAXFG_LOGGING_H_

#include <linux/circ_buf.h>

#define MAX_FG_LEARN_PARAM_MAX_HIST 32
#define MAX_FG_CAPTURE_CONFIG_NAME_MAX 32

struct maxfg_capture_regs {
	struct max17x0x_regmap *regmap;
	const enum max17x0x_reg_tags *tag;
	int reg_cnt;
};

/* a configuration can simply be a list of tags, a regmap and a name */
struct maxfg_capture_config {
	char name[MAX_FG_CAPTURE_CONFIG_NAME_MAX];
	struct maxfg_capture_regs normal;
	struct maxfg_capture_regs debug;
	int data_size;
};

/* only one configuration now */
struct maxfg_capture_buf {
	struct maxfg_capture_config config;

	int slots;
	struct circ_buf cb;
	void *latest_entry;
	struct mutex cb_wr_lock;
	struct mutex cb_rd_lock;
};

void maxfg_init_fg_learn_capture_config(struct maxfg_capture_config *config,
					struct max17x0x_regmap *regmap,
					struct max17x0x_regmap *debug_regmap);

int maxfg_alloc_capture_buf(struct maxfg_capture_buf *buf, int slots);
void maxfg_clear_capture_buf(struct maxfg_capture_buf *buf);
void maxfg_free_capture_buf(struct maxfg_capture_buf *buf);

int maxfg_capture_registers(struct maxfg_capture_buf *buf);

int maxfg_show_captured_buffer(struct maxfg_capture_buf *buf,
			       char *str_buf, int buf_len);
int maxfg_capture_to_cstr(struct maxfg_capture_config *config, u16 *reg_val,
			  char *str_buf, int buf_len);

bool maxfg_ce_relaxed(struct max17x0x_regmap *regmap, const u16 relax_mask,
		      const u16 *prev_val);

#endif