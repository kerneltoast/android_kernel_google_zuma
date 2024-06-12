/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google, LLC
 *
 */

#ifndef MAX77779_USECASE_H_
#define MAX77779_USECASE_H_

struct max77779_usecase_data {
	int otg_enable;		/* enter/exit from OTG cases */
	bool rx_otg_en;		/* enable WLC_RX -> WLC_RX + OTG case */
	int dc_sw_gpio;		/* WLC-DC switch enable */

	int vin_is_valid;	/* MAX20339 STATUS1.vinvalid */

	int wlc_en;		/* wlcrx/chgin coex */
	int wlc_vbus_en;	/* b/202526678 */

	u8 otg_ilim;		/* TODO: TCPM to control this? */
	u8 otg_vbyp;		/* TODO: TCPM to control this? */
	u8 otg_orig;		/* restore value */
	u8 otg_value;		/* CHG_CNFG_11:VBYPSET for USB OTG Voltage */

	struct i2c_client *client;
	bool init_done;
	int use_case;
};

enum gsu_usecases {
	GSU_RAW_MODE 		= -1,	/* raw mode, default, */

	GSU_MODE_STANDBY	= 0,	/* 8, PMIC mode 0 */
	GSU_MODE_USB_CHG	= 1,	/* 1-1 wired mode 0x4, mode 0x5 */
	GSU_MODE_USB_DC 	= 2,	/* 1-2 wired mode 0x0 */
	GSU_MODE_USB_CHG_WLC_TX = 3,	/* 2-1, 1041, */

	GSU_MODE_WLC_RX		= 5,	/* 3-1, mode 0x4, mode 0x5 */
	GSU_MODE_WLC_DC		= 6,	/* 3-2, mode 0x0 */

	GSU_MODE_USB_OTG_WLC_RX = 7,	/* 7, 524, */
	GSU_MODE_USB_OTG 	= 9,	/* 5-1, 516,*/
	GSU_MODE_USB_OTG_FRS	= GSU_MODE_USB_OTG,

	GSU_MODE_WLC_TX 	= 11,	/* 6-2, 1056, */
	GSU_MODE_USB_OTG_WLC_TX = 12,
	GSU_MODE_USB_WLC_RX	= 13,
};

extern int gs201_wlc_en(struct max77779_usecase_data *uc_data, bool wlc_on);
extern int gs201_to_standby(struct max77779_usecase_data *uc_data, int use_case);
extern int gs201_to_usecase(struct max77779_usecase_data *uc_data, int use_case);
extern int gs201_force_standby(struct max77779_usecase_data *uc_data);
extern bool gs201_setup_usecases(struct max77779_usecase_data *uc_data,
				 struct device_node *node);
extern void gs201_dump_usecasase_config(struct max77779_usecase_data *uc_data);
extern int max77779_otg_vbyp_mv_to_code(u8 *code, int vbyp);

#endif
