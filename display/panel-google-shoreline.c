// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based Google Shoreline panel driver.
 *
 * Copyright (c) 2022 Google LLC
 */

#include <drm/drm_vblank.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "include/trace/dpu_trace.h"
#include "panel/panel-samsung-drv.h"

static const struct drm_dsc_config pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 540,
	.slice_height = 48,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2400,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 526,
	.block_pred_enable = true,
	.first_line_bpg_offset = 12,
	.initial_offset = 6144,
	.rc_buf_thresh = {
		14, 28, 42, 56,
		70, 84, 98, 105,
		112, 119, 121, 123,
		125, 126
	},
	.rc_range_params = {
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 2},
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 5, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 6, .range_bpg_offset = 62},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 60},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 58},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 8, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 9, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 10, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 12, .range_bpg_offset = 52},
		{.range_min_qp = 5, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 7, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 13, .range_max_qp = 15, .range_bpg_offset = 52}
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 7,
	.scale_increment_interval = 1190,
	.nfl_bpg_offset = 523,
	.slice_bpg_offset = 543,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 540,
	.dsc_version_minor = 1,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

#define SHORELINE_WRCTRLD_DIMMING_BIT    0x08
#define SHORELINE_WRCTRLD_BCTRL_BIT      0x20
#define SHORELINE_WRCTRLD_LOCAL_HBM_BIT  0x10

#define SHORELINE_TE2_RISING_EDGE_60HZ       0x0F
#define SHORELINE_TE2_FALLING_EDGE_60HZ      0x2F
#define SHORELINE_TE2_RISING_EDGE_120HZ_AOD  0x960
#define SHORELINE_TE2_FALLING_EDGE_120HZ_AOD 0x30

#define MIPI_DSI_FREQ_DEFAULT 756
#define MIPI_DSI_FREQ_ALTERNATIVE 776

#define MIPI_DSI_FREQ_DEFAULT 756
#define MIPI_DSI_FREQ_ALTERNATIVE 776

#define WIDTH_MM 64
#define HEIGHT_MM 143

#define PROJECT "SB3"

static const u8 test_key_on_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_off_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 test_key_on_fc[] = { 0xFC, 0x5A, 0x5A };
static const u8 test_key_off_fc[] = { 0xFC, 0xA5, 0xA5 };
static const u8 sync_begin[] = { 0x72, 0x2C, 0x2C, 0xA1, 0x00, 0x00 };
static const u8 sync_end[] = { 0x72, 0x2C, 0x2C, 0x81, 0x00, 0x00 };
static const u8 freq_update[] = { 0xF7, 0x0F };
static const u8 lhbm_brightness_index[] = { 0xB0, 0x03, 0xD7, 0x66 };
static const u8 lhbm_brightness_reg = 0x66;

static const struct exynos_dsi_cmd shoreline_lp_low_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(34, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x25), /* AOD 10 nit */
};

static const struct exynos_dsi_cmd shoreline_lp_high_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(34, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24), /* AOD 50 nit */
};

static const struct exynos_binned_lp shoreline_binned_lp[] = {
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 819, shoreline_lp_low_cmds,
			SHORELINE_TE2_RISING_EDGE_120HZ_AOD, SHORELINE_TE2_FALLING_EDGE_120HZ_AOD),
	BINNED_LP_MODE_TIMING("high", 4095, shoreline_lp_high_cmds,
			SHORELINE_TE2_RISING_EDGE_120HZ_AOD, SHORELINE_TE2_FALLING_EDGE_120HZ_AOD),
};


static const struct exynos_dsi_cmd shoreline_vgh_init_cmds[] = {
	/* VGH/VLIN1 Setting (P1.0~EVT1.1 only) */
	EXYNOS_DSI_CMD0(test_key_on_f0),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x0A, 0xB5), /* global para */
	EXYNOS_DSI_CMD_SEQ(0xB5, 0x08), /* NBM/HBM VLIN 7.9V */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x0E, 0xB5), /* global para */
	EXYNOS_DSI_CMD_SEQ(0xB5, 0x00), /* AOD VLIN 7.9V */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x0F, 0xF4), /* global para */
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18), /* VGH 7.4V */
	EXYNOS_DSI_CMD0(test_key_off_f0),
 };
 static DEFINE_EXYNOS_CMD_SET(shoreline_vgh_init);

 static const struct exynos_dsi_cmd shoreline_vreg_init_cmds[] = {
	/* VREG 4.5V Set */
	EXYNOS_DSI_CMD0(test_key_on_f0),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x60, 0xF4), /* global para */
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x50), /* AMP Type Change */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x3A, 0xF4), /* global para */
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), /* VREG 4.5V */
	EXYNOS_DSI_CMD0(test_key_off_f0),
 };
 static DEFINE_EXYNOS_CMD_SET(shoreline_vreg_init);

static const struct exynos_dsi_cmd shoreline_init_cmds[] = {
	/* GPO DC Settings */
	EXYNOS_DSI_CMD0(test_key_on_f0),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x0B, 0xF2),
	EXYNOS_DSI_CMD_SEQ(0xF2, 0x0C, 0x00, 0x04),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x1F, 0xCB),
	EXYNOS_DSI_CMD_SEQ(0xCB, 0xFF),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x22, 0xCB),
	EXYNOS_DSI_CMD_SEQ(0xCB, 0xC2),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x8C, 0xCB),
	EXYNOS_DSI_CMD_SEQ(0xCB, 0xBA),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x01, 0xBD),
	EXYNOS_DSI_CMD_SEQ(0xBD, 0x81, 0x01),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x2F, 0xBD),
	EXYNOS_DSI_CMD_SEQ(0xBD, 0x02),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x82, 0xBD),
	EXYNOS_DSI_CMD_SEQ(0xBD, 0x02),
	EXYNOS_DSI_CMD(freq_update, 115),

	/* Common Settings */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5F),

	/* FFC Settings (off, OSC: 180 MHz, MIPI: 756 Mbps) */
	EXYNOS_DSI_CMD0(test_key_on_fc), /* Test Key Enable */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x36, 0xC5), /* Global Para */
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x10), /* FFC OFF */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x3E, 0xC5), /* Global Para 120HS */
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x98, 0x62), /* OSC frequency Setting */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x46, 0xC5), /* Global Para 60HS */
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x98, 0x62), /* OSC frequency Setting */
	EXYNOS_DSI_CMD0(test_key_off_fc), /* Test Key Disable */

	/* DBV Voltage Workaround */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_EVT1, 0x60, 0x00, 0x00), /* 120Hz HS */
	EXYNOS_DSI_CMD0_REV(freq_update, PANEL_REV_EVT1), /* Freq Update */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_EVT1, 0x71, 0xC6, 0x00, 0x00, 0xC8), /* Setting Enable */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_EVT1, 0x6C, 0xEC, 0xFD, 0x0D, 0x1D, 0x2D, /* Voltage Set */
						0x39, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8,
						0x39, 0x39, 0x39, 0x00, 0x00),

	/* LHBM Location */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x09, 0x6D), /* global para */
	EXYNOS_DSI_CMD_SEQ(0x6D, 0xC6, 0xE3, 0x65), /* Size and Location */
	EXYNOS_DSI_CMD0(test_key_off_f0),
};
static DEFINE_EXYNOS_CMD_SET(shoreline_init);

#define LHBM_GAMMA_CMD_SIZE 6
#define VREG_SET_CMD_SIZE 8

/**
 * enum shoreline_lhbm_brt - local hbm brightness
 * @LHBM_R_COARSE: red coarse
 * @LHBM_GB_COARSE: green and blue coarse
 * @LHBM_R_FINE: red fine
 * @LHBM_G_FINE: green fine
 * @LHBM_B_FINE: blue fine
 * @LHBM_BRT_LEN: local hbm brightness array length
 */
enum shoreline_lhbm_brt {
	LHBM_R_COARSE = 0,
	LHBM_GB_COARSE,
	LHBM_R_FINE,
	LHBM_G_FINE,
	LHBM_B_FINE,
	LHBM_BRT_LEN
};

/**
 * enum shoreline_lhbm_brt_overdrive_group - lhbm brightness overdrive group number
 * @LHBM_OVERDRIVE_GRP_0_NIT: group number for 0 nit
 * @LHBM_OVERDRIVE_GRP_6_NIT: group number for 0-6 nits
 * @LHBM_OVERDRIVE_GRP_50_NIT: group number for 6-50 nits
 * @LHBM_OVERDRIVE_GRP_300_NIT: group number for 50-300 nits
 * @LHBM_OVERDRIVE_GRP_MAX: maximum group number
 */
enum shoreline_lhbm_brt_overdrive_group {
	LHBM_OVERDRIVE_GRP_0_NIT = 0,
	LHBM_OVERDRIVE_GRP_6_NIT,
	LHBM_OVERDRIVE_GRP_50_NIT,
	LHBM_OVERDRIVE_GRP_300_NIT,
	LHBM_OVERDRIVE_GRP_MAX
};

struct shoreline_lhbm_ctl {
	/** @brt_normal: normal LHBM brightness parameters */
	u8 brt_normal[LHBM_BRT_LEN];
	/** @brt_overdrive: overdrive LHBM brightness parameters */
	u8 brt_overdrive[LHBM_OVERDRIVE_GRP_MAX][LHBM_BRT_LEN];
	/** @overdrived: whether LHBM is overdrived */
	bool overdrived;
	/** @hist_roi_configured: whether LHBM histogram configuration is done */
	bool hist_roi_configured;
};

/**
 * struct shoreline_panel - panel specific runtime info
 *
 * This struct maintains shoreline panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc
 */
struct shoreline_panel {
	/** @base: base panel struct */
	struct exynos_panel base;

	/** @lhbm_gamma: lhbm gamma data */
	u8 lhbm_gamma[LHBM_GAMMA_CMD_SIZE];
	/** @lhbm_ctl: lhbm brightness control */
	struct shoreline_lhbm_ctl lhbm_ctl;

	/** @vreg_cmd: vreg data */
	u8 vreg_cmd[VREG_SET_CMD_SIZE];
};

#define to_spanel(ctx) container_of(ctx, struct shoreline_panel, base)

static void shoreline_lhbm_gamma_read(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct shoreline_panel *spanel = to_spanel(ctx);
	int ret;
	u8 *lhbm_gamma = spanel->lhbm_gamma;

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x22, 0xD8); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xD8, lhbm_gamma + 1, LHBM_GAMMA_CMD_SIZE - 1);
	if (ret == (LHBM_GAMMA_CMD_SIZE - 1)) {
		/* fill in gamma write command 0x66 in offset 0 */
		lhbm_gamma[0] = 0x66;
		dev_info(ctx->dev, "lhbm gamma: %*phN\n", LHBM_GAMMA_CMD_SIZE - 1, lhbm_gamma + 1);
	} else {
		dev_err(ctx->dev, "fail to read LHBM gamma\n");
	}

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
}

static void shoreline_lhbm_gamma_write(struct exynos_panel *ctx)
{
	struct shoreline_panel *spanel = to_spanel(ctx);

	if (!spanel->lhbm_gamma[0]) {
		dev_err(ctx->dev, "%s: no lhbm gamma!\n", __func__);
		return;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x03, 0xD7, 0x66); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->lhbm_gamma); /* write gamma */
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void shoreline_wait_for_vsync_done(struct exynos_panel *ctx)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	DPU_ATRACE_BEGIN(__func__);
	exynos_panel_wait_for_vsync_done(ctx, pmode->exynos_mode.te_usec,
			EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh));

	/* Additional sleep time to account for TE variability*/
	usleep_range(1000, 1010);
	DPU_ATRACE_END(__func__);
}

static void shoreline_vreg_read(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct shoreline_panel *spanel = to_spanel(ctx);
	u8 *vreg_cmd = spanel->vreg_cmd;
	int ret;

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x3A , 0xF4); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xF4, vreg_cmd + 1, VREG_SET_CMD_SIZE - 1);
	if (ret == (VREG_SET_CMD_SIZE - 1)) {
		/* fill in vreg command 0xF4 in offset 0 */
		vreg_cmd[0] = 0xF4;
		dev_dbg(ctx->dev, "vreg: %*phN\n", VREG_SET_CMD_SIZE - 1, vreg_cmd + 1);
	} else {
		dev_err(ctx->dev, "fail to read vreg setting\n");
	}

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
};

static void shoreline_display_on(struct exynos_panel *ctx)
{
	struct shoreline_panel *spanel = to_spanel(ctx);

	if (spanel->vreg_cmd[0]) {
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
		EXYNOS_DCS_BUF_ADD_SET(ctx, sync_begin);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x12, 0xF8); /* global para */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF8, 0x3F); /* auto power saving off */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x60, 0xF4); /* global para */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF4, 0x70); /* AMP type Return */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x3A, 0xF4);/* global para */
		EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->vreg_cmd); /* VREG OTP Value */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x12, 0xF8); /* global para */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF8, 0x00); /* auto power saving on */
		EXYNOS_DCS_BUF_ADD_SET(ctx, sync_end);
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_off_f0);
	}

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_DISPLAY_ON);
};

static void shoreline_display_off(struct exynos_panel *ctx)
{
	struct shoreline_panel *spanel = to_spanel(ctx);

	EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	if (spanel->vreg_cmd[0]) {
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
		EXYNOS_DCS_BUF_ADD_SET(ctx, sync_begin);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x12, 0xF8); /* global para */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF8, 0x3F); /* auto power saving off */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x60, 0xF4); /* global para */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF4, 0x50);		 /* AMP Type Change */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x3A, 0xF4); /* global para */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF4, 0x00, 0x00, 0x00,  /* VREG 4.5V */
					0x00, 0x00, 0x00, 0x00);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x12, 0xF8); /* global para */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF8, 0x00); /* auto power saving on */
		EXYNOS_DCS_BUF_ADD_SET(ctx, sync_end);
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_off_f0);
	}

	/* Empty command to flush */
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0x00);
};

static void shoreline_update_te(struct exynos_panel *ctx, const unsigned int vrefresh)
{
	static const u8 te_setting[2][5] = {
		{0xB9, 0x09, 0x74, 0x00, 0x0C}, /* HS 60Hz */
		{0xB9, 0x00, 0x44, 0x00, 0x0C}, /* HS 120Hz */
	};

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, (vrefresh == 60) ? 0x11 : 0x31); /* TE SELECT */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x10, 0xB9); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, te_setting[(vrefresh == 60) ? 0 : 1]); /* TE Width */
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

/**
 * shoreline_update_te2 - update the TE2 for shoreline panels
 * @ctx: panel struct
 *
 * Current definition of the B9h command parameter:
 *
 * TE2 rising at 120Hz:
 * Refer to next vsync falling and shift left, min 0x1, max 0x96F
 *
 * TE2 falling at 120Hz:
 * Refer to current vsync falling and shift right, min 0x2, max 0x970
 *
 * TE2 rising at 60Hz (with GPO DC):
 * Refer to current vsync falling and shift right, min 0x0, max 0x96E
 *
 * TE2 falling at 60Hz (with GPO DC):
 * Refer to current vsync falling and shift right, min 0x1, max 0x96F
 */
static void shoreline_update_te2(struct exynos_panel *ctx)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	unsigned int vrefresh = drm_mode_vrefresh(&pmode->mode);
	struct exynos_panel_te2_timing timing = {
		.rising_edge = SHORELINE_TE2_RISING_EDGE_60HZ,
		.falling_edge = SHORELINE_TE2_FALLING_EDGE_60HZ,
	};
	u32 rising, falling;
	int ret;

	if (!ctx)
		return;

	/* Not needed to update TE2 in LP mode */
	if (ctx->current_mode->exynos_mode.is_lp_mode)
		return;

	ret = exynos_panel_get_current_mode_te2(ctx, &timing);
	if (ret) {
		dev_dbg(ctx->dev, "failed to get TE2 timng\n");
		return;
	}
	rising = timing.rising_edge;
	falling = timing.falling_edge;

	dev_dbg(ctx->dev, "TE2 updated: rising=0x%X falling=0x%X for %uHz\n",
		rising, falling, vrefresh);

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0xB9); /* global para */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, (vrefresh == 60) ? 0x04 : 0x31); /* TE2 SELECT */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, (vrefresh == 60) ? 0x1A : 0x26, 0xB9); /* global para */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, (rising >> 8) & 0xFF, rising & 0xFF,
			     (falling >> 8) & 0xFF, falling & 0xFF); /* TE2 Width */
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void shoreline_change_frequency(struct exynos_panel *ctx,
				       const unsigned int vrefresh)
{
	if (!ctx || (vrefresh != 60 && vrefresh != 120))
		return;

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, (vrefresh == 120) ? 0x00 : 0x08, 0x00);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	shoreline_update_te(ctx, vrefresh);

	dev_dbg(ctx->dev, "frequency changed to %uhz\n", vrefresh);
}

static void shoreline_update_lhbm_hist_config(struct exynos_panel *ctx)
{
	struct shoreline_panel *spanel = to_spanel(ctx);
	struct shoreline_lhbm_ctl *ctl = &spanel->lhbm_ctl;
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

static int shoreline_atomic_check(struct exynos_panel *ctx, struct drm_atomic_state *state)
{
	shoreline_update_lhbm_hist_config(ctx);
	return 0;
}

static void shoreline_pre_update_ffc(struct exynos_panel *ctx)
{
	dev_dbg(ctx->dev, "%s\n", __func__);

	DPU_ATRACE_BEGIN(__func__);

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_fc);
	/* FFC off */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x36, 0xC5);
	EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x10);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_off_fc);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	DPU_ATRACE_END(__func__);
}

static void shoreline_update_ffc(struct exynos_panel *ctx, unsigned int hs_clk)
{
	dev_dbg(ctx->dev, "%s: hs_clk: current=%d, target=%d\n",
		__func__, ctx->dsi_hs_clk, hs_clk);

	DPU_ATRACE_BEGIN(__func__);

	if (hs_clk != MIPI_DSI_FREQ_DEFAULT && hs_clk != MIPI_DSI_FREQ_ALTERNATIVE) {
		dev_warn(ctx->dev, "%s: invalid hs_clk=%d for FFC\n", __func__, hs_clk);
	} else if (ctx->dsi_hs_clk != hs_clk) {
		dev_info(ctx->dev, "%s: updating for hs_clk=%d\n", __func__, hs_clk);
		ctx->dsi_hs_clk = hs_clk;

		/* Update FFC */
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_fc);
		/* 120HS */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x3E, 0xC5);
		if (hs_clk == MIPI_DSI_FREQ_DEFAULT)
			EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x98, 0x62);
		else /* MIPI_DSI_FREQ_ALTERNATIVE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x94, 0x74);
		/* 60HS */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x46, 0xC5);
		if (hs_clk == MIPI_DSI_FREQ_DEFAULT)
			EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x98, 0x62);
		else /* MIPI_DSI_FREQ_ALTERNATIVE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x94, 0x74);
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_off_fc);
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_off_f0);
	}

	/* FFC on */
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_fc);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x36, 0xC5);
	EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x11, 0x10, 0x50, 0x05);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_off_fc);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	DPU_ATRACE_END(__func__);
}

static void shoreline_update_wrctrld(struct exynos_panel *ctx)
{
	u8 val = SHORELINE_WRCTRLD_BCTRL_BIT;

	if (ctx->hbm.local_hbm.enabled)
		val |= SHORELINE_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= SHORELINE_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev,
		"%s(wrctrld:%#x, hbm: %s, dimming: %s, local_hbm: %s)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		ctx->dimming_on ? "on" : "off",
		ctx->hbm.local_hbm.enabled ? "on" : "off");

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);

	/* TODO: need to perform gamma updates */
}

static void shoreline_set_lp_mode(struct exynos_panel *ctx, const struct exynos_panel_mode *pmode)
{
	const u16 brightness = exynos_panel_get_brightness(ctx);
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	shoreline_update_te(ctx, vrefresh);

	exynos_panel_set_binned_lp(ctx, brightness);

	dev_info(ctx->dev, "enter %dhz LP mode\n", vrefresh);
}

static void shoreline_set_nolp_mode(struct exynos_panel *ctx,
				    const struct exynos_panel_mode *pmode)
{
	unsigned int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!ctx->enabled)
		return;

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	/* backlight control and dimming */
	shoreline_update_wrctrld(ctx);
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
	shoreline_change_frequency(ctx, vrefresh);
	shoreline_wait_for_vsync_done(ctx);

	dev_info(ctx->dev, "exit LP mode\n");
}

static int shoreline_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	struct drm_dsc_picture_parameter_set pps_payload;
	struct shoreline_panel *spanel = to_spanel(ctx);

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;

	dev_dbg(ctx->dev, "%s\n", __func__);

	exynos_panel_reset(ctx);

	/* DSC related configuration */
	drm_dsc_pps_payload_pack(&pps_payload, &pps_config);
	exynos_dcs_compression_mode(ctx, 0x1); /* DSC_DEC_ON */
	EXYNOS_PPS_WRITE_BUF(ctx, &pps_payload);

	EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 5, MIPI_DCS_EXIT_SLEEP_MODE);

	if (ctx->panel_rev < PANEL_REV_DVT1)
		exynos_panel_send_cmd_set(ctx, &shoreline_vgh_init_cmd_set);

	if (spanel->vreg_cmd[0])
		exynos_panel_send_cmd_set(ctx, &shoreline_vreg_init_cmd_set);

	exynos_panel_send_cmd_set(ctx, &shoreline_init_cmd_set);

	shoreline_change_frequency(ctx, drm_mode_vrefresh(mode));

	shoreline_lhbm_gamma_write(ctx);

	shoreline_update_wrctrld(ctx); /* dimming and HBM */

	ctx->enabled = true;

	if (pmode->exynos_mode.is_lp_mode)
		shoreline_set_lp_mode(ctx, pmode);
	shoreline_wait_for_vsync_done(ctx);
	shoreline_display_on(ctx);

	spanel->lhbm_ctl.hist_roi_configured = false;
	ctx->dsi_hs_clk = MIPI_DSI_FREQ_DEFAULT;

	return 0;
}

static int shoreline_disable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	int ret, vrefresh, delay_us;

	dev_dbg(ctx->dev, "%s\n", __func__);

	ret = exynos_panel_disable(panel);
	if (ret)
		return ret;

	vrefresh = drm_mode_vrefresh(&(ctx->current_mode->mode));
	delay_us = EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh) + 1000;
	exynos_panel_msleep(delay_us / 1000);

	shoreline_display_off(ctx);
	exynos_panel_msleep(20);
	EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 100, MIPI_DCS_ENTER_SLEEP_MODE);

	return 0;
}

static void shoreline_set_hbm_mode(struct exynos_panel *ctx,
				enum exynos_hbm_mode mode)
{
	const bool hbm_update =
		(IS_HBM_ON(ctx->hbm_mode) != IS_HBM_ON(mode));
	const bool irc_update =
		(IS_HBM_ON_IRC_OFF(ctx->hbm_mode) != IS_HBM_ON_IRC_OFF(mode));
	static const u8 cyc[2][6] = {
		{0xBD, 0x01, 0x81, 0x01, 0x01, 0x03}, /* Normal EM CYC */
		{0xBD, 0x01, 0x80, 0x00, 0x01, 0x01}, /* HBM EM CYC */
	};

	if (!hbm_update && !irc_update)
		return;

	ctx->hbm_mode = mode;

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);

	if (hbm_update) {
		/* CYC Set */
		EXYNOS_DCS_BUF_ADD_SET(ctx, cyc[IS_HBM_ON(mode)]);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x2F, 0xBD);
		EXYNOS_DCS_BUF_ADD(ctx, 0xBD, IS_HBM_ON(mode) ? 0x01 : 0x02);
		/* Update Key */
		EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	}

	if (irc_update && IS_HBM_ON(mode)) {
		if (ctx->panel_rev < PANEL_REV_EVT1) {
			/* Global para */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0x6A);
			/* IRC Setting */
			EXYNOS_DCS_BUF_ADD(ctx, 0x6A, IS_HBM_ON_IRC_OFF(mode) ? 0x01 : 0x21);
		} else {
			static const u8 irc_mode[2][5] = {
				{0x6B, 0x00, 0x00, 0xFF, 0x90}, /* Flat gamma */
				{0x6B, 0x11, 0xDB, 0xFF, 0x94}, /* FGZ Mode */
			};

			/* Global para */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x0A, 0x6B);
			/* IRC Setting */
			EXYNOS_DCS_BUF_ADD_SET(ctx, irc_mode[IS_HBM_ON_IRC_OFF(mode)]);
		}
	}
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
	shoreline_update_wrctrld(ctx);

	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(ctx->hbm_mode),
		 IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void shoreline_set_dimming_on(struct exynos_panel *exynos_panel,
				 bool dimming_on)
{
	const struct exynos_panel_mode *pmode = exynos_panel->current_mode;

	exynos_panel->dimming_on = dimming_on;
	if (pmode->exynos_mode.is_lp_mode) {
		dev_info(exynos_panel->dev, "in lp mode, skip to update\n");
		return;
	}

	shoreline_update_wrctrld(exynos_panel);
}

static void shoreline_set_local_hbm_brightness(struct exynos_panel *ctx, bool is_first_stage)
{
	struct shoreline_panel *spanel = to_spanel(ctx);
	struct shoreline_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	const u8 *brt;
	enum shoreline_lhbm_brt_overdrive_group group = LHBM_OVERDRIVE_GRP_MAX;
	/* command uses one byte besiddes brightness */
	static u8 cmd[LHBM_BRT_LEN + 1];
	int i;

	if (!is_local_hbm_post_enabling_supported(ctx))
		return;

	dev_info(ctx->dev, "set LHBM brightness at %s stage\n", is_first_stage ? "1st" : "2nd");
	if (is_first_stage) {
		u32 gray = exynos_drm_connector_get_lhbm_gray_level(&ctx->exynos_connector);
		u32 dbv = exynos_panel_get_brightness(ctx);
		u32 normal_dbv_max = ctx->desc->brt_capability->normal.level.max;
		u32 normal_nit_max = ctx->desc->brt_capability->normal.nits.max;
		u32 luma = 0;

		if (gray < 15) {
			group = LHBM_OVERDRIVE_GRP_0_NIT;
		} else {
			if (dbv <= normal_dbv_max)
				luma = panel_cmn_calc_gamma_2_2_luminance(dbv, normal_dbv_max,
				normal_nit_max);
			else
				luma = panel_cmn_calc_linear_luminance(dbv, 645, -1256);
			luma = panel_cmn_calc_gamma_2_2_luminance(gray, 255, luma);

			if (luma < 6)
				group = LHBM_OVERDRIVE_GRP_6_NIT;
			else if (luma < 50)
				group = LHBM_OVERDRIVE_GRP_50_NIT;
			else if (luma < 300)
				group = LHBM_OVERDRIVE_GRP_300_NIT;
			else
				group = LHBM_OVERDRIVE_GRP_MAX;
		}
		dev_dbg(ctx->dev, "check LHBM overdrive condition | gray=%u dbv=%u luma=%u\n",
			gray, dbv, luma);
	}

	if (group < LHBM_OVERDRIVE_GRP_MAX) {
		brt = ctl->brt_overdrive[group];
		ctl->overdrived = true;
	} else {
		brt = ctl->brt_normal;
		ctl->overdrived = false;
	}
	cmd[0] = lhbm_brightness_reg;
	for (i = 0; i < LHBM_BRT_LEN; i++)
		cmd[i+1] = brt[i];
	dev_dbg(ctx->dev, "set %s brightness: [%d] %*ph\n",
		ctl->overdrived ? "overdrive" : "normal",
		ctl->overdrived ? group : -1, LHBM_BRT_LEN, brt);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD_SET(ctx, lhbm_brightness_index);
	EXYNOS_DCS_BUF_ADD_SET(ctx, cmd);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void shoreline_set_local_hbm_mode_post(struct exynos_panel *ctx)
{
	const struct shoreline_panel *spanel = to_spanel(ctx);

	if (spanel->lhbm_ctl.overdrived)
		shoreline_set_local_hbm_brightness(ctx, false);
}

static void shoreline_set_local_hbm_mode(struct exynos_panel *exynos_panel,
				 bool local_hbm_en)
{
	shoreline_update_wrctrld(exynos_panel);

	if (local_hbm_en)
		shoreline_set_local_hbm_brightness(exynos_panel, true);
}

static void shoreline_mode_set(struct exynos_panel *ctx,
			       const struct exynos_panel_mode *pmode)
{
	shoreline_change_frequency(ctx, drm_mode_vrefresh(&pmode->mode));
}

static bool shoreline_is_mode_seamless(const struct exynos_panel *ctx,
				       const struct exynos_panel_mode *pmode)
{
	/* seamless mode switch is possible if only changing refresh rate */
	return drm_mode_equal_no_clocks(&ctx->current_mode->mode, &pmode->mode);
}

static void shoreline_calc_lhbm_od_brightness(u8 n_fine, u8 n_coarse,
	u8 *o_fine, u8 *o_coarse,
	u8 fine_offset_0, u8 fine_offset_1,
	u8 coarse_offset_0, u8 coarse_offset_1)
{
	if (n_fine <= (0xFF - fine_offset_0)) {
		*o_coarse = n_coarse + coarse_offset_0;
		*o_fine = n_fine + fine_offset_0;
	} else {
		*o_coarse = n_coarse + coarse_offset_1;
		*o_fine = n_fine - fine_offset_1;
	}
}

static void shoreline_lhbm_brightness_init(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct shoreline_panel *spanel = to_spanel(ctx);
	struct shoreline_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	int ret;
	u8 g_coarse, b_coarse;
	u8 *p_norm = ctl->brt_normal;
	u8 *p_over;
	enum shoreline_lhbm_brt_overdrive_group grp;

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_TABLE(ctx, lhbm_brightness_index);
	ret = mipi_dsi_dcs_read(dsi, lhbm_brightness_reg, p_norm, LHBM_BRT_LEN);
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
	if (ret != LHBM_BRT_LEN) {
		dev_err(ctx->dev, "failed to read lhbm para ret=%d\n", ret);
		return;
	}
	dev_info(ctx->dev, "lhbm normal brightness: %*ph\n", LHBM_BRT_LEN, p_norm);

	/* 0 nit */
	grp = LHBM_OVERDRIVE_GRP_0_NIT;
	p_over = ctl->brt_overdrive[grp];
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x83, 0x5A, 0x00, 0x01);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x90, 0x60, 0x00, 0x10);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0xB0, 0x23, 0x00, 0x01);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	/* 0 - 6 nits */
	grp = LHBM_OVERDRIVE_GRP_6_NIT;
	p_over = ctl->brt_overdrive[grp];
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x53, 0x8A, 0x00, 0x01);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x60, 0x90, 0x00, 0x10);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0x80, 0x53, 0x00, 0x01);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	/* 6 - 50 nits */
	grp = LHBM_OVERDRIVE_GRP_50_NIT;
	p_over = ctl->brt_overdrive[grp];
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x36, 0x9E, 0x00, 0x01);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x49, 0xA4, 0x00, 0x10);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0x66, 0x67, 0x00, 0x01);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	/* 50 - 300 nits */
	grp = LHBM_OVERDRIVE_GRP_300_NIT;
	p_over = ctl->brt_overdrive[grp];
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x16, 0xBE, 0x00, 0x01);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x29, 0xC4, 0x00, 0x10);
	shoreline_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0x46, 0x87, 0x00, 0x01);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	print_hex_dump_debug("shoreline-od-brightness: ", DUMP_PREFIX_NONE,
		16, 1,
		ctl->brt_overdrive, sizeof(ctl->brt_overdrive), false);
}

static void shoreline_panel_init(struct exynos_panel *ctx)
{
	struct dentry *csroot = ctx->debugfs_cmdset_entry;

	exynos_panel_debugfs_create_cmdset(ctx, csroot,
					   &shoreline_init_cmd_set, "init");
	shoreline_lhbm_gamma_read(ctx);
	shoreline_lhbm_gamma_write(ctx);

	shoreline_vreg_read(ctx);

	/* LHBM overdrive init */
	shoreline_lhbm_brightness_init(ctx);
	/* LHBM Location */
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x09, 0x6D);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0x6D, 0xC6, 0xE3, 0x65);
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
}

static int shoreline_read_id(struct exynos_panel *ctx)
{
	return exynos_panel_read_ddic_id(ctx);
}

static void shoreline_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	const u8 build_code = (id & 0xFF00) >> 8;
	const u8 main = (build_code & 0xE0) >> 3;
	const u8 sub = (build_code & 0x0C) >> 2;

	exynos_panel_get_panel_rev(ctx, main | sub);
}

static int shoreline_panel_probe(struct mipi_dsi_device *dsi)
{
	struct shoreline_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 120;

	return exynos_panel_common_init(dsi, &spanel->base);
}


static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 280,
	.te_var = 1,
};

static const u32 shoreline_bl_range[] = {
	95, 205, 315, 400, 4095
};

#define GOOGLE_SHORELINE_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.slice_count = 2,\
	.slice_height = 48,\
	.cfg = &pps_config,\
	}
static const struct exynos_panel_mode shoreline_modes[] = {
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
			.te_usec = 8356,
			.bpc = 8,
			.dsc = GOOGLE_SHORELINE_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = SHORELINE_TE2_RISING_EDGE_60HZ,
			.falling_edge = SHORELINE_TE2_FALLING_EDGE_60HZ,
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
			.te_usec = 273,
			.bpc = 8,
			.dsc = GOOGLE_SHORELINE_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = SHORELINE_TE2_RISING_EDGE_120HZ_AOD,
			.falling_edge = SHORELINE_TE2_FALLING_EDGE_120HZ_AOD,
		},
	},
};

static const struct exynos_panel_mode shoreline_lp_mode = {
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
		.te_usec = 1100,
		.bpc = 8,
		.dsc = GOOGLE_SHORELINE_DSC,
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static const struct drm_panel_funcs shoreline_drm_funcs = {
	.disable = shoreline_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = shoreline_enable,
	.get_modes = exynos_panel_get_modes,
};

static int shoreline_panel_config(struct exynos_panel *ctx);

static const struct exynos_panel_funcs shoreline_exynos_funcs = {
	.set_brightness = exynos_panel_set_brightness,
	.set_lp_mode = shoreline_set_lp_mode,
	.set_nolp_mode = shoreline_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = shoreline_set_hbm_mode,
	.set_dimming_on = shoreline_set_dimming_on,
	.set_local_hbm_mode = shoreline_set_local_hbm_mode,
	.set_local_hbm_mode_post = shoreline_set_local_hbm_mode_post,
	.is_mode_seamless = shoreline_is_mode_seamless,
	.mode_set = shoreline_mode_set,
	.panel_config = shoreline_panel_config,
	.panel_init = shoreline_panel_init,
	.get_panel_rev = shoreline_get_panel_rev,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = shoreline_update_te2,
	.read_id = shoreline_read_id,
	.atomic_check = shoreline_atomic_check,
	.pre_update_ffc = shoreline_pre_update_ffc,
	.update_ffc = shoreline_update_ffc,
};

static const struct exynos_brightness_configuration shoreline_btr_configs[] = {
	{
		.panel_rev = PANEL_REV_EVT1 | PANEL_REV_EVT1_1 | PANEL_REV_LATEST,
		.dft_brightness = 1431,
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 1000,
				},
				.level = {
					.min = 209,
					.max = 3514,
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
					.min = 3515,
					.max = 4095,
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
		.dft_brightness = 1454,
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 800,
				},
				.level = {
					.min = 209,
					.max = 3175,
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
					.min = 3176,
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
		.dft_brightness = 374,
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 800,
				},
				.level = {
					.min = 2,
					.max = 2047,
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
					.min = 2048,
					.max = 4095,
				},
				.percentage = {
					.min = 67,
					.max = 100,
				},
			},
		},
	},
};

static struct exynos_panel_desc google_shoreline = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.bl_range = shoreline_bl_range,
	.bl_num_ranges = ARRAY_SIZE(shoreline_bl_range),
	.modes = shoreline_modes,
	.num_modes = ARRAY_SIZE(shoreline_modes),
	.lp_mode = &shoreline_lp_mode,
	.binned_lp = shoreline_binned_lp,
	.num_binned_lp = ARRAY_SIZE(shoreline_binned_lp),
	.panel_func = &shoreline_drm_funcs,
	.exynos_panel_func = &shoreline_exynos_funcs,
	.lhbm_effective_delay_frames = 1,
	.lhbm_post_cmd_delay_frames = 1,
	.default_dsi_hs_clk = MIPI_DSI_FREQ_DEFAULT,
	.reset_timing_ms = {1, 1, 20},
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDD, 10},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDI, 0},
	},
};

static int shoreline_panel_config(struct exynos_panel *ctx)
{
	exynos_panel_model_init(ctx, PROJECT, 0);

	return exynos_panel_init_brightness(&google_shoreline,
						shoreline_btr_configs,
						ARRAY_SIZE(shoreline_btr_configs),
						ctx->panel_rev);
}

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,shoreline", .data = &google_shoreline },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = shoreline_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-shoreline",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Jeremy DeHaan <jdehaan@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google Shoreline panel driver");
MODULE_LICENSE("GPL");
