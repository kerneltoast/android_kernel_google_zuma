/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <max77779.h>
#include <max777x9_bcl.h>
#include "bcl.h"

int max77779_adjust_batoilo_lvl(struct bcl_device *bcl_dev, u8 lower_enable, u8 set_batoilo1_lvl,
                                u8 set_batoilo2_lvl)
{
	int ret;
	u8 val, batoilo1_lvl, batoilo2_lvl;

	if (lower_enable) {
		batoilo1_lvl = set_batoilo1_lvl;
		batoilo2_lvl = set_batoilo2_lvl;
	} else {
		batoilo1_lvl = bcl_dev->batt_irq_conf1.batoilo_trig_lvl;
		batoilo2_lvl = bcl_dev->batt_irq_conf2.batoilo_trig_lvl;
	}
	ret = max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev, MAX77779_BAT_OILO1_CNFG_0, &val);
	if (ret < 0)
		return ret;
	val = _max77779_bat_oilo1_cnfg_0_bat_oilo1_set(val, batoilo1_lvl);
	ret = max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev, MAX77779_BAT_OILO1_CNFG_0, val);
	if (ret < 0)
		return ret;
	ret = max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev, MAX77779_BAT_OILO2_CNFG_0, &val);
	if (ret < 0)
		return ret;
	val = _max77779_bat_oilo2_cnfg_0_bat_oilo2_set(val, batoilo2_lvl);
	ret = max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev, MAX77779_BAT_OILO2_CNFG_0, val);

	return ret;
}

int max77779_get_irq(struct bcl_device *bcl_dev, u8 *irq_val)
{
	u8 vdroop_int;
	u8 ret;
	u8 clr_bcl_irq_mask;

	clr_bcl_irq_mask = (MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK |
			    MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK |
			    MAX77779_PMIC_VDROOP_INT_SYS_UVLO1_INT_MASK |
			    MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK);
	ret = max77779_external_pmic_reg_read(bcl_dev->irq_pmic_dev,
		                              MAX77779_PMIC_VDROOP_INT,
					      &vdroop_int);
	if (ret < 0)
		return IRQ_NONE;
	if ((vdroop_int & clr_bcl_irq_mask) == 0)
		return IRQ_NONE;

	/* UVLO2 has the highest priority and then BATOILO, then UVLO1 */
	if (vdroop_int & MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK)
		*irq_val = UVLO2;
	else if (vdroop_int & MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK)
		*irq_val = BATOILO2;
	else if (vdroop_int & MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK)
		*irq_val = BATOILO1;
	else if (vdroop_int & MAX77779_PMIC_VDROOP_INT_SYS_UVLO1_INT_MASK)
		*irq_val = UVLO1;

	return ret;
}

int max77779_clr_irq(struct bcl_device *bcl_dev, int idx)
{
	u8 irq_val = 0;
	u8 chg_int = 0;
	int ret;

	if (idx != NOT_USED)
		irq_val = idx;
	else {
		if (max77779_get_irq(bcl_dev, &irq_val) != 0)
			return IRQ_NONE;
	}
	if (irq_val == UVLO2)
		chg_int = MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK |
				MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK |
				MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK;
	else if (irq_val == UVLO1)
		chg_int = MAX77779_PMIC_VDROOP_INT_SYS_UVLO1_INT_MASK;
	else if (irq_val == BATOILO1)
		chg_int = MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK |
				MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK |
				MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK;
	else if (irq_val == BATOILO2)
		chg_int = MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK |
				MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK |
				MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK;

	ret = max77779_external_pmic_reg_write(bcl_dev->irq_pmic_dev,
		                               MAX77779_PMIC_VDROOP_INT, chg_int);
	if (ret < 0)
		return IRQ_NONE;
	return ret;
}

int max77779_vimon_read(struct bcl_device *bcl_dev)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_SOC_ZUMAPRO)
	ret = max77779_external_vimon_read_buffer(bcl_dev->vimon_dev, bcl_dev->vimon_intf.data,
						  &bcl_dev->vimon_intf.count, VIMON_BUF_SIZE);
	if (ret == 0)
		return bcl_dev->vimon_intf.count;
#endif
	return ret;
}
