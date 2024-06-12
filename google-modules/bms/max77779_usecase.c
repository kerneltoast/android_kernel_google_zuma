/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2023 Google, LLC
 *
 */


#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include "max77779.h"
#include "max77779_charger.h"

/* ----------------------------------------------------------------------- */
static int max77779_chgr_reg_read(struct i2c_client *client, u8 reg, u8 *value)
{
	struct max77779_chgr_data *data;
	int ret, ival;

	if (!client)
		return -ENODEV;

	data = i2c_get_clientdata(client);
	if (!data || !data->regmap)
		return -ENODEV;

	ret = regmap_read(data->regmap, reg, &ival);
	if (ret == 0)
		*value = 0xFF & ival;

	return ret;
}

static int max77779_chgr_reg_update(struct i2c_client *client,
			    u8 reg, u8 mask, u8 value)
{
	struct max77779_chgr_data *data;

	if (!client)
		return -ENODEV;

	data = i2c_get_clientdata(client);
	if (!data || !data->regmap)
		return -ENODEV;

	return regmap_write_bits(data->regmap, reg, mask, value);
}

static int max77779_chgr_mode_write(struct i2c_client *client,
			    enum max77779_charger_modes mode)
{
	struct max77779_chgr_data *data;

	if (!client)
		return -ENODEV;

	data = i2c_get_clientdata(client);
	if (!data || !data->regmap)
		return -ENODEV;

	return regmap_write_bits(data->regmap, MAX77779_CHG_CNFG_00,
				 MAX77779_CHG_CNFG_00_MODE_MASK,
				 mode);
}

static int max77779_chgr_insel_write(struct i2c_client *client, u8 mask, u8 value)
{
	struct max77779_chgr_data *data;
	int ret;

	if (!client)
		return -ENODEV;

	data = i2c_get_clientdata(client);
	if (!data || !data->regmap)
		return -ENODEV;

	ret = regmap_write_bits(data->regmap, MAX77779_CHG_CNFG_12, mask, value);
	if (ret < 0)
		return ret;

	return ret;
}

/* ----------------------------------------------------------------------- */

int gs201_wlc_en(struct max77779_usecase_data *uc_data, bool wlc_on)
{
	int ret = 0;

	pr_debug("%s: wlc_en=%d wlc_on=%d\n", __func__,
		 uc_data->wlc_en, wlc_on);

	if (uc_data->wlc_en >= 0)
		gpio_set_value_cansleep(uc_data->wlc_en, wlc_on);

	return ret;
}

/* RTX reverse wireless charging */
/* pvp TODO check this. Need to interact with CP and WLC */
static int gs201_wlc_tx_enable(struct max77779_usecase_data *uc_data,
			       bool enable)
{
	int ret = 0;

	if (enable) {
		/* p9412 will not be in RX when powered from EXT */
		ret = gs201_wlc_en(uc_data, true);
		if (ret < 0)
			return ret;
	} else {
		/* p9412 is already off from insel */
		ret = gs201_wlc_en(uc_data, false);
		if (ret < 0)
			return ret;

		/* STBY will re-enable WLC */
	}

	return ret;
}

static int gs201_otg_update_ilim(struct max77779_usecase_data *uc_data, int enable)
{
	u8 ilim;

	if (uc_data->otg_orig == uc_data->otg_ilim)
		return 0;

	if (enable) {
		int rc;

		rc = max77779_chgr_reg_read(uc_data->client, MAX77779_CHG_CNFG_05,
					   &uc_data->otg_orig);
		if (rc < 0) {
			pr_err("%s: cannot read otg_ilim (%d), use default\n",
			       __func__, rc);
			uc_data->otg_orig = MAX77779_CHG_CNFG_05_OTG_ILIM_1500MA;
		} else {
			uc_data->otg_orig &= MAX77779_CHG_CNFG_05_OTG_ILIM_MASK;
		}

		ilim = uc_data->otg_ilim;
	} else {
		ilim = uc_data->otg_orig;
	}

	return max77779_chgr_reg_update(uc_data->client, MAX77779_CHG_CNFG_05,
				      MAX77779_CHG_CNFG_05_OTG_ILIM_MASK,
				      ilim);
}

/*
 * Transition to standby (if needed) at the beginning of the sequences
 * @return <0 on error, 0 on success. ->use_case becomes GSU_MODE_STANDBY
 * if the transition is necessary (and successful).
 */
int gs201_to_standby(struct max77779_usecase_data *uc_data, int use_case)
{
	const int from_uc = uc_data->use_case;
	bool need_stby = false;
	bool from_otg = false;
	int ret;

	switch (from_uc) {
	case GSU_MODE_USB_CHG:
		need_stby = use_case != GSU_MODE_USB_CHG_WLC_TX &&
			    use_case != GSU_MODE_WLC_RX &&
			    use_case != GSU_MODE_USB_DC;
		break;
	case GSU_MODE_WLC_RX:
		need_stby = use_case != GSU_MODE_USB_OTG_WLC_RX &&
			    use_case != GSU_MODE_WLC_DC;
		break;
	case GSU_MODE_WLC_TX:
		need_stby = use_case != GSU_MODE_USB_OTG_WLC_TX &&
			    use_case != GSU_MODE_USB_CHG_WLC_TX;
		break;
	case GSU_MODE_USB_CHG_WLC_TX:
		need_stby = use_case != GSU_MODE_USB_CHG &&
			    use_case != GSU_MODE_USB_OTG_WLC_TX &&
			    use_case != GSU_MODE_USB_DC;
		break;

	case GSU_MODE_USB_OTG:
		from_otg = true;
		if (use_case == GSU_MODE_USB_OTG_WLC_TX)
			break;
		if (use_case == GSU_MODE_USB_OTG_WLC_RX)
			break;

		need_stby = true;
		break;

	case GSU_MODE_USB_OTG_WLC_RX:
		from_otg = true;
		need_stby = use_case != GSU_MODE_WLC_RX &&
			    use_case != GSU_MODE_USB_OTG;
		break;
	case GSU_MODE_USB_DC:
		need_stby = use_case != GSU_MODE_USB_DC;
		break;
	case GSU_MODE_WLC_DC:
		need_stby = use_case != GSU_MODE_WLC_DC;
		break;
	case GSU_RAW_MODE:
		need_stby = true;
		break;
	case GSU_MODE_USB_OTG_WLC_TX:
		from_otg = true;
		need_stby = false;
		break;
	case GSU_MODE_STANDBY:
	default:
		need_stby = false;
		break;
	}

	if (use_case == GSU_MODE_USB_WLC_RX || use_case == GSU_RAW_MODE)
		need_stby = true;

	pr_info("%s: use_case=%d->%d from_otg=%d need_stby=%d\n", __func__,
		 from_uc, use_case, from_otg, need_stby);

	if (!need_stby)
		return 0;

	/* from WLC_TX to STBY */
	if (from_uc == GSU_MODE_WLC_TX) {
		// pvp TODO: Check how to enable wlc tx
		ret = gs201_wlc_tx_enable(uc_data, false);
		if (ret < 0) {
			pr_err("%s: cannot tun off wlc_tx (%d)\n", __func__, ret);
			return ret;
		}

		/* re-enable wlc IC if disabled */
		ret = gs201_wlc_en(uc_data, true);
		if (ret < 0)
			pr_err("%s: cannot enable WLC (%d)\n", __func__, ret);
	}

	/* transition to STBY (might need to be up) */
	ret = max77779_chgr_mode_write(uc_data->client, MAX77779_CHGR_MODE_ALL_OFF);
	if (ret < 0)
		return -EIO;

	uc_data->use_case = GSU_MODE_STANDBY;
	return ret;
}

/* enable/disable soft-start */
static int gs201_ramp_bypass(struct max77779_usecase_data *uc_data, bool enable)
{
	const u8 value = enable ? MAX77779_CHG_CNFG_00_BYPV_RAMP_BYPASS_MASK : 0;

	return max77779_chgr_reg_update(uc_data->client, MAX77779_CHG_CNFG_00,
				       MAX77779_CHG_CNFG_00_BYPV_RAMP_BYPASS_MASK,
				       value);
}

/* cleanup from every usecase */
int gs201_force_standby(struct max77779_usecase_data *uc_data)
{
	const u8 insel_mask = MAX77779_CHG_CNFG_12_CHGINSEL_MASK |
			      MAX77779_CHG_CNFG_12_WCINSEL_MASK;
	const u8 insel_value = MAX77779_CHG_CNFG_12_CHGINSEL |
			       MAX77779_CHG_CNFG_12_WCINSEL;
	int ret;

	pr_debug("%s: recovery\n", __func__);

	ret = gs201_ramp_bypass(uc_data, false);
	if (ret < 0)
		pr_err("%s: cannot reset ramp_bypass (%d)\n",
			__func__, ret);

	ret = max77779_chgr_mode_write(uc_data->client, MAX77779_CHGR_MODE_ALL_OFF);
	if (ret < 0)
		pr_err("%s: cannot reset mode register (%d)\n",
			__func__, ret);

	ret = max77779_chgr_insel_write(uc_data->client, insel_mask, insel_value);
	if (ret < 0)
		pr_err("%s: cannot reset insel (%d)\n",
			__func__, ret);

	return 0;
}

static int gs201_otg_mode(struct max77779_usecase_data *uc_data, int to)
{
	int ret = -EINVAL;

	pr_debug("%s: to=%d\n", __func__, to);

	if (to == GSU_MODE_USB_OTG) {
		ret = max77779_chgr_mode_write(uc_data->client,
					      MAX77779_CHGR_MODE_ALL_OFF);
	}

	return ret;
}

/*
 * This must follow different paths depending on the platforms.
 *
 * NOTE: the USB stack expects VBUS to be on after voting for the usecase.
 */
static int gs201_otg_enable(struct max77779_usecase_data *uc_data)
{
	int ret;

	ret = gs201_otg_update_ilim(uc_data, true);
	if (ret < 0) {
		pr_debug("%s: cannot update otg ilim ret:%d\n", __func__, ret);
		return ret;
	}

	/* the code default to write to the MODE register */

	ret = max77779_chgr_mode_write(uc_data->client,
					MAX77779_CHGR_MODE_OTG_BOOST_ON);
	if (ret < 0) {
		pr_debug("%s: cannot set CNFG_00 to 0xa ret:%d\n", __func__, ret);
		return ret;
	}

	return ret;
}

/* configure ilim wlctx */
static int gs201_wlctx_otg_en(struct max77779_usecase_data *uc_data, bool enable)
{
	int ret;

	if (enable) {
		/* this should be already set */
		ret = gs201_otg_update_ilim(uc_data, true);
		if (ret < 0)
			pr_err("%s: cannot update otg_ilim (%d)\n", __func__, ret);
	} else {
		/* TODO: Discharge IN/OUT nodes with AO37 should be done in TCPM */

		msleep(100);

		ret = gs201_otg_update_ilim(uc_data, false);
		if (ret < 0)
			pr_err("%s: cannot restore otg_ilim (%d)\n", __func__, ret);

		ret = 0;
        }

	return ret;
}

/*
 * Case	USB_chg USB_otg	WLC_chg	WLC_TX	PMIC_Charger	Name
 * -------------------------------------------------------------------------------------
 * 7	0	1	1	0	IF-PMIC-WCIN	USB_OTG_WLC_RX
 * 9	0	1	0	0	0		USB_OTG / USB_OTG_FRS
 * -------------------------------------------------------------------------------------
 * WLC_chg = 0 off, 1 = on, 2 = PPS
 *
 * NOTE: do not call with (cb_data->wlc_rx && cb_data->wlc_tx)
 */

static int gs201_standby_to_otg(struct max77779_usecase_data *uc_data, int use_case)
{
	int ret;

	ret = gs201_otg_enable(uc_data);

	if (ret == 0)
		usleep_range(5 * USEC_PER_MSEC, 5 * USEC_PER_MSEC + 100);
	/*
	 * Assumption: gs201_to_usecase() will write back cached values to
	 * CHG_CNFG_00.Mode. At the moment, the cached value at
	 * max77779_mode_callback is 0. If the cached value changes to something
	 * other than 0, then, the code has to be revisited.
	 */

	return ret;
}

/* was b/179816224 WLC_RX -> WLC_RX + OTG (Transition #10) */
static int gs201_wlcrx_to_wlcrx_otg(struct max77779_usecase_data *uc_data)
{
	pr_warn("%s: disabled\n", __func__);
	return 0;
}

static int gs201_to_otg_usecase(struct max77779_usecase_data *uc_data, int use_case)
{
	const int from_uc = uc_data->use_case;
	int ret = 0;

	switch (from_uc) {
	/* 9: stby to USB OTG, mode = 1 */
	case GSU_MODE_STANDBY:
		ret = gs201_standby_to_otg(uc_data, use_case);
		if (ret < 0) {
			pr_err("%s: cannot enable OTG ret:%d\n",  __func__, ret);
			return ret;
		}
	break;

	case GSU_MODE_USB_CHG:
	case GSU_MODE_USB_CHG_WLC_TX:
		/* need to go through stby out of this */
		if (use_case != GSU_MODE_USB_OTG && use_case != GSU_MODE_USB_OTG_WLC_TX)
			return -EINVAL;

		ret = gs201_otg_enable(uc_data);
	break;


	case GSU_MODE_WLC_TX:
		/* b/179820595: WLC_TX -> WLC_TX + OTG */
		if (use_case == GSU_MODE_USB_OTG_WLC_TX) {
			ret = max77779_chgr_mode_write(uc_data->client, MAX77779_CHGR_MODE_OTG_BOOST_ON);
			if (ret < 0) {
				pr_err("%s: cannot set CNFG_00 to 0xa ret:%d\n",  __func__, ret);
				return ret;
			}
		}
	break;

	case GSU_MODE_WLC_RX:
		if (use_case == GSU_MODE_USB_OTG_WLC_RX) {
			if (uc_data->rx_otg_en) {
				ret = gs201_standby_to_otg(uc_data, use_case);
			} else {
				ret = gs201_wlcrx_to_wlcrx_otg(uc_data);
			}
		}
	break;

	case GSU_MODE_USB_OTG:
		/* b/179820595: OTG -> WLC_TX + OTG (see b/181371696) */
		if (use_case == GSU_MODE_USB_OTG_WLC_TX) {
			ret = gs201_otg_mode(uc_data, GSU_MODE_USB_OTG);
			if (ret == 0)
				ret = gs201_wlc_tx_enable(uc_data, true);
		}
		/* b/179816224: OTG -> WLC_RX + OTG */
		if (use_case == GSU_MODE_USB_OTG_WLC_RX) {
			/* pvp TODO: Is it just WLC Rx enable? */
		}
	break;
	case GSU_MODE_USB_OTG_WLC_TX:
		/*  b/179820595: WLC_TX + OTG -> OTG */
		if (use_case == GSU_MODE_USB_OTG) {
			ret = gs201_wlc_tx_enable(uc_data, false);
		}
	break;
	case GSU_MODE_USB_OTG_WLC_RX:
		/* b/179816224: WLC_RX + OTG -> OTG */
		if (use_case == GSU_MODE_USB_OTG) {
			/* pvp TODO: is it just WLC Rx disable? */
		}
	break;

	default:
		return -ENOTSUPP;
	}

	return ret;
}

/* handles the transition data->use_case ==> use_case */
int gs201_to_usecase(struct max77779_usecase_data *uc_data, int use_case)
{
	const int from_uc = uc_data->use_case;
	int ret = 0;

	switch (use_case) {
	case GSU_MODE_USB_OTG:
	case GSU_MODE_USB_OTG_WLC_RX:
		ret = gs201_to_otg_usecase(uc_data, use_case);
		break;
	case GSU_MODE_WLC_TX:
	case GSU_MODE_USB_CHG_WLC_TX:
		/* Coex Case #4, WLC_TX + OTG -> WLC_TX */
		if (from_uc == GSU_MODE_USB_OTG_WLC_TX) {
			ret = max77779_chgr_mode_write(uc_data->client,
						      MAX77779_CHGR_MODE_ALL_OFF);
			if (ret == 0)
				ret = gs201_wlctx_otg_en(uc_data, false);
		} else {
			ret = gs201_wlc_tx_enable(uc_data, true);
		}

		break;
	case GSU_MODE_WLC_RX:
		if (from_uc == GSU_MODE_USB_OTG_WLC_RX) {
			ret = gs201_otg_mode(uc_data, GSU_MODE_USB_OTG);
		}
		break;
	case GSU_MODE_USB_CHG:
	case GSU_MODE_USB_DC:
		if (from_uc == GSU_MODE_WLC_TX || from_uc == GSU_MODE_USB_CHG_WLC_TX)
			ret = gs201_wlc_tx_enable(uc_data, false);
		break;
	case GSU_MODE_USB_WLC_RX:
	case GSU_RAW_MODE:
		/* just write the value to the register (it's in stby) */
		break;
	case GSU_MODE_WLC_DC:
		break;
	default:
		break;
	}

	return ret;
}

static int max77779_otg_ilim_ma_to_code(u8 *code, int otg_ilim)
{
	if (otg_ilim == 0)
		*code = 0;
	else if (otg_ilim >= 500 && otg_ilim <= 1500)
		*code = 1 + (otg_ilim - 500) / 100;
	else
		return -EINVAL;

	return 0;
}

int max77779_otg_vbyp_mv_to_code(u8 *code, int vbyp)
{
	if (vbyp >= 12000)
		*code = 0x8c;
	else if (vbyp >= 5000)
		*code = (vbyp - 5000) / 50;
	else
		return -EINVAL;

	return 0;
}

#define GS201_OTG_ILIM_DEFAULT_MA	1500
#define GS201_OTG_VBYPASS_DEFAULT_MV	5100

/* lazy init on the switches */


static bool gs201_setup_usecases_done(struct max77779_usecase_data *uc_data)
{
	return (uc_data->wlc_en != -EPROBE_DEFER);

	/* TODO: handle platform specific differences..	*/
}

static void gs201_setup_default_usecase(struct max77779_usecase_data *uc_data)
{
	int ret;

	uc_data->otg_enable = -EPROBE_DEFER;

	uc_data->wlc_en = -EPROBE_DEFER;

	uc_data->init_done = false;

	/* TODO: override in bootloader and remove */
	ret = max77779_otg_ilim_ma_to_code(&uc_data->otg_ilim,
					   GS201_OTG_ILIM_DEFAULT_MA);
	if (ret < 0)
		uc_data->otg_ilim = MAX77779_CHG_CNFG_05_OTG_ILIM_1500MA;
	ret = max77779_chgr_reg_read(uc_data->client, MAX77779_CHG_CNFG_05,
				    &uc_data->otg_orig);
	if (ret == 0) {
		uc_data->otg_orig &= MAX77779_CHG_CNFG_05_OTG_ILIM_MASK;
	} else {
		uc_data->otg_orig = uc_data->otg_ilim;
	}

	ret = max77779_otg_vbyp_mv_to_code(&uc_data->otg_vbyp,
					   GS201_OTG_VBYPASS_DEFAULT_MV);
	if (ret < 0)
		uc_data->otg_vbyp = MAX77779_CHG_CNFG_11_OTG_VBYP_5100MV;
}

bool gs201_setup_usecases(struct max77779_usecase_data *uc_data,
			  struct device_node *node)
{
	if (!node) {
		gs201_setup_default_usecase(uc_data);
		return false;
	}

	/*  wlc_rx: disable when chgin, CPOUT is safe */
	if (uc_data->wlc_en == -EPROBE_DEFER)
		uc_data->wlc_en = of_get_named_gpio(node, "max77779,wlc-en", 0);

	/* OPTIONAL: support wlc_rx -> wlc_rx+otg */
	uc_data->rx_otg_en = of_property_read_bool(node, "max77779,rx-to-rx-otg-en");

	return gs201_setup_usecases_done(uc_data);
}

void gs201_dump_usecasase_config(struct max77779_usecase_data *uc_data)
{
	pr_info("wlc_en:%d, rx_to_rx_otg:%d\n",
		uc_data->wlc_en, uc_data->rx_otg_en);
}

