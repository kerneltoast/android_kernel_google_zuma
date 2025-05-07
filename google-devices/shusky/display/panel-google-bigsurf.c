// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based bigsurf AMOLED LCD panel driver.
 *
 * Copyright (c) 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "trace/dpu_trace.h"
#include "panel/panel-samsung-drv.h"

#define BIGSURF_DDIC_ID_LEN 8
#define BIGSURF_DIMMING_FRAME 32

#define MIPI_DSI_FREQ_MBPS_DEFAULT 756
#define MIPI_DSI_FREQ_MBPS_ALTERNATIVE 776

#define WIDTH_MM 64
#define HEIGHT_MM 143

#define PROJECT "SB3"

enum bigsurf_lhbm_brt {
	LHBM_R = 0,
	LHBM_G,
	LHBM_B,
	LHBM_BRT_MAX
};

#define LHBM_BRT_LEN (LHBM_BRT_MAX * 2)
#define LHBM_BRT_CMD_LEN (LHBM_BRT_LEN + 1)
#define LHBM_COMPENSATION_THRESHOLD 1380

enum bigsurf_lhbm_brt_overdrive_group {
	LHBM_OVERDRIVE_GRP_0_NIT = 0,
	LHBM_OVERDRIVE_GRP_15_NIT,
	LHBM_OVERDRIVE_GRP_200_NIT,
	LHBM_OVERDRIVE_GRP_MAX
};

struct bigsurf_lhbm_ctl {
	/** @brt_normal: normal LHBM brightness parameters */
	u8 brt_normal[LHBM_BRT_LEN];
	/** @brt_overdrive: overdrive LHBM brightness parameters */
	u8 brt_overdrive[LHBM_OVERDRIVE_GRP_MAX][LHBM_BRT_LEN];
	/** @overdrived: whether or not LHBM is overdrived */
	bool overdrived;
	/** @hist_roi_configured: whether LHBM histogram configuration is done */
	bool hist_roi_configured;
};

static const u8 bigsurf_cmd2_page2[] = {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x02};
static const u8 bigsurf_lhbm_brightness_reg = 0xD0;

/**
 * struct bigsurf_panel - panel specific runtime info
 *
 * This struct maintains bigsurf panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc.
 */
struct bigsurf_panel {
	/** @base: base panel struct */
	struct exynos_panel base;
	/** @lhbm_ctl: LHBM control parameters */
	struct bigsurf_lhbm_ctl lhbm_ctl;
	/** @idle_exit_dimming_delay_ts: the delay time to exit dimming */
	ktime_t idle_exit_dimming_delay_ts;
	/** @panel_brightness: the brightness of the panel */
	u16 panel_brightness;
};

#define to_spanel(ctx) container_of(ctx, struct bigsurf_panel, base)

static const struct exynos_dsi_cmd bigsurf_lp_cmds[] = {
	/* Disable the Black insertion in AoD */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xC0, 0x54),

	/* disable dimming */
	EXYNOS_DSI_CMD_SEQ(0x53, 0x20),
	/* enter AOD */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_ENTER_IDLE_MODE),
	EXYNOS_DSI_CMD_SEQ(0x5A, 0x04),
};
static DEFINE_EXYNOS_CMD_SET(bigsurf_lp);

static const struct exynos_dsi_cmd bigsurf_lp_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
};

static const struct exynos_dsi_cmd bigsurf_lp_low_cmds[] = {
	/* 10 nit */
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x04),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x03, 0x33),
};

static const struct exynos_dsi_cmd bigsurf_lp_high_cmds[] = {
	/* 50 nit */
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x04),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFE),
};

static const struct exynos_binned_lp bigsurf_binned_lp[] = {
	BINNED_LP_MODE("off", 0, bigsurf_lp_off_cmds),
	/* rising = 0, falling = 32 */
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 932, bigsurf_lp_low_cmds, 0, 32),
	BINNED_LP_MODE_TIMING("high", 3574, bigsurf_lp_high_cmds, 0, 32),
};

static const struct exynos_dsi_cmd bigsurf_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(100, MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_EXYNOS_CMD_SET(bigsurf_off);

static const struct exynos_dsi_cmd bigsurf_init_cmds[] = {
	/* CMD2, Page0 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_EVT1), 0x6F, 0x18),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_PROTO1_1), 0xB2, 0x3B, 0x34),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1_1, 0xB2, 0x39, 0x60),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1B),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x18),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x2C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x3C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03,
				0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x4C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x0B, 0x0B, 0x0B, 0x0B,
				0x0B, 0x0B, 0x0B, 0x0B, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x5C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x6C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x7C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x8C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x9C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0xA4),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0xA8),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0xB0),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),

	EXYNOS_DSI_CMD_SEQ(0x6F, 0x08),
	EXYNOS_DSI_CMD_SEQ(0xBB, 0x01, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x18),
	EXYNOS_DSI_CMD_SEQ(0xBB, 0x01, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1C),
	EXYNOS_DSI_CMD_SEQ(0xBB, 0x01, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x01),
	EXYNOS_DSI_CMD_SEQ(0xBE, 0x47),

	/* CMD2, Page1 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01),
	/* Adjust source output timing */
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x18),
	EXYNOS_DSI_CMD_SEQ(0xD8, 0x38),
	/* FFC Off */
	EXYNOS_DSI_CMD_SEQ(0xC3, 0x00),
	/* FFC setting (MIPI: 756Mbps) */
	EXYNOS_DSI_CMD_SEQ(0xC3, 0x00, 0x06, 0x20, 0x0C, 0xFF, 0x00, 0x06, 0x20,
				 0x0C, 0xFF, 0x00, 0x04, 0x63, 0x0C, 0x05, 0xD9,
				 0x10, 0x04, 0x63, 0x0C, 0x05, 0xD9, 0x10, 0x04,
				 0x63, 0x0C, 0x05, 0xD9, 0x10, 0x04, 0x63, 0x0C,
				 0x05, 0xD9, 0x10, 0x04, 0x63, 0x0C, 0x05, 0xD9,
				 0x10),

	EXYNOS_DSI_CMD_SEQ(0x6F, 0x05),
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x15, 0x15, 0x15, 0xDD),
	/* Allow VGSP voltage to change earlier */
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x0A),
	EXYNOS_DSI_CMD_SEQ(0xE3, 0x00, 0x00, 0x00, 0x00),

	/* Idle delay frame */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_EVT1), 0x6F, 0x0E),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_EVT1), 0xD2, 0x00),

	/* CMD2, Page3 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x03),
	/* TE width = 275 us */
	EXYNOS_DSI_CMD_SEQ(0xB3, 0x00, 0x00, 0x00, 0x00, 0x60, 0x10, 0x70, 0x40,
				 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				 0x00, 0x00, 0x00, 0x00, 0x60, 0x10, 0x70, 0x40,
				 0x60, 0x10, 0x70, 0x40, 0x60, 0x10, 0x70, 0x40,
				 0x60, 0x10, 0x70, 0x40),

	/* CMD2, Page4 */
	/* b/343326731: Extend DBI Flash Data Update Cycle time */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x04),
	EXYNOS_DSI_CMD_SEQ(0xBB, 0xB3, 0x07, 0x01),

	/* CMD2, Page7 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x07),
	/* Disable round corner and punch hole */
	EXYNOS_DSI_CMD_SEQ(0xC9, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xCA, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xCB, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xCC, 0x00),

	/* CMD3, Page0 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x80),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x19),
	EXYNOS_DSI_CMD_SEQ(0xF2, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1A),
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x55),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x2D),
	EXYNOS_DSI_CMD_SEQ(0xFC, 0x44),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x11),
	EXYNOS_DSI_CMD_SEQ(0xF8, 0x01, 0x7B),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x2D),
	EXYNOS_DSI_CMD_SEQ(0xF8, 0x01, 0x1D),

	/* CMD3, Page1 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x81),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x05),
	EXYNOS_DSI_CMD_SEQ(0xFE, 0x3C),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x02),
	EXYNOS_DSI_CMD_SEQ(0xF9, 0x04),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1E),
	EXYNOS_DSI_CMD_SEQ(0xFB, 0x0F),
	/* disable the repeat-SEQ1 option */
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x0D),
	EXYNOS_DSI_CMD_SEQ(0xFB, 0x84),
	/* BOIS clk gated turn on */
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x0F),
	EXYNOS_DSI_CMD_SEQ(0xF5, 0x20),
	/* CMD3, Page2 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x82),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x09),
	EXYNOS_DSI_CMD_SEQ(0xF2, 0x55),
	/* CMD3, Page3 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x83),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x12),
	EXYNOS_DSI_CMD_SEQ(0xFE, 0x41),

	/* CMD2, Page0 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0xA8),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),

	/* CMD, Disable */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x00),

	/* config 60hz TE setting */
	EXYNOS_DSI_CMD_SEQ(0x2F, 0x30),
	EXYNOS_DSI_CMD_SEQ(0x6D, 0x00, 0x00),

	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_SCANLINE, 0x00, 0x00),
	/* b/241726710, long write 0x35 as a WA */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON, 0x00, 0x20),
	EXYNOS_DSI_CMD_SEQ(0x5A, 0x04),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5F),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_GAMMA_CURVE, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x81, 0x01, 0x19),
	EXYNOS_DSI_CMD_SEQ(0x88, 0x01, 0x02, 0x1C, 0x06, 0xE2, 0x00, 0x00, 0x00, 0x00),
	/* 8bpc PPS */
	EXYNOS_DSI_CMD_SEQ(0x03, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x90, 0x03, 0x03),
	EXYNOS_DSI_CMD_SEQ(0x91, 0x89, 0x28, 0x00, 0x1E, 0xD2, 0x00, 0x02, 0x25, 0x02,
				0xC5, 0x00, 0x07, 0x03, 0x97, 0x03, 0x64, 0x10, 0xF0),
	/* VRGH = 7.4V */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xF0, 0x55, 0xAA, 0x52,
				0x08, 0x01),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xB7, 0x22, 0x22, 0x22,
				0x22, 0x22, 0x22, 0x22),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x6F, 0x07),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xB7, 0x22, 0x22, 0x22,
				0x22, 0x22, 0x22, 0x22),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x6F, 0x11),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xB7, 0x22, 0x22),
	/* Burn-in Compensation */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xF0, 0x55, 0xAA, 0x52,
				0x08, 0x00),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x6F, 0x11),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xB2, 0x01, 0x01, 0x43,
				0x01, 0x03, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x6F, 0x2E),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xB4, 0x00, 0x92, 0x00,
				0x92, 0x01, 0xA9, 0x01, 0xA9, 0x01, 0xE6, 0x01,
				0xE6, 0x02, 0xF2, 0x02, 0xF2, 0x03, 0xCC, 0x03,
				0xCC, 0x05, 0x39, 0x05, 0x39, 0x06, 0xA6, 0x06,
				0xA6, 0x08, 0x2B, 0x08, 0x2B, 0x08, 0x2B, 0x08,
				0x2B, 0x08, 0xD5, 0x08, 0xD5, 0x08, 0xD5, 0x08,
				0xD5, 0x08, 0xD5),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x6F, 0x07),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xBA, 0x00, 0x75, 0x00,
				0x10, 0x00, 0x10, 0x10),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xC0, 0x54),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x6F, 0x03),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xC0, 0x30, 0x75, 0x02),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xF0, 0x55, 0xAA, 0x52,
				0x08, 0x08),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xC2, 0x33, 0x01, 0x78,
				0x03, 0x82, 0x00),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x6F, 0x10),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xC2, 0x04, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xD0, 0x44, 0x00, 0x00,
				0x44, 0x00, 0x00, 0x44, 0x00, 0x00, 0x04, 0x00,
				0x46, 0x00, 0x00, 0x44, 0x00, 0x00, 0x41, 0x00,
				0x00),

	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_EXIT_SLEEP_MODE)
};
static DEFINE_EXYNOS_CMD_SET(bigsurf_init);

static void bigsurf_set_local_hbm_mode(struct exynos_panel *ctx,
				       bool local_hbm_en);

static void bigsurf_update_te2(struct exynos_panel *ctx)
{
	struct exynos_panel_te2_timing timing;
	u8 width = 0x20; /* default width */
	u32 rising = 0, falling;
	int ret;

	if (!ctx)
		return;

	ret = exynos_panel_get_current_mode_te2(ctx, &timing);
	if (!ret) {
		falling = timing.falling_edge;
		if (falling >= timing.rising_edge) {
			rising = timing.rising_edge;
			width = falling - rising;
		} else {
			dev_warn(ctx->dev, "invalid timing, use default setting\n");
		}
	} else if (ret == -EAGAIN) {
		dev_dbg(ctx->dev, "Panel is not ready, use default setting\n");
	} else {
		return;
	}

	dev_dbg(ctx->dev, "TE2 updated: rising= 0x%x, width= 0x%x", rising, width);

	EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_TEAR_SCANLINE, 0x00, rising);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_TEAR_ON, 0x00, width);
}

static void bigsurf_update_irc(struct exynos_panel *ctx,
				const enum exynos_hbm_mode hbm_mode,
				const int vrefresh)
{
	const u16 level = exynos_panel_get_brightness(ctx);

	if (!IS_HBM_ON(hbm_mode)) {
		dev_info(ctx->dev, "hbm is off, skip update irc\n");
		return;
	}

	if (IS_HBM_ON_IRC_OFF(hbm_mode)) {
		if (ctx->panel_rev >= PANEL_REV_EVT1 &&
		    level == ctx->desc->brt_capability->hbm.level.max)
			EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFF);
		EXYNOS_DCS_BUF_ADD(ctx, 0x5F, 0x01);
		if (vrefresh == 120) {
			if (ctx->hbm.local_hbm.enabled) {
				EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
				EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x04);
				EXYNOS_DCS_BUF_ADD(ctx, 0xC0, 0x76);
			}
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x00);
			EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x02);
		} else {
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x30);
			EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
			EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0xB0);
			EXYNOS_DCS_BUF_ADD(ctx, 0xBA, 0x44);
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
		EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x03);
		EXYNOS_DCS_BUF_ADD(ctx, 0xC0, 0x32);
	} else {
		EXYNOS_DCS_BUF_ADD(ctx, 0x5F, 0x00);
		if (vrefresh == 120) {
			if (ctx->hbm.local_hbm.enabled) {
				EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
				EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x04);
				EXYNOS_DCS_BUF_ADD(ctx, 0xC0, 0x75);
			}
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x00);
			EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x00);
		} else {
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x30);
			EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
			EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0xB0);
			EXYNOS_DCS_BUF_ADD(ctx, 0xBA, 0x41);
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
		EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x03);
		EXYNOS_DCS_BUF_ADD(ctx, 0xC0, 0x30);
		if (ctx->panel_rev >= PANEL_REV_EVT1) {
			const u8 val1 = level >> 8;
			const u8 val2 = level & 0xff;

			EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, val1, val2);
		}
	}
	/* Empty command is for flush */
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0x00);
}

static bool bigsurf_rr_need_te_high(struct exynos_panel *ctx,
				    const struct exynos_panel_mode *pmode)
{
	if (!ctx->current_mode || !pmode)
		return false;

	if (drm_mode_vrefresh(&ctx->current_mode->mode) == 60 &&
			drm_mode_vrefresh(&pmode->mode) == 120)
		return true;

	return false;
}

static void bigsurf_change_frequency(struct exynos_panel *ctx,
				    const struct exynos_panel_mode *pmode)
{
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!ctx || (vrefresh != 60 && vrefresh != 120))
		return;

	if (vrefresh != 120 &&
		ctx->hbm.local_hbm.effective_state != LOCAL_HBM_DISABLED) {
		dev_err(ctx->dev,
			"%s: switch to %uhz will fail when LHBM is on, disable LHBM\n",
			__func__, vrefresh);
		bigsurf_set_local_hbm_mode(ctx, false);
		ctx->hbm.local_hbm.effective_state = LOCAL_HBM_DISABLED;
	}

	if (!IS_HBM_ON(ctx->hbm_mode)) {
		if (vrefresh == 120) {
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x00);
			EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x00);
		} else {
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x30);
			EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
			EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0xB0);
			EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xBA, 0x41);
		}
	} else {
		bigsurf_update_irc(ctx, ctx->hbm_mode, vrefresh);
	}

	dev_dbg(ctx->dev, "%s: change to %uhz\n", __func__, vrefresh);
}

static void bigsurf_set_dimming_on(struct exynos_panel *ctx,
				 bool dimming_on)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	if (pmode->exynos_mode.is_lp_mode) {
		dev_warn(ctx->dev, "in lp mode, skip to update\n");
		return;
	}

	ctx->dimming_on = dimming_on;
	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
					ctx->dimming_on ? 0x28 : 0x20);
	dev_dbg(ctx->dev, "%s dimming_on=%d\n", __func__, dimming_on);
}

static void bigsurf_set_nolp_mode(struct exynos_panel *ctx,
				  const struct exynos_panel_mode *pmode)
{
	struct bigsurf_panel *spanel = to_spanel(ctx);
	int vrefresh = drm_mode_vrefresh(&pmode->mode);
	if (!is_panel_active(ctx))
		return;

	/* exit AOD */
	EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	EXYNOS_DCS_BUF_ADD(ctx, 0xC0, 0x54);
	EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_EXIT_IDLE_MODE);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0x5A, 0x04);

	bigsurf_change_frequency(ctx, pmode);
	spanel->idle_exit_dimming_delay_ts = ktime_add_us(
		ktime_get(), 100 + EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh) * 2);

	dev_info(ctx->dev, "exit LP mode\n");
}

static void bigsurf_dimming_frame_setting(struct exynos_panel *ctx, u8 dimming_frame)
{
	if (!dimming_frame)
		dimming_frame = 0x01;

	EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB2, 0x19);
	EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x05);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xB2, dimming_frame, dimming_frame);
}

static int bigsurf_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	struct bigsurf_panel *spanel = to_spanel(ctx);

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);

	exynos_panel_reset(ctx);
	exynos_panel_send_cmd_set(ctx, &bigsurf_init_cmd_set);
	bigsurf_change_frequency(ctx, pmode);
	bigsurf_dimming_frame_setting(ctx, BIGSURF_DIMMING_FRAME);
	spanel->idle_exit_dimming_delay_ts = 0;

	if (!pmode->exynos_mode.is_lp_mode) {
		if (ctx->panel_rev < PANEL_REV_EVT1) {
			/* Gamma update setting */
			EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x02);
			EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xCC, 0x10);
			exynos_panel_msleep(9);
		}
	} else {
		exynos_panel_set_lp_mode(ctx, pmode);
	}

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON);

	spanel->lhbm_ctl.hist_roi_configured = false;
	ctx->dsi_hs_clk_mbps = MIPI_DSI_FREQ_MBPS_DEFAULT;

	return 0;
}

static void bigsurf_update_lhbm_hist_config(struct exynos_panel *ctx)
{
	struct bigsurf_panel *spanel = to_spanel(ctx);
	struct bigsurf_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	/* lhbm center below the center of AA: 563, radius: 101 */
	const int d = 563, r = 101;

	if (ctl->hist_roi_configured)
		return;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return;
	}
	mode = &pmode->mode;
	if (!exynos_drm_connector_set_lhbm_hist(&ctx->exynos_connector,
		mode->hdisplay, mode->vdisplay, d, r)) {
		ctl->hist_roi_configured = true;
		dev_dbg(ctx->dev, "configure lhbm hist: %d %d %d %d\n",
			mode->hdisplay, mode->vdisplay, d, r);
	}
}

static int bigsurf_atomic_check(struct exynos_panel *ctx, struct drm_atomic_state *state)
{
	struct drm_connector *conn = &ctx->exynos_connector.base;
	struct drm_connector_state *new_conn_state =
					drm_atomic_get_new_connector_state(state, conn);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	const struct exynos_panel_mode *pmode;
	bool was_lp_mode, is_lp_mode;

	bigsurf_update_lhbm_hist_config(ctx);

	if (!ctx->current_mode || drm_mode_vrefresh(&ctx->current_mode->mode) == 120 ||
	    !new_conn_state || !new_conn_state->crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	old_crtc_state = drm_atomic_get_old_crtc_state(state, new_conn_state->crtc);
	if (!old_crtc_state || !new_crtc_state || !new_crtc_state->active)
		return 0;

	was_lp_mode = ctx->current_mode->exynos_mode.is_lp_mode;
	/* don't skip update when switching between AoD and normal mode */
	pmode = exynos_panel_get_mode(ctx, &new_crtc_state->mode);
	if (pmode) {
		is_lp_mode = pmode->exynos_mode.is_lp_mode;
		if ((was_lp_mode && !is_lp_mode) || (!was_lp_mode && is_lp_mode))
			new_crtc_state->color_mgmt_changed = true;
	} else {
		dev_err(ctx->dev, "%s: no new mode\n", __func__);
	}

	if (!drm_atomic_crtc_effectively_active(old_crtc_state) ||
	    (was_lp_mode && drm_mode_vrefresh(&new_crtc_state->mode) == 60)) {
		struct drm_display_mode *mode = &new_crtc_state->adjusted_mode;

		mode->clock = mode->htotal * mode->vtotal * 120 / 1000;
		if (mode->clock != new_crtc_state->mode.clock) {
			new_crtc_state->mode_changed = true;
			ctx->exynos_connector.needs_commit = true;
			dev_dbg(ctx->dev, "raise mode (%s) clock to 120hz on %s\n",
				mode->name,
				!drm_atomic_crtc_effectively_active(old_crtc_state) ?
				"resume" : "lp exit");
		}
	} else if (old_crtc_state->adjusted_mode.clock != old_crtc_state->mode.clock) {
		/* clock hacked in last commit due to resume or lp exit, undo that */
		new_crtc_state->mode_changed = true;
		new_crtc_state->adjusted_mode.clock = new_crtc_state->mode.clock;
		ctx->exynos_connector.needs_commit = false;
		dev_dbg(ctx->dev, "restore mode (%s) clock after resume or lp exit\n",
			new_crtc_state->mode.name);
	}

	return 0;
}

static void bigsurf_pre_update_ffc(struct exynos_panel *ctx)
{
	dev_dbg(ctx->dev, "%s\n", __func__);

	DPU_ATRACE_BEGIN(__func__);

	/* FFC off */
	EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xC3, 0x00);

	DPU_ATRACE_END(__func__);
}

static void bigsurf_update_ffc(struct exynos_panel *ctx, unsigned int hs_clk_mbps)
{
	dev_dbg(ctx->dev, "%s: hs_clk_mbps: current=%d, target=%d\n",
		__func__, ctx->dsi_hs_clk_mbps, hs_clk_mbps);

	DPU_ATRACE_BEGIN(__func__);

	if (hs_clk_mbps != MIPI_DSI_FREQ_MBPS_DEFAULT &&
	    hs_clk_mbps != MIPI_DSI_FREQ_MBPS_ALTERNATIVE) {
		dev_warn(ctx->dev, "invalid hs_clk_mbps=%d for FFC\n", hs_clk_mbps);
	} else if (ctx->dsi_hs_clk_mbps != hs_clk_mbps) {
		dev_info(ctx->dev, "%s: updating for hs_clk_mbps=%d\n", __func__, hs_clk_mbps);
		ctx->dsi_hs_clk_mbps = hs_clk_mbps;

		/* Update FFC */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01);
		if (hs_clk_mbps == MIPI_DSI_FREQ_MBPS_DEFAULT)
			EXYNOS_DCS_BUF_ADD(ctx, 0xC3, 0x00, 0x06, 0x20, 0x0C, 0xFF,
						0x00, 0x06, 0x20, 0x0C, 0xFF, 0x00,
						0x04, 0x63, 0x0C, 0x05, 0xD9, 0x10,
						0x04, 0x63, 0x0C, 0x05, 0xD9, 0x10,
						0x04, 0x63, 0x0C, 0x05, 0xD9, 0x10,
						0x04, 0x63, 0x0C, 0x05, 0xD9, 0x10,
						0x04, 0x63, 0x0C, 0x05, 0xD9, 0x10);
		else /* MIPI_DSI_FREQ_MBPS_ALTERNATIVE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xC3, 0x00, 0x06, 0x20, 0x0C, 0xFF,
						0x00, 0x06, 0x20, 0x0C, 0xFF, 0x00,
						0x04, 0x46, 0x0C, 0x06, 0x0D, 0x11,
						0x04, 0x46, 0x0C, 0x06, 0x0D, 0x11,
						0x04, 0x46, 0x0C, 0x06, 0x0D, 0x11,
						0x04, 0x46, 0x0C, 0x06, 0x0D, 0x11,
						0x04, 0x46, 0x0C, 0x06, 0x0D, 0x11);
	}

	/* FFC on */
	EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xC3, 0xDD);

	DPU_ATRACE_END(__func__);
}

static void bigsurf_set_local_hbm_background_brightness(struct exynos_panel *ctx, u16 br)
{
	u16 level;
	u8 val1, val2;

	if (IS_HBM_ON_IRC_OFF(ctx->hbm_mode) && ctx->panel_rev >= PANEL_REV_EVT1 &&
	    br == ctx->desc->brt_capability->hbm.level.max)
		br = 0x0FFF;

	level = br * 4;
	val1 = level >> 8;
	val2 = level & 0xff;

	/* set LHBM background brightness */
	EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x4C);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xDF, val1, val2, val1, val2, val1, val2);
}

static int bigsurf_set_brightness(struct exynos_panel *ctx, u16 br)
{
	struct bigsurf_panel *spanel = to_spanel(ctx);
	u16 old_brightness = spanel->panel_brightness;
	bool low_to_high;

	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	if (br) {
		if (ctx->hbm.local_hbm.enabled)
			bigsurf_set_local_hbm_background_brightness(ctx, br);

		if (spanel->idle_exit_dimming_delay_ts &&
			(ktime_sub(spanel->idle_exit_dimming_delay_ts, ktime_get()) <= 0)) {
			EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
						ctx->dimming_on ? 0x28 : 0x20);
			spanel->idle_exit_dimming_delay_ts = 0;
		}
	}

	/* Check for passing brightness threshold */
	if ((ctx->panel_rev < PANEL_REV_MP) &&
	    ((old_brightness < LHBM_COMPENSATION_THRESHOLD) ^ (br < LHBM_COMPENSATION_THRESHOLD))) {
		low_to_high = old_brightness < LHBM_COMPENSATION_THRESHOLD;
		EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x08);
		EXYNOS_DCS_BUF_ADD(ctx, 0xD0, 0x44, 0x00, 0x00, 0x44, 0x00,
					0x00, 0x44, 0x00, 0x00, 0x04,
					0x00, low_to_high ? 0x46: 0x4A,
					0x00, 0x00, 0x44, 0x00, 0x00,
					low_to_high ? 0x41 : 0x40,
					low_to_high ? 0x00 : 0xAA,
					0x00);
	}
	if (IS_HBM_ON_IRC_OFF(ctx->hbm_mode) && ctx->panel_rev >= PANEL_REV_EVT1 &&
	    br == ctx->desc->brt_capability->hbm.level.max) {
		EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFF);
		dev_dbg(ctx->dev, " apply max DBV when reach hbm max with irc off\n");
	} else {
		EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, br >> 8,
						br & 0xff);
	}
	spanel->panel_brightness = br;
	return 0;
}

static void bigsurf_set_hbm_mode(struct exynos_panel *ctx,
				 enum exynos_hbm_mode hbm_mode)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (ctx->hbm_mode == hbm_mode)
		return;

	bigsurf_update_irc(ctx, hbm_mode, vrefresh);

	ctx->hbm_mode = hbm_mode;
	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(ctx->hbm_mode),
		 IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void bigsurf_set_local_hbm_brightness(struct exynos_panel *ctx, bool is_first_stage)
{
	struct bigsurf_panel *spanel = to_spanel(ctx);
	struct bigsurf_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	const u8 *brt;
	enum bigsurf_lhbm_brt_overdrive_group group = LHBM_OVERDRIVE_GRP_MAX;
	static u8 cmd[LHBM_BRT_CMD_LEN];
	int i;

	dev_info(ctx->dev, "set LHBM brightness at %s stage\n", is_first_stage ? "1st" : "2nd");
	if (is_first_stage) {
		u32 gray = exynos_drm_connector_get_lhbm_gray_level(&ctx->exynos_connector);
		int dbv = exynos_panel_get_brightness(ctx);

		dev_dbg(ctx->dev, "check LHBM overdrive condition | gray=%d dbv=%d\n",
			gray, dbv);
		if (gray < 15)
			group = LHBM_OVERDRIVE_GRP_0_NIT;
		else if (gray < 48 || dbv < 467)
			group = LHBM_OVERDRIVE_GRP_15_NIT;
		else if (dbv < 2287)
			group = LHBM_OVERDRIVE_GRP_200_NIT;
		else
			group = LHBM_OVERDRIVE_GRP_MAX;
		brt = group < LHBM_OVERDRIVE_GRP_MAX ?
			ctl->brt_overdrive[group] : ctl->brt_normal;
	}

	if (group < LHBM_OVERDRIVE_GRP_MAX) {
		brt = ctl->brt_overdrive[group];
		ctl->overdrived = true;
	} else {
		brt = ctl->brt_normal;
		ctl->overdrived = false;
	}
	cmd[0] = bigsurf_lhbm_brightness_reg;
	for (i = 0; i < LHBM_BRT_LEN; i++)
		cmd[i+1] = brt[i];
	dev_dbg(ctx->dev, "set %s brightness: [%d] %*ph\n",
		ctl->overdrived ? "overdrive" : "normal",
		ctl->overdrived ? group : -1, LHBM_BRT_LEN, brt);
	EXYNOS_DCS_BUF_ADD_SET(ctx, bigsurf_cmd2_page2);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, cmd);
}

static void bigsurf_set_local_hbm_mode(struct exynos_panel *ctx,
				       bool local_hbm_en)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (local_hbm_en) {
		u16 level = exynos_panel_get_brightness(ctx);

		if (IS_HBM_ON(ctx->hbm_mode)) {
			bigsurf_update_irc(ctx, ctx->hbm_mode, vrefresh);
		} else if (vrefresh == 120) {
			EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
			EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x04);
			EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xC0, 0x75);
		} else {
			dev_warn(ctx->dev, "enable LHBM at unexpected state (HBM: %d, vrefresh: %dhz)\n",
				ctx->hbm_mode, vrefresh);
		}
		bigsurf_set_local_hbm_background_brightness(ctx, level);
		bigsurf_set_local_hbm_brightness(ctx, true);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x87, 0x05);
	} else {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x87, 0x00);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x2F, 0x00);
	}
}

static void bigsurf_set_local_hbm_mode_post(struct exynos_panel *ctx)
{
	const struct bigsurf_panel *spanel = to_spanel(ctx);

	if (spanel->lhbm_ctl.overdrived)
		bigsurf_set_local_hbm_brightness(ctx, false);
}

static void bigsurf_mode_set(struct exynos_panel *ctx,
			     const struct exynos_panel_mode *pmode)
{
	bigsurf_change_frequency(ctx, pmode);
}

static bool bigsurf_is_mode_seamless(const struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	const struct drm_display_mode *c = &ctx->current_mode->mode;
	const struct drm_display_mode *n = &pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay) &&
	       (c->flags == n->flags);
}

static void bigsurf_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	const u8 build_code = (id & 0xFF00) >> 8;
	const u8 main = (build_code & 0xE0) >> 3;
	const u8 sub = (build_code & 0x0C) >> 2;

	exynos_panel_get_panel_rev(ctx, main | sub);
}

static int bigsurf_read_id(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	char buf[BIGSURF_DDIC_ID_LEN] = {0};
	int ret;

	EXYNOS_DCS_WRITE_SEQ(ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	ret = mipi_dsi_dcs_read(dsi, 0xF2, buf, BIGSURF_DDIC_ID_LEN);
	if (ret != BIGSURF_DDIC_ID_LEN) {
		dev_warn(ctx->dev, "Unable to read DDIC id (%d)\n", ret);
		goto done;
	} else {
		ret = 0;
	}

	exynos_bin2hex(buf, BIGSURF_DDIC_ID_LEN,
		ctx->panel_id, sizeof(ctx->panel_id));
done:
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x00);
	return ret;
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 280,
	.te_var = 1,
};

/* Truncate 8-bit signed value to 6-bit signed value */
#define TO_6BIT_SIGNED(v) ((v) & 0x3F)

static const struct drm_dsc_config bigsurf_dsc_cfg = {
	.first_line_bpg_offset = 13,
	.rc_range_params = {
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{4, 10, TO_6BIT_SIGNED(-10)},
		{5, 10, TO_6BIT_SIGNED(-10)},
		{5, 11, TO_6BIT_SIGNED(-10)},
		{5, 11, TO_6BIT_SIGNED(-12)},
		{8, 12, TO_6BIT_SIGNED(-12)},
		{12, 13, TO_6BIT_SIGNED(-12)},
	},
};

#define BIGSURF_DSC_CONFIG \
	.dsc = { \
		.enabled = true, \
		.dsc_count = 2, \
		.slice_count = 2, \
		.slice_height = 30, \
		.cfg = &bigsurf_dsc_cfg, \
	}

static const struct exynos_panel_mode bigsurf_modes[] = {
	{
		.mode = {
			.name = "1080x2400x60",
			.clock = 168498,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 8604,
			.bpc = 8,
			BIGSURF_DSC_CONFIG,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 0,
			.falling_edge = 32,
		},
	},
	{
		.mode = {
			.name = "1080x2400x120",
			.clock = 336996,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 274,
			.bpc = 8,
			BIGSURF_DSC_CONFIG,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 0,
			.falling_edge = 32,
		},
	},
};

static const struct exynos_panel_mode bigsurf_lp_mode = {
	.mode = {
		.name = "1080x2400x30",
		.clock = 84249,
		.hdisplay = 1080,
		.hsync_start = 1080 + 32, // add hfp
		.hsync_end = 1080 + 32 + 12, // add hsa
		.htotal = 1080 + 32 + 12 + 26, // add hbp
		.vdisplay = 2400,
		.vsync_start = 2400 + 12, // add vfp
		.vsync_end = 2400 + 12 + 4, // add vsa
		.vtotal = 2400 + 12 + 4 + 26, // add vbp
		.flags = 0,
		.type = DRM_MODE_TYPE_DRIVER,
		.width_mm = WIDTH_MM,
		.height_mm = HEIGHT_MM,
	},
	.exynos_mode = {
		.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
		.vblank_usec = 120,
		.bpc = 8,
		BIGSURF_DSC_CONFIG,
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static void _update_lhbm_overdrive_brightness(struct exynos_panel *ctx,
	enum bigsurf_lhbm_brt_overdrive_group grp,
	enum bigsurf_lhbm_brt ch, u8 offset)
{
	struct bigsurf_panel *spanel = to_spanel(ctx);
	u8 *p_norm = spanel->lhbm_ctl.brt_normal;
	u8 *p_over = spanel->lhbm_ctl.brt_overdrive[grp];
	u16 val;
	int p = ch * 2;

	val = (p_norm[p] << 8) | p_norm[p + 1];
	val += offset;
	p_over[p] = (val & 0xFF00) >> 8;
	p_over[p + 1] = val & 0x00FF;
}

static void bigsurf_lhbm_brightness_init(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct bigsurf_panel *spanel = to_spanel(ctx);
	int ret;
	enum bigsurf_lhbm_brt_overdrive_group grp;
	u8 *p_norm = spanel->lhbm_ctl.brt_normal;

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, bigsurf_cmd2_page2);
	ret = mipi_dsi_dcs_read(dsi, bigsurf_lhbm_brightness_reg, p_norm, LHBM_BRT_LEN);
	if (ret != LHBM_BRT_LEN) {
		dev_err(ctx->dev, "failed to read lhbm brightness ret=%d\n", ret);
		return;
	}
	dev_dbg(ctx->dev, "lhbm normal brightness: %*ph\n", LHBM_BRT_LEN, p_norm);

	/* 0 nit */
	grp = LHBM_OVERDRIVE_GRP_0_NIT;
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_R, 0xB5);
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_G, 0x85);
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_B, 0xBA);
	/* 0-15 nit */
	grp = LHBM_OVERDRIVE_GRP_15_NIT;
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_R, 0x6E);
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_G, 0x5A);
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_B, 0x78);
	/* 15-200 nit */
	grp = LHBM_OVERDRIVE_GRP_200_NIT;
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_R, 0x46);
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_G, 0x32);
	_update_lhbm_overdrive_brightness(ctx, grp, LHBM_B, 0x50);

	for (grp = 0; grp < LHBM_OVERDRIVE_GRP_MAX; grp++)
		dev_dbg(ctx->dev, "lhbm overdrive brightness[%d]: %*ph\n",
			grp, LHBM_BRT_LEN, spanel->lhbm_ctl.brt_overdrive[grp]);
}

static void bigsurf_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	struct dentry *panel_root, *csroot;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot) {
		goto panel_out;
	}

	exynos_panel_debugfs_create_cmdset(ctx, csroot, &bigsurf_init_cmd_set, "init");
	dput(csroot);
panel_out:
	dput(panel_root);
}

static void bigsurf_panel_init(struct exynos_panel *ctx)
{
	struct bigsurf_panel *spanel = to_spanel(ctx);
	bigsurf_dimming_frame_setting(ctx, BIGSURF_DIMMING_FRAME);
	bigsurf_lhbm_brightness_init(ctx);
	spanel->panel_brightness = exynos_panel_get_brightness(ctx);
}

static int bigsurf_panel_probe(struct mipi_dsi_device *dsi)
{
	struct bigsurf_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	return exynos_panel_common_init(dsi, &spanel->base);
}

static const struct drm_panel_funcs bigsurf_drm_funcs = {
	.disable = exynos_panel_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = bigsurf_enable,
	.get_modes = exynos_panel_get_modes,
	.debugfs_init = bigsurf_debugfs_init,
};

static int bigsurf_panel_config(struct exynos_panel *ctx);

static const struct exynos_panel_funcs bigsurf_exynos_funcs = {
	.set_brightness = bigsurf_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = bigsurf_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = bigsurf_set_hbm_mode,
	.set_local_hbm_mode = bigsurf_set_local_hbm_mode,
	.set_local_hbm_mode_post = bigsurf_set_local_hbm_mode_post,
	.set_dimming_on = bigsurf_set_dimming_on,
	.is_mode_seamless = bigsurf_is_mode_seamless,
	.mode_set = bigsurf_mode_set,
	.panel_init = bigsurf_panel_init,
	.panel_config = bigsurf_panel_config,
	.get_panel_rev = bigsurf_get_panel_rev,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = bigsurf_update_te2,
	.read_id = bigsurf_read_id,
	.atomic_check = bigsurf_atomic_check,
	.pre_update_ffc = bigsurf_pre_update_ffc,
	.update_ffc = bigsurf_update_ffc,
	.rr_need_te_high = bigsurf_rr_need_te_high,
};

static const struct exynos_brightness_configuration bigsurf_btr_configs[] = {
	{
		.panel_rev = PANEL_REV_EVT1 | PANEL_REV_EVT1_1 | PANEL_REV_LATEST,
		.dft_brightness = 1966,
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 1000,
				},
				.level = {
					.min = 1,
					.max = 3574,
				},
				.percentage = {
					.min = 0,
					.max = 71,
				},
			},
			.hbm = {
				.nits = {
					.min = 1000,
					.max = 1400,
				},
				.level = {
					.min = 3575,
					.max = 3827,
				},
				.percentage = {
					.min = 71,
					.max = 100,
				},
			},
		},
	},
	{
		.panel_rev = PANEL_REV_PROTO1_1,
		.dft_brightness = 2242,
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 800,
				},
				.level = {
					.min = 268,
					.max = 3672,
				},
				.percentage = {
					.min = 0,
					.max = 57,
				},
			},
			.hbm = {
				.nits = {
					.min = 800,
					.max = 1400,
				},
				.level = {
					.min = 3673,
					.max = 4095,
				},
				.percentage = {
					.min = 57,
					.max = 100,
				},
			},
		},
	},
	{
		.panel_rev = PANEL_REV_PROTO1,
		.dft_brightness = 2395,
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 800,
				},
				.level = {
					.min = 290,
					.max = 3789,
				},
				.percentage = {
					.min = 0,
					.max = 67,
				},
			},
			.hbm = {
				.nits = {
					.min = 800,
					.max = 1200,
				},
				.level = {
					.min = 3790,
					.max = 4094,
				},
				.percentage = {
					.min = 67,
					.max = 100,
				},
			},
		},
	},
};

struct exynos_panel_desc google_bigsurf = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.modes = bigsurf_modes,
	.num_modes = ARRAY_SIZE(bigsurf_modes),
	.off_cmd_set = &bigsurf_off_cmd_set,
	.lp_mode = &bigsurf_lp_mode,
	.lp_cmd_set = &bigsurf_lp_cmd_set,
	.binned_lp = bigsurf_binned_lp,
	.num_binned_lp = ARRAY_SIZE(bigsurf_binned_lp),
	.panel_func = &bigsurf_drm_funcs,
	.exynos_panel_func = &bigsurf_exynos_funcs,
	.lhbm_effective_delay_frames = 2,
	.lhbm_post_cmd_delay_frames = 3,
	.lhbm_on_delay_frames = 2,
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ_MBPS_DEFAULT,
	.reset_timing_ms = {1, 1, 20},
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDD, 10},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 1},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDI, 0},
	},
	.refresh_on_lp = true,
};

static int bigsurf_panel_config(struct exynos_panel *ctx)
{
	int ret;

	exynos_panel_model_init(ctx, PROJECT, 0);

	ret = exynos_panel_init_brightness(&google_bigsurf,
						bigsurf_btr_configs,
						ARRAY_SIZE(bigsurf_btr_configs),
						ctx->panel_rev);

	if (ctx->panel_rev == PANEL_REV_EVT1) {
		google_bigsurf.min_brightness = 268;
		google_bigsurf.max_brightness = 4095;
	}

	return ret;
}

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,bigsurf", .data = &google_bigsurf },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = bigsurf_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-bigsurf",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Ken Huang <kenbshuang@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google bigsurf panel driver");
MODULE_LICENSE("GPL");
