/* SPDX-License-Identifier: MIT */
/*
 * MIPI-DSI based bigsurf AMOLED LCD panel driver.
 *
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/thermal.h>
#include <video/mipi_display.h>

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel/gs_panel_funcs_defaults.h"
#include "trace/panel_trace.h"

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
 * The following features are correlated, if one or more of them change, the
 * others need to be updated unconditionally.
 */
enum hk3_panel_feature {
	FEAT_HBM,
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
	LHBM_R_COARSE,
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
	LHBM_OVERDRIVE_GRP_0_NIT,
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
	MATERIAL_E6,
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

/**
 * struct hk3_panel - panel specific info
 *
 * This struct maintains hk3 panel specific info. The variables with the prefix
 * hw_ keep track of the features that were actually committed to hardware, and
 * should be modified after sending cmds to panel, i.e. updating hw state.
 */
struct hk3_panel {
	/** @base: base panel struct with unified driver */
	struct gs_panel base;
	/**
	 * @feat: software or working correlated features,
	 *        not guaranteed to be effective in panel
	 */
	DECLARE_BITMAP(feat, FEAT_MAX);
	/** @hw_feat: correlated states effective in panel */
	DECLARE_BITMAP(hw_feat, FEAT_MAX);
	/** @hw_vrefresh: vrefresh rate effective in panel */
	u32 hw_vrefresh;
	/** @hw_idle_vrefresh: idle vrefresh rate effective in panel */
	u32 hw_idle_vrefresh;
	/**
	 * @auto_mode_vrefresh: indicates current minimum refresh rate
	 *			while in auto mode,
	 *			if 0 it means that auto mode is not enabled
	 */
	u32 auto_mode_vrefresh;
	/**
	 * @force_changeable_te: force changeable TE instead of fixed
	 *			 during early exit
	 */
	bool force_changeable_te;
	/**
	 * @force_changeable_te2: force changeable TE instad of fixed
	 *			  for monitoring refresh rate
	 */
	bool force_changeable_te2;
	/** @hw_acl_setting: automatic current limiting setting */
	u8 hw_acl_setting;
	/** @hw_dbv: indecate the current dbv */
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
	/** @pending_temp_update: whether there is pending temperature update. It will
	 * be handled in the commit_done function.
	 */
	bool pending_temp_update;
	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on
	 * or resetting panel can recover to normal mode after entering pixel-off
	 * state.
	 */
	bool is_pixel_off;
};

#define to_spanel(ctx) container_of(ctx, struct hk3_panel, base)

/* 1344x2992 */
static const struct drm_dsc_config wqhd_pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_count = 2,
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
	.rc_buf_thresh = { 14, 28, 42, 56, 70, 84, 98, 105, 112, 119, 121, 123, 125, 126 },
	.rc_range_params = {
		{ .range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 2 },
		{ .range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 0 },
		{ .range_min_qp = 1, .range_max_qp = 5, .range_bpg_offset = 0 },
		{ .range_min_qp = 1, .range_max_qp = 6, .range_bpg_offset = 62 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 60 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 58 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 8, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 9, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 10, .range_bpg_offset = 54 },
		{ .range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 54 },
		{ .range_min_qp = 5, .range_max_qp = 12, .range_bpg_offset = 52 },
		{ .range_min_qp = 5, .range_max_qp = 13, .range_bpg_offset = 52 },
		{ .range_min_qp = 7, .range_max_qp = 13, .range_bpg_offset = 52 },
		{ .range_min_qp = 13, .range_max_qp = 15, .range_bpg_offset = 52 }
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
	.slice_count = 2,
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
	.rc_buf_thresh = { 14, 28, 42, 56, 70, 84, 98, 105, 112, 119, 121, 123, 125, 126 },
	.rc_range_params = {
		{ .range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 2 },
		{ .range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 0 },
		{ .range_min_qp = 1, .range_max_qp = 5, .range_bpg_offset = 0 },
		{ .range_min_qp = 1, .range_max_qp = 6, .range_bpg_offset = 62 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 60 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 58 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 8, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 9, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 10, .range_bpg_offset = 54 },
		{ .range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 54 },
		{ .range_min_qp = 5, .range_max_qp = 12, .range_bpg_offset = 52 },
		{ .range_min_qp = 5, .range_max_qp = 13, .range_bpg_offset = 52 },
		{ .range_min_qp = 7, .range_max_qp = 13, .range_bpg_offset = 52 },
		{ .range_min_qp = 13, .range_max_qp = 15, .range_bpg_offset = 52 }
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

#define HK3_WRCTRLD_DIMMING_BIT 0x08
#define HK3_WRCTRLD_BCTRL_BIT 0x20
#define HK3_WRCTRLD_HBM_BIT 0xC0
#define HK3_WRCTRLD_LOCAL_HBM_BIT 0x10

#define HK3_TE2_CHANGEABLE 0x04
#define HK3_TE2_FIXED 0x51
#define HK3_TE2_RISING_EDGE_OFFSET 0x10
#define HK3_TE2_FALLING_EDGE_OFFSET 0x30
#define HK3_TE2_FALLING_EDGE_OFFSET_NS 0x25

#define HK3_TE_USEC_AOD 693
#define HK3_TE_USEC_120HZ 273
#define HK3_TE_USEC_60HZ_HS 8500
#define HK3_TE_USEC_60HZ_NS 546
#define HK3_TE_PERIOD_DELTA_TOLERANCE_USEC 2000

#define PROJECT "HK3"

static const u8 unlock_cmd_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 lock_cmd_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 freq_update[] = { 0xF7, 0x0F };
static const u8 lhbm_brightness_index[] = { 0xB0, 0x03, 0x21, 0x95 };
static const u8 lhbm_brightness_reg = 0x95;

static const u8 pixel_off[] = { 0x22 };
static const u8 sync_begin[] = { 0xE4, 0x00, 0x2C, 0x2C, 0xA2, 0x00, 0x00 };
static const u8 sync_end[] = { 0xE4, 0x00, 0x2C, 0x2C, 0x82, 0x00, 0x00 };
static const u8 aod_on[] = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24 };
static const u8 aod_off[] = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20 };
static const u8 min_dbv[] = { MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x04 };

static const struct gs_dsi_cmd hk3_lp_low_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* AOD Low Mode, 10nit */
	GS_DSI_CMD(0xB0, 0x00, 0x52, 0x94),
	GS_DSI_CMD(0x94, 0x01, 0x07, 0x6A, 0x02),
	GS_DSI_CMDLIST(lock_cmd_f0),
	GS_DSI_CMDLIST(min_dbv),
};

static const struct gs_dsi_cmd hk3_lp_high_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* AOD High Mode, 50nit */
	GS_DSI_CMD(0xB0, 0x00, 0x52, 0x94),
	GS_DSI_CMD(0x94, 0x00, 0x07, 0x6A, 0x02),
	GS_DSI_CMDLIST(lock_cmd_f0),
	GS_DSI_CMDLIST(min_dbv),
};

static const struct gs_binned_lp hk3_binned_lp[] = {
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 766, hk3_lp_low_cmds, HK3_TE2_RISING_EDGE_OFFSET,
			      HK3_TE2_FALLING_EDGE_OFFSET),
	BINNED_LP_MODE_TIMING("high", 3307, hk3_lp_high_cmds, HK3_TE2_RISING_EDGE_OFFSET,
			      HK3_TE2_FALLING_EDGE_OFFSET)
};

static inline bool is_in_comp_range(int temp)
{
	return (temp >= 10 && temp <= 49);
}

/**
 * gs_hk3_update_disp_thermi() - Read temperature, apply gain to reduce burn-in
 * @ctx: Panel handle
 *
 * Reads temperature and applies appropriate gain into DDIC for burn-in
 * compensation if needed
 */
static void gs_hk3_update_disp_therm(struct gs_panel *ctx)
{
	/* temperature*1000 in celsius */
	int temp, ret;
	struct device *dev = ctx->dev;
	struct hk3_panel *spanel = to_spanel(ctx);

	if (IS_ERR_OR_NULL(spanel->tz))
		return;

	if (ctx->panel_rev < PANEL_REV_EVT1_1 || ctx->panel_state != GPANEL_STATE_NORMAL)
		return;

	spanel->pending_temp_update = false;

	ret = thermal_zone_get_temp(spanel->tz, &temp);
	if (ret) {
		dev_err(dev, "%s: fail to read temperature ret:%d\n", __func__, ret);
		return;
	}

	temp = DIV_ROUND_CLOSEST(temp, 1000);
	dev_dbg(dev, "%s: temp=%d\n", __func__, temp);
	if (temp == spanel->hw_temp || !is_in_comp_range(temp))
		return;

	dev_dbg(dev, "%s: apply gain into ddic at %ddeg c\n", __func__, temp);

	/*TODO(tknelms) DPU_ATRACE_BEGIN(__func__);*/
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x03, 0x67);
	GS_DCS_BUF_ADD_CMD(dev, 0x67, temp);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	/*TODO(tknelms) DPU_ATRACE_END(__func__);*/

	spanel->hw_temp = temp;
}

static inline bool is_auto_mode_allowed(struct gs_panel *ctx)
{
	/* don't want to enable auto mode/early exit during dimming on */
	if (ctx->dimming_on)
		return false;

	if (ctx->idle_data.idle_delay_ms) {
		return false;
		/* TODO(tknelms): idle time stuff
		const unsigned int delta_ms = panel_get_idle_time_delta(ctx);

		if (delta_ms < ctx->idle_data.idle_delay_ms)
			return false;
		*/
	}

	return ctx->idle_data.panel_idle_enabled;
}

static u32 gs_hk3_get_min_idle_vrefresh(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
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

static void gs_hk3_set_panel_feat_te(struct device *dev, unsigned long changed_feat[],
				     unsigned long feat[], u32 vrefresh, bool force_changeable_te,
				     bool vrefresh_changed)
{
	/* TE setting */
	if (!test_bit(FEAT_EARLY_EXIT, changed_feat) && !test_bit(FEAT_OP_NS, changed_feat))
		return;
	if (test_bit(FEAT_EARLY_EXIT, feat) && !force_changeable_te) {
		u32 peak_vrefresh = test_bit(FEAT_OP_NS, feat) ? 60 : 120;

		/* Fixed TE */
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x51);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x02, 0xB9);
		/* Set TE frequency same with vrefresh */
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, peak_vrefresh == vrefresh ? 0x00 : 0x01);
		/* Set fixed TE width */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x0B, 0xBB, 0x00, 0x2F, 0x0B, 0xBB, 0x00, 0x2F);
	} else {
		/* Changeable TE */
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x04);
		/* Changeable TE width setting and frequency */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
		/* width 273us in normal mode */
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x0B, 0xBB, 0x00, 0x2F);
	}
};

static void gs_hk3_set_panel_feat_hbm_irc(struct device *dev, unsigned long changed_feat[],
					  unsigned long feat[], u32 panel_rev,
					  enum hk3_material material)
{
	u8 val;
	/*
	 * HBM IRC setting
	 *
	 * Description: after EVT1, IRC will be always on. "Flat mode" is used to
	 * replace IRC on for normal mode and HDR video, and "Flat Z mode" is used
	 * to replace IRC off for sunlight environment.
	 */
	if (panel_rev < PANEL_REV_EVT1) {
		if (test_bit(FEAT_IRC_OFF, changed_feat)) {
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x9B, 0x92);
			val = test_bit(FEAT_IRC_OFF, feat) ? 0x07 : 0x27;
			GS_DCS_BUF_ADD_CMD(dev, 0x92, val);
		}
		return;
	}
	if (!test_bit(FEAT_IRC_Z_MODE, changed_feat))
		return;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0x00, 0x92);
	if (test_bit(FEAT_IRC_Z_MODE, feat)) {
		if (material == MATERIAL_E6) {
			GS_DCS_BUF_ADD_CMD(dev, 0x92, 0xBE, 0x98);
			GS_DCS_BUF_ADD_CMD(dev, 0x92, 0xF1, 0xC1);
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0xF3, 0x68);
		} else {
			GS_DCS_BUF_ADD_CMD(dev, 0x92, 0xF1, 0xC1);
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0xF3, 0x68);
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x82, 0x70, 0x23, 0x91, 0x88, 0x3C);
		}
	} else {
		GS_DCS_BUF_ADD_CMD(dev, 0x92, 0x00, 0x00);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0xF3, 0x68);

		if (material == MATERIAL_E6)
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x71, 0x81, 0x59, 0x90, 0xA2, 0x80);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x77, 0x81, 0x23, 0x8C, 0x99, 0x3C);
	}
}

static void gs_hk3_set_panel_feat_opmode(struct device *dev, unsigned long changed_feat[],
					 unsigned long feat[])
{
	u8 val;
	/*
	 * Operating Mode: NS or HS
	 *
	 * Description: the configs could possibly be overrided by frequency setting,
	 * depending on FI mode.
	 */
	if (test_bit(FEAT_OP_NS, changed_feat)) {
		/* mode set */
		GS_DCS_BUF_ADD_CMD(dev, 0xF2, 0x01);
		val = test_bit(FEAT_OP_NS, feat) ? 0x18 : 0x00;
		GS_DCS_BUF_ADD_CMD(dev, 0x60, val);
	}
}

static void gs_hk3_set_panel_feat_early_exit(struct device *dev, unsigned long feat[])
{
	u8 val;
	/*
	 * Note: the following command sequence should be sent as a whole if one of
	 * panel state defined by enum panel_state changes or at turning on panel, or
	 * unexpected behaviors will be seen, e.g. black screen, flicker.
	 */

	/*
	 * Early-exit: enable or disable
	 *
	 * Description: early-exit sequence overrides some configs HBM set.
	 */
	if (test_bit(FEAT_EARLY_EXIT, feat)) {
		if (test_bit(FEAT_HBM, feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x00, 0x83, 0x03, 0x01);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x01, 0x83, 0x03, 0x03);
	} else {
		if (test_bit(FEAT_HBM, feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x80, 0x83, 0x03, 0x01);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x81, 0x83, 0x03, 0x03);
	}
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x10, 0xBD);
	val = test_bit(FEAT_EARLY_EXIT, feat) ? 0x22 : 0x00;
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x82, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val, val, val, val);
	val = test_bit(FEAT_OP_NS, feat) ? 0x4E : 0x1E;
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, val, 0xBD);
	if (test_bit(FEAT_HBM, feat)) {
		if (test_bit(FEAT_OP_NS, feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00,
					   0x0A, 0x00, 0x16, 0x00, 0x76);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00,
					   0x0B, 0x00, 0x17, 0x00, 0x77);
	} else {
		if (test_bit(FEAT_OP_NS, feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00,
					   0x14, 0x00, 0x2C, 0x00, 0xEC);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00,
					   0x16, 0x00, 0x2E, 0x00, 0xEE);
	}
}

static void gs_hk3_set_panel_feat_frequency_auto(struct device *dev, unsigned long feat[],
						 u32 vrefresh, u32 idle_vrefresh)
{
	u8 val;

	if (test_bit(FEAT_OP_NS, feat)) {
		/* threshold setting */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x0C, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00);
	} else {
		/* initial frequency */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x92, 0xBD);
		if (vrefresh == 60) {
			val = test_bit(FEAT_HBM, feat) ? 0x01 : 0x02;
		} else {
			if (vrefresh != 120)
				dev_warn(dev, "%s: unsupported init freq %d (hs mode)\n",
					 __func__, vrefresh);
			/* 120Hz */
			val = 0x00;
		}
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, val);
	}
	/* target frequency */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x12, 0xBD);
	if (test_bit(FEAT_OP_NS, feat)) {
		if (idle_vrefresh == 30) {
			val = test_bit(FEAT_HBM, feat) ? 0x02 : 0x04;
		} else if (idle_vrefresh == 10) {
			val = test_bit(FEAT_HBM, feat) ? 0x0A : 0x14;
		} else {
			if (idle_vrefresh != 1)
				dev_warn(dev, "%s: unsupported target freq %d (ns mode)\n",
					 __func__, idle_vrefresh);
			/* 1Hz */
			val = test_bit(FEAT_HBM, feat) ? 0x76 : 0xEC;
		}
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, val);
	} else {
		if (idle_vrefresh == 30) {
			val = test_bit(FEAT_HBM, feat) ? 0x03 : 0x06;
		} else if (idle_vrefresh == 10) {
			val = test_bit(FEAT_HBM, feat) ? 0x0B : 0x16;
		} else {
			if (idle_vrefresh != 1)
				dev_warn(dev, "%s: unsupported target freq %d (hs mode)\n", __func__,
					 idle_vrefresh);
			/* 1Hz */
			val = test_bit(FEAT_HBM, feat) ? 0x77 : 0xEE;
		}
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, val);
	}
	/* step setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x9E, 0xBD);
	if (test_bit(FEAT_OP_NS, feat)) {
		if (test_bit(FEAT_HBM, feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x04, 0x00, 0x14, 0x00, 0x00);
	} else {
		if (test_bit(FEAT_HBM, feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x01, 0x00, 0x03, 0x00, 0x0B);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x02, 0x00, 0x06, 0x00, 0x16);
	}
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xAE, 0xBD);
	if (test_bit(FEAT_OP_NS, feat)) {
		if (idle_vrefresh == 30) {
			/* 60Hz -> 30Hz idle */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00);
		} else if (idle_vrefresh == 10) {
			/* 60Hz -> 10Hz idle */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x00, 0x00);
		} else {
			if (idle_vrefresh != 1)
				dev_warn(dev, "%s: unsupported freq step to %d (ns mode)\n", __func__,
					 idle_vrefresh);
			/* 60Hz -> 1Hz idle */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x03, 0x00);
		}
	} else {
		if (vrefresh == 60) {
			if (idle_vrefresh == 30) {
				/* 60Hz -> 30Hz idle */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x00, 0x00);
			} else if (idle_vrefresh == 10) {
				/* 60Hz -> 10Hz idle */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x01, 0x00);
			} else {
				if (idle_vrefresh != 1)
					dev_warn(dev, "%s: unsupported freq step to %d (hs mode)\n",
						 __func__, vrefresh);
				/* 60Hz -> 1Hz idle */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x01, 0x03);
			}
		} else {
			if (vrefresh != 120)
				dev_warn(dev, "%s: unsupported freq step from %d (hs mode)\n", __func__,
					 vrefresh);
			if (idle_vrefresh == 30) {
				/* 120Hz -> 30Hz idle */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00);
			} else if (idle_vrefresh == 10) {
				/* 120Hz -> 10Hz idle */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x03, 0x00);
			} else {
				if (idle_vrefresh != 1)
					dev_warn(dev, "%s: unsupported freq step to %d (hs mode)\n",
						 __func__, idle_vrefresh);
				/* 120Hz -> 1Hz idle */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x01, 0x03);
			}
		}
	}
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0xA3);
}

static void gs_hk3_set_panel_feat_frequency_manual(struct device *dev, unsigned long feat[],
						   u32 vrefresh, u32 idle_vrefresh)
{
	u8 val;

	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21);
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
				dev_warn(dev, "%s: unsupported manual freq %d (ns mode)\n", __func__,
					 vrefresh);
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
				dev_warn(dev, "%s: unsupported manual freq %d (hs mode)\n", __func__,
					 vrefresh);
			/* 120Hz */
			val = 0x00;
		}
	}
	GS_DCS_BUF_ADD_CMD(dev, 0x60, val);
}

static void gs_hk3_set_panel_feat_frequency(struct device *dev, unsigned long feat[], u32 vrefresh,
					    u32 idle_vrefresh)
{
	/*
	 * Frequency setting: FI, frequency, idle frequency
	 *
	 * Description: this sequence possibly overrides some configs early-exit
	 * and operation set, depending on FI mode.
	 */
	if (test_bit(FEAT_FRAME_AUTO, feat))
		gs_hk3_set_panel_feat_frequency_auto(dev, feat, vrefresh, idle_vrefresh);
	else
		gs_hk3_set_panel_feat_frequency_manual(dev, feat, vrefresh, idle_vrefresh);
}

static void gs_hk3_set_panel_feat(struct gs_panel *ctx, const u32 vrefresh, const u32 idle_vrefresh,
				  const unsigned long *feat, bool enforce)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	DECLARE_BITMAP(changed_feat, FEAT_MAX);
	bool vrefresh_changed = spanel->hw_vrefresh != vrefresh;

	if (enforce) {
		bitmap_fill(changed_feat, FEAT_MAX);
		vrefresh_changed = true;
	} else {
		bitmap_xor(changed_feat, feat, spanel->hw_feat, FEAT_MAX);
		if (bitmap_empty(changed_feat, FEAT_MAX) && vrefresh == spanel->hw_vrefresh &&
		    idle_vrefresh == spanel->hw_idle_vrefresh) {
			dev_dbg(dev, "%s: no changes, skip update\n", __func__);
			return;
		}
	}

	spanel->hw_vrefresh = vrefresh;
	spanel->hw_idle_vrefresh = idle_vrefresh;
	bitmap_copy(spanel->hw_feat, feat, FEAT_MAX);
	dev_dbg(dev, "op=%s ee=%s hbm=%s irc=%s fi=%s fps=%u idle_fps=%u\n",
		test_bit(FEAT_OP_NS, feat) ? "ns" : "hs",
		test_bit(FEAT_EARLY_EXIT, feat) ? "on" : "off",
		test_bit(FEAT_HBM, feat) ? "on" : "off",
		ctx->panel_rev >= PANEL_REV_EVT1 ?
			(test_bit(FEAT_IRC_Z_MODE, feat) ? "flat_z" : "flat") :
			(test_bit(FEAT_IRC_OFF, feat) ? "off" : "on"),
		test_bit(FEAT_FRAME_AUTO, feat) ? "auto" : "manual", vrefresh, idle_vrefresh);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);

	gs_hk3_set_panel_feat_te(dev, changed_feat, spanel->feat, vrefresh,
				 spanel->force_changeable_te, vrefresh_changed);

	/* TE2 setting */
	/*TODO(tknelms): te2 updating
	if (test_bit(FEAT_OP_NS, changed_feat))
		gs_hk3_update_te2_internal(ctx, false);
	*/

	gs_hk3_set_panel_feat_hbm_irc(dev, changed_feat, spanel->feat, ctx->panel_rev,
				      spanel->material);
	gs_hk3_set_panel_feat_opmode(dev, changed_feat, spanel->feat);
	gs_hk3_set_panel_feat_early_exit(dev, spanel->feat);

	gs_hk3_set_panel_feat_frequency(dev, spanel->feat, vrefresh, idle_vrefresh);

	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

/**
 * hk3_disable_panel_feat - set the panel at the state of powering up except refresh rate
 * @ctx: gs_panel struct
 * @vrefresh: refresh rate
 * This function disables HBM, switches to HS, sets manual mode and changeable TE.
 */
static void gs_hk3_disable_panel_feat(struct gs_panel *ctx, u32 vrefresh)
{
	DECLARE_BITMAP(feat, FEAT_MAX);

	bitmap_zero(feat, FEAT_MAX);
	gs_hk3_set_panel_feat(ctx, vrefresh, 0, feat, true);
}

static void gs_hk3_update_panel_feat(struct gs_panel *ctx, bool enforce)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	const struct gs_panel_mode *pmode = ctx->current_mode;
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = spanel->auto_mode_vrefresh;

	gs_hk3_set_panel_feat(ctx, vrefresh, idle_vrefresh, spanel->feat, enforce);
}

static void gs_hk3_update_refresh_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
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
		return;
	}

	dev_dbg(ctx->dev, "%s: mode: %s set idle_vrefresh: %u\n", __func__, pmode->mode.name,
		idle_vrefresh);

	if (idle_vrefresh)
		set_bit(FEAT_FRAME_AUTO, spanel->feat);
	else
		clear_bit(FEAT_FRAME_AUTO, spanel->feat);

	/*
	 * fixed TE + early exit: 60NS, 120HS, 60HS + auto mode
	 * changeable TE + disabling early exit: 60HS + manual mode
	 */
	if ((vrefresh == ctx->op_hz) || idle_vrefresh)
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
	ctx->idle_data.panel_idle_vrefresh = idle_vrefresh;
	gs_hk3_set_panel_feat(ctx, vrefresh, idle_vrefresh, spanel->feat, false);
	schedule_work(&ctx->state_notify);

	dev_dbg(ctx->dev, "%s: display state is notified\n", __func__);
}

static void gs_hk3_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	if (vrefresh > ctx->op_hz) {
		dev_err(ctx->dev, "invalid freq setting: op_hz=%u, vrefresh=%u\n", ctx->op_hz,
			vrefresh);
		return;
	}

	/* TODO(tknelms): this
	if (pmode->idle_mode == IDLE_MODE_ON_INACTIVITY)
					idle_vrefresh = hk3_get_min_idle_vrefresh(ctx, pmode);
	*/

	gs_hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);

	dev_dbg(ctx->dev, "change to %u hz\n", vrefresh);
}

static void gs_hk3_panel_idle_notification(struct gs_panel *ctx, u32 display_id, u32 vrefresh,
					   u32 idle_te_vrefresh)
{
	char event_string[64];
	char *envp[] = { event_string, NULL };
	struct drm_device *dev = ctx->bridge.dev;

	if (!dev) {
		dev_warn(ctx->dev, "%s: drm_device is null\n", __func__);
		return;
	}
	scnprintf(event_string, sizeof(event_string), "PANEL_IDLE_ENTER=%u,%u,%u", display_id,
		  vrefresh, idle_te_vrefresh);
	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}

static void gs_hk3_wait_one_vblank(struct gs_panel *ctx)
{
	struct drm_crtc *crtc = NULL;

	if (ctx->gs_connector->base.state)
		crtc = ctx->gs_connector->base.state->crtc;

	/*TODO(tknelms) DPU_ATRACE_BEGIN(__func__);*/
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
	/*TODO(tknelms) DPU_ATRACE_END(__func__);*/
}

static bool gs_hk3_set_self_refresh(struct gs_panel *ctx, bool enable)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	struct device *dev = ctx->dev;
	struct hk3_panel *spanel = to_spanel(ctx);
	struct gs_panel_idle_data *idle_data = &ctx->idle_data;
	u32 idle_vrefresh;

	dev_dbg(dev, "%s: %d\n", __func__, enable);

	if (unlikely(!pmode))
		return false;

	/* self refresh is not supported in lp mode since that always makes use of
	 * early exit */
	if (pmode->gs_mode.is_lp_mode) {
		/* set 1Hz while self refresh is active, otherwise clear it */
		idle_data->panel_idle_vrefresh = enable ? 1 : 0;
		schedule_work(&ctx->state_notify);
		return false;
	}

	if (spanel->pending_temp_update && enable)
		gs_hk3_update_disp_therm(ctx);

	idle_vrefresh = gs_hk3_get_min_idle_vrefresh(ctx, pmode);

	if (pmode->idle_mode != GIDLE_MODE_ON_SELF_REFRESH) {
		/*
		 * if idle mode is on inactivity, may need to update the target fps for auto
		 * mode, or switch to manual mode if idle should be disabled
		 * (idle_vrefresh=0)
		 */
		if ((pmode->idle_mode == GIDLE_MODE_ON_INACTIVITY) &&
		    (spanel->auto_mode_vrefresh != idle_vrefresh)) {
			gs_hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);
			return true;
		}
		return false;
	}

	if (!enable)
		idle_vrefresh = 0;

	/* if there's no change in idle state then skip cmds */
	if (idle_data->panel_idle_vrefresh == idle_vrefresh)
		return false;

	/*TODO(tknelms) DPU_ATRACE_BEGIN(__func__);*/
	gs_hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);

	if (idle_vrefresh) {
		const int vrefresh = drm_mode_vrefresh(&pmode->mode);

		gs_hk3_panel_idle_notification(ctx, 0, vrefresh, 120);
	} else if (idle_data->panel_need_handle_idle_exit) {
		/*
		 * after exit idle mode with fixed TE at non-120hz, TE may still keep at
		 * 120hz. If any layer that already be assigned to DPU that can't be handled
		 * at 120hz, panel_need_handle_idle_exit will be set then we need to wait
		 * one vblank to avoid underrun issue.
		 */
		dev_dbg(dev, "wait one vblank after exit idle\n");
		gs_hk3_wait_one_vblank(ctx);
	}

	/*TODO(tknelms) DPU_ATRACE_END(__func__);*/

	return true;
}

static int gs_hk3_atomic_check(struct gs_panel *ctx, struct drm_atomic_state *state)
{
	struct drm_connector *conn = &ctx->gs_connector->base;
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(state, conn);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct hk3_panel *spanel = to_spanel(ctx);

	/*
	 * TODO(b/279521693)
	 * lhbm hist config
	 */

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

		/*
		 * set clock to max refresh rate on self refresh exit,
		 * or resume due to early exit
		 */
		mode->clock = mode->htotal * mode->vtotal * 120 / 1000;

		if (mode->clock != new_crtc_state->mode.clock) {
			new_crtc_state->mode_changed = true;
			dev_dbg(ctx->dev, "raise mode (%s) clock to 120hz on %s\n", mode->name,
				old_crtc_state->self_refresh_active ? "self refresh exit" :
								      "resume");
		}
	} else if (old_crtc_state->active_changed &&
		   (old_crtc_state->adjusted_mode.clock != old_crtc_state->mode.clock)) {
		/*
		 * clock hacked in last commit due to
		 * self refresh exit or resume, undo that
		 */
		new_crtc_state->mode_changed = true;
		new_crtc_state->adjusted_mode.clock = new_crtc_state->mode.clock;
		dev_dbg(ctx->dev, "restore mode (%s) clock after self refresh exit or resume\n",
			new_crtc_state->mode.name);
	}

	return 0;
}

static void gs_hk3_write_display_mode(struct gs_panel *ctx, const struct drm_display_mode *mode)
{
	u8 val = HK3_WRCTRLD_BCTRL_BIT;

	if (GS_IS_HBM_ON(ctx->hbm_mode))
		val |= HK3_WRCTRLD_HBM_BIT;

	if (!gs_is_local_hbm_disabled(ctx))
		val |= HK3_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= HK3_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev, "%s(wrctrld:%#x, hbm: %s, dimming: %s local_hbm: %s)\n", __func__, val,
		GS_IS_HBM_ON(ctx->hbm_mode) ? "on" : "off", ctx->dimming_on ? "on" : "off",
		!gs_is_local_hbm_disabled(ctx) ? "on" : "off");

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(ctx->dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

#define HK3_OPR_VAL_LEN 2
#define HK3_MAX_OPR_VAL 0x3FF
/* Get OPR (on pixel ratio), the unit is percent */
static int gs_hk3_get_opr(struct gs_panel *ctx, u8 *opr)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = ctx->dev;
	u8 buf[HK3_OPR_VAL_LEN] = { 0 };
	u16 val;
	int ret;

	/*TODO(tknelms)DPU_ATRACE_BEGIN(__func__);*/
	GS_DCS_WRITE_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_WRITE_CMD(dev, 0xB0, 0x00, 0xE7, 0x91);
	ret = mipi_dsi_dcs_read(dsi, 0x91, buf, HK3_OPR_VAL_LEN);
	GS_DCS_WRITE_CMDLIST(dev, lock_cmd_f0);
	/*TODO(tknelms)DPU_ATRACE_END(__func__);*/

	if (ret != HK3_OPR_VAL_LEN) {
		dev_warn(dev, "Failed to read OPR (%d)\n", ret);
		return ret;
	}

	val = (buf[0] << 8) | buf[1];
	*opr = DIV_ROUND_CLOSEST(val * 100, HK3_MAX_OPR_VAL);
	dev_dbg(dev, "%s: %u (%#X)\n", __func__, *opr, val);

	return 0;
}

#define HK3_ZA_THRESHOLD_OPR 80
static void gs_hk3_update_za(struct gs_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	bool enable_za = false;
	u8 opr;

	if ((spanel->hw_acl_setting > 0) && !spanel->force_za_off) {
		if (ctx->panel_rev != PANEL_REV_PROTO1) {
			enable_za = true;
		} else if (!gs_hk3_get_opr(ctx, &opr)) {
			enable_za = (opr > HK3_ZA_THRESHOLD_OPR);
		} else {
			dev_warn(ctx->dev, "Unable to update za\n");
			return;
		}
	}

	if (spanel->hw_za_enabled != enable_za) {
		/* LP setting - 0x21 or 0x11: 7.5%, 0x00: off */
		u8 val = 0;

		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x6C, 0x92);
		if (enable_za)
			val = (ctx->panel_rev == PANEL_REV_PROTO1) ? 0x21 : 0x11;
		GS_DCS_BUF_ADD_CMD(dev, 0x92, val);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

		spanel->hw_za_enabled = enable_za;
		dev_dbg(dev, "%s: %s\n", __func__, enable_za ? "on" : "off");
	}
}

#define HK3_ACL_ZA_THRESHOLD_DBV_P1_0 3917
#define HK3_ACL_ZA_THRESHOLD_DBV_P1_1 3781
#define HK3_ACL_ENHANCED_THRESHOLD_DBV 3865
#define HK3_ACL_NORMAL_THRESHOLD_DBV_1 3570
#define HK3_ACL_NORMAL_THRESHOLD_DBV_2 3963

#define HK3_ACL_SETTING_EVT_17 0x03
#define HK3_ACL_SETTING_EVT_12 0x02
#define HK3_ACL_SETTING_EVT_7p5 0x01
#define HK3_ACL_SETTING_PROTO_5 0x01
#define HK3_ACL_SETTING_PROTO_7p5 0x02

static void gs_hk3_set_acl_mode(struct gs_panel *ctx, enum gs_acl_mode mode)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
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
		setting = HK3_ACL_SETTING_PROTO_5;
	} else if (ctx->panel_rev == PANEL_REV_PROTO1_1) {
		dbv_th = HK3_ACL_ZA_THRESHOLD_DBV_P1_1;
		setting = HK3_ACL_SETTING_PROTO_7p5;
	} else {
		if (mode == ACL_ENHANCED) {
			dbv_th = HK3_ACL_ENHANCED_THRESHOLD_DBV;
			setting = HK3_ACL_SETTING_EVT_17;
		} else if (mode == ACL_NORMAL) {
			if (spanel->hw_dbv >= HK3_ACL_NORMAL_THRESHOLD_DBV_1 &&
			    spanel->hw_dbv < HK3_ACL_NORMAL_THRESHOLD_DBV_2) {
				dbv_th = HK3_ACL_NORMAL_THRESHOLD_DBV_1;
				setting = HK3_ACL_SETTING_EVT_7p5;
			} else if (spanel->hw_dbv >= HK3_ACL_NORMAL_THRESHOLD_DBV_2) {
				dbv_th = HK3_ACL_NORMAL_THRESHOLD_DBV_2;
				setting = HK3_ACL_SETTING_EVT_12;
			}
		}
	}

	enable_acl = (spanel->hw_dbv >= dbv_th && GS_IS_HBM_ON(ctx->hbm_mode) && mode != ACL_OFF);
	if (enable_acl == false)
		setting = 0;

	if (spanel->hw_acl_setting != setting) {
		GS_DCS_WRITE_CMD(dev, 0x55, setting);
		spanel->hw_acl_setting = setting;
		dev_dbg(dev, "%s: %d\n", __func__, setting);
		/* Keep ZA off after EVT1 */
		if (ctx->panel_rev < PANEL_REV_EVT1)
			gs_hk3_update_za(ctx);
	}
}

static int gs_hk3_set_brightness(struct gs_panel *ctx, u16 br)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	u16 brightness;
	int ret;

	/* TODO(tknelms): lp mode  brightness
	if (ctx->current_mode->gs_mode.is_lp_mode) {
		// don't stay at pixel-off state in AOD, or black screen is possibly seen
		if (spanel->is_pixel_off) {
			GS_DCS_WRITE_CMD(dev, MIPI_DCS_ENTER_NORMAL_MODE);
			spanel->is_pixel_off = false;
		}

		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}
	*/

	/* Use pixel off command instead of setting DBV 0 */
	if (!br) {
		if (!spanel->is_pixel_off) {
			GS_DCS_WRITE_CMDLIST(dev, pixel_off);
			spanel->is_pixel_off = true;
			dev_dbg(dev, "%s: pixel off instead of dbv 0\n", __func__);
		}
		return 0;
	}
	if (spanel->is_pixel_off) {
		GS_DCS_WRITE_CMD(dev, MIPI_DCS_ENTER_NORMAL_MODE);
		spanel->is_pixel_off = false;
	}

	brightness = swab16(br);
	ret = gs_dcs_set_brightness(ctx, brightness);
	if (ret)
		return ret;

	spanel->hw_dbv = br;
	gs_hk3_set_acl_mode(ctx, ctx->acl_mode);

	return 0;
}

static const struct gs_dsi_cmd hk3_display_on_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0),
	GS_DSI_CMDLIST(sync_begin),
	/* AMP type change (return) */
	GS_DSI_CMD(0xB0, 0x00, 0x4F, 0xF4),
	GS_DSI_CMD(0xF4, 0x70),
	/* Vreg = 7.1V (return) */
	GS_DSI_CMD(0xB0, 0x00, 0x31, 0xF4),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xF4, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1), 0xF4, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B),
	GS_DSI_CMDLIST(sync_end),
	GS_DSI_CMDLIST(lock_cmd_f0),

	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_ON),
};
static DEFINE_GS_CMDSET(hk3_display_on);

static const struct gs_dsi_cmd hk3_display_off_cmds[] = {
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_OFF),

	GS_DSI_CMDLIST(unlock_cmd_f0),
	GS_DSI_CMDLIST(sync_begin),
	/* AMP type change */
	GS_DSI_CMD(0xB0, 0x00, 0x4F, 0xF4),
	GS_DSI_CMD(0xF4, 0x50),
	/* Vreg = 4.5 */
	GS_DSI_CMD(0xB0, 0x00, 0x31, 0xF4),
	GS_DSI_CMD(0xF4, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_CMDLIST(sync_end),
	GS_DSI_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(hk3_display_off);

static unsigned int gs_hk3_get_te_usec(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	if (spanel->hw_vrefresh != 60)
		return pmode->gs_mode.te_usec;
	else
		return (test_bit(FEAT_OP_NS, spanel->feat) ? HK3_TE_USEC_60HZ_NS :
							     HK3_TE_USEC_60HZ_HS);
}

static u32 gs_hk3_get_te_width_usec(u32 vrefresh, bool is_ns)
{
	/* TODO: update this line if supporting 30 Hz normal mode in the future */
	if (vrefresh == 30)
		return HK3_TE_USEC_AOD;
	else if (vrefresh == 120)
		return HK3_TE_USEC_120HZ;
	else
		return is_ns ? HK3_TE_USEC_60HZ_NS : HK3_TE_USEC_60HZ_HS;
}

static void gs_hk3_wait_for_vsync_done(struct gs_panel *ctx, u32 vrefresh, bool is_ns)
{
	u32 te_width_us = gs_hk3_get_te_width_usec(vrefresh, is_ns);

	dev_dbg(ctx->dev, "%s: %dhz\n", __func__, vrefresh);

	/*TODO(tknelms): DPU_ATRACE_BEGIN(__func__);*/
	gs_panel_wait_for_vsync_done(ctx, te_width_us, GS_VREFRESH_TO_PERIOD_USEC(vrefresh));
	/* add 1ms tolerance */
	gs_panel_msleep(1);
	/*TODO(tknelms): DPU_ATRACE_END(__func__);*/
}

/**
 * gs_hk3_wait_for_vsync_done_changeable - wait for finishing vsync for changeable TE to avoid
 * fake TE at transition from fixed TE to changeable TE.
 * @ctx: panel struct
 * @vrefresh: current refresh rate
 */
static void gs_hk3_wait_for_vsync_done_changeable(struct gs_panel *ctx, u32 vrefresh, bool is_ns)
{
	int i = 0;
	const int timeout = 5;
	u32 te_width_us = gs_hk3_get_te_width_usec(vrefresh, is_ns);

	dev_dbg(ctx->dev, "%s\n", __func__);

	while (i++ < timeout) {
		ktime_t t;
		s64 delta_us;
		int period_us = GS_VREFRESH_TO_PERIOD_USEC(vrefresh);

		gs_panel_wait_for_vblank(ctx);
		t = ktime_get();
		gs_panel_wait_for_vblank(ctx);
		delta_us = ktime_us_delta(ktime_get(), t);
		if (abs(delta_us - period_us) < HK3_TE_PERIOD_DELTA_TOLERANCE_USEC)
			break;
	}
	if (i >= timeout)
		dev_warn(ctx->dev, "timeout of waiting for changeable TE @ %d Hz\n", vrefresh);
	usleep_range(te_width_us, te_width_us + 10);
}

static bool gs_hk3_is_peak_vrefresh(u32 vrefresh, bool is_ns)
{
	return (is_ns && vrefresh == 60) || (!is_ns && vrefresh == 120);
}

static void gs_hk3_set_lp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	/*TODO(tknelms) when binned_lp:const u16 brightness = gs_panel_get_brightness(ctx);*/
	bool is_changeable_te = !test_bit(FEAT_EARLY_EXIT, spanel->feat);
	bool is_ns = test_bit(FEAT_OP_NS, spanel->feat);
	bool panel_enabled = gs_is_panel_enabled(ctx);
	u32 vrefresh = panel_enabled ? spanel->hw_vrefresh : 60;

	dev_dbg(dev, "%s: panel: %s\n", __func__, panel_enabled ? "ON" : "OFF");

	/*TODO(tknelms): DPU_ATRACE_BEGIN(__func__);*/

	gs_hk3_disable_panel_feat(ctx, vrefresh);
	if (panel_enabled) {
		/* init sequence has sent display-off command already */
		if (!gs_hk3_is_peak_vrefresh(vrefresh, is_ns) && is_changeable_te)
			gs_hk3_wait_for_vsync_done_changeable(ctx, vrefresh, is_ns);
		else
			gs_hk3_wait_for_vsync_done(ctx, vrefresh, is_ns);
		gs_panel_send_cmdset(ctx, &hk3_display_off_cmdset);
	}
	gs_hk3_wait_for_vsync_done(ctx, vrefresh, false);

	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, aod_on);
	/*TODO(tknelms)gs_panel_set_binned_lp(ctx, brightness);*/
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* Fixed TE: sync on */
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x51);
	/* Default TE pulse width 693us */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x0B, 0xE0, 0x00, 0x2F, 0x0B, 0xE0, 0x00, 0x2F);
	/* Frequency set for AOD */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x02, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x00);
	/* Auto frame insertion: 1Hz */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x18, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x04, 0x00, 0x74);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xB8, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x08);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xC8, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x03);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0xA7);
	/* Enable early exit */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xE8, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x10, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x22);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x82, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x22, 0x22, 0x22, 0x22);
	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	gs_panel_send_cmdset(ctx, &hk3_display_on_cmdset);

	spanel->hw_vrefresh = 30;

	/*TODO(tknelms): DPU_ATRACE_END(__func__);*/

	dev_info(dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
}

static void gs_hk3_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 delay_us = mult_frac(1000, 1020, vrefresh);
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;

	/* clear the brightness level (temporary solution) */
	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x00);
	dev_dbg(dev, "%s\n", __func__);

	/*TODO(tknelms): DPU_ATRACE_BEGIN(__func__);*/

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* manual mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21);
	/* Changeable TE is a must to ensure command sync */
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x04);
	/* Changeable TE width setting and frequency */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
	/* width 693us in AOD mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x0B, 0xE0, 0x00, 0x2F);
	/* AOD 30Hz */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0x60);
	GS_DCS_BUF_ADD_CMD(dev, 0x60, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	spanel->hw_idle_vrefresh = 0;

	gs_hk3_wait_for_vsync_done(ctx, 30, false);
	gs_panel_send_cmdset(ctx, &hk3_display_off_cmdset);

	gs_hk3_wait_for_vsync_done(ctx, 30, false);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* disabling AOD low Mode is a must before aod-off */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x52, 0x94);
	GS_DCS_BUF_ADD_CMD(dev, 0x94, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, aod_off);
	gs_hk3_set_panel_feat(ctx, drm_mode_vrefresh(&pmode->mode), spanel->auto_mode_vrefresh,
			      spanel->feat, true);
	/* backlight control and dimming */
	gs_hk3_write_display_mode(ctx, &pmode->mode);
	gs_hk3_change_frequency(ctx, pmode);
	gs_panel_send_cmdset(ctx, &hk3_display_on_cmdset);

	gs_panel_msleep(delay_us / 1000);
	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);
	/*TODO(tknelms): DPU_ATRACE_END(__func__);*/

	dev_info(dev, "exit LP mode\n");
}

static const struct gs_dsi_cmd hk3_init_cmds[] = {
	GS_DSI_DELAY_CMD(10, MIPI_DCS_EXIT_SLEEP_MODE),

	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* Delete Toggle */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0xB0, 0x00, 0x58, 0x94),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0x94, 0x0B, 0xF0, 0x0B, 0xF0),
	/* AMP type change */
	GS_DSI_CMD(0xB0, 0x00, 0x4F, 0xF4),
	GS_DSI_CMD(0xF4, 0x50),
	/* VREG 4.5V */
	GS_DSI_CMD(0xB0, 0x00, 0x31, 0xF4),
	GS_DSI_CMD(0xF4, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_DELAY_CMDLIST(110, lock_cmd_f0),

	/* Enable TE*/
	GS_DSI_CMD(MIPI_DCS_SET_TEAR_ON),

	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* AOD Transition Set */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1), 0xB0, 0x00, 0x03, 0xBB),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1), 0xBB, 0x41),

	/* TSP SYNC Enable (Auto Set) */
	GS_DSI_CMD(0xB0, 0x00, 0x3C, 0xB9),
	GS_DSI_CMD(0xB9, 0x19, 0x09),

	/* FFC: 165MHz, MIPI Speed 1368 Mbps */
	GS_DSI_CMD(0xB0, 0x00, 0x36, 0xC5),
	GS_DSI_CMD(0xC5, 0x11, 0x10, 0x50, 0x05, 0x4D, 0x31, 0x40, 0x00, 0x40, 0x00, 0x40, 0x00,
		   0x4D, 0x31, 0x40, 0x00, 0x40, 0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00, 0x40,
		   0x00, 0x40, 0x00, 0x4D, 0x31, 0x40, 0x00, 0x40, 0x00, 0x40, 0x00),

	/* TE width setting */
	GS_DSI_CMD(0xB0, 0x00, 0x04, 0xB9),
	GS_DSI_CMD(0xB9, 0x0B, 0xBB, 0x00, 0x2F, /* changeable TE */
		   0x0B, 0xBB, 0x00, 0x2F, 0x0B, 0xBB, 0x00, 0x2F), /* fixed TE */

	/* enable OPEC (auto still IMG detect off) */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_MP), 0xB0, 0x00, 0x1D, 0x63),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_MP), 0x63, 0x02, 0x18),

	/* PMIC Fast Discharge off */
	GS_DSI_CMD(0xB0, 0x00, 0x18, 0xB1),
	GS_DSI_CMD(0xB1, 0x55, 0x01),
	GS_DSI_CMD(0xB0, 0x00, 0x13, 0xB1),
	GS_DSI_CMD(0xB1, 0x80),

	GS_DSI_CMDLIST(freq_update),
	GS_DSI_CMDLIST(lock_cmd_f0),
	/* CASET: 1343 */
	GS_DSI_CMD(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x05, 0x3F),
	/* PASET: 2991 */
	GS_DSI_CMD(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x0B, 0xAF),
};
static DEFINE_GS_CMDSET(hk3_init);

static const struct gs_dsi_cmd hk3_ns_gamma_fix_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0), GS_DSI_CMD(0xB0, 0x02, 0x3F, 0xCB),
	GS_DSI_CMD(0xCB, 0x0A),	       GS_DSI_CMD(0xB0, 0x02, 0x45, 0xCB),
	GS_DSI_CMD(0xCB, 0x0A),	       GS_DSI_CMDLIST(freq_update),
	GS_DSI_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(hk3_ns_gamma_fix);

static void gs_hk3_lhbm_luminance_opr_setting(struct gs_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	bool is_ns_mode = test_bit(FEAT_OP_NS, spanel->feat);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0xF9, 0x95);
	/* DBV setting */
	GS_DCS_BUF_ADD_CMD(dev, 0x95, 0x00, 0x40, 0x0C, 0x01, 0x90, 0x33, 0x06, 0x60, 0xCC, 0x11,
			   0x92, 0x7F);
	GS_DCS_BUF_ADD_CMD(dev, 0x71, 0xC6, 0x00, 0x00, 0x19);
	/* 120Hz base (HS) offset */
	GS_DCS_BUF_ADD_CMD(dev, 0x6C, 0x9C, 0x9F, 0x59, 0x58, 0x50, 0x2F, 0x2B, 0x2E);
	GS_DCS_BUF_ADD_CMD(dev, 0x71, 0xC6, 0x00, 0x00, 0x6A);
	/* 60Hz base (NS) offset */
	GS_DCS_BUF_ADD_CMD(dev, 0x6C, 0xA0, 0xA7, 0x57, 0x5C, 0x52, 0x37, 0x37, 0x40);

	/* Target frequency */
	GS_DCS_BUF_ADD_CMD(dev, 0x60, is_ns_mode ? 0x18 : 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	/* Opposite setting of target frequency */
	GS_DCS_BUF_ADD_CMD(dev, 0x60, is_ns_mode ? 0x00 : 0x18);
	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	/* Target frequency */
	GS_DCS_BUF_ADD_CMD(dev, 0x60, is_ns_mode ? 0x18 : 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static void gs_hk3_negative_field_setting(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	/* all settings will take effect in AOD mode automatically */
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* Vint -3V */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x21, 0xF4);
	GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0x1E);
	/* Vaint -4V */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x69, 0xF4);
	GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0x78);
	/* VGL -8V */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x17, 0xF4);
	GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0x1E);
	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static int gs_hk3_enable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	struct hk3_panel *spanel = to_spanel(ctx);
	const bool needs_reset = !gs_is_panel_enabled(ctx);
	bool is_fhd;

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;
	is_fhd = mode->hdisplay == 1008;

	dev_dbg(dev, "%s (%s)\n", __func__, is_fhd ? "fhd" : "wqhd");

	/*TODO(tknelms) DPU_ATRACE_BEGIN(__func__);*/

	if (needs_reset)
		gs_panel_reset_helper(ctx);

	/*TODO(tknelms) PANEL_SEQ_LABEL_BEGIN("init"); */
	/* DSC related configuration */
	GS_DCS_WRITE_CMD(dev, 0x9D, 0x01);
	gs_dcs_write_dsc_config(dev, pmode->gs_mode.dsc.cfg);

	if (needs_reset) {
		gs_panel_send_cmdset(ctx, &hk3_init_cmdset);
		if (spanel->material == MATERIAL_E7_DOE)
			gs_panel_send_cmdset(ctx, &hk3_ns_gamma_fix_cmdset);
		if (ctx->panel_rev == PANEL_REV_PROTO1)
			gs_hk3_lhbm_luminance_opr_setting(ctx);
		if (ctx->panel_rev >= PANEL_REV_DVT1)
			gs_hk3_negative_field_setting(ctx);

		spanel->is_pixel_off = false;
	}
	/*TODO(tknelms) PANEL_SEQ_LABEL_END("init"); */

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xC3, is_fhd ? 0x0D : 0x0C);
	/* 8/10bit config for QHD/FHD */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xF2);
	GS_DCS_BUF_ADD_CMD(dev, 0xF2, is_fhd ? 0x81 : 0x01);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	if (pmode->gs_mode.is_lp_mode) {
		gs_hk3_set_lp_mode(ctx, pmode);
	} else {
		u32 vrefresh = drm_mode_vrefresh(mode);
		bool is_ns = needs_reset ? false : test_bit(FEAT_OP_NS, spanel->feat);

		gs_hk3_update_panel_feat(ctx, true);
		gs_hk3_write_display_mode(ctx, mode); /* dimming and HBM */
		gs_hk3_change_frequency(ctx, pmode);

		if (needs_reset || (ctx->panel_state == GPANEL_STATE_BLANK)) {
			gs_hk3_wait_for_vsync_done(ctx, needs_reset ? 60 : vrefresh, is_ns);
			gs_panel_send_cmdset(ctx, &hk3_display_on_cmdset);
		}
	}

	spanel->lhbm_ctl.hist_roi_configured = false;

	/*TODO(tknelms) DPU_ATRACE_END(__func__);*/

	return 0;
}

static int gs_hk3_disable(struct drm_panel *panel)
{
	int ret;
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 vrefresh = spanel->hw_vrefresh;

	/* skip disable sequence if going through RRS */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
	    ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		dev_dbg(ctx->dev, "%s: RRS in progress, skip\n", __func__);
		return 0;
	}

	ret = gs_panel_disable(panel);
	if (ret)
		return ret;

	gs_hk3_disable_panel_feat(ctx, 60);
	/*
	 * can't get crtc pointer here, fallback to sleep. hk3_disable_panel_feat() sends freq
	 * update command to trigger early exit if auto mode is enabled before, waiting for one
	 * frame (for either auto or manual mode) should be sufficient to make sure the previous
	 * commands become effective.
	 */
	gs_panel_msleep(GS_VREFRESH_TO_PERIOD_USEC(vrefresh) / 1000 + 1);

	gs_panel_send_cmdset(ctx, &hk3_display_off_cmdset);
	gs_panel_msleep(20);
	if (ctx->panel_state == GPANEL_STATE_OFF)
		GS_DCS_WRITE_DELAY_CMD(dev, 100, MIPI_DCS_ENTER_SLEEP_MODE);

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
 * 120hz auto mode takes at least 2 frames to start lowering refresh rate in
 * addition to time to next vblank. Use just over 2 frames time to consider
 * worst case scenario
 */
#define EARLY_EXIT_THRESHOLD_US 17000

/**
 * gs_hk3_update_idle_state - update panel auto frame insertion state
 * @ctx: panel struct
 *
 * - update timestamp of switching to manual mode in case its been a while since
 * the last frame update and auto mode may have started to lower refresh rate.
 * - trigger early exit by command if it's changeable TE and no switching delay,
 * which could result in fast 120 Hz boost and seeing 120 Hz TE earlier,
 * otherwise disable auto refresh mode to avoid lowering frequency too fast.
 */
static void gs_hk3_update_idle_state(struct gs_panel *ctx)
{
	s64 delta_us;
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	struct gs_panel_timestamps *timestamps = &ctx->timestamps;

	ctx->idle_data.panel_idle_vrefresh = 0;
	if (!test_bit(FEAT_FRAME_AUTO, spanel->feat))
		return;

	delta_us = ktime_us_delta(ktime_get(), timestamps->last_commit_ts);
	if (delta_us < EARLY_EXIT_THRESHOLD_US) {
		dev_dbg(dev, "skip early exit. %lldus since last commit\n", delta_us);
		return;
	}

	/* triggering early exit causes a switch to 120hz */
	timestamps->last_mode_set_ts = ktime_get();

	/*TODO(tknelms)DPU_ATRACE_BEGIN(__func__);*/

	if (!ctx->idle_data.idle_delay_ms && spanel->force_changeable_te) {
		dev_dbg(dev, "sending early exit out cmd\n");
		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
		GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	} else {
		/* turn off auto mode to prevent panel from lowering frequency too fast */
		gs_hk3_update_refresh_mode(ctx, ctx->current_mode, 0);
	}

	/*TODO(tknelms)DPU_ATRACE_END(__func__);*/
}

static void gs_hk3_commit_done(struct gs_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode->gs_mode.is_lp_mode)
		return;

	/* skip idle update if going through RRS */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
	    ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		dev_dbg(ctx->dev, "%s: RRS in progress, skip\n", __func__);
		return;
	}

	gs_hk3_update_idle_state(ctx);

	gs_hk3_update_za(ctx);

	if (spanel->pending_temp_update)
		gs_hk3_update_disp_therm(ctx);
}

static void gs_hk3_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	gs_hk3_change_frequency(ctx, pmode);
}

static bool gs_hk3_is_mode_seamless(const struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const struct drm_display_mode *c = &ctx->current_mode->mode;
	const struct drm_display_mode *n = &pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay) &&
	       (c->flags == n->flags);
}

/* Note the format is 0x<DAh><DBh><DCh> which is reverse of bootloader
 * (0x<DCh><DBh><DAh>) */
static void gs_hk3_get_panel_material(struct gs_panel *ctx, u32 id)
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

	dev_dbg(ctx->dev, "%s: %d\n", __func__, spanel->material);
}

static void gs_hk3_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = ((build_code & 0xE0) >> 3) | ((build_code & 0x0C) >> 2);

	gs_panel_get_panel_rev(ctx, rev);

	gs_hk3_get_panel_material(ctx, id);
}

static void gs_hk3_normal_mode_work(struct gs_panel *ctx)
{
	if (ctx->idle_data.self_refresh_active)
		gs_hk3_update_disp_therm(ctx);
	else {
		struct hk3_panel *spanel = to_spanel(ctx);

		spanel->pending_temp_update = true;
	}
}

static const struct gs_display_underrun_param gs_underrun_param = {
	.te_idle_us = 350,
	.te_var = 1,
};

static const u32 hk3_bl_range[] = { 94, 180, 270, 360, 3307 };

#define HK3_WQHD_DSC                                                                               \
	{                                                                                          \
		.enabled = true, .dsc_count = 2, .cfg = &wqhd_pps_config,                          \
	}
#define HK3_FHD_DSC                                                                                \
	{                                                                                          \
		.enabled = true, .dsc_count = 2, .cfg = &fhd_pps_config,                           \
	}

#define HK3_WIDTH_MM 70
#define HK3_HEIGHT_MM 155

static const struct gs_panel_mode_array gs_hk3_modes = {
#ifdef PANEL_FACTORY_BUILD
	.num_modes = 6,
#else
	.num_modes = 4,
#endif
	.modes = {
#ifdef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1344x2992x1",
			DRM_MODE_TIMING(1, 1344, 80, 24, 52, 2992, 12, 4, 22),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing ={
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_UNSUPPORTED,
	},
	{
		.mode = {
			.name = "1344x2992x5",
			DRM_MODE_TIMING(5, 1344, 80, 24, 52, 2992, 12, 4, 22),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_UNSUPPORTED,
	},
	{
		.mode = {
			.name = "1344x2992x10",
			DRM_MODE_TIMING(10, 1344, 80, 24, 42, 2992, 12, 4, 22),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_UNSUPPORTED,
	},
	{
		.mode = {
			.name = "1344x2992x30",
			DRM_MODE_TIMING(30, 1344, 80, 22, 44, 2992, 12, 4, 22),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_UNSUPPORTED,
	},
#endif
	{
		.mode = {
			.name = "1344x2992x60",
			DRM_MODE_TIMING(60, 1344, 80, 24, 42, 2992, 12, 4, 22),
			.flags = 0,
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_ON_SELF_REFRESH,
	},
	{
		.mode = {
			.name = "1344x2992x120",
			DRM_MODE_TIMING(120, 1344, 80, 24, 42, 2992, 12, 4, 22),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_ON_INACTIVITY,
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1008x2244x60",
			DRM_MODE_TIMING(60, 1008, 80, 24, 38, 2244, 12, 4, 20),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = HK3_FHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_ON_SELF_REFRESH,
	},
	{
		.mode = {
			.name = "1008x2244x120",
			DRM_MODE_TIMING(120, 1008, 80, 24, 38, 2244, 12, 4, 20),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = HK3_FHD_DSC,
			.underrun_param = &gs_underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = GIDLE_MODE_ON_INACTIVITY,
	},
#endif
	},/* modes */
};

static const struct gs_panel_mode_array gs_hk3_lp_modes = {
#ifdef PANEL_FACTORY_BUILD
    .num_modes = 1,
#else
    .num_modes = 2,
#endif
	.modes = {
	{
		.mode = {
			.name = "1344x2992x30",
			DRM_MODE_TIMING(30, 1344, 80, 24, 42, 2992, 12, 4, 22),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_AOD,
			.bpc = 8,
			.dsc = HK3_WQHD_DSC,
			.underrun_param = &gs_underrun_param,
			.is_lp_mode = true,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1008x2244x30",
			DRM_MODE_TIMING(30, 1008, 80, 24, 38, 2244, 12, 4, 20),
			.flags = 0,
			.width_mm = HK3_WIDTH_MM,
			.height_mm = HK3_HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = HK3_TE_USEC_AOD,
			.bpc = 8,
			.dsc = HK3_FHD_DSC,
			.underrun_param = &gs_underrun_param,
			.is_lp_mode = true,
		},
	},
#endif
	},/* modes */
};

static void gs_hk3_calc_lhbm_od_brightness(u8 n_fine, u8 n_coarse, u8 *o_fine, u8 *o_coarse,
					   u8 fine_offset_0, u8 fine_offset_1, u8 coarse_offset_0,
					   u8 coarse_offset_1)
{
	if (((int)n_fine + (int)fine_offset_0) <= 0xFF) {
		*o_coarse = n_coarse + coarse_offset_0;
		*o_fine = n_fine + fine_offset_0;
	} else {
		*o_coarse = n_coarse + coarse_offset_1;
		*o_fine = n_fine - fine_offset_1;
	}
}

/**
 * mark_unused_functions() - do-nothing function to clear compiler errors
 *
 * There's a fair bit of code that we want to keep around but isn't currently
 * hooked in to the panel framework.
 *
 * As functionality is filled back in, these should be removed.
 */
static void mark_unused_functions(void)
{
	(void)gs_hk3_set_self_refresh;
	(void)gs_hk3_atomic_check;
	(void)gs_hk3_commit_done;
	(void)gs_hk3_is_mode_seamless;
	(void)gs_hk3_get_te_usec;
	(void)gs_hk3_calc_lhbm_od_brightness;
	(void)lhbm_brightness_index;
	(void)lhbm_brightness_reg;
}

static void gs_hk3_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;
	struct hk3_panel *spanel;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot) {
		goto panel_out;
	}

	spanel = to_spanel(ctx);

	gs_panel_debugfs_create_cmdset(csroot, &hk3_init_cmdset, "init");
	debugfs_create_bool("force_changeable_te", 0644, panel_root, &spanel->force_changeable_te);
	debugfs_create_bool("force_changeable_te2", 0644, panel_root,
			    &spanel->force_changeable_te2);
	debugfs_create_bool("force_za_off", 0644, panel_root, &spanel->force_za_off);
	debugfs_create_u8("hw_acl_setting", 0644, panel_root, &spanel->hw_acl_setting);
	dput(csroot);
panel_out:
	dput(panel_root);
#endif
}

static void gs_hk3_panel_init(struct gs_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;

	mark_unused_functions();
#ifdef PANEL_FACTORY_BUILD
	ctx->panel_idle_enabled = false;
#endif
	/*TODO(tknelms)gs_hk3_lhbm_brightness_init(ctx);*/

	if (ctx->panel_rev < PANEL_REV_DVT1) {
		/* AOD Transition Set */
		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x03, 0xBB);
		GS_DCS_BUF_ADD_CMD(dev, 0xBB, 0x41);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	}

	if (ctx->panel_rev >= PANEL_REV_DVT1)
		gs_hk3_negative_field_setting(ctx);

	spanel->tz = thermal_zone_get_zone_by_name("disp_therm");
	if (IS_ERR_OR_NULL(spanel->tz))
		dev_err(dev, "%s: failed to get thermal zone disp_therm\n", __func__);
}

static int gs_hk3_panel_probe(struct mipi_dsi_device *dsi)
{
	struct hk3_panel *spanel;
	struct gs_panel *ctx;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	ctx = &spanel->base;
	ctx->op_hz = 120;
	spanel->hw_vrefresh = 60;
	/* ddic default temp */
	spanel->hw_temp = 25;

	return gs_dsi_panel_common_init(dsi, ctx);
}

static const struct drm_panel_funcs hk3_drm_funcs = {
	.enable = gs_hk3_enable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.get_modes = gs_panel_get_modes,
	.disable = gs_hk3_disable,
	.debugfs_init = gs_hk3_debugfs_init,
};

static const struct gs_panel_funcs hk3_gs_funcs = {
	.set_brightness = gs_hk3_set_brightness,
	.panel_init = gs_hk3_panel_init,
	.set_nolp_mode = gs_hk3_set_nolp_mode,
	.mode_set = gs_hk3_mode_set,
	.get_panel_rev = gs_hk3_get_panel_rev,
	.set_acl_mode = gs_hk3_set_acl_mode,
	.run_normal_mode_work = gs_hk3_normal_mode_work,
};

static const struct brightness_capability hk3_brightness_capability = {
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

/*TODO(tknelms): verify if this works? */
static const struct gs_dsi_cmd hk3_off_cmds[] = {
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_OFF),
};
static DEFINE_GS_CMDSET(hk3_off);

static const struct gs_panel_brightness_desc gs_hk3_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.max_brightness = 4095,
	.default_brightness = 1353, /* 140 nits */
	.brt_capability = &hk3_brightness_capability,
};

static const struct gs_panel_lhbm_desc gs_hk3_lhbm_desc = {
	.no_lhbm_rr_constraints = true,
	.post_cmd_delay_frames = 1,
	.effective_delay_frames = 1,
};

static const struct gs_panel_reg_ctrl_desc gs_hk3_regctrl_desc = {
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

static const struct gs_panel_desc gs_hk3_desc = {
	.data_lane_cnt = 4,
	.brightness_desc = &gs_hk3_brightness_desc,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.lhbm_desc = &gs_hk3_lhbm_desc,
	.dbv_extra_frame = true,

	.bl_range = hk3_bl_range,
	.bl_num_ranges = ARRAY_SIZE(hk3_bl_range),
	.off_cmdset = &hk3_off_cmdset,

	.modes = &gs_hk3_modes,
	.lp_modes = &gs_hk3_lp_modes,
	.binned_lp = hk3_binned_lp,
	.num_binned_lp = ARRAY_SIZE(hk3_binned_lp),

	.panel_func = &hk3_drm_funcs,
	.gs_panel_func = &hk3_gs_funcs,
	.reset_timing_ms = { 1, 1, 5 },
	.reg_ctrl_desc = &gs_hk3_regctrl_desc,
};

static const struct of_device_id gs_panel_of_match[] = {
	{
		.compatible = "google,gs-hk3",
		.data = &gs_hk3_desc
	},
	{}
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = gs_hk3_panel_probe,
	/*TODO(tknelms): create this.remove = gs_panel_remove,*/
	.driver = {
		.name = "panel-gs-hk3",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Taylor Nelms <tknelms@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google HK3 panel driver");
MODULE_LICENSE("Dual MIT/GPL");
