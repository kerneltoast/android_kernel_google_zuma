// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based HK3 AMOLED LCD panel driver.
 *
 * Copyright (c) 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/thermal.h>
#include <video/mipi_display.h>

#include "include/trace/dpu_trace.h"
#include "include/trace/panel_trace.h"
#include "panel/panel-samsung-drv.h"

/**
 * enum hk3_panel_feature - features supported by this panel
 * @FEAT_HBM: high brightness mode
 * @FEAT_IRC_OFF: IR compensation off state
 * @FEAT_IRC_Z_MODE: IR compensation on state and use Flat Z mode
 * @FEAT_EARLY_EXIT: early exit from a long frame
 * @FEAT_OP_NS: normal speed (not high speed)
 * @FEAT_FRAME_AUTO: automatic (not manual) frame control
 * @FEAT_MAX: placeholder, counter for number of features
 *
 * The following features are correlated, if one or more of them change, the others need
 * to be updated unconditionally.
 */
enum hk3_panel_feature {
	FEAT_HBM = 0,
	FEAT_IRC_OFF,
	FEAT_IRC_Z_MODE,
	FEAT_EARLY_EXIT,
	FEAT_OP_NS,
	FEAT_FRAME_AUTO,
	FEAT_MAX,
};

/**
 * enum hk3_lhbm_brt - local hbm brightness
 * @LHBM_R_COARSE: red coarse
 * @LHBM_GB_COARSE: green and blue coarse
 * @LHBM_R_FINE: red fine
 * @LHBM_G_FINE: green fine
 * @LHBM_B_FINE: blue fine
 * @LHBM_BRT_LEN: local hbm brightness array length
 */
enum hk3_lhbm_brt {
	LHBM_R_COARSE = 0,
	LHBM_GB_COARSE,
	LHBM_R_FINE,
	LHBM_G_FINE,
	LHBM_B_FINE,
	LHBM_BRT_LEN
};
#define LHBM_BRT_CMD_LEN (LHBM_BRT_LEN + 1)

/**
 * enum hk3_lhbm_brt_overdrive_group - lhbm brightness overdrive group number
 * @LHBM_OVERDRIVE_GRP_0_NIT: group number for 0 nit
 * @LHBM_OVERDRIVE_GRP_6_NIT: group number for 0-6 nits
 * @LHBM_OVERDRIVE_GRP_50_NIT: group number for 6-50 nits
 * @LHBM_OVERDRIVE_GRP_300_NIT: group number for 50-300 nits
 * @LHBM_OVERDRIVE_GRP_MAX: maximum group number
 */
enum hk3_lhbm_brt_overdrive_group {
	LHBM_OVERDRIVE_GRP_0_NIT = 0,
	LHBM_OVERDRIVE_GRP_6_NIT,
	LHBM_OVERDRIVE_GRP_50_NIT,
	LHBM_OVERDRIVE_GRP_300_NIT,
	LHBM_OVERDRIVE_GRP_MAX
};

/**
 * enum hk3_material - different materials in HW
 * @MATERIAL_E6: EVT1 material E6
 * @MATERIAL_E7_DOE: EVT1 material E7
 * @MATERIAL_E7: EVT1.1 maetrial E7
 * @MATERIAL_LPC5: EVT1.1 material LPC5
 */
enum hk3_material {
	MATERIAL_E6 = 0,
	MATERIAL_E7_DOE,
	MATERIAL_E7,
	MATERIAL_LPC5
};

struct hk3_lhbm_ctl {
	/** @brt_normal: normal LHBM brightness parameters */
	u8 brt_normal[LHBM_BRT_LEN];
	/** @brt_overdrive: overdrive LHBM brightness parameters */
	u8 brt_overdrive[LHBM_OVERDRIVE_GRP_MAX][LHBM_BRT_LEN];
	/** @overdrived: whether LHBM is overdrived */
	bool overdrived;
	/** @hist_roi_configured: whether LHBM histogram configuration is done */
	bool hist_roi_configured;
};

#define HK3_VREG_STR_SIZE 11
#define HK3_VREG_PARAM_NUM 5

/**
 * HK3_VREG_STR
 * @ctx: exynos_panel struct
 *
 * Expect to have five values for the Vreg parameters:
 * EVT1.1 and earlier: 0x1B
 * DVT1 and later: 0x1A
 */
#define HK3_VREG_STR(ctx) (((ctx)->panel_rev >= PANEL_REV_DVT1) ? "1a1a1a1a1a" : "1b1b1b1b1b")

/**
 * struct hk3_panel - panel specific info
 *
 * This struct maintains hk3 panel specific info. The variables with the prefix hw_ keep
 * track of the features that were actually committed to hardware, and should be modified
 * after sending cmds to panel, i.e. updating hw state.
 */
struct hk3_panel {
	/** @base: base panel struct */
	struct exynos_panel base;
	/** @feat: software or working correlated features, not guaranteed to be effective in panel */
	DECLARE_BITMAP(feat, FEAT_MAX);
	/** @hw_feat: correlated states effective in panel */
	DECLARE_BITMAP(hw_feat, FEAT_MAX);
	/** @hw_vrefresh: vrefresh rate effective in panel */
	u32 hw_vrefresh;
	/** @hw_idle_vrefresh: idle vrefresh rate effective in panel */
	u32 hw_idle_vrefresh;
	/**
	 * @auto_mode_vrefresh: indicates current minimum refresh rate while in auto mode,
	 *			if 0 it means that auto mode is not enabled
	 */
	u32 auto_mode_vrefresh;
	/** @force_changeable_te: force changeable TE (instead of fixed) during early exit */
	bool force_changeable_te;
	/** @force_changeable_te2: force changeable TE (instead of fixed) for monitoring refresh rate */
	bool force_changeable_te2;
	/** @hw_acl_setting: automatic current limiting setting */
	u8 hw_acl_setting;
	/** @hw_dbv: indicate the current dbv, will be zero after sleep in/out */
	u16 hw_dbv;
	/** @hw_za_enabled: whether zonal attenuation is enabled */
	bool hw_za_enabled;
	/** @force_za_off: force to turn off zonal attenuation */
	bool force_za_off;
	/** @lhbm_ctl: lhbm brightness control */
	struct hk3_lhbm_ctl lhbm_ctl;
	/** @material: the material version used in panel */
	enum hk3_material material;
	/** @tz: thermal zone device for reading temperature */
	struct thermal_zone_device *tz;
	/** @hw_temp: the temperature applied into panel */
	u32 hw_temp;
	/**
	 * @pending_temp_update: whether there is pending temperature update. It will be
	 *                       handled in the commit_done function.
	 */
	bool pending_temp_update;
	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on or resetting
	 *		  panel can recover to normal mode after entering pixel-off state.
	 */
	bool is_pixel_off;
	/** @hw_vreg: the Vreg setting after calling hk3_read_back_vreg() */
	char hw_vreg[HK3_VREG_STR_SIZE];
	/**
	 * @read_vreg: whether need to read back Vreg setting after self_refresh. The Vreg cannot
	 *	       be read right after it's set, so we have to wait for taking effect, but
	 *	       cannot block the main thread.
	 */
	bool read_vreg;
};

#define to_spanel(ctx) container_of(ctx, struct hk3_panel, base)

/* 1344x2992 */
static const struct drm_dsc_config wqhd_pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 672,
	.slice_height = 187,
	.simple_422 = false,
	.pic_width = 1344,
	.pic_height = 2992,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 592,
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
	.scale_decrement_interval = 9,
	.scale_increment_interval = 5177,
	.nfl_bpg_offset = 133,
	.slice_bpg_offset = 112,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 672,
	.dsc_version_minor = 1,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

/* 1008x2244 */
static const struct drm_dsc_config fhd_pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 504,
	.slice_height = 187,
	.simple_422 = false,
	.pic_width = 1008,
	.pic_height = 2244,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 508,
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
	.scale_increment_interval = 4482,
	.nfl_bpg_offset = 133,
	.slice_bpg_offset = 150,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 504,
	.dsc_version_minor = 1,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

#define HK3_WRCTRLD_DIMMING_BIT    0x08
#define HK3_WRCTRLD_BCTRL_BIT      0x20
#define HK3_WRCTRLD_HBM_BIT        0xC0
#define HK3_WRCTRLD_LOCAL_HBM_BIT  0x10

#define HK3_TE2_CHANGEABLE 0x04
#define HK3_TE2_FIXED      0x51
#define HK3_TE2_RISING_EDGE_OFFSET 0x10
#define HK3_TE2_FALLING_EDGE_OFFSET 0x30
#define HK3_TE2_FALLING_EDGE_OFFSET_NS 0x25

#define HK3_TE_USEC_AOD 693
#define HK3_TE_USEC_120HZ 273
#define HK3_TE_USEC_60HZ_HS 8500
#define HK3_TE_USEC_60HZ_NS 1223
#define HK3_TE_PERIOD_DELTA_TOLERANCE_USEC 2000

#define MIPI_DSI_FREQ_DEFAULT 1368
#define MIPI_DSI_FREQ_ALTERNATIVE 1346

#define PROJECT "HK3"

static const u8 unlock_cmd_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 lock_cmd_f0[]   = { 0xF0, 0xA5, 0xA5 };
static const u8 freq_update[] = { 0xF7, 0x0F };
static const u8 lhbm_brightness_index[] = { 0xB0, 0x03, 0x21, 0x95 };
static const u8 lhbm_brightness_reg = 0x95;
static const u8 pixel_off[] = { 0x22 };
static const u8 sync_begin[] = { 0xE4, 0x00, 0x2C, 0x2C, 0xA2, 0x00, 0x00 };
static const u8 sync_end[] = { 0xE4, 0x00, 0x2C, 0x2C, 0x82, 0x00, 0x00 };
static const u8 aod_on[] = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24 };
static const u8 aod_off[] = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20 };
/* 50 nits */
static const u8 aod_dbv[] = { MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x03, 0x55 };

static const struct exynos_dsi_cmd hk3_lp_low_cmds[] = {
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	/* AOD Low Mode, 10nit */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x52, 0x94),
	EXYNOS_DSI_CMD_SEQ(0x94, 0x01, 0x07, 0x6A, 0x02),
	EXYNOS_DSI_CMD0(lock_cmd_f0),
};

static const struct exynos_dsi_cmd hk3_lp_high_cmds[] = {
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	/* AOD High Mode, 50nit */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x52, 0x94),
	EXYNOS_DSI_CMD_SEQ(0x94, 0x00, 0x07, 0x6A, 0x02),
	EXYNOS_DSI_CMD0(lock_cmd_f0),
};

static const struct exynos_binned_lp hk3_binned_lp[] = {
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 766, hk3_lp_low_cmds,
			      HK3_TE2_RISING_EDGE_OFFSET, HK3_TE2_FALLING_EDGE_OFFSET),
	BINNED_LP_MODE_TIMING("high", 3307, hk3_lp_high_cmds,
			      HK3_TE2_RISING_EDGE_OFFSET, HK3_TE2_FALLING_EDGE_OFFSET)
};

static inline bool is_in_comp_range(int temp)
{
	return (temp >= 10 && temp <= 49);
}

/* Read temperature and apply appropriate gain into DDIC for burn-in compensation if needed */
static void hk3_update_disp_therm(struct exynos_panel *ctx)
{
	/* temperature*1000 in celsius */
	int temp, ret;
	struct hk3_panel *spanel = to_spanel(ctx);

	if (IS_ERR_OR_NULL(spanel->tz))
		return;

	if (ctx->panel_rev < PANEL_REV_EVT1_1 || ctx->panel_state != PANEL_STATE_NORMAL)
		return;

	spanel->pending_temp_update = false;

	ret = thermal_zone_get_temp(spanel->tz, &temp);
	if (ret) {
		dev_err(ctx->dev, "%s: fail to read temperature ret:%d\n", __func__, ret);
		return;
	}

	temp = DIV_ROUND_CLOSEST(temp, 1000);
	dev_dbg(ctx->dev, "%s: temp=%d\n", __func__, temp);
	if (temp == spanel->hw_temp || !is_in_comp_range(temp))
		return;

	dev_dbg(ctx->dev, "%s: apply gain into ddic at %ddeg c\n", __func__, temp);

	DPU_ATRACE_BEGIN(__func__);
	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x03, 0x67);
	EXYNOS_DCS_BUF_ADD(ctx, 0x67, temp);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
	DPU_ATRACE_END(__func__);

	spanel->hw_temp = temp;
}

static u8 hk3_get_te2_option(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	if (!ctx || !ctx->current_mode || spanel->force_changeable_te2)
		return HK3_TE2_CHANGEABLE;

	if (ctx->current_mode->exynos_mode.is_lp_mode ||
	    (test_bit(FEAT_EARLY_EXIT, spanel->feat) &&
		spanel->auto_mode_vrefresh < 30))
		return HK3_TE2_FIXED;

	return HK3_TE2_CHANGEABLE;
}

static void hk3_update_te2_internal(struct exynos_panel *ctx, bool lock)
{
	struct exynos_panel_te2_timing timing = {
		.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
		.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
	};
	u32 rising, falling;
	struct hk3_panel *spanel = to_spanel(ctx);
	u8 option = hk3_get_te2_option(ctx);
	u8 idx;

	if (!ctx)
		return;

	/* skip TE2 update if going through RRS */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
	    ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		dev_dbg(ctx->dev, "%s: RRS in progress, skip\n", __func__);
		return;
	}

	if (test_bit(FEAT_OP_NS, spanel->feat)) {
		rising = HK3_TE2_RISING_EDGE_OFFSET;
		falling = HK3_TE2_FALLING_EDGE_OFFSET_NS;
	} else {
		if (exynos_panel_get_current_mode_te2(ctx, &timing)) {
			dev_dbg(ctx->dev, "failed to get TE2 timng\n");
			return;
		}
		rising = timing.rising_edge;
		falling = timing.falling_edge;
	}

	ctx->te2.option = (option == HK3_TE2_FIXED) ? TE2_OPT_FIXED : TE2_OPT_CHANGEABLE;

	dev_dbg(ctx->dev,
		"TE2 updated: %s mode, option %s, idle %s, rising=0x%X falling=0x%X\n",
		test_bit(FEAT_OP_NS, spanel->feat) ? "NS" : "HS",
		(option == HK3_TE2_CHANGEABLE) ? "changeable" : "fixed",
		ctx->panel_idle_vrefresh ? "active" : "inactive",
		rising, falling);

	if (lock)
		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x42, 0xF2);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x0D);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0xB9);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, option);
	idx = option == HK3_TE2_FIXED ? 0x22 : 0x1E;
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, idx, 0xB9);
	if (option == HK3_TE2_FIXED) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB9, (rising >> 8) & 0xF, rising & 0xFF,
			(falling >> 8) & 0xF, falling & 0xFF,
			(rising >> 8) & 0xF, rising & 0xFF,
			(falling >> 8) & 0xF, falling & 0xFF);
	} else {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB9, (rising >> 8) & 0xF, rising & 0xFF,
			(falling >> 8) & 0xF, falling & 0xFF);
	}
	if (lock)
		EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
}

static void hk3_update_te2(struct exynos_panel *ctx)
{
	hk3_update_te2_internal(ctx, true);
}

static inline bool is_auto_mode_allowed(struct exynos_panel *ctx)
{
	/* don't want to enable auto mode/early exit during dimming on */
	if (ctx->dimming_on)
		return false;

	if (ctx->idle_delay_ms) {
		const unsigned int delta_ms = panel_get_idle_time_delta(ctx);

		if (delta_ms < ctx->idle_delay_ms)
			return false;
	}

	return ctx->panel_idle_enabled;
}

static u32 hk3_get_min_idle_vrefresh(struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	const int vrefresh = drm_mode_vrefresh(&pmode->mode);
	int min_idle_vrefresh = ctx->min_vrefresh;

	if ((min_idle_vrefresh < 0) || !is_auto_mode_allowed(ctx))
		return 0;

	if (min_idle_vrefresh <= 1)
		min_idle_vrefresh = 1;
	else if (min_idle_vrefresh <= 10)
		min_idle_vrefresh = 10;
	else if (min_idle_vrefresh <= 30)
		min_idle_vrefresh = 30;
	else
		return 0;

	if (min_idle_vrefresh >= vrefresh) {
		dev_dbg(ctx->dev, "min idle vrefresh (%d) higher than target (%d)\n",
				min_idle_vrefresh, vrefresh);
		return 0;
	}

	dev_dbg(ctx->dev, "%s: min_idle_vrefresh %d\n", __func__, min_idle_vrefresh);

	return min_idle_vrefresh;
}

static void hk3_set_panel_feat(struct exynos_panel *ctx,
	const u32 vrefresh, const u32 idle_vrefresh, const unsigned long *feat, bool enforce)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	u8 val;
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

	if (enforce) {
		bitmap_fill(changed_feat, FEAT_MAX);
	} else {
		bitmap_xor(changed_feat, feat, spanel->hw_feat, FEAT_MAX);
		if (bitmap_empty(changed_feat, FEAT_MAX) &&
			vrefresh == spanel->hw_vrefresh &&
			idle_vrefresh == spanel->hw_idle_vrefresh) {
			dev_dbg(ctx->dev, "%s: no changes, skip update\n", __func__);
			return;
		}
	}

	spanel->hw_vrefresh = vrefresh;
	spanel->hw_idle_vrefresh = idle_vrefresh;
	bitmap_copy(spanel->hw_feat, feat, FEAT_MAX);
	dev_dbg(ctx->dev,
		"op=%s ee=%s hbm=%s irc=%s fi=%s fps=%u idle_fps=%u\n",
		test_bit(FEAT_OP_NS, feat) ? "ns" : "hs",
		test_bit(FEAT_EARLY_EXIT, feat) ? "on" : "off",
		test_bit(FEAT_HBM, feat) ? "on" : "off",
		ctx->panel_rev >= PANEL_REV_EVT1 ?
			(test_bit(FEAT_IRC_Z_MODE, feat) ? "flat_z" : "flat") :
			(test_bit(FEAT_IRC_OFF, feat) ? "off" : "on"),
		test_bit(FEAT_FRAME_AUTO, feat) ? "auto" : "manual",
		vrefresh,
		idle_vrefresh);

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);

	/* TE setting */
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) ||
		test_bit(FEAT_OP_NS, changed_feat)) {
		if (test_bit(FEAT_EARLY_EXIT, feat) && !spanel->force_changeable_te) {
			/* Fixed TE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x51);
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x02, 0xB9);
			val = test_bit(FEAT_OP_NS, feat) ? 0x01 : 0x00;
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, val);
			/* Fixed TE width setting */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x08, 0xB9);
			if (test_bit(FEAT_OP_NS, feat)) {
				EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0x43, 0x00, 0x2F,
					0x0B, 0x43, 0x00, 0x2F);
			} else {
				EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0xBB, 0x00, 0x2F,
					0x0B, 0xBB, 0x00, 0x2F);
			}
		} else {
			/* Changeable TE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x04);
			/* Changeable TE width setting and frequency */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x04, 0xB9);
			if (test_bit(FEAT_OP_NS, feat))
				EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0x43, 0x00, 0x2F);
			else
				EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0xBB, 0x00, 0x2F);
		}
	}

	/* TE2 setting */
	if (test_bit(FEAT_OP_NS, changed_feat))
		hk3_update_te2_internal(ctx, false);

	/*
	 * HBM IRC setting
	 *
	 * Description: after EVT1, IRC will be always on. "Flat mode" is used to
	 * replace IRC on for normal mode and HDR video, and "Flat Z mode" is used
	 * to replace IRC off for sunlight environment.
	 */
	if (ctx->panel_rev >= PANEL_REV_EVT1) {
		if (test_bit(FEAT_IRC_Z_MODE, changed_feat)) {
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x02, 0x00, 0x92);
			if (test_bit(FEAT_IRC_Z_MODE, feat)) {
				if (spanel->material == MATERIAL_E6) {
					EXYNOS_DCS_BUF_ADD(ctx, 0x92, 0xBE, 0x98);
					EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x02, 0xF3, 0x68);
					EXYNOS_DCS_BUF_ADD(ctx, 0x68, 0x97, 0x87, 0x87, 0xFB, 0xFD, 0xF1);
				} else {
					EXYNOS_DCS_BUF_ADD(ctx, 0x92, 0xF1, 0xC1);
					EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x02, 0xF3, 0x68);
					EXYNOS_DCS_BUF_ADD(ctx, 0x68, 0x82, 0x70, 0x23, 0x91, 0x88, 0x3C);
				}
			} else {
				EXYNOS_DCS_BUF_ADD(ctx, 0x92, 0x00, 0x00);
				EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x02, 0xF3, 0x68);
				if (spanel->material == MATERIAL_E6)
					EXYNOS_DCS_BUF_ADD(ctx, 0x68, 0x71, 0x81, 0x59, 0x90, 0xA2, 0x80);
				else
					EXYNOS_DCS_BUF_ADD(ctx, 0x68, 0x77, 0x81, 0x23, 0x8C, 0x99, 0x3C);
			}
		}
	} else {
		if (test_bit(FEAT_IRC_OFF, changed_feat)) {
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x01, 0x9B, 0x92);
			val = test_bit(FEAT_IRC_OFF, feat) ? 0x07 : 0x27;
			EXYNOS_DCS_BUF_ADD(ctx, 0x92, val);
		}
	}

	/*
	 * Operating Mode: NS or HS
	 *
	 * Description: the configs could possibly be overrided by frequency setting,
	 * depending on FI mode.
	 */
	if (test_bit(FEAT_OP_NS, changed_feat)) {
		/* mode set */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x01);
		val = test_bit(FEAT_OP_NS, feat) ? 0x18 : 0x00;
		EXYNOS_DCS_BUF_ADD(ctx, 0x60, val);
	}

	/*
	 * Note: the following command sequence should be sent as a whole if one of panel
	 * state defined by enum panel_state changes or at turning on panel, or unexpected
	 * behaviors will be seen, e.g. black screen, flicker.
	 */

	/*
	 * Early-exit: enable or disable
	 *
	 * Description: early-exit sequence overrides some configs HBM set.
	 */
	if (test_bit(FEAT_EARLY_EXIT, feat)) {
		if (test_bit(FEAT_HBM, feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x00, 0x83, 0x03, 0x01);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x01, 0x83, 0x03, 0x03);
	} else {
		if (test_bit(FEAT_HBM, feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x80, 0x83, 0x03, 0x01);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x81, 0x83, 0x03, 0x03);
	}
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x10, 0xBD);
	val = test_bit(FEAT_EARLY_EXIT, feat) ? 0x22 : 0x00;
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, val);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x82, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, val, val, val, val);
	val = test_bit(FEAT_OP_NS, feat) ? 0x4E : 0x1E;
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, val, 0xBD);
	if (test_bit(FEAT_HBM, feat)) {
		if (test_bit(FEAT_OP_NS, feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x02,
				0x00, 0x04, 0x00, 0x0A, 0x00, 0x16, 0x00, 0x76);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x01,
				0x00, 0x03, 0x00, 0x0B, 0x00, 0x17, 0x00, 0x77);
	} else {
		if (test_bit(FEAT_OP_NS, feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x04,
				0x00, 0x08, 0x00, 0x14, 0x00, 0x2C, 0x00, 0xEC);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x02,
				0x00, 0x06, 0x00, 0x16, 0x00, 0x2E, 0x00, 0xEE);
	}

	/*
	 * Frequency setting: FI, frequency, idle frequency
	 *
	 * Description: this sequence possibly overrides some configs early-exit
	 * and operation set, depending on FI mode.
	 */
	if (test_bit(FEAT_FRAME_AUTO, feat)) {
		if (test_bit(FEAT_OP_NS, feat)) {
			/* threshold setting */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x0C, 0xBD);
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00);
		} else {
			/* initial frequency */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x92, 0xBD);
			if (vrefresh == 60) {
				val = test_bit(FEAT_HBM, feat) ? 0x01 : 0x02;
			} else {
				if (vrefresh != 120)
					dev_warn(ctx->dev, "%s: unsupported init freq %d (hs)\n",
						 __func__, vrefresh);
				/* 120Hz */
				val = 0x00;
			}
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, val);
		}
		/* target frequency */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x12, 0xBD);
		if (test_bit(FEAT_OP_NS, feat)) {
			if (idle_vrefresh == 30) {
				val = test_bit(FEAT_HBM, feat) ? 0x02 : 0x04;
			} else if (idle_vrefresh == 10) {
				val = test_bit(FEAT_HBM, feat) ? 0x0A : 0x14;
			} else {
				if (idle_vrefresh != 1)
					dev_warn(ctx->dev, "%s: unsupported target freq %d (ns)\n",
						 __func__, idle_vrefresh);
				/* 1Hz */
				val = test_bit(FEAT_HBM, feat) ? 0x76 : 0xEC;
			}
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, val);
		} else {
			if (idle_vrefresh == 30) {
				val = test_bit(FEAT_HBM, feat) ? 0x03 : 0x06;
			} else if (idle_vrefresh == 10) {
				val = test_bit(FEAT_HBM, feat) ? 0x0B : 0x16;
			} else {
				if (idle_vrefresh != 1)
					dev_warn(ctx->dev, "%s: unsupported target freq %d (hs)\n",
						 __func__, idle_vrefresh);
				/* 1Hz */
				val = test_bit(FEAT_HBM, feat) ? 0x77 : 0xEE;
			}
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, val);
		}
		/* step setting */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x9E, 0xBD);
		if (test_bit(FEAT_OP_NS, feat)) {
			if (test_bit(FEAT_HBM, feat))
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00);
			else
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x04, 0x00, 0x14, 0x00, 0x00);
		} else {
			if (test_bit(FEAT_HBM, feat))
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x01, 0x00, 0x03, 0x00, 0x0B);
			else
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x02, 0x00, 0x06, 0x00, 0x16);
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xAE, 0xBD);
		if (test_bit(FEAT_OP_NS, feat)) {
			if (idle_vrefresh == 30) {
				/* 60Hz -> 30Hz idle */
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00);
			} else if (idle_vrefresh == 10) {
				/* 60Hz -> 10Hz idle */
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x00, 0x00);
			} else {
				if (idle_vrefresh != 1)
					dev_warn(ctx->dev, "%s: unsupported freq step to %d (ns)\n",
						 __func__, idle_vrefresh);
				/* 60Hz -> 1Hz idle */
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x03, 0x00);
			}
		} else {
			if (vrefresh == 60) {
				if (idle_vrefresh == 30) {
					/* 60Hz -> 30Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x00, 0x00);
				} else if (idle_vrefresh == 10) {
					/* 60Hz -> 10Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x01, 0x00);
				} else {
					if (idle_vrefresh != 1)
						dev_warn(ctx->dev, "%s: unsupported freq step to %d (hs)\n",
							 __func__, vrefresh);
					/* 60Hz -> 1Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x01, 0x03);
				}
			} else {
				if (vrefresh != 120)
					dev_warn(ctx->dev, "%s: unsupported freq step from %d (hs)\n",
						 __func__, vrefresh);
				if (idle_vrefresh == 30) {
					/* 120Hz -> 30Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00);
				} else if (idle_vrefresh == 10) {
					/* 120Hz -> 10Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x03, 0x00);
				} else {
					if (idle_vrefresh != 1)
						dev_warn(ctx->dev, "%s: unsupported freq step to %d (hs)\n",
						 __func__, idle_vrefresh);
					/* 120Hz -> 1Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x01, 0x03);
				}
			}
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0xA3);
	} else { /* manual */
		EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21);
		if (test_bit(FEAT_OP_NS, feat)) {
			if (vrefresh == 1) {
				val = 0x1F;
			} else if (vrefresh == 5) {
				val = 0x1E;
			} else if (vrefresh == 10) {
				val = 0x1B;
			} else if (vrefresh == 30) {
				val = 0x19;
			} else {
				if (vrefresh != 60)
					dev_warn(ctx->dev,
						 "%s: unsupported manual freq %d (ns)\n",
						 __func__, vrefresh);
				/* 60Hz */
				val = 0x18;
			}
		} else {
			if (vrefresh == 1) {
				val = 0x07;
			} else if (vrefresh == 5) {
				val = 0x06;
			} else if (vrefresh == 10) {
				val = 0x03;
			} else if (vrefresh == 30) {
				val = 0x02;
			} else if (vrefresh == 60) {
				val = 0x01;
			} else {
				if (vrefresh != 120)
					dev_warn(ctx->dev,
						 "%s: unsupported manual freq %d (hs)\n",
						 __func__, vrefresh);
				/* 120Hz */
				val = 0x00;
			}
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0x60, val);
	}

	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);;
}

/**
 * hk3_disable_panel_feat - set the panel at the state of powering up except refresh rate
 * @ctx: exynos_panel struct
 * @vrefresh: refresh rate
 * This function disables HBM, switches to HS, sets manual mode and changeable TE.
 */
static void hk3_disable_panel_feat(struct exynos_panel *ctx, u32 vrefresh)
{
	DECLARE_BITMAP(feat, FEAT_MAX);

	bitmap_zero(feat, FEAT_MAX);
	hk3_set_panel_feat(ctx, vrefresh, 0, feat, true);
}

static void hk3_update_panel_feat(struct exynos_panel *ctx, u32 vrefresh, bool enforce)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	hk3_set_panel_feat(ctx, vrefresh, spanel->auto_mode_vrefresh, spanel->feat, enforce);
}

static void hk3_update_refresh_mode(struct exynos_panel *ctx,
					const struct exynos_panel_mode *pmode,
					const u32 idle_vrefresh)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);

	/*
	 * Skip idle update if going through RRS without refresh rate change. If
	 * we're switching resolution and refresh rate in the same atomic commit
	 * (MODE_RES_AND_RR_IN_PROGRESS), we shouldn't skip the update to
	 * ensure the refresh rate will be set correctly to avoid problems.
	 */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS) {
		dev_dbg(ctx->dev, "%s: RRS in progress without RR change, skip\n", __func__);
		notify_panel_mode_changed(ctx, false);
		return;
	}

	dev_dbg(ctx->dev, "%s: mode: %s set idle_vrefresh: %u\n", __func__,
		pmode->mode.name, idle_vrefresh);

	if (idle_vrefresh)
		set_bit(FEAT_FRAME_AUTO, spanel->feat);
	else
		clear_bit(FEAT_FRAME_AUTO, spanel->feat);

	if (vrefresh == 120 || idle_vrefresh)
		set_bit(FEAT_EARLY_EXIT, spanel->feat);
	else
		clear_bit(FEAT_EARLY_EXIT, spanel->feat);

	spanel->auto_mode_vrefresh = idle_vrefresh;
	/*
	 * Note: when mode is explicitly set, panel performs early exit to get out
	 * of idle at next vsync, and will not back to idle until not seeing new
	 * frame traffic for a while. If idle_vrefresh != 0, try best to guess what
	 * panel_idle_vrefresh will be soon, and hk3_update_idle_state() in
	 * new frame commit will correct it if the guess is wrong.
	 */
	ctx->panel_idle_vrefresh = idle_vrefresh;
	hk3_update_panel_feat(ctx, vrefresh, false);

	notify_panel_mode_changed(ctx, false);

	dev_dbg(ctx->dev, "%s: display state is notified\n", __func__);
}

static void hk3_change_frequency(struct exynos_panel *ctx,
				 const struct exynos_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	if (vrefresh > ctx->op_hz) {
		/* resolution may has been changed but refresh rate */
		if (ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS)
			notify_panel_mode_changed(ctx, false);
		dev_err(ctx->dev,
		"invalid freq setting: op_hz=%u, vrefresh=%u\n",
		ctx->op_hz, vrefresh);
		return;
	}

	if (pmode->idle_mode == IDLE_MODE_ON_INACTIVITY)
		idle_vrefresh = hk3_get_min_idle_vrefresh(ctx, pmode);

	hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);

	dev_dbg(ctx->dev, "change to %u hz\n", vrefresh);
}

static void hk3_panel_idle_notification(struct exynos_panel *ctx,
		u32 display_id, u32 vrefresh, u32 idle_te_vrefresh)
{
	char event_string[64];
	char *envp[] = { event_string, NULL };
	struct drm_device *dev = ctx->bridge.dev;

	if (!dev) {
		dev_warn(ctx->dev, "%s: drm_device is null\n", __func__);
	} else {
		snprintf(event_string, sizeof(event_string),
			"PANEL_IDLE_ENTER=%u,%u,%u", display_id, vrefresh, idle_te_vrefresh);
		kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	}
}

static void hk3_wait_one_vblank(struct exynos_panel *ctx)
{
	struct drm_crtc *crtc = NULL;

	if (ctx->exynos_connector.base.state)
		crtc = ctx->exynos_connector.base.state->crtc;

	DPU_ATRACE_BEGIN(__func__);
	if (crtc) {
		int ret = drm_crtc_vblank_get(crtc);

		if (!ret) {
			drm_crtc_wait_one_vblank(crtc);
			drm_crtc_vblank_put(crtc);
		} else {
			usleep_range(8350, 8500);
		}
	} else {
		usleep_range(8350, 8500);
	}
	DPU_ATRACE_END(__func__);
}

static void hk3_read_back_vreg(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	char buf[HK3_VREG_PARAM_NUM] = {0};
	int ret;

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xB0, 0x00, 0x31, 0xF4);
	ret = mipi_dsi_dcs_read(dsi, 0xF4, buf, HK3_VREG_PARAM_NUM);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
	if (ret != HK3_VREG_PARAM_NUM) {
		dev_warn(ctx->dev, "unable to read vreg setting (%d)\n", ret);
	} else {
		exynos_bin2hex(buf, HK3_VREG_PARAM_NUM,
			       spanel->hw_vreg, sizeof(spanel->hw_vreg));
		if (!strcmp(spanel->hw_vreg, HK3_VREG_STR(ctx)))
			dev_dbg(ctx->dev, "normal vreg: %s\n", spanel->hw_vreg);
		else
			dev_warn(ctx->dev, "abnormal vreg: %s (expect %s)\n",
				 spanel->hw_vreg, HK3_VREG_STR(ctx));
	}

	spanel->read_vreg = false;
}

static bool hk3_set_self_refresh(struct exynos_panel *ctx, bool enable)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 idle_vrefresh;

	dev_dbg(ctx->dev, "%s: %d\n", __func__, enable);

	if (unlikely(!pmode))
		return false;

	if (enable && spanel->read_vreg)
		hk3_read_back_vreg(ctx);

	/* self refresh is not supported in lp mode since that always makes use of early exit */
	if (pmode->exynos_mode.is_lp_mode) {
		/* set 1Hz while self refresh is active, otherwise clear it */
		ctx->panel_idle_vrefresh = enable ? 1 : 0;
		notify_panel_mode_changed(ctx, true);
		return false;
	}

	if (spanel->pending_temp_update && enable)
		hk3_update_disp_therm(ctx);

	idle_vrefresh = hk3_get_min_idle_vrefresh(ctx, pmode);

	if (pmode->idle_mode != IDLE_MODE_ON_SELF_REFRESH) {
		/*
		 * if idle mode is on inactivity, may need to update the target fps for auto mode,
		 * or switch to manual mode if idle should be disabled (idle_vrefresh=0)
		 */
		if ((pmode->idle_mode == IDLE_MODE_ON_INACTIVITY) &&
			(spanel->auto_mode_vrefresh != idle_vrefresh)) {
			hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);
			return true;
		}
		return false;
	}

	if (!enable)
		idle_vrefresh = 0;

	/* if there's no change in idle state then skip cmds */
	if (ctx->panel_idle_vrefresh == idle_vrefresh)
		return false;

	DPU_ATRACE_BEGIN(__func__);
	hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);

	if (idle_vrefresh) {
		const int vrefresh = drm_mode_vrefresh(&pmode->mode);

		hk3_panel_idle_notification(ctx, 0, vrefresh, 120);
	} else if (ctx->panel_need_handle_idle_exit) {
		/*
		 * after exit idle mode with fixed TE at non-120hz, TE may still keep at 120hz.
		 * If any layer that already be assigned to DPU that can't be handled at 120hz,
		 * panel_need_handle_idle_exit will be set then we need to wait one vblank to
		 * avoid underrun issue.
		 */
		dev_dbg(ctx->dev, "wait one vblank after exit idle\n");
		hk3_wait_one_vblank(ctx);
	}

	DPU_ATRACE_END(__func__);

	return true;
}

static void hk3_update_lhbm_hist_config(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct hk3_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	int d = 766, r = 115;

	if (ctl->hist_roi_configured)
		return;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return;
	}
	mode = &pmode->mode;
	if (mode->hdisplay == 1008) {
		d = 575;
		r = 87;
	}
	if (!exynos_drm_connector_set_lhbm_hist(&ctx->exynos_connector,
		mode->hdisplay, mode->vdisplay, d, r)) {
		ctl->hist_roi_configured = true;
		dev_info(ctx->dev, "configure lhbm hist: %d %d %d %d\n",
			mode->hdisplay, mode->vdisplay, d, r);
	}
}

static int hk3_atomic_check(struct exynos_panel *ctx, struct drm_atomic_state *state)
{
	struct drm_connector *conn = &ctx->exynos_connector.base;
	struct drm_connector_state *new_conn_state = drm_atomic_get_new_connector_state(state, conn);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct hk3_panel *spanel = to_spanel(ctx);

	hk3_update_lhbm_hist_config(ctx);

	if (!ctx->current_mode || drm_mode_vrefresh(&ctx->current_mode->mode) == 120 ||
	    !new_conn_state || !new_conn_state->crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	old_crtc_state = drm_atomic_get_old_crtc_state(state, new_conn_state->crtc);
	if (!old_crtc_state || !new_crtc_state || !new_crtc_state->active)
		return 0;

	if ((spanel->auto_mode_vrefresh && old_crtc_state->self_refresh_active) ||
	    !drm_atomic_crtc_effectively_active(old_crtc_state)) {
		struct drm_display_mode *mode = &new_crtc_state->adjusted_mode;

		/* set clock to max refresh rate on self refresh exit or resume due to early exit */
		mode->clock = mode->htotal * mode->vtotal * 120 / 1000;

		if (mode->clock != new_crtc_state->mode.clock) {
			new_crtc_state->mode_changed = true;
			dev_dbg(ctx->dev, "raise mode (%s) clock to 120hz on %s\n",
				mode->name,
				old_crtc_state->self_refresh_active ? "self refresh exit" : "resume");
		}
	} else if (old_crtc_state->active_changed &&
		   (old_crtc_state->adjusted_mode.clock != old_crtc_state->mode.clock)) {
		/* clock hacked in last commit due to self refresh exit or resume, undo that */
		new_crtc_state->mode_changed = true;
		new_crtc_state->adjusted_mode.clock = new_crtc_state->mode.clock;
		dev_dbg(ctx->dev, "restore mode (%s) clock after self refresh exit or resume\n",
			new_crtc_state->mode.name);
	}

	return 0;
}

static void hk3_write_display_mode(struct exynos_panel *ctx,
				   const struct drm_display_mode *mode)
{
	u8 val = HK3_WRCTRLD_BCTRL_BIT;

	if (IS_HBM_ON(ctx->hbm_mode))
		val |= HK3_WRCTRLD_HBM_BIT;

	if (ctx->hbm.local_hbm.enabled)
		val |= HK3_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= HK3_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev,
		"%s(wrctrld:0x%x, hbm: %s, dimming: %s local_hbm: %s)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		ctx->dimming_on ? "on" : "off",
		ctx->hbm.local_hbm.enabled ? "on" : "off");

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

#define HK3_OPR_VAL_LEN 2
#define HK3_MAX_OPR_VAL 0x3FF
/* Get OPR (on pixel ratio), the unit is percent */
static int hk3_get_opr(struct exynos_panel *ctx, u8 *opr)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 buf[HK3_OPR_VAL_LEN] = {0};
	u16 val;
	int ret;

	DPU_ATRACE_BEGIN(__func__);
	EXYNOS_DCS_WRITE_TABLE(ctx, unlock_cmd_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0xE7, 0x91);
	ret = mipi_dsi_dcs_read(dsi, 0x91, buf, HK3_OPR_VAL_LEN);
	EXYNOS_DCS_WRITE_TABLE(ctx, lock_cmd_f0);
	DPU_ATRACE_END(__func__);

	if (ret != HK3_OPR_VAL_LEN) {
		dev_warn(ctx->dev, "Failed to read OPR (%d)\n", ret);
		return ret;
	}

	val = (buf[0] << 8) | buf[1];
	*opr = DIV_ROUND_CLOSEST(val * 100, HK3_MAX_OPR_VAL);
	dev_dbg(ctx->dev, "%s: %u (0x%X)\n", __func__, *opr, val);

	return 0;
}

#define HK3_ZA_THRESHOLD_OPR 80
static void hk3_update_za(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	bool enable_za = false;
	u8 opr;

	if ((spanel->hw_acl_setting > 0) && !spanel->force_za_off) {
		if (ctx->panel_rev != PANEL_REV_PROTO1) {
			enable_za = true;
		} else if (!hk3_get_opr(ctx, &opr)) {
			enable_za = (opr > HK3_ZA_THRESHOLD_OPR);
		} else {
			dev_warn(ctx->dev, "Unable to update za\n");
			return;
		}
	}

	if (spanel->hw_za_enabled != enable_za) {
		/* LP setting - 0x21 or 0x11: 7.5%, 0x00: off */
		u8 val = 0;

		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x01, 0x6C, 0x92);
		if (enable_za)
			val = (ctx->panel_rev == PANEL_REV_PROTO1) ? 0x21 : 0x11;
		EXYNOS_DCS_BUF_ADD(ctx, 0x92, val);
		EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);

		spanel->hw_za_enabled = enable_za;
		dev_info(ctx->dev, "%s: %s\n", __func__, enable_za ? "on" : "off");
	}
}

#define HK3_ACL_ZA_THRESHOLD_DBV_P1_0 3917
#define HK3_ACL_ZA_THRESHOLD_DBV_P1_1 3781
#define HK3_ACL_ENHANCED_THRESHOLD_DBV 3865
#define HK3_ACL_NORMAL_THRESHOLD_DBV_1 3570
#define HK3_ACL_NORMAL_THRESHOLD_DBV_2 3963

/* updated za when acl mode changed */
static void hk3_set_acl_mode(struct exynos_panel *ctx, enum exynos_acl_mode mode)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	u16 dbv_th = 0;
	u8 setting = 0;
	bool enable_acl = false;
	/*
	 * ACL mode and setting:
	 *
	 * P1.0
	 *    NORMAL/ENHANCED- 5% (0x01)
	 * P1.1
	 *    NORMAL/ENHANCED- 7.5% (0x02)
	 *
	 * EVT1 and later
	 *    ENHANCED   - 17%  (0x03)
	 *    NORMAL     - 12%  (0x02)
	 *               - 7.5% (0x01)
	 *
	 * Set 0x00 to disable it
	 */
	if (ctx->panel_rev == PANEL_REV_PROTO1) {
		dbv_th = HK3_ACL_ZA_THRESHOLD_DBV_P1_0;
		setting = 0x01;
	} else if (ctx->panel_rev == PANEL_REV_PROTO1_1) {
		dbv_th = HK3_ACL_ZA_THRESHOLD_DBV_P1_1;
		setting = 0x02;
	} else {
		if (mode == ACL_ENHANCED) {
			dbv_th = HK3_ACL_ENHANCED_THRESHOLD_DBV;
			setting = 0x03;
		} else if (mode == ACL_NORMAL) {
			if (spanel->hw_dbv >= HK3_ACL_NORMAL_THRESHOLD_DBV_1 &&
				spanel->hw_dbv < HK3_ACL_NORMAL_THRESHOLD_DBV_2) {
				dbv_th = HK3_ACL_NORMAL_THRESHOLD_DBV_1;
				setting = 0x01;
			} else if (spanel->hw_dbv >= HK3_ACL_NORMAL_THRESHOLD_DBV_2) {
				dbv_th = HK3_ACL_NORMAL_THRESHOLD_DBV_2;
				setting = 0x02;
			}
		}
	}

	enable_acl = (spanel->hw_dbv >= dbv_th && IS_HBM_ON(ctx->hbm_mode) && mode != ACL_OFF);
	if (enable_acl == false)
		setting = 0;

	if (spanel->hw_acl_setting != setting) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x55, setting);
		spanel->hw_acl_setting = setting;
		dev_info(ctx->dev, "%s: %d\n", __func__, setting);
		/* Keep ZA off after EVT1 */
		if (ctx->panel_rev < PANEL_REV_EVT1)
			hk3_update_za(ctx);
	}
}

static int hk3_set_brightness(struct exynos_panel *ctx, u16 br)
{
	int ret;
	u16 brightness;
	struct hk3_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		/* don't stay at pixel-off state in AOD, or black screen is possibly seen */
		if (spanel->is_pixel_off) {
			EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_ENTER_NORMAL_MODE);
			spanel->is_pixel_off = false;
		}
		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	/* Use pixel off command instead of setting DBV 0 */
	if (!br) {
		if (!spanel->is_pixel_off) {
			EXYNOS_DCS_WRITE_TABLE(ctx, pixel_off);
			spanel->is_pixel_off = true;
			dev_dbg(ctx->dev, "%s: pixel off instead of dbv 0\n", __func__);
		}
		return 0;
	} else if (br && spanel->is_pixel_off) {
		EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_ENTER_NORMAL_MODE);
		spanel->is_pixel_off = false;
	}

	brightness = (br & 0xff) << 8 | br >> 8;
	ret = exynos_dcs_set_brightness(ctx, brightness);
	if (!ret) {
		spanel->hw_dbv = br;
		hk3_set_acl_mode(ctx, ctx->acl_mode);
	}

	return ret;
}

static const struct exynos_dsi_cmd hk3_display_on_cmds[] = {
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	EXYNOS_DSI_CMD0(sync_begin),
	/* AMP type change (return) */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x4F, 0xF4),
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x70),
	/* Vreg = 7.1V (return) */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x31, 0xF4),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_DVT1), 0xF4, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_DVT1), 0xF4, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B),
	EXYNOS_DSI_CMD0(sync_end),
	EXYNOS_DSI_CMD0(lock_cmd_f0),

	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON),
};
static DEFINE_EXYNOS_CMD_SET(hk3_display_on);

static const struct exynos_dsi_cmd hk3_display_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),

	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	EXYNOS_DSI_CMD0(sync_begin),
	/* AMP type change */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x4F, 0xF4),
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x50),
	/* Vreg = 4.5 */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x31, 0xF4),
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD0(sync_end),
	EXYNOS_DSI_CMD0(lock_cmd_f0),
};
static DEFINE_EXYNOS_CMD_SET(hk3_display_off);

static unsigned int hk3_get_te_usec(struct exynos_panel *ctx,
				    const struct exynos_panel_mode *pmode)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	if (spanel->hw_vrefresh != 60)
		return pmode->exynos_mode.te_usec;
	else
		return (test_bit(FEAT_OP_NS, spanel->feat) ? HK3_TE_USEC_60HZ_NS :
							     HK3_TE_USEC_60HZ_HS);
}

static u32 hk3_get_te_width_usec(u32 vrefresh, bool is_ns)
{
	/* TODO: update this line if supporting 30 Hz normal mode in the future */
	if (vrefresh == 30)
		return HK3_TE_USEC_AOD;
	else if (vrefresh == 120)
		return HK3_TE_USEC_120HZ;
	else
		return is_ns ? HK3_TE_USEC_60HZ_NS : HK3_TE_USEC_60HZ_HS;
}

static void hk3_wait_for_vsync_done(struct exynos_panel *ctx, u32 vrefresh, bool is_ns)
{
	u32 te_width_us = hk3_get_te_width_usec(vrefresh, is_ns);

	dev_dbg(ctx->dev, "%s: %dhz\n", __func__, vrefresh);

	DPU_ATRACE_BEGIN(__func__);
	exynos_panel_wait_for_vsync_done(ctx, te_width_us,
		EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh));
	/* add 1ms tolerance */
	exynos_panel_msleep(1);
	DPU_ATRACE_END(__func__);
}

/**
 * hk3_wait_for_vsync_done_changeable - wait for finishing vsync for changeable TE to avoid
 * fake TE at transition from fixed TE to changeable TE.
 * @ctx: panel struct
 * @vrefresh: current refresh rate
 * @is_ns: whether it is normal speed or not
 */
static void hk3_wait_for_vsync_done_changeable(struct exynos_panel *ctx, u32 vrefresh, bool is_ns)
{
	int i = 0;
	const int timeout = 5;
	u32 te_width_us = hk3_get_te_width_usec(vrefresh, is_ns);

	while (i++ < timeout) {
		ktime_t t;
		s64 delta_us;
		int period_us = EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh);

		exynos_panel_wait_for_vblank(ctx);
		t = ktime_get();
		exynos_panel_wait_for_vblank(ctx);
		delta_us = ktime_us_delta(ktime_get(), t);
		if (abs(delta_us - period_us) < HK3_TE_PERIOD_DELTA_TOLERANCE_USEC)
			break;
	}
	if (i >= timeout)
		dev_warn(ctx->dev, "timeout of waiting for changeable TE @ %d Hz\n", vrefresh);
	usleep_range(te_width_us, te_width_us + 10);
}

static bool hk3_is_peak_vrefresh(u32 vrefresh, bool is_ns)
{
	return (is_ns && vrefresh == 60) || (!is_ns && vrefresh == 120);
}

static void hk3_set_lp_mode(struct exynos_panel *ctx, const struct exynos_panel_mode *pmode)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	const u16 brightness = exynos_panel_get_brightness(ctx);
	bool is_changeable_te = !test_bit(FEAT_EARLY_EXIT, spanel->feat);
	bool is_ns = test_bit(FEAT_OP_NS, spanel->feat);
	bool panel_enabled = is_panel_enabled(ctx);
	u32 vrefresh = panel_enabled ? spanel->hw_vrefresh : 60;

	dev_dbg(ctx->dev, "%s: panel: %s\n", __func__, panel_enabled ? "ON" : "OFF");

	DPU_ATRACE_BEGIN(__func__);

	hk3_disable_panel_feat(ctx, vrefresh);
	if (panel_enabled) {
		/* init sequence has sent display-off command already */
		if (!hk3_is_peak_vrefresh(vrefresh, is_ns) && is_changeable_te)
			hk3_wait_for_vsync_done_changeable(ctx, vrefresh, is_ns);
		else
			hk3_wait_for_vsync_done(ctx, vrefresh, is_ns);
		exynos_panel_send_cmd_set(ctx, &hk3_display_off_cmd_set);
	}
	/* display should be off here, set dbv before entering lp mode */
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, aod_dbv);
	hk3_wait_for_vsync_done(ctx, vrefresh, false);

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, aod_on);
	exynos_panel_set_binned_lp(ctx, brightness);
	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	/* Fixed TE: sync on */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x51);
	/* Default TE pulse width 693us */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x08, 0xB9);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0xE0, 0x00, 0x2F, 0x0B, 0xE0, 0x00, 0x2F);
	/* Frequency set for AOD */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x02, 0xB9);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x00);
	/* Auto frame insertion: 1Hz */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x18, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x04, 0x00, 0x74);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xB8, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x08);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xC8, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x03);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0xA7);
	/* Enable early exit */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xE8, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x10, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x22);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x82, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x22, 0x22, 0x22, 0x22);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
	exynos_panel_send_cmd_set(ctx, &hk3_display_on_cmd_set);

	spanel->hw_vrefresh = 30;
	spanel->read_vreg = true;

	DPU_ATRACE_END(__func__);

	dev_info(ctx->dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
}

static void hk3_set_nolp_mode(struct exynos_panel *ctx,
			      const struct exynos_panel_mode *pmode)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	dev_dbg(ctx->dev, "%s\n", __func__);

	DPU_ATRACE_BEGIN(__func__);

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	/* manual mode */
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21);
	/* Changeable TE is a must to ensure command sync */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x04);
	/* Changeable TE width setting and frequency */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x04, 0xB9);
	/* width 693us in AOD mode */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0xE0, 0x00, 0x2F);
	/* AOD 30Hz */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0x60);
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, 0x00);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
	spanel->hw_idle_vrefresh = 0;

	hk3_wait_for_vsync_done(ctx, 30, false);
	exynos_panel_send_cmd_set(ctx, &hk3_display_off_cmd_set);

	hk3_wait_for_vsync_done(ctx, 30, false);
	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	/* TE width setting */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x04, 0xB9);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0xBB, 0x00, 0x2F, /* changeable TE */
			   0x0B, 0xBB, 0x00, 0x2F, 0x0B, 0xBB, 0x00, 0x2F); /* fixed TE */
	/* disabling AOD low Mode is a must before aod-off */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x52, 0x94);
	EXYNOS_DCS_BUF_ADD(ctx, 0x94, 0x00);
	EXYNOS_DCS_BUF_ADD_SET(ctx, lock_cmd_f0);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, aod_off);
	hk3_update_panel_feat(ctx, drm_mode_vrefresh(&pmode->mode), true);
	/* backlight control and dimming */
	hk3_write_display_mode(ctx, &pmode->mode);
	hk3_change_frequency(ctx, pmode);
	exynos_panel_send_cmd_set(ctx, &hk3_display_on_cmd_set);
	spanel->read_vreg = true;

	DPU_ATRACE_END(__func__);

	dev_info(ctx->dev, "exit LP mode\n");
}

static const struct exynos_dsi_cmd hk3_init_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(10, MIPI_DCS_EXIT_SLEEP_MODE),

	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	/* AMP type change */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x4F, 0xF4),
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x50),
	/* VREG 4.5V */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x31, 0xF4),
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD(lock_cmd_f0, 110),
	/* Enable TE*/
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON),

	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	/* AOD Transition Set */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_DVT1), 0xB0, 0x00, 0x03, 0xBB),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_DVT1), 0xBB, 0x41),

	/* TSP SYNC Enable (Auto Set) */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x3C, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x19, 0x09),

	/* FFC: off, 165MHz, MIPI Speed 1368 Mbps */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x36, 0xC5),
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x10, 0x10, 0x50, 0x05, 0x4D, 0x31, 0x40, 0x00,
				 0x40, 0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00,
				 0x40, 0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00,
				 0x40, 0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00,
				 0x40, 0x00, 0x40, 0x00),

	/* TE width setting */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x04, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x0B, 0xBB, 0x00, 0x2F, /* changeable TE */
			   0x0B, 0xBB, 0x00, 0x2F, 0x0B, 0xBB, 0x00, 0x2F), /* fixed TE */

	/* enable OPEC (auto still IMG detect off) */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0xB0, 0x00, 0x1D, 0x63),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_MP), 0x63, 0x02, 0x18),

	/* PMIC Fast Discharge off */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x18, 0xB1),
	EXYNOS_DSI_CMD_SEQ(0xB1, 0x55, 0x01),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x13, 0xB1),
	EXYNOS_DSI_CMD_SEQ(0xB1, 0x80),

	EXYNOS_DSI_CMD0(freq_update),
	EXYNOS_DSI_CMD0(lock_cmd_f0),
	/* CASET: 1343 */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x05, 0x3F),
	/* PASET: 2991 */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x0B, 0xAF),
};
static DEFINE_EXYNOS_CMD_SET(hk3_init);

static const struct exynos_dsi_cmd hk3_ns_gamma_fix_cmds[] = {
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x02, 0x3F, 0xCB),
	EXYNOS_DSI_CMD_SEQ(0xCB, 0x0A),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x02, 0x45, 0xCB),
	EXYNOS_DSI_CMD_SEQ(0xCB, 0x0A),
	EXYNOS_DSI_CMD0(freq_update),
	EXYNOS_DSI_CMD0(lock_cmd_f0),
};
static DEFINE_EXYNOS_CMD_SET(hk3_ns_gamma_fix);

static void hk3_lhbm_luminance_opr_setting(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	bool is_ns_mode = test_bit(FEAT_OP_NS, spanel->feat);

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x02, 0xF9, 0x95);
	/* DBV setting */
	EXYNOS_DCS_BUF_ADD(ctx, 0x95, 0x00, 0x40, 0x0C, 0x01, 0x90, 0x33, 0x06, 0x60,
				0xCC, 0x11, 0x92, 0x7F);
	EXYNOS_DCS_BUF_ADD(ctx, 0x71, 0xC6, 0x00, 0x00, 0x19);
	/* 120Hz base (HS) offset */
	EXYNOS_DCS_BUF_ADD(ctx, 0x6C, 0x9C, 0x9F, 0x59, 0x58, 0x50, 0x2F, 0x2B, 0x2E);
	EXYNOS_DCS_BUF_ADD(ctx, 0x71, 0xC6, 0x00, 0x00, 0x6A);
	/* 60Hz base (NS) offset */
	EXYNOS_DCS_BUF_ADD(ctx, 0x6C, 0xA0, 0xA7, 0x57, 0x5C, 0x52, 0x37, 0x37, 0x40);

	/* Target frequency */
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, is_ns_mode ? 0x18 : 0x00);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	/* Opposite setting of target frequency */
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, is_ns_mode ? 0x00 : 0x18);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	/* Target frequency */
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, is_ns_mode ? 0x18 : 0x00);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
}

static void hk3_negative_field_setting(struct exynos_panel *ctx)
{
	/* all settings will take effect in AOD mode automatically */
	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	/* Vint -3V */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x21, 0xF4);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF4, 0x1E);
	/* Vaint -4V */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x69, 0xF4);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF4, 0x78);
	/* VGL -8V */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x17, 0xF4);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF4, 0x1E);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
}

static int hk3_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	struct hk3_panel *spanel = to_spanel(ctx);
	const bool needs_reset = !is_panel_enabled(ctx);
	bool is_ns = needs_reset ? false : test_bit(FEAT_OP_NS, spanel->feat);
	struct drm_dsc_picture_parameter_set pps_payload;
	bool is_fhd;
	u32 vrefresh;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;
	is_fhd = mode->hdisplay == 1008;
	vrefresh = drm_mode_vrefresh(mode);

	dev_info(ctx->dev, "%s (%s)\n", __func__, is_fhd ? "fhd" : "wqhd");

	DPU_ATRACE_BEGIN(__func__);

	if (needs_reset)
		exynos_panel_reset(ctx);

	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS) {
		u32 te_width_us = hk3_get_te_width_usec(vrefresh, is_ns);

		exynos_panel_wait_for_vsync_done(ctx, te_width_us,
			EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh));
	} else if (ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		u32 te_width_us = hk3_get_te_width_usec(ctx->last_rr, is_ns);

		exynos_panel_wait_for_vsync_done(ctx, te_width_us,
			EXYNOS_VREFRESH_TO_PERIOD_USEC(ctx->last_rr));
	}

	/* DSC related configuration */
	PANEL_SEQ_LABEL_BEGIN("pps");
	drm_dsc_pps_payload_pack(&pps_payload,
				 is_fhd ? &fhd_pps_config : &wqhd_pps_config);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0x9D, 0x01);
	EXYNOS_PPS_WRITE_BUF(ctx, &pps_payload);
	PANEL_SEQ_LABEL_END("pps");

	if (needs_reset) {
		PANEL_SEQ_LABEL_BEGIN("init_cmd");
		exynos_panel_send_cmd_set(ctx, &hk3_init_cmd_set);
		PANEL_SEQ_LABEL_END("init_cmd");
		if (ctx->panel_rev == PANEL_REV_PROTO1)
			hk3_lhbm_luminance_opr_setting(ctx);
		if (ctx->panel_rev >= PANEL_REV_DVT1)
			hk3_negative_field_setting(ctx);

		spanel->is_pixel_off = false;
		ctx->dsi_hs_clk = MIPI_DSI_FREQ_DEFAULT;
	}

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xC3, is_fhd ? 0x0D : 0x0C);
	/* 8/10bit config for QHD/FHD */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0xF2);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF2, is_fhd ? 0x81 : 0x01);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);

	if (needs_reset && spanel->material == MATERIAL_E7_DOE)
		exynos_panel_send_cmd_set(ctx, &hk3_ns_gamma_fix_cmd_set);

	if (pmode->exynos_mode.is_lp_mode) {
		hk3_set_lp_mode(ctx, pmode);
	} else {
		hk3_update_panel_feat(ctx, vrefresh, true);
		hk3_write_display_mode(ctx, mode); /* dimming and HBM */
		hk3_change_frequency(ctx, pmode);

		if (needs_reset || (ctx->panel_state == PANEL_STATE_BLANK)) {
			hk3_wait_for_vsync_done(ctx, needs_reset ? 60 : vrefresh, is_ns);
			exynos_panel_send_cmd_set(ctx, &hk3_display_on_cmd_set);
			spanel->read_vreg = true;
		}
	}

	spanel->lhbm_ctl.hist_roi_configured = false;

	DPU_ATRACE_END(__func__);

	return 0;
}

static int hk3_disable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 vrefresh = spanel->hw_vrefresh;
	int ret;

	dev_info(ctx->dev, "%s\n", __func__);

	/* skip disable sequence if going through RRS */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
	    ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		dev_dbg(ctx->dev, "%s: RRS in progress, skip\n", __func__);
		return 0;
	}

	ret = exynos_panel_disable(panel);
	if (ret)
		return ret;

	hk3_disable_panel_feat(ctx, 60);
	/*
	 * can't get crtc pointer here, fallback to sleep. hk3_disable_panel_feat() sends freq
	 * update command to trigger early exit if auto mode is enabled before, waiting for one
	 * frame (for either auto or manual mode) should be sufficient to make sure the previous
	 * commands become effective.
	 */
	exynos_panel_msleep(EXYNOS_VREFRESH_TO_PERIOD_USEC(vrefresh) / 1000 + 1);

	exynos_panel_send_cmd_set(ctx, &hk3_display_off_cmd_set);
	exynos_panel_msleep(20);
	if (ctx->panel_state == PANEL_STATE_OFF)
		EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 100, MIPI_DCS_ENTER_SLEEP_MODE);

	/* panel register state gets reset after disabling hardware */
	bitmap_clear(spanel->hw_feat, 0, FEAT_MAX);
	spanel->hw_vrefresh = 60;
	spanel->hw_idle_vrefresh = 0;
	spanel->hw_acl_setting = 0;
	spanel->hw_za_enabled = false;
	spanel->hw_dbv = 0;

	return 0;
}

/*
 * 120hz auto mode takes at least 2 frames to start lowering refresh rate in addition to
 * time to next vblank. Use just over 2 frames time to consider worst case scenario
 */
#define EARLY_EXIT_THRESHOLD_US 17000

/**
 * hk3_update_idle_state - update panel auto frame insertion state
 * @ctx: panel struct
 *
 * - update timestamp of switching to manual mode in case its been a while since the
 *   last frame update and auto mode may have started to lower refresh rate.
 * - trigger early exit by command if it's changeable TE and no switching delay, which
 *   could result in fast 120 Hz boost and seeing 120 Hz TE earlier, otherwise disable
 *   auto refresh mode to avoid lowering frequency too fast.
 */
static void hk3_update_idle_state(struct exynos_panel *ctx)
{
	s64 delta_us;
	struct hk3_panel *spanel = to_spanel(ctx);

	ctx->panel_idle_vrefresh = 0;
	if (!test_bit(FEAT_FRAME_AUTO, spanel->feat))
		return;

	delta_us = ktime_us_delta(ktime_get(), ctx->last_commit_ts);
	if (delta_us < EARLY_EXIT_THRESHOLD_US) {
		dev_dbg(ctx->dev, "skip early exit. %lldus since last commit\n",
			delta_us);
		return;
	}

	/* triggering early exit causes a switch to 120hz */
	ctx->last_mode_set_ts = ktime_get();

	DPU_ATRACE_BEGIN(__func__);

	if (!ctx->idle_delay_ms && spanel->force_changeable_te) {
		dev_dbg(ctx->dev, "sending early exit out cmd\n");
		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
		EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
		EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
	} else {
		/* turn off auto mode to prevent panel from lowering frequency too fast */
		hk3_update_refresh_mode(ctx, ctx->current_mode, 0);
	}

	DPU_ATRACE_END(__func__);
}

static void hk3_commit_done(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode->exynos_mode.is_lp_mode)
		return;

	/* skip idle update if going through RRS */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
	    ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		dev_dbg(ctx->dev, "%s: RRS in progress, skip\n", __func__);
		return;
	}

	hk3_update_idle_state(ctx);

	hk3_update_za(ctx);

	if (spanel->pending_temp_update)
		hk3_update_disp_therm(ctx);
}

static void hk3_set_hbm_mode(struct exynos_panel *ctx,
			     enum exynos_hbm_mode mode)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	if (mode == ctx->hbm_mode)
		return;

	if (unlikely(!pmode))
		return;

	ctx->hbm_mode = mode;

	if (IS_HBM_ON(mode)) {
		set_bit(FEAT_HBM, spanel->feat);
		/* enforce IRC on for factory builds */
#ifndef PANEL_FACTORY_BUILD
		if (mode == HBM_ON_IRC_ON)
			clear_bit(ctx->panel_rev >= PANEL_REV_EVT1 ?
				  FEAT_IRC_Z_MODE : FEAT_IRC_OFF, spanel->feat);
		else
			set_bit(ctx->panel_rev >= PANEL_REV_EVT1 ?
				FEAT_IRC_Z_MODE : FEAT_IRC_OFF, spanel->feat);
#endif
	} else {
		clear_bit(FEAT_HBM, spanel->feat);
		clear_bit(ctx->panel_rev >= PANEL_REV_EVT1 ?
			  FEAT_IRC_Z_MODE : FEAT_IRC_OFF, spanel->feat);
	}

	if (ctx->panel_state == PANEL_STATE_NORMAL) {
		if (!IS_HBM_ON(mode))
			hk3_write_display_mode(ctx, &pmode->mode);
		hk3_update_panel_feat(ctx, drm_mode_vrefresh(&pmode->mode), false);
		if (IS_HBM_ON(mode))
			hk3_write_display_mode(ctx, &pmode->mode);
	}
}

static void hk3_set_dimming_on(struct exynos_panel *ctx,
			       bool dimming_on)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;
	if (pmode->exynos_mode.is_lp_mode) {
		dev_info(ctx->dev,"in lp mode, skip to update");
		return;
	}
	hk3_write_display_mode(ctx, &pmode->mode);
}

static void hk3_set_local_hbm_brightness(struct exynos_panel *ctx, bool is_first_stage)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct hk3_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	const u8 *brt;
	enum hk3_lhbm_brt_overdrive_group group = LHBM_OVERDRIVE_GRP_MAX;
	static u8 cmd[LHBM_BRT_CMD_LEN];
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
				luma = panel_cmn_calc_linear_luminance(dbv, 700, -1271);
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
	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD_SET(ctx, lhbm_brightness_index);
	EXYNOS_DCS_BUF_ADD_SET(ctx, cmd);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
}

static void hk3_set_local_hbm_mode(struct exynos_panel *ctx,
				 bool local_hbm_en)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	/* TODO: LHBM Position & Size */
	hk3_write_display_mode(ctx, &pmode->mode);

	if (local_hbm_en)
		hk3_set_local_hbm_brightness(ctx, true);

}

static void hk3_set_local_hbm_mode_post(struct exynos_panel *ctx)
{
	const struct hk3_panel *spanel = to_spanel(ctx);

	if (spanel->lhbm_ctl.overdrived)
		hk3_set_local_hbm_brightness(ctx, false);
}

static void hk3_mode_set(struct exynos_panel *ctx,
			 const struct exynos_panel_mode *pmode)
{
	hk3_change_frequency(ctx, pmode);
}

static bool hk3_is_mode_seamless(const struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	const struct drm_display_mode *c = &ctx->current_mode->mode;
	const struct drm_display_mode *n = &pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay) &&
	       (c->flags == n->flags);
}

static int hk3_set_op_hz(struct exynos_panel *ctx, unsigned int hz)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if (vrefresh > hz || (hz != 60 && hz != 120)) {
		dev_err(ctx->dev, "invalid op_hz=%d for vrefresh=%d\n",
			hz, vrefresh);
		return -EINVAL;
	}

	DPU_ATRACE_BEGIN(__func__);

	ctx->op_hz = hz;
	if (hz == 60)
		set_bit(FEAT_OP_NS, spanel->feat);
	else
		clear_bit(FEAT_OP_NS, spanel->feat);

	if (is_panel_active(ctx))
		hk3_update_panel_feat(ctx, vrefresh, false);
	dev_info(ctx->dev, "%s op_hz at %d\n",
		is_panel_active(ctx) ? "set" : "cache", hz);

	DPU_ATRACE_END(__func__);

	return 0;
}

static int hk3_read_id(struct exynos_panel *ctx)
{
	return exynos_panel_read_ddic_id(ctx);
}

/* Note the format is 0x<DAh><DBh><DCh> which is reverse of bootloader (0x<DCh><DBh><DAh>) */
static void hk3_get_panel_material(struct exynos_panel *ctx, u32 id)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	switch (id) {
	case 0x000A4000:
		spanel->material = MATERIAL_E6;
		break;
	case 0x000A4020:
		spanel->material = MATERIAL_E7_DOE;
		break;
	case 0x000A4420:
		spanel->material = MATERIAL_E7;
		break;
	case 0x000A4520:
		spanel->material = MATERIAL_LPC5;
		break;
	default:
		dev_warn(ctx->dev, "unknown material from panel (%#x), default to E7\n", id);
		spanel->material = MATERIAL_E7;
		break;
	}

	dev_info(ctx->dev, "%s: %d\n", __func__, spanel->material);
}

static void hk3_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = ((build_code & 0xE0) >> 3) | ((build_code & 0x0C) >> 2);

	exynos_panel_get_panel_rev(ctx, rev);

	hk3_get_panel_material(ctx, id);
}

static void hk3_normal_mode_work(struct exynos_panel *ctx)
{
	if (ctx->self_refresh_active) {
		hk3_update_disp_therm(ctx);
	} else {
		struct hk3_panel *spanel = to_spanel(ctx);

		spanel->pending_temp_update = true;
	}
}

static void hk3_pre_update_ffc(struct exynos_panel *ctx)
{
	dev_dbg(ctx->dev, "%s\n", __func__);

	DPU_ATRACE_BEGIN(__func__);

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	/* FFC off */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x36, 0xC5);
	EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x10);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);

	DPU_ATRACE_END(__func__);
}

static void hk3_update_ffc(struct exynos_panel *ctx, unsigned int hs_clk)
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
		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x37, 0xC5);
		if (hs_clk == MIPI_DSI_FREQ_DEFAULT)
			EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x10, 0x50, 0x05, 0x4D, 0x31, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00);
		else /* MIPI_DSI_FREQ_ALTERNATIVE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x10, 0x50, 0x05, 0x4E, 0x74, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00, 0x4E, 0x74, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00, 0x4E, 0x74, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00, 0x4E, 0x74, 0x40, 0x00,
						0x40, 0x00, 0x40, 0x00);
		EXYNOS_DCS_BUF_ADD_SET(ctx, lock_cmd_f0);
	}

	/* FFC on */
	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x36, 0xC5);
	EXYNOS_DCS_BUF_ADD(ctx, 0xC5, 0x11);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);

	DPU_ATRACE_END(__func__);
}

static void hk3_get_pwr_vreg(struct exynos_panel *ctx, char *buf, size_t len)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	strlcpy(buf, spanel->hw_vreg, len);
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 350,
	.te_var = 1,
};

static const u32 hk3_bl_range[] = {
	94, 180, 270, 360, 3307
};

static const int hk3_vrefresh_range[] = {
#ifdef PANEL_FACTORY_BUILD
	1, 5, 10, 30, 60, 120
#else
	1, 10, 30, 60, 120
#endif
};

static const int hk3_lp_vrefresh_range[] = {
	1, 30
};

#define HK3_WQHD_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.slice_count = 2,\
	.slice_height = 187,\
	.cfg = &wqhd_pps_config,\
}
#define HK3_FHD_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.slice_count = 2,\
	.slice_height = 187,\
	.cfg = &fhd_pps_config,\
}

static const struct exynos_panel_mode hk3_modes[] = {
#ifdef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1344x2992x1",
			.clock = 4545,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 52, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 22, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
	{
		.mode = {
			.name = "1344x2992x5",
			.clock = 22725,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 52, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 22, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
	{
		.mode = {
			.name = "1344x2992x10",
			.clock = 45147,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 42, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 22, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
	{
		.mode = {
			.name = "1344x2992x30",
			.clock = 135441,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			/* change hsa and hbp to avoid conflicting to LP mode 30Hz */
			.hsync_end = 1344 + 80 + 22, // add hsa
			.htotal = 1344 + 80 + 22 + 44, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 22, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
#endif
	{
		.mode = {
			.name = "1344x2992x60",
			.clock = 270882,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 42, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 22, // add vbp
			.flags = 0,
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_ON_SELF_REFRESH,
	},
	{
		.mode = {
			.name = "1344x2992x120",
			.clock = 541764,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 42, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 22, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_ON_INACTIVITY,
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1008x2244x60",
			.clock = 157320,
			.hdisplay = 1008,
			.hsync_start = 1008 + 80, // add hfp
			.hsync_end = 1008 + 80 + 24, // add hsa
			.htotal = 1008 + 80 + 24 + 38, // add hbp
			.vdisplay = 2244,
			.vsync_start = 2244 + 12, // add vfp
			.vsync_end = 2244 + 12 + 4, // add vsa
			.vtotal = 2244 + 12 + 4 + 20, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_FHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_ON_SELF_REFRESH,
	},
	{
		.mode = {
			.name = "1008x2244x120",
			.clock = 314640,
			.hdisplay = 1008,
			.hsync_start = 1008 + 80, // add hfp
			.hsync_end = 1008 + 80 + 24, // add hsa
			.htotal = 1008 + 80 + 24 + 38, // add hbp
			.vdisplay = 2244,
			.vsync_start = 2244 + 12, // add vfp
			.vsync_end = 2244 + 12 + 4, // add vsa
			.vtotal = 2244 + 12 + 4 + 20, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = HK3_FHD_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_ON_INACTIVITY,
	},
#endif
};

static const struct exynos_panel_mode hk3_lp_modes[] = {
	{
		.mode = {
			.name = "1344x2992x30",
			.clock = 135441,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 42, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 22, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_AOD,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &underrun_param,
			.is_lp_mode = true,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1008x2244x30",
			.clock = 78660,
			.hdisplay = 1008,
			.hsync_start = 1008 + 80, // add hfp
			.hsync_end = 1008 + 80 + 24, // add hsa
			.htotal = 1008 + 80 + 24 + 38, // add hbp
			.vdisplay = 2244,
			.vsync_start = 2244 + 12, // add vfp
			.vsync_end = 2244 + 12 + 4, // add vsa
			.vtotal = 2244 + 12 + 4 + 20, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_AOD,
			.bpc = 8,
			.dsc = HK3_FHD_DSC,
			.underrun_param = &underrun_param,
			.is_lp_mode = true,
		},
	},
#endif
};

static void hk3_calc_lhbm_od_brightness(u8 n_fine, u8 n_coarse,
	u8 *o_fine, u8 *o_coarse,
	u8 fine_offset_0, u8 fine_offset_1,
	u8 coarse_offset_0, u8 coarse_offset_1)
{
	if (((int)n_fine + (int)fine_offset_0) <= 0xFF) {
		*o_coarse = n_coarse + coarse_offset_0;
		*o_fine = n_fine + fine_offset_0;
	} else {
		*o_coarse = n_coarse + coarse_offset_1;
		*o_fine = n_fine - fine_offset_1;
	}
}

static void hk3_lhbm_brightness_init(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct hk3_panel *spanel = to_spanel(ctx);
	struct hk3_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	int ret;
	u8 g_coarse, b_coarse;
	u8 *p_norm = ctl->brt_normal;
	u8 *p_over;
	enum hk3_lhbm_brt_overdrive_group grp;

	EXYNOS_DCS_WRITE_TABLE(ctx, unlock_cmd_f0);
	EXYNOS_DCS_WRITE_TABLE(ctx, lhbm_brightness_index);
	ret = mipi_dsi_dcs_read(dsi, lhbm_brightness_reg, p_norm, LHBM_BRT_LEN);
	EXYNOS_DCS_WRITE_TABLE(ctx, lock_cmd_f0);
	if (ret != LHBM_BRT_LEN) {
		dev_err(ctx->dev, "failed to read lhbm brightness ret=%d\n", ret);
		return;
	}
	dev_dbg(ctx->dev, "lhbm normal brightness: %*ph\n", LHBM_BRT_LEN, p_norm);

	/* 0 nit */
	grp = LHBM_OVERDRIVE_GRP_0_NIT;
	p_over = ctl->brt_overdrive[grp];
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x00, 0x00, 0x01, 0x01);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x00, 0x00, 0x10, 0x10);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0x5C, 0x79, 0x01, 0x02);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	/* 0 - 6 nits */
	grp = LHBM_OVERDRIVE_GRP_6_NIT;
	p_over = ctl->brt_overdrive[grp];
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x63, 0x7A, 0x00, 0x01);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x70, 0x80, 0x00, 0x10);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0x90, 0x43, 0x00, 0x01);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	/* 6 - 100 nits */
	grp = LHBM_OVERDRIVE_GRP_50_NIT;
	p_over = ctl->brt_overdrive[grp];
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x45, 0x8F, 0x00, 0x01);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x55, 0x98, 0x00, 0x10);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0x75, 0x58, 0x00, 0x01);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	/* 100 - 300 nits */
	grp = LHBM_OVERDRIVE_GRP_300_NIT;
	p_over = ctl->brt_overdrive[grp];
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_R_FINE], p_norm[LHBM_R_COARSE],
		&p_over[LHBM_R_FINE], &p_over[LHBM_R_COARSE],
		0x44, 0xA2, 0x00, 0x01);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_G_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_G_FINE], &g_coarse,
		0x41, 0xAC, 0x00, 0x10);
	hk3_calc_lhbm_od_brightness(p_norm[LHBM_B_FINE], p_norm[LHBM_GB_COARSE],
		&p_over[LHBM_B_FINE], &b_coarse,
		0x55, 0x78, 0x00, 0x01);
	p_over[LHBM_GB_COARSE] = (g_coarse & 0xF0) | (b_coarse & 0x0F);

	for (grp = 0; grp < LHBM_OVERDRIVE_GRP_MAX; grp++) {
		dev_dbg(ctx->dev, "lhbm overdrive brightness[%d]: %*ph\n",
			grp, LHBM_BRT_LEN, ctl->brt_overdrive[grp]);
	}
}

static void hk3_panel_init(struct exynos_panel *ctx)
{
#ifdef CONFIG_DEBUG_FS
	struct dentry *csroot = ctx->debugfs_cmdset_entry;
	struct hk3_panel *spanel = to_spanel(ctx);

	exynos_panel_debugfs_create_cmdset(ctx, csroot, &hk3_init_cmd_set, "init");
	debugfs_create_bool("force_changeable_te", 0644, ctx->debugfs_entry,
				&spanel->force_changeable_te);
	debugfs_create_bool("force_changeable_te2", 0644, ctx->debugfs_entry,
				&spanel->force_changeable_te2);
	debugfs_create_bool("force_za_off", 0644, ctx->debugfs_entry,
				&spanel->force_za_off);
	debugfs_create_u8("hw_acl_setting", 0644, ctx->debugfs_entry,
				&spanel->hw_acl_setting);
#endif

#ifdef PANEL_FACTORY_BUILD
	ctx->panel_idle_enabled = false;
#endif
	hk3_lhbm_brightness_init(ctx);

	if (ctx->panel_rev < PANEL_REV_DVT1) {
		/* AOD Transition Set */
		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x03, 0xBB);
		EXYNOS_DCS_BUF_ADD(ctx, 0xBB, 0x41);
		EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
	}

	if (ctx->panel_rev >= PANEL_REV_DVT1)
		hk3_negative_field_setting(ctx);

	spanel->tz = thermal_zone_get_zone_by_name("disp_therm");
	if (IS_ERR_OR_NULL(spanel->tz))
		dev_err(ctx->dev, "%s: failed to get thermal zone disp_therm\n",
			__func__);
}

static int hk3_panel_probe(struct mipi_dsi_device *dsi)
{
	struct hk3_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 120;
	spanel->hw_vrefresh = 60;
	spanel->hw_acl_setting = 0;
	spanel->hw_za_enabled = false;
	spanel->hw_dbv = 0;
	/* ddic default temp */
	spanel->hw_temp = 25;
	spanel->pending_temp_update = false;
	spanel->is_pixel_off = false;
	spanel->read_vreg = false;

	return exynos_panel_common_init(dsi, &spanel->base);
}

static int hk3_panel_config(struct exynos_panel *ctx)
{
	exynos_panel_model_init(ctx, PROJECT, 0);

	return 0;
}

static const struct drm_panel_funcs hk3_drm_funcs = {
	.disable = hk3_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = hk3_enable,
	.get_modes = exynos_panel_get_modes,
};

static const struct exynos_panel_funcs hk3_exynos_funcs = {
	.set_brightness = hk3_set_brightness,
	.set_lp_mode = hk3_set_lp_mode,
	.set_nolp_mode = hk3_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = hk3_set_hbm_mode,
	.set_dimming_on = hk3_set_dimming_on,
	.set_local_hbm_mode = hk3_set_local_hbm_mode,
	.set_local_hbm_mode_post = hk3_set_local_hbm_mode_post,
	.is_mode_seamless = hk3_is_mode_seamless,
	.mode_set = hk3_mode_set,
	.panel_init = hk3_panel_init,
	.panel_config = hk3_panel_config,
	.get_panel_rev = hk3_get_panel_rev,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = hk3_update_te2,
	.commit_done = hk3_commit_done,
	.atomic_check = hk3_atomic_check,
	.set_self_refresh = hk3_set_self_refresh,
	.set_op_hz = hk3_set_op_hz,
	.read_id = hk3_read_id,
	.get_te_usec = hk3_get_te_usec,
	.set_acl_mode = hk3_set_acl_mode,
	.run_normal_mode_work = hk3_normal_mode_work,
	.pre_update_ffc = hk3_pre_update_ffc,
	.update_ffc = hk3_update_ffc,
	.get_pwr_vreg = hk3_get_pwr_vreg,
};

const struct brightness_capability hk3_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 1000,
		},
		.level = {
			.min = 196,
			.max = 3307,
		},
		.percentage = {
			.min = 0,
			.max = 63,
		},
	},
	.hbm = {
		.nits = {
			.min = 1000,
			.max = 1600,
		},
		.level = {
			.min = 3308,
			.max = 4095,
		},
		.percentage = {
			.min = 63,
			.max = 100,
		},
	},
};

const struct exynos_panel_desc google_hk3 = {
	.data_lane_cnt = 4,
	.max_brightness = 4095,
	.dft_brightness = 1353, /* 140 nits */
	.brt_capability = &hk3_brightness_capability,
	.dbv_extra_frame = true,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.bl_range = hk3_bl_range,
	.bl_num_ranges = ARRAY_SIZE(hk3_bl_range),
	.modes = hk3_modes,
	.num_modes = ARRAY_SIZE(hk3_modes),
	.vrefresh_range = hk3_vrefresh_range,
	.vrefresh_range_count = ARRAY_SIZE(hk3_vrefresh_range),
	.lp_mode = hk3_lp_modes,
	.lp_mode_count = ARRAY_SIZE(hk3_lp_modes),
	.lp_vrefresh_range = hk3_lp_vrefresh_range,
	.lp_vrefresh_range_count = ARRAY_SIZE(hk3_lp_vrefresh_range),
	.binned_lp = hk3_binned_lp,
	.num_binned_lp = ARRAY_SIZE(hk3_binned_lp),
	.is_panel_idle_supported = true,
	.no_lhbm_rr_constraints = true,
	.panel_func = &hk3_drm_funcs,
	.exynos_panel_func = &hk3_exynos_funcs,
	.lhbm_effective_delay_frames = 1,
	.lhbm_post_cmd_delay_frames = 1,
	.normal_mode_work_delay_ms = 30000,
	.default_dsi_hs_clk = MIPI_DSI_FREQ_DEFAULT,
	.reset_timing_ms = {1, 1, 5},
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 1},
		{PANEL_REG_ID_VCI, 10},
	},
	.reg_ctrl_post_enable = {
		{PANEL_REG_ID_VDDD, 1},
	},
	.reg_ctrl_pre_disable = {
		{PANEL_REG_ID_VDDD, 1},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VCI, 1},
		{PANEL_REG_ID_VDDI, 1},
	},
};

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,hk3", .data = &google_hk3 },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = hk3_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-hk3",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Chris Lu <luchris@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google HK3 panel driver");
MODULE_LICENSE("GPL");
