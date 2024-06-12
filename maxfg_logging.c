/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include "google_bms.h"
#include "max1720x_battery.h"
#include "maxfg_logging.h"

/* learning parameters */
#define MAX_FG_LEARNING_CONFIG_NORMAL_REGS 8
#define MAX_FG_LEARNING_CONFIG_DEBUG_REGS 2

/* see maxfg_ce_relaxed() */
static const enum max17x0x_reg_tags fg_learning_param[] = {
	/* from normal regmap */
	MAXFG_TAG_fcnom,
	MAXFG_TAG_dpacc,
	MAXFG_TAG_dqacc,
	MAXFG_TAG_fcrep,
	MAXFG_TAG_repsoc,
	MAXFG_TAG_msoc,
	MAX17X0X_TAG_vfsoc,
	MAXFG_TAG_fstat,

	/* from debug_regmap */
	MAXFG_TAG_rcomp0,
	MAXFG_TAG_tempco,
};

/* this could be static */
void maxfg_init_fg_learn_capture_config(struct maxfg_capture_config *config,
					struct max17x0x_regmap *regmap,
					struct max17x0x_regmap *debug_regmap)
{
	strscpy(config->name,  "FG Learning Events", sizeof(config->name));
	config->normal.tag = &fg_learning_param[0];
	config->normal.reg_cnt = MAX_FG_LEARNING_CONFIG_NORMAL_REGS;
	config->normal.regmap = regmap;

	config->debug.tag = &fg_learning_param[MAX_FG_LEARNING_CONFIG_NORMAL_REGS];
	config->debug.reg_cnt = MAX_FG_LEARNING_CONFIG_DEBUG_REGS;
	config->debug.regmap = debug_regmap;

	config->data_size = ARRAY_SIZE(fg_learning_param) * sizeof(u16);
}


int maxfg_alloc_capture_buf(struct maxfg_capture_buf *buf, int slots)
{
	if ((slots & (slots - 1)) || !buf || !buf->config.data_size || !slots)
		return -EINVAL;

	buf->slots = 0;
	buf->latest_entry = NULL;

	buf->cb.buf = kzalloc(buf->config.data_size * slots, GFP_KERNEL);
	if (!buf->cb.buf)
		return -ENOMEM;

	buf->cb.head = 0;
	buf->cb.tail = 0;
	buf->slots = slots;

	mutex_init(&buf->cb_wr_lock);
	mutex_init(&buf->cb_rd_lock);

	return 0;
}

void maxfg_clear_capture_buf(struct maxfg_capture_buf *buf)
{
	int head, tail;
	if (!buf)
		return;

	mutex_lock(&buf->cb_wr_lock);
	mutex_lock(&buf->cb_rd_lock);

	head = buf->cb.head;
	tail = buf->cb.tail;

	if (CIRC_CNT(head, tail, buf->slots)) {
		head = (head + 1) & (buf->slots - 1);

		smp_wmb();

		/* make buffer empty by (head == tail) while preserving latest_entry as a seed */
		WRITE_ONCE(buf->cb.head, head);
		WRITE_ONCE(buf->cb.tail, head);
	}

	mutex_unlock(&buf->cb_rd_lock);
	mutex_unlock(&buf->cb_wr_lock);
}

void maxfg_free_capture_buf(struct maxfg_capture_buf *buf)
{
	if (!buf)
		return;

	if (buf->cb.buf && buf->slots > 0)
		kfree(buf->cb.buf);

	mutex_destroy(&buf->cb_wr_lock);
	mutex_destroy(&buf->cb_rd_lock);

	buf->cb.buf = NULL;
	buf->slots = 0;
}

static inline int maxfg_read_registers(struct maxfg_capture_regs *regs, u16 *buffer)
{
	int ret, idx;

	for (idx = 0; idx < regs->reg_cnt; idx++) {
		ret = max17x0x_reg_read(regs->regmap, regs->tag[idx], &buffer[idx]);
		if (ret < 0) {
			pr_err("failed to reg_tag(%u) %d\n", regs->tag[idx], ret);
			return ret;
		}
	}

	return 0;
}

int maxfg_capture_registers(struct maxfg_capture_buf *buf)
{
	struct maxfg_capture_config *config = &buf->config;
	const int data_size = config->data_size;
	void *latest_entry;
	int head, tail, ret;
	u16 *reg_val;

	head = buf->cb.head;
	tail = READ_ONCE(buf->cb.tail);

	/* if buffer is full, drop the last entry */
	if (CIRC_SPACE(head, tail, buf->slots) == 0) {
		mutex_lock(&buf->cb_rd_lock);
		WRITE_ONCE(buf->cb.tail, (tail + 1) & (buf->slots - 1));
		mutex_unlock(&buf->cb_rd_lock);
	}

	reg_val = (u16 *)&buf->cb.buf[head * data_size];
	latest_entry = reg_val;

	ret = maxfg_read_registers(&config->normal, reg_val);
	if (ret < 0)
		goto exit_done;

	reg_val += config->normal.reg_cnt;

	ret = maxfg_read_registers(&config->debug, reg_val);
	if (ret < 0)
		goto exit_done;

	smp_wmb();
	WRITE_ONCE(buf->cb.head, (head + 1) & (buf->slots - 1));

	buf->latest_entry = latest_entry;

exit_done:
	return ret;
}

int maxfg_capture_to_cstr(struct maxfg_capture_config *config, u16 *reg_val,
			  char *str_buf, int buf_len)
{
	const struct max17x0x_reg *fg_reg;
	int reg_idx;
	int len = 0;

	for (reg_idx = 0; reg_idx < config->normal.reg_cnt && len < buf_len; reg_idx++) {
		fg_reg = max17x0x_find_by_tag(config->normal.regmap,
					      config->normal.tag[reg_idx]);
		if (!fg_reg)
			return len;

		len += scnprintf(&str_buf[len], buf_len - len, "%02X:%04X ",
				 fg_reg->reg, reg_val[reg_idx]);
	}

	reg_val += config->normal.reg_cnt;

	for (reg_idx = 0; reg_idx < config->debug.reg_cnt && len < buf_len; reg_idx++) {
		fg_reg = max17x0x_find_by_tag(config->debug.regmap,
					      config->debug.tag[reg_idx]);
		if (!fg_reg)
			return len;

		len += scnprintf(&str_buf[len], buf_len - len, "%02X:%04X ",
				 fg_reg->reg, reg_val[reg_idx]);
	}

	return len;
}

int maxfg_show_captured_buffer(struct maxfg_capture_buf *buf,
			       char *str_buf, int buf_len)
{
	struct maxfg_capture_config *config = &buf->config;
	const int data_size = config->data_size;
	int head, tail, count, to_end, idx, rt;
	u16 *reg_val;

	if (!buf)
		return -EINVAL;

	mutex_lock(&buf->cb_rd_lock);

	head = READ_ONCE(buf->cb.head);
	tail = buf->cb.tail;

	count = CIRC_CNT(head, tail, buf->slots);
	rt = scnprintf(&str_buf[0], buf_len, "%s (%d):\n", config->name, count);

	if (count == 0)
		goto maxfg_show_captured_buffer_exit;

	to_end = CIRC_CNT_TO_END(head, tail, buf->slots);

	for (idx = 0; idx < to_end && rt < buf_len; idx++) {
		reg_val = (u16 *)&buf->cb.buf[(tail + idx) * data_size];
		rt += maxfg_capture_to_cstr(config, reg_val, &str_buf[rt],
					    buf_len - rt);
		rt += scnprintf(&str_buf[rt], buf_len - rt, "\n");
	}

	count -= idx;

	for (idx = 0; idx < count && rt < buf_len; idx++) {
		reg_val = (u16 *)&buf->cb.buf[idx * data_size];
		rt += maxfg_capture_to_cstr(config, reg_val, &str_buf[rt],
					    buf_len - rt);
		rt += scnprintf(&str_buf[rt], buf_len - rt, "\n");
	}

maxfg_show_captured_buffer_exit:
	mutex_unlock(&buf->cb_rd_lock);
	return rt;
}

/*
 * data in prev_val follows the order of fg_learning_param[]
 *  prev_val[0]: fcnom
 *  prev_val[1]: dpacc
 *  prev_val[2]: dqacc
 *  prev_val[7]: fstat
 */
bool maxfg_ce_relaxed(struct max17x0x_regmap *regmap, const u16 relax_mask,
                      const u16 *prev_val)
{
	u16 fstat, fcnom, dpacc, dqacc;
	int ret;

	ret = max17x0x_reg_read(regmap, MAXFG_TAG_fstat, &fstat);
	if (ret < 0)
		return false;

	ret = max17x0x_reg_read(regmap, MAXFG_TAG_fcnom, &fcnom);
	if (ret < 0)
		return false;

	ret = max17x0x_reg_read(regmap, MAXFG_TAG_dpacc, &dpacc);
	if (ret < 0)
		return false;

	ret = max17x0x_reg_read(regmap, MAXFG_TAG_dqacc, &dqacc);
	if (ret < 0)
		return false;

	/*
	 * log when relaxed state changes, when fcnom, dpacc, dqacc change
	 * TODO: log only when dpacc, dqacc or fcnom change and simply
	 * count the relaxation event otherwise.
	 */
	return (fstat & relax_mask) != (prev_val[7] & relax_mask) ||
		dpacc != prev_val[1] || dqacc != prev_val[2] ||
		fcnom != prev_val[0];
}
