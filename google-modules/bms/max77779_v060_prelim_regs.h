/* SPDX-License-Identifier: GPL-2.0 */
/*
 * machine generated DO NOT MODIFY
 * source Sequoia_RegMap_customer_060.xml
 * 2023-02-14
 */

#ifndef SEQUOIA_REGMAP_CUSTOMER_060_REG_H_
#define SEQUOIA_REGMAP_CUSTOMER_060_REG_H_

/* needs linux/bits.h */

#define MAX77779_BFF(name, h, l) \
static inline uint8_t _ ## name ## _set(uint8_t r, uint8_t v) \
{ \
	return ((r & ~GENMASK(h, l)) | v << l); \
} \
\
static inline uint8_t _ ## name ## _get(uint8_t r) \
{ \
	return ((r & GENMASK(h, l)) >> l); \
}


#define FIELD2VALUE(field,value) \
	(((value) & field##_MASK) >> field##_SHIFT)
#define VALUE2FIELD(field,       value) \
	(((value) << field##_SHIFT) & field##_MASK)


/*
 * Section: PMIC_FUNC 0x00 8
 */


/*
 * PMIC_ID,0x00,0b01111001,0x79,Reset_Type:O
 */
#define MAX77779_PMIC_ID	0x00

/*
 * PMIC_REVISION,0x01,0b00000001,0x1,Reset_Type:O
 * REV[0:3],VER[3:5]
 */
#define MAX77779_PMIC_REVISION	0x01
#define MAX77779_PMIC_REVISION_REV_SHIFT	0
#define MAX77779_PMIC_REVISION_REV_MASK	(0x7 << 0)
#define MAX77779_PMIC_REVISION_REV_CLEAR	(~(0x7 << 0))
#define MAX77779_PMIC_REVISION_VER_SHIFT	3
#define MAX77779_PMIC_REVISION_VER_MASK	(0x1f << 3)
#define MAX77779_PMIC_REVISION_VER_CLEAR	(~(0x1f << 3))
static inline const char *
max77779_pmic_revision_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " REV=%x",
		FIELD2VALUE(MAX77779_REV, val));
	i += scnprintf(&buff[i], len - i, " VER=%x",
		FIELD2VALUE(MAX77779_VER, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmic_revision_rev,2,0)
MAX77779_BFF(max77779_pmic_revision_ver,7,3)

/*
 * OTP_REVISION,0x02,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 */
#define MAX77779_OTP_REVISION	0x02

/*
 * INTSRC_STS,0x22,0b00000000,0x0,Reset_Type:S
 * TCPC_INT,FG_INT,CHGR_INT,I2CM_INT,BATTVIMON_INT,GPIO_INT,VDROOP_INT,PMICTOP_INT
 */
#define MAX77779_INTSRC_STS	0x22
#define MAX77779_INTSRC_STS_TCPC_INT_SHIFT	0
#define MAX77779_INTSRC_STS_TCPC_INT_MASK	(0x1 << 0)
#define MAX77779_INTSRC_STS_TCPC_INT_CLEAR	(~(0x1 << 0))
#define MAX77779_INTSRC_STS_FG_INT_SHIFT	1
#define MAX77779_INTSRC_STS_FG_INT_MASK	(0x1 << 1)
#define MAX77779_INTSRC_STS_FG_INT_CLEAR	(~(0x1 << 1))
#define MAX77779_INTSRC_STS_CHGR_INT_SHIFT	2
#define MAX77779_INTSRC_STS_CHGR_INT_MASK	(0x1 << 2)
#define MAX77779_INTSRC_STS_CHGR_INT_CLEAR	(~(0x1 << 2))
#define MAX77779_INTSRC_STS_I2CM_INT_SHIFT	3
#define MAX77779_INTSRC_STS_I2CM_INT_MASK	(0x1 << 3)
#define MAX77779_INTSRC_STS_I2CM_INT_CLEAR	(~(0x1 << 3))
#define MAX77779_INTSRC_STS_BATTVIMON_INT_SHIFT	4
#define MAX77779_INTSRC_STS_BATTVIMON_INT_MASK	(0x1 << 4)
#define MAX77779_INTSRC_STS_BATTVIMON_INT_CLEAR	(~(0x1 << 4))
#define MAX77779_INTSRC_STS_GPIO_INT_SHIFT	5
#define MAX77779_INTSRC_STS_GPIO_INT_MASK	(0x1 << 5)
#define MAX77779_INTSRC_STS_GPIO_INT_CLEAR	(~(0x1 << 5))
#define MAX77779_INTSRC_STS_VDROOP_INT_SHIFT	6
#define MAX77779_INTSRC_STS_VDROOP_INT_MASK	(0x1 << 6)
#define MAX77779_INTSRC_STS_VDROOP_INT_CLEAR	(~(0x1 << 6))
#define MAX77779_INTSRC_STS_PMICTOP_INT_SHIFT	7
#define MAX77779_INTSRC_STS_PMICTOP_INT_MASK	(0x1 << 7)
#define MAX77779_INTSRC_STS_PMICTOP_INT_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_intsrc_sts_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " TCPC_INT=%x",
		FIELD2VALUE(MAX77779_TCPC_INT, val));
	i += scnprintf(&buff[i], len - i, " FG_INT=%x",
		FIELD2VALUE(MAX77779_FG_INT, val));
	i += scnprintf(&buff[i], len - i, " CHGR_INT=%x",
		FIELD2VALUE(MAX77779_CHGR_INT, val));
	i += scnprintf(&buff[i], len - i, " I2CM_INT=%x",
		FIELD2VALUE(MAX77779_I2CM_INT, val));
	i += scnprintf(&buff[i], len - i, " BATTVIMON_INT=%x",
		FIELD2VALUE(MAX77779_BATTVIMON_INT, val));
	i += scnprintf(&buff[i], len - i, " GPIO_INT=%x",
		FIELD2VALUE(MAX77779_GPIO_INT, val));
	i += scnprintf(&buff[i], len - i, " VDROOP_INT=%x",
		FIELD2VALUE(MAX77779_VDROOP_INT, val));
	i += scnprintf(&buff[i], len - i, " PMICTOP_INT=%x",
		FIELD2VALUE(MAX77779_PMICTOP_INT, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_intsrc_sts_tcpc_int,0,0)
MAX77779_BFF(max77779_intsrc_sts_fg_int,1,1)
MAX77779_BFF(max77779_intsrc_sts_chgr_int,2,2)
MAX77779_BFF(max77779_intsrc_sts_i2cm_int,3,3)
MAX77779_BFF(max77779_intsrc_sts_battvimon_int,4,4)
MAX77779_BFF(max77779_intsrc_sts_gpio_int,5,5)
MAX77779_BFF(max77779_intsrc_sts_vdroop_int,6,6)
MAX77779_BFF(max77779_intsrc_sts_pmictop_int,7,7)

/*
 * VDROOP_INT,0x23,0b00000000,0x0,Reset_Type:S
 * OILO2_CNT_INT,OILO1_CNT_INT,UVLO2_CNT_INT,UVLO1_CNT_INT,BAT_OILO2_INT,
 * BAT_OILO1_INT,SYS_UVLO2_INT,SYS_UVLO1_INT
 */
#define MAX77779_VDROOP_INT	0x23
#define MAX77779_VDROOP_INT_OILO2_CNT_INT_SHIFT	0
#define MAX77779_VDROOP_INT_OILO2_CNT_INT_MASK	(0x1 << 0)
#define MAX77779_VDROOP_INT_OILO2_CNT_INT_CLEAR	(~(0x1 << 0))
#define MAX77779_VDROOP_INT_OILO1_CNT_INT_SHIFT	1
#define MAX77779_VDROOP_INT_OILO1_CNT_INT_MASK	(0x1 << 1)
#define MAX77779_VDROOP_INT_OILO1_CNT_INT_CLEAR	(~(0x1 << 1))
#define MAX77779_VDROOP_INT_UVLO2_CNT_INT_SHIFT	2
#define MAX77779_VDROOP_INT_UVLO2_CNT_INT_MASK	(0x1 << 2)
#define MAX77779_VDROOP_INT_UVLO2_CNT_INT_CLEAR	(~(0x1 << 2))
#define MAX77779_VDROOP_INT_UVLO1_CNT_INT_SHIFT	3
#define MAX77779_VDROOP_INT_UVLO1_CNT_INT_MASK	(0x1 << 3)
#define MAX77779_VDROOP_INT_UVLO1_CNT_INT_CLEAR	(~(0x1 << 3))
#define MAX77779_VDROOP_INT_BAT_OILO2_INT_SHIFT	4
#define MAX77779_VDROOP_INT_BAT_OILO2_INT_MASK	(0x1 << 4)
#define MAX77779_VDROOP_INT_BAT_OILO2_INT_CLEAR	(~(0x1 << 4))
#define MAX77779_VDROOP_INT_BAT_OILO1_INT_SHIFT	5
#define MAX77779_VDROOP_INT_BAT_OILO1_INT_MASK	(0x1 << 5)
#define MAX77779_VDROOP_INT_BAT_OILO1_INT_CLEAR	(~(0x1 << 5))
#define MAX77779_VDROOP_INT_SYS_UVLO2_INT_SHIFT	6
#define MAX77779_VDROOP_INT_SYS_UVLO2_INT_MASK	(0x1 << 6)
#define MAX77779_VDROOP_INT_SYS_UVLO2_INT_CLEAR	(~(0x1 << 6))
#define MAX77779_VDROOP_INT_SYS_UVLO1_INT_SHIFT	7
#define MAX77779_VDROOP_INT_SYS_UVLO1_INT_MASK	(0x1 << 7)
#define MAX77779_VDROOP_INT_SYS_UVLO1_INT_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_vdroop_int_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " OILO2_CNT_INT=%x",
		FIELD2VALUE(MAX77779_OILO2_CNT_INT, val));
	i += scnprintf(&buff[i], len - i, " OILO1_CNT_INT=%x",
		FIELD2VALUE(MAX77779_OILO1_CNT_INT, val));
	i += scnprintf(&buff[i], len - i, " UVLO2_CNT_INT=%x",
		FIELD2VALUE(MAX77779_UVLO2_CNT_INT, val));
	i += scnprintf(&buff[i], len - i, " UVLO1_CNT_INT=%x",
		FIELD2VALUE(MAX77779_UVLO1_CNT_INT, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_INT=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_INT, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_INT=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_INT, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_INT=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_INT, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_INT=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_INT, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_vdroop_int_oilo2_cnt_int,0,0)
MAX77779_BFF(max77779_vdroop_int_oilo1_cnt_int,1,1)
MAX77779_BFF(max77779_vdroop_int_uvlo2_cnt_int,2,2)
MAX77779_BFF(max77779_vdroop_int_uvlo1_cnt_int,3,3)
MAX77779_BFF(max77779_vdroop_int_bat_oilo2_int,4,4)
MAX77779_BFF(max77779_vdroop_int_bat_oilo1_int,5,5)
MAX77779_BFF(max77779_vdroop_int_sys_uvlo2_int,6,6)
MAX77779_BFF(max77779_vdroop_int_sys_uvlo1_int,7,7)

/*
 * INTB_MASK,0x24,0b11111111,0xff,OTP:SHADOW, Reset_Type:S
 * TCPC_INT_M,FG_INT_M,CHGR_INT_M,I2CM_INT_M,BATTVIMON_INT_M,GPIO_INT_M,
 * VDROOP_INT_M__INT_MSK,PMICTOP_INT_M
 */
#define MAX77779_INTB_MASK	0x24
#define MAX77779_INTB_MASK_TCPC_INT_M_SHIFT	0
#define MAX77779_INTB_MASK_TCPC_INT_M_MASK	(0x1 << 0)
#define MAX77779_INTB_MASK_TCPC_INT_M_CLEAR	(~(0x1 << 0))
#define MAX77779_INTB_MASK_FG_INT_M_SHIFT	1
#define MAX77779_INTB_MASK_FG_INT_M_MASK	(0x1 << 1)
#define MAX77779_INTB_MASK_FG_INT_M_CLEAR	(~(0x1 << 1))
#define MAX77779_INTB_MASK_CHGR_INT_M_SHIFT	2
#define MAX77779_INTB_MASK_CHGR_INT_M_MASK	(0x1 << 2)
#define MAX77779_INTB_MASK_CHGR_INT_M_CLEAR	(~(0x1 << 2))
#define MAX77779_INTB_MASK_I2CM_INT_M_SHIFT	3
#define MAX77779_INTB_MASK_I2CM_INT_M_MASK	(0x1 << 3)
#define MAX77779_INTB_MASK_I2CM_INT_M_CLEAR	(~(0x1 << 3))
#define MAX77779_INTB_MASK_BATTVIMON_INT_M_SHIFT	4
#define MAX77779_INTB_MASK_BATTVIMON_INT_M_MASK	(0x1 << 4)
#define MAX77779_INTB_MASK_BATTVIMON_INT_M_CLEAR	(~(0x1 << 4))
#define MAX77779_INTB_MASK_GPIO_INT_M_SHIFT	5
#define MAX77779_INTB_MASK_GPIO_INT_M_MASK	(0x1 << 5)
#define MAX77779_INTB_MASK_GPIO_INT_M_CLEAR	(~(0x1 << 5))
#define MAX77779_INTB_MASK_VDROOP_INT_M__INT_MSK_SHIFT	6
#define MAX77779_INTB_MASK_VDROOP_INT_M__INT_MSK_MASK	(0x1 << 6)
#define MAX77779_INTB_MASK_VDROOP_INT_M__INT_MSK_CLEAR	(~(0x1 << 6))
#define MAX77779_INTB_MASK_PMICTOP_INT_M_SHIFT	7
#define MAX77779_INTB_MASK_PMICTOP_INT_M_MASK	(0x1 << 7)
#define MAX77779_INTB_MASK_PMICTOP_INT_M_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_intb_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " TCPC_INT_M=%x",
		FIELD2VALUE(MAX77779_TCPC_INT_M, val));
	i += scnprintf(&buff[i], len - i, " FG_INT_M=%x",
		FIELD2VALUE(MAX77779_FG_INT_M, val));
	i += scnprintf(&buff[i], len - i, " CHGR_INT_M=%x",
		FIELD2VALUE(MAX77779_CHGR_INT_M, val));
	i += scnprintf(&buff[i], len - i, " I2CM_INT_M=%x",
		FIELD2VALUE(MAX77779_I2CM_INT_M, val));
	i += scnprintf(&buff[i], len - i, " BATTVIMON_INT_M=%x",
		FIELD2VALUE(MAX77779_BATTVIMON_INT_M, val));
	i += scnprintf(&buff[i], len - i, " GPIO_INT_M=%x",
		FIELD2VALUE(MAX77779_GPIO_INT_M, val));
	i += scnprintf(&buff[i], len - i, " VDROOP_INT_M__INT_MSK=%x",
		FIELD2VALUE(MAX77779_VDROOP_INT_M__INT_MSK, val));
	i += scnprintf(&buff[i], len - i, " PMICTOP_INT_M=%x",
		FIELD2VALUE(MAX77779_PMICTOP_INT_M, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_intb_mask_tcpc_int_m,0,0)
MAX77779_BFF(max77779_intb_mask_fg_int_m,1,1)
MAX77779_BFF(max77779_intb_mask_chgr_int_m,2,2)
MAX77779_BFF(max77779_intb_mask_i2cm_int_m,3,3)
MAX77779_BFF(max77779_intb_mask_battvimon_int_m,4,4)
MAX77779_BFF(max77779_intb_mask_gpio_int_m,5,5)
MAX77779_BFF(max77779_intb_mask_vdroop_int_m__int_msk,6,6)
MAX77779_BFF(max77779_intb_mask_pmictop_int_m,7,7)

/*
 * SPMI_INT_MASK,0x25,0b11111111,0xff,OTP:SHADOW, Reset_Type:S
 * TCPC_INT_SM,FG_INT_SM,CHGR_INT_SM,I2CM_INT_SM,BATTVIMON_INT_SM,GPIO_INT_SM,
 * VDROOP_INT_SM,PMICTOP_INT_SM
 */
#define MAX77779_SPMI_INT_MASK	0x25
#define MAX77779_SPMI_INT_MASK_TCPC_INT_SM_SHIFT	0
#define MAX77779_SPMI_INT_MASK_TCPC_INT_SM_MASK	(0x1 << 0)
#define MAX77779_SPMI_INT_MASK_TCPC_INT_SM_CLEAR	(~(0x1 << 0))
#define MAX77779_SPMI_INT_MASK_FG_INT_SM_SHIFT	1
#define MAX77779_SPMI_INT_MASK_FG_INT_SM_MASK	(0x1 << 1)
#define MAX77779_SPMI_INT_MASK_FG_INT_SM_CLEAR	(~(0x1 << 1))
#define MAX77779_SPMI_INT_MASK_CHGR_INT_SM_SHIFT	2
#define MAX77779_SPMI_INT_MASK_CHGR_INT_SM_MASK	(0x1 << 2)
#define MAX77779_SPMI_INT_MASK_CHGR_INT_SM_CLEAR	(~(0x1 << 2))
#define MAX77779_SPMI_INT_MASK_I2CM_INT_SM_SHIFT	3
#define MAX77779_SPMI_INT_MASK_I2CM_INT_SM_MASK	(0x1 << 3)
#define MAX77779_SPMI_INT_MASK_I2CM_INT_SM_CLEAR	(~(0x1 << 3))
#define MAX77779_SPMI_INT_MASK_BATTVIMON_INT_SM_SHIFT	4
#define MAX77779_SPMI_INT_MASK_BATTVIMON_INT_SM_MASK	(0x1 << 4)
#define MAX77779_SPMI_INT_MASK_BATTVIMON_INT_SM_CLEAR	(~(0x1 << 4))
#define MAX77779_SPMI_INT_MASK_GPIO_INT_SM_SHIFT	5
#define MAX77779_SPMI_INT_MASK_GPIO_INT_SM_MASK	(0x1 << 5)
#define MAX77779_SPMI_INT_MASK_GPIO_INT_SM_CLEAR	(~(0x1 << 5))
#define MAX77779_SPMI_INT_MASK_VDROOP_INT_SM_SHIFT	6
#define MAX77779_SPMI_INT_MASK_VDROOP_INT_SM_MASK	(0x1 << 6)
#define MAX77779_SPMI_INT_MASK_VDROOP_INT_SM_CLEAR	(~(0x1 << 6))
#define MAX77779_SPMI_INT_MASK_PMICTOP_INT_SM_SHIFT	7
#define MAX77779_SPMI_INT_MASK_PMICTOP_INT_SM_MASK	(0x1 << 7)
#define MAX77779_SPMI_INT_MASK_PMICTOP_INT_SM_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_spmi_int_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " TCPC_INT_SM=%x",
		FIELD2VALUE(MAX77779_TCPC_INT_SM, val));
	i += scnprintf(&buff[i], len - i, " FG_INT_SM=%x",
		FIELD2VALUE(MAX77779_FG_INT_SM, val));
	i += scnprintf(&buff[i], len - i, " CHGR_INT_SM=%x",
		FIELD2VALUE(MAX77779_CHGR_INT_SM, val));
	i += scnprintf(&buff[i], len - i, " I2CM_INT_SM=%x",
		FIELD2VALUE(MAX77779_I2CM_INT_SM, val));
	i += scnprintf(&buff[i], len - i, " BATTVIMON_INT_SM=%x",
		FIELD2VALUE(MAX77779_BATTVIMON_INT_SM, val));
	i += scnprintf(&buff[i], len - i, " GPIO_INT_SM=%x",
		FIELD2VALUE(MAX77779_GPIO_INT_SM, val));
	i += scnprintf(&buff[i], len - i, " VDROOP_INT_SM=%x",
		FIELD2VALUE(MAX77779_VDROOP_INT_SM, val));
	i += scnprintf(&buff[i], len - i, " PMICTOP_INT_SM=%x",
		FIELD2VALUE(MAX77779_PMICTOP_INT_SM, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_spmi_int_mask_tcpc_int_sm,0,0)
MAX77779_BFF(max77779_spmi_int_mask_fg_int_sm,1,1)
MAX77779_BFF(max77779_spmi_int_mask_chgr_int_sm,2,2)
MAX77779_BFF(max77779_spmi_int_mask_i2cm_int_sm,3,3)
MAX77779_BFF(max77779_spmi_int_mask_battvimon_int_sm,4,4)
MAX77779_BFF(max77779_spmi_int_mask_gpio_int_sm,5,5)
MAX77779_BFF(max77779_spmi_int_mask_vdroop_int_sm,6,6)
MAX77779_BFF(max77779_spmi_int_mask_pmictop_int_sm,7,7)

/*
 * SPMI_INT_PRIORITY,0x26,0b11111111,0xff,Reset_Type:S
 * TCPC_INT_PR,FG_INT_PR,CHGR_INT_PR,I2CM_INT_PR,BATTVIMON_PR,GPIO_INT_PR,
 * VDROOP_INT_PR,PMICTOP_INTB_PR
 */
#define MAX77779_SPMI_INT_PRIORITY	0x26
#define MAX77779_SPMI_INT_PRIORITY_TCPC_INT_PR_SHIFT	0
#define MAX77779_SPMI_INT_PRIORITY_TCPC_INT_PR_MASK	(0x1 << 0)
#define MAX77779_SPMI_INT_PRIORITY_TCPC_INT_PR_CLEAR	(~(0x1 << 0))
#define MAX77779_SPMI_INT_PRIORITY_FG_INT_PR_SHIFT	1
#define MAX77779_SPMI_INT_PRIORITY_FG_INT_PR_MASK	(0x1 << 1)
#define MAX77779_SPMI_INT_PRIORITY_FG_INT_PR_CLEAR	(~(0x1 << 1))
#define MAX77779_SPMI_INT_PRIORITY_CHGR_INT_PR_SHIFT	2
#define MAX77779_SPMI_INT_PRIORITY_CHGR_INT_PR_MASK	(0x1 << 2)
#define MAX77779_SPMI_INT_PRIORITY_CHGR_INT_PR_CLEAR	(~(0x1 << 2))
#define MAX77779_SPMI_INT_PRIORITY_I2CM_INT_PR_SHIFT	3
#define MAX77779_SPMI_INT_PRIORITY_I2CM_INT_PR_MASK	(0x1 << 3)
#define MAX77779_SPMI_INT_PRIORITY_I2CM_INT_PR_CLEAR	(~(0x1 << 3))
#define MAX77779_SPMI_INT_PRIORITY_BATTVIMON_PR_SHIFT	4
#define MAX77779_SPMI_INT_PRIORITY_BATTVIMON_PR_MASK	(0x1 << 4)
#define MAX77779_SPMI_INT_PRIORITY_BATTVIMON_PR_CLEAR	(~(0x1 << 4))
#define MAX77779_SPMI_INT_PRIORITY_GPIO_INT_PR_SHIFT	5
#define MAX77779_SPMI_INT_PRIORITY_GPIO_INT_PR_MASK	(0x1 << 5)
#define MAX77779_SPMI_INT_PRIORITY_GPIO_INT_PR_CLEAR	(~(0x1 << 5))
#define MAX77779_SPMI_INT_PRIORITY_VDROOP_INT_PR_SHIFT	6
#define MAX77779_SPMI_INT_PRIORITY_VDROOP_INT_PR_MASK	(0x1 << 6)
#define MAX77779_SPMI_INT_PRIORITY_VDROOP_INT_PR_CLEAR	(~(0x1 << 6))
#define MAX77779_SPMI_INT_PRIORITY_PMICTOP_INTB_PR_SHIFT	7
#define MAX77779_SPMI_INT_PRIORITY_PMICTOP_INTB_PR_MASK	(0x1 << 7)
#define MAX77779_SPMI_INT_PRIORITY_PMICTOP_INTB_PR_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_spmi_int_priority_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " TCPC_INT_PR=%x",
		FIELD2VALUE(MAX77779_TCPC_INT_PR, val));
	i += scnprintf(&buff[i], len - i, " FG_INT_PR=%x",
		FIELD2VALUE(MAX77779_FG_INT_PR, val));
	i += scnprintf(&buff[i], len - i, " CHGR_INT_PR=%x",
		FIELD2VALUE(MAX77779_CHGR_INT_PR, val));
	i += scnprintf(&buff[i], len - i, " I2CM_INT_PR=%x",
		FIELD2VALUE(MAX77779_I2CM_INT_PR, val));
	i += scnprintf(&buff[i], len - i, " BATTVIMON_PR=%x",
		FIELD2VALUE(MAX77779_BATTVIMON_PR, val));
	i += scnprintf(&buff[i], len - i, " GPIO_INT_PR=%x",
		FIELD2VALUE(MAX77779_GPIO_INT_PR, val));
	i += scnprintf(&buff[i], len - i, " VDROOP_INT_PR=%x",
		FIELD2VALUE(MAX77779_VDROOP_INT_PR, val));
	i += scnprintf(&buff[i], len - i, " PMICTOP_INTB_PR=%x",
		FIELD2VALUE(MAX77779_PMICTOP_INTB_PR, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_spmi_int_priority_tcpc_int_pr,0,0)
MAX77779_BFF(max77779_spmi_int_priority_fg_int_pr,1,1)
MAX77779_BFF(max77779_spmi_int_priority_chgr_int_pr,2,2)
MAX77779_BFF(max77779_spmi_int_priority_i2cm_int_pr,3,3)
MAX77779_BFF(max77779_spmi_int_priority_battvimon_pr,4,4)
MAX77779_BFF(max77779_spmi_int_priority_gpio_int_pr,5,5)
MAX77779_BFF(max77779_spmi_int_priority_vdroop_int_pr,6,6)
MAX77779_BFF(max77779_spmi_int_priority_pmictop_intb_pr,7,7)

/*
 * VDROOP_INT_MASK,0x27,0b11111111,0xff,OTP:SHADOW, Reset_Type:O
 * OILO2_CNT_M,OILO1_CNT_M,UVLO2_CNT_M,UVLO1_CNT_M,BAT_OILO2_M,BAT_OILO1_M,
 * SYS_UVLO2_M,SYS_UVLO1_M
 */
#define MAX77779_VDROOP_INT_MASK	0x27
#define MAX77779_VDROOP_INT_MASK_OILO2_CNT_M_SHIFT	0
#define MAX77779_VDROOP_INT_MASK_OILO2_CNT_M_MASK	(0x1 << 0)
#define MAX77779_VDROOP_INT_MASK_OILO2_CNT_M_CLEAR	(~(0x1 << 0))
#define MAX77779_VDROOP_INT_MASK_OILO1_CNT_M_SHIFT	1
#define MAX77779_VDROOP_INT_MASK_OILO1_CNT_M_MASK	(0x1 << 1)
#define MAX77779_VDROOP_INT_MASK_OILO1_CNT_M_CLEAR	(~(0x1 << 1))
#define MAX77779_VDROOP_INT_MASK_UVLO2_CNT_M_SHIFT	2
#define MAX77779_VDROOP_INT_MASK_UVLO2_CNT_M_MASK	(0x1 << 2)
#define MAX77779_VDROOP_INT_MASK_UVLO2_CNT_M_CLEAR	(~(0x1 << 2))
#define MAX77779_VDROOP_INT_MASK_UVLO1_CNT_M_SHIFT	3
#define MAX77779_VDROOP_INT_MASK_UVLO1_CNT_M_MASK	(0x1 << 3)
#define MAX77779_VDROOP_INT_MASK_UVLO1_CNT_M_CLEAR	(~(0x1 << 3))
#define MAX77779_VDROOP_INT_MASK_BAT_OILO2_M_SHIFT	4
#define MAX77779_VDROOP_INT_MASK_BAT_OILO2_M_MASK	(0x1 << 4)
#define MAX77779_VDROOP_INT_MASK_BAT_OILO2_M_CLEAR	(~(0x1 << 4))
#define MAX77779_VDROOP_INT_MASK_BAT_OILO1_M_SHIFT	5
#define MAX77779_VDROOP_INT_MASK_BAT_OILO1_M_MASK	(0x1 << 5)
#define MAX77779_VDROOP_INT_MASK_BAT_OILO1_M_CLEAR	(~(0x1 << 5))
#define MAX77779_VDROOP_INT_MASK_SYS_UVLO2_M_SHIFT	6
#define MAX77779_VDROOP_INT_MASK_SYS_UVLO2_M_MASK	(0x1 << 6)
#define MAX77779_VDROOP_INT_MASK_SYS_UVLO2_M_CLEAR	(~(0x1 << 6))
#define MAX77779_VDROOP_INT_MASK_SYS_UVLO1_M_SHIFT	7
#define MAX77779_VDROOP_INT_MASK_SYS_UVLO1_M_MASK	(0x1 << 7)
#define MAX77779_VDROOP_INT_MASK_SYS_UVLO1_M_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_vdroop_int_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " OILO2_CNT_M=%x",
		FIELD2VALUE(MAX77779_OILO2_CNT_M, val));
	i += scnprintf(&buff[i], len - i, " OILO1_CNT_M=%x",
		FIELD2VALUE(MAX77779_OILO1_CNT_M, val));
	i += scnprintf(&buff[i], len - i, " UVLO2_CNT_M=%x",
		FIELD2VALUE(MAX77779_UVLO2_CNT_M, val));
	i += scnprintf(&buff[i], len - i, " UVLO1_CNT_M=%x",
		FIELD2VALUE(MAX77779_UVLO1_CNT_M, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_M=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_M, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_M=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_M, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_M=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_M, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_M=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_M, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_vdroop_int_mask_oilo2_cnt_m,0,0)
MAX77779_BFF(max77779_vdroop_int_mask_oilo1_cnt_m,1,1)
MAX77779_BFF(max77779_vdroop_int_mask_uvlo2_cnt_m,2,2)
MAX77779_BFF(max77779_vdroop_int_mask_uvlo1_cnt_m,3,3)
MAX77779_BFF(max77779_vdroop_int_mask_bat_oilo2_m,4,4)
MAX77779_BFF(max77779_vdroop_int_mask_bat_oilo1_m,5,5)
MAX77779_BFF(max77779_vdroop_int_mask_sys_uvlo2_m,6,6)
MAX77779_BFF(max77779_vdroop_int_mask_sys_uvlo1_m,7,7)

/*
 * VDROOP_INT_SPMI_MASK,0x28,0b11111111,0xff,OTP:SHADOW, Reset_Type:O
 * OILO2_CNT_SM,OILO1_CNT_SM,UVLO2_CNT_SM,UVLO1_CNT_SM,BAT_OILO2_SM,BAT_OILO1_SM,
 * SYS_UVLO2_SM,SYS_UVLO1_SM
 */
#define MAX77779_VDROOP_INT_SPMI_MASK	0x28
#define MAX77779_VDROOP_INT_SPMI_MASK_OILO2_CNT_SM_SHIFT	0
#define MAX77779_VDROOP_INT_SPMI_MASK_OILO2_CNT_SM_MASK	(0x1 << 0)
#define MAX77779_VDROOP_INT_SPMI_MASK_OILO2_CNT_SM_CLEAR	(~(0x1 << 0))
#define MAX77779_VDROOP_INT_SPMI_MASK_OILO1_CNT_SM_SHIFT	1
#define MAX77779_VDROOP_INT_SPMI_MASK_OILO1_CNT_SM_MASK	(0x1 << 1)
#define MAX77779_VDROOP_INT_SPMI_MASK_OILO1_CNT_SM_CLEAR	(~(0x1 << 1))
#define MAX77779_VDROOP_INT_SPMI_MASK_UVLO2_CNT_SM_SHIFT	2
#define MAX77779_VDROOP_INT_SPMI_MASK_UVLO2_CNT_SM_MASK	(0x1 << 2)
#define MAX77779_VDROOP_INT_SPMI_MASK_UVLO2_CNT_SM_CLEAR	(~(0x1 << 2))
#define MAX77779_VDROOP_INT_SPMI_MASK_UVLO1_CNT_SM_SHIFT	3
#define MAX77779_VDROOP_INT_SPMI_MASK_UVLO1_CNT_SM_MASK	(0x1 << 3)
#define MAX77779_VDROOP_INT_SPMI_MASK_UVLO1_CNT_SM_CLEAR	(~(0x1 << 3))
#define MAX77779_VDROOP_INT_SPMI_MASK_BAT_OILO2_SM_SHIFT	4
#define MAX77779_VDROOP_INT_SPMI_MASK_BAT_OILO2_SM_MASK	(0x1 << 4)
#define MAX77779_VDROOP_INT_SPMI_MASK_BAT_OILO2_SM_CLEAR	(~(0x1 << 4))
#define MAX77779_VDROOP_INT_SPMI_MASK_BAT_OILO1_SM_SHIFT	5
#define MAX77779_VDROOP_INT_SPMI_MASK_BAT_OILO1_SM_MASK	(0x1 << 5)
#define MAX77779_VDROOP_INT_SPMI_MASK_BAT_OILO1_SM_CLEAR	(~(0x1 << 5))
#define MAX77779_VDROOP_INT_SPMI_MASK_SYS_UVLO2_SM_SHIFT	6
#define MAX77779_VDROOP_INT_SPMI_MASK_SYS_UVLO2_SM_MASK	(0x1 << 6)
#define MAX77779_VDROOP_INT_SPMI_MASK_SYS_UVLO2_SM_CLEAR	(~(0x1 << 6))
#define MAX77779_VDROOP_INT_SPMI_MASK_SYS_UVLO1_SM_SHIFT	7
#define MAX77779_VDROOP_INT_SPMI_MASK_SYS_UVLO1_SM_MASK	(0x1 << 7)
#define MAX77779_VDROOP_INT_SPMI_MASK_SYS_UVLO1_SM_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_vdroop_int_spmi_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " OILO2_CNT_SM=%x",
		FIELD2VALUE(MAX77779_OILO2_CNT_SM, val));
	i += scnprintf(&buff[i], len - i, " OILO1_CNT_SM=%x",
		FIELD2VALUE(MAX77779_OILO1_CNT_SM, val));
	i += scnprintf(&buff[i], len - i, " UVLO2_CNT_SM=%x",
		FIELD2VALUE(MAX77779_UVLO2_CNT_SM, val));
	i += scnprintf(&buff[i], len - i, " UVLO1_CNT_SM=%x",
		FIELD2VALUE(MAX77779_UVLO1_CNT_SM, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_SM=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_SM, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_SM=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_SM, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_SM=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_SM, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_SM=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_SM, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_vdroop_int_spmi_mask_oilo2_cnt_sm,0,0)
MAX77779_BFF(max77779_vdroop_int_spmi_mask_oilo1_cnt_sm,1,1)
MAX77779_BFF(max77779_vdroop_int_spmi_mask_uvlo2_cnt_sm,2,2)
MAX77779_BFF(max77779_vdroop_int_spmi_mask_uvlo1_cnt_sm,3,3)
MAX77779_BFF(max77779_vdroop_int_spmi_mask_bat_oilo2_sm,4,4)
MAX77779_BFF(max77779_vdroop_int_spmi_mask_bat_oilo1_sm,5,5)
MAX77779_BFF(max77779_vdroop_int_spmi_mask_sys_uvlo2_sm,6,6)
MAX77779_BFF(max77779_vdroop_int_spmi_mask_sys_uvlo1_sm,7,7)

/*
 * VDROOP_INT_SPMI_PRIORITY,0x29,0b11111111,0xff,OTP:SHADOW, Reset_Type:O
 * OILO2_CNT_PR,OILO1_CNT_PR,UVLO2_CNT_PR,UVLO1_CNT_PR,BAT_OILO2_PR,BAT_OILO1_PR,
 * SYS_UVLO2_PR,SYS_UVLO1_PR
 */
#define MAX77779_VDROOP_INT_SPMI_PRIORITY	0x29
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_OILO2_CNT_PR_SHIFT	0
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_OILO2_CNT_PR_MASK	(0x1 << 0)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_OILO2_CNT_PR_CLEAR	(~(0x1 << 0))
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_OILO1_CNT_PR_SHIFT	1
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_OILO1_CNT_PR_MASK	(0x1 << 1)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_OILO1_CNT_PR_CLEAR	(~(0x1 << 1))
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_UVLO2_CNT_PR_SHIFT	2
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_UVLO2_CNT_PR_MASK	(0x1 << 2)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_UVLO2_CNT_PR_CLEAR	(~(0x1 << 2))
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_UVLO1_CNT_PR_SHIFT	3
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_UVLO1_CNT_PR_MASK	(0x1 << 3)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_UVLO1_CNT_PR_CLEAR	(~(0x1 << 3))
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_BAT_OILO2_PR_SHIFT	4
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_BAT_OILO2_PR_MASK	(0x1 << 4)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_BAT_OILO2_PR_CLEAR	(~(0x1 << 4))
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_BAT_OILO1_PR_SHIFT	5
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_BAT_OILO1_PR_MASK	(0x1 << 5)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_BAT_OILO1_PR_CLEAR	(~(0x1 << 5))
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_SYS_UVLO2_PR_SHIFT	6
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_SYS_UVLO2_PR_MASK	(0x1 << 6)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_SYS_UVLO2_PR_CLEAR	(~(0x1 << 6))
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_SYS_UVLO1_PR_SHIFT	7
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_SYS_UVLO1_PR_MASK	(0x1 << 7)
#define MAX77779_VDROOP_INT_SPMI_PRIORITY_SYS_UVLO1_PR_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_vdroop_int_spmi_priority_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " OILO2_CNT_PR=%x",
		FIELD2VALUE(MAX77779_OILO2_CNT_PR, val));
	i += scnprintf(&buff[i], len - i, " OILO1_CNT_PR=%x",
		FIELD2VALUE(MAX77779_OILO1_CNT_PR, val));
	i += scnprintf(&buff[i], len - i, " UVLO2_CNT_PR=%x",
		FIELD2VALUE(MAX77779_UVLO2_CNT_PR, val));
	i += scnprintf(&buff[i], len - i, " UVLO1_CNT_PR=%x",
		FIELD2VALUE(MAX77779_UVLO1_CNT_PR, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_PR=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_PR, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_PR=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_PR, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_PR=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_PR, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_PR=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_PR, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_vdroop_int_spmi_priority_oilo2_cnt_pr,0,0)
MAX77779_BFF(max77779_vdroop_int_spmi_priority_oilo1_cnt_pr,1,1)
MAX77779_BFF(max77779_vdroop_int_spmi_priority_uvlo2_cnt_pr,2,2)
MAX77779_BFF(max77779_vdroop_int_spmi_priority_uvlo1_cnt_pr,3,3)
MAX77779_BFF(max77779_vdroop_int_spmi_priority_bat_oilo2_pr,4,4)
MAX77779_BFF(max77779_vdroop_int_spmi_priority_bat_oilo1_pr,5,5)
MAX77779_BFF(max77779_vdroop_int_spmi_priority_sys_uvlo2_pr,6,6)
MAX77779_BFF(max77779_vdroop_int_spmi_priority_sys_uvlo1_pr,7,7)

/*
 * PMICTOP_INT_STS,0x2A,0b00000000,0x0,Reset_Type:S
 * SPR_2_0[0:2],VDDPOR_INT,SysMsgI_INT,SYSUVLO_INT,SYSOVLO_INT,TSHDN_INT,
 * APCmdResI_INT
 */
#define MAX77779_PMICTOP_INT_STS	0x2a
#define MAX77779_PMICTOP_INT_STS_SPR_2_0_SHIFT	0
#define MAX77779_PMICTOP_INT_STS_SPR_2_0_MASK	(0x3 << 0)
#define MAX77779_PMICTOP_INT_STS_SPR_2_0_CLEAR	(~(0x3 << 0))
#define MAX77779_PMICTOP_INT_STS_VDDPOR_INT_SHIFT	2
#define MAX77779_PMICTOP_INT_STS_VDDPOR_INT_MASK	(0x1 << 2)
#define MAX77779_PMICTOP_INT_STS_VDDPOR_INT_CLEAR	(~(0x1 << 2))
#define MAX77779_PMICTOP_INT_STS_SysMsgI_INT_SHIFT	3
#define MAX77779_PMICTOP_INT_STS_SysMsgI_INT_MASK	(0x1 << 3)
#define MAX77779_PMICTOP_INT_STS_SysMsgI_INT_CLEAR	(~(0x1 << 3))
#define MAX77779_PMICTOP_INT_STS_SYSUVLO_INT_SHIFT	4
#define MAX77779_PMICTOP_INT_STS_SYSUVLO_INT_MASK	(0x1 << 4)
#define MAX77779_PMICTOP_INT_STS_SYSUVLO_INT_CLEAR	(~(0x1 << 4))
#define MAX77779_PMICTOP_INT_STS_SYSOVLO_INT_SHIFT	5
#define MAX77779_PMICTOP_INT_STS_SYSOVLO_INT_MASK	(0x1 << 5)
#define MAX77779_PMICTOP_INT_STS_SYSOVLO_INT_CLEAR	(~(0x1 << 5))
#define MAX77779_PMICTOP_INT_STS_TSHDN_INT_SHIFT	6
#define MAX77779_PMICTOP_INT_STS_TSHDN_INT_MASK	(0x1 << 6)
#define MAX77779_PMICTOP_INT_STS_TSHDN_INT_CLEAR	(~(0x1 << 6))
#define MAX77779_PMICTOP_INT_STS_APCmdResI_INT_SHIFT	7
#define MAX77779_PMICTOP_INT_STS_APCmdResI_INT_MASK	(0x1 << 7)
#define MAX77779_PMICTOP_INT_STS_APCmdResI_INT_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_pmictop_int_sts_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SPR_2_0=%x",
		FIELD2VALUE(MAX77779_SPR_2_0, val));
	i += scnprintf(&buff[i], len - i, " VDDPOR_INT=%x",
		FIELD2VALUE(MAX77779_VDDPOR_INT, val));
	i += scnprintf(&buff[i], len - i, " SysMsgI_INT=%x",
		FIELD2VALUE(MAX77779_SysMsgI_INT, val));
	i += scnprintf(&buff[i], len - i, " SYSUVLO_INT=%x",
		FIELD2VALUE(MAX77779_SYSUVLO_INT, val));
	i += scnprintf(&buff[i], len - i, " SYSOVLO_INT=%x",
		FIELD2VALUE(MAX77779_SYSOVLO_INT, val));
	i += scnprintf(&buff[i], len - i, " TSHDN_INT=%x",
		FIELD2VALUE(MAX77779_TSHDN_INT, val));
	i += scnprintf(&buff[i], len - i, " APCmdResI_INT=%x",
		FIELD2VALUE(MAX77779_APCmdResI_INT, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmictop_int_sts_spr_2_0,1,0)
MAX77779_BFF(max77779_pmictop_int_sts_vddpor_int,2,2)
MAX77779_BFF(max77779_pmictop_int_sts_sysmsgi_int,3,3)
MAX77779_BFF(max77779_pmictop_int_sts_sysuvlo_int,4,4)
MAX77779_BFF(max77779_pmictop_int_sts_sysovlo_int,5,5)
MAX77779_BFF(max77779_pmictop_int_sts_tshdn_int,6,6)
MAX77779_BFF(max77779_pmictop_int_sts_apcmdresi_int,7,7)

/*
 * PMICTOP_INT_MASK,0x2B,0b11111111,0xff,Reset_Type:S
 * FSHIP_NOT_RD,SPR_2_1,VDDPOR_M,SysMsg_M,SYSUVLO_INT_M,SYSOVLO_INT_M,
 * TSHDN_INT_M,APCmdRes_M
 */
#define MAX77779_PMICTOP_INT_MASK	0x2b
#define MAX77779_PMICTOP_INT_MASK_FSHIP_NOT_RD_SHIFT	0
#define MAX77779_PMICTOP_INT_MASK_FSHIP_NOT_RD_MASK	(0x1 << 0)
#define MAX77779_PMICTOP_INT_MASK_FSHIP_NOT_RD_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICTOP_INT_MASK_SPR_2_1_SHIFT	1
#define MAX77779_PMICTOP_INT_MASK_SPR_2_1_MASK	(0x1 << 1)
#define MAX77779_PMICTOP_INT_MASK_SPR_2_1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICTOP_INT_MASK_VDDPOR_M_SHIFT	2
#define MAX77779_PMICTOP_INT_MASK_VDDPOR_M_MASK	(0x1 << 2)
#define MAX77779_PMICTOP_INT_MASK_VDDPOR_M_CLEAR	(~(0x1 << 2))
#define MAX77779_PMICTOP_INT_MASK_SysMsg_M_SHIFT	3
#define MAX77779_PMICTOP_INT_MASK_SysMsg_M_MASK	(0x1 << 3)
#define MAX77779_PMICTOP_INT_MASK_SysMsg_M_CLEAR	(~(0x1 << 3))
#define MAX77779_PMICTOP_INT_MASK_SYSUVLO_INT_M_SHIFT	4
#define MAX77779_PMICTOP_INT_MASK_SYSUVLO_INT_M_MASK	(0x1 << 4)
#define MAX77779_PMICTOP_INT_MASK_SYSUVLO_INT_M_CLEAR	(~(0x1 << 4))
#define MAX77779_PMICTOP_INT_MASK_SYSOVLO_INT_M_SHIFT	5
#define MAX77779_PMICTOP_INT_MASK_SYSOVLO_INT_M_MASK	(0x1 << 5)
#define MAX77779_PMICTOP_INT_MASK_SYSOVLO_INT_M_CLEAR	(~(0x1 << 5))
#define MAX77779_PMICTOP_INT_MASK_TSHDN_INT_M_SHIFT	6
#define MAX77779_PMICTOP_INT_MASK_TSHDN_INT_M_MASK	(0x1 << 6)
#define MAX77779_PMICTOP_INT_MASK_TSHDN_INT_M_CLEAR	(~(0x1 << 6))
#define MAX77779_PMICTOP_INT_MASK_APCmdRes_M_SHIFT	7
#define MAX77779_PMICTOP_INT_MASK_APCmdRes_M_MASK	(0x1 << 7)
#define MAX77779_PMICTOP_INT_MASK_APCmdRes_M_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_pmictop_int_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " FSHIP_NOT_RD=%x",
		FIELD2VALUE(MAX77779_FSHIP_NOT_RD, val));
	i += scnprintf(&buff[i], len - i, " SPR_2_1=%x",
		FIELD2VALUE(MAX77779_SPR_2_1, val));
	i += scnprintf(&buff[i], len - i, " VDDPOR_M=%x",
		FIELD2VALUE(MAX77779_VDDPOR_M, val));
	i += scnprintf(&buff[i], len - i, " SysMsg_M=%x",
		FIELD2VALUE(MAX77779_SysMsg_M, val));
	i += scnprintf(&buff[i], len - i, " SYSUVLO_INT_M=%x",
		FIELD2VALUE(MAX77779_SYSUVLO_INT_M, val));
	i += scnprintf(&buff[i], len - i, " SYSOVLO_INT_M=%x",
		FIELD2VALUE(MAX77779_SYSOVLO_INT_M, val));
	i += scnprintf(&buff[i], len - i, " TSHDN_INT_M=%x",
		FIELD2VALUE(MAX77779_TSHDN_INT_M, val));
	i += scnprintf(&buff[i], len - i, " APCmdRes_M=%x",
		FIELD2VALUE(MAX77779_APCmdRes_M, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmictop_int_mask_fship_not_rd,0,0)
MAX77779_BFF(max77779_pmictop_int_mask_spr_2_1,1,1)
MAX77779_BFF(max77779_pmictop_int_mask_vddpor_m,2,2)
MAX77779_BFF(max77779_pmictop_int_mask_sysmsg_m,3,3)
MAX77779_BFF(max77779_pmictop_int_mask_sysuvlo_int_m,4,4)
MAX77779_BFF(max77779_pmictop_int_mask_sysovlo_int_m,5,5)
MAX77779_BFF(max77779_pmictop_int_mask_tshdn_int_m,6,6)
MAX77779_BFF(max77779_pmictop_int_mask_apcmdres_m,7,7)

/*
 * EVENT_CNT_CFG,0x30,0b00000000,0x0,Reset_Type:O
 * ENABLE,SAMPLE_RATE[1:2],SPR_7_3[3:5]
 */
#define MAX77779_EVENT_CNT_CFG	0x30
#define MAX77779_EVENT_CNT_CFG_ENABLE_SHIFT	0
#define MAX77779_EVENT_CNT_CFG_ENABLE_MASK	(0x1 << 0)
#define MAX77779_EVENT_CNT_CFG_ENABLE_CLEAR	(~(0x1 << 0))
#define MAX77779_EVENT_CNT_CFG_SAMPLE_RATE_SHIFT	1
#define MAX77779_EVENT_CNT_CFG_SAMPLE_RATE_MASK	(0x3 << 1)
#define MAX77779_EVENT_CNT_CFG_SAMPLE_RATE_CLEAR	(~(0x3 << 1))
#define MAX77779_EVENT_CNT_CFG_SPR_7_3_SHIFT	3
#define MAX77779_EVENT_CNT_CFG_SPR_7_3_MASK	(0x1f << 3)
#define MAX77779_EVENT_CNT_CFG_SPR_7_3_CLEAR	(~(0x1f << 3))
static inline const char *
max77779_event_cnt_cfg_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " ENABLE=%x",
		FIELD2VALUE(MAX77779_ENABLE, val));
	i += scnprintf(&buff[i], len - i, " SAMPLE_RATE=%x",
		FIELD2VALUE(MAX77779_SAMPLE_RATE, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_3=%x",
		FIELD2VALUE(MAX77779_SPR_7_3, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_event_cnt_cfg_enable,0,0)
MAX77779_BFF(max77779_event_cnt_cfg_sample_rate,2,1)
MAX77779_BFF(max77779_event_cnt_cfg_spr_7_3,7,3)

/*
 * EVENT_CNT_OILO0_THR,0x31,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_OILO0_THR	0x31

/*
 * EVENT_CNT_OILO1_THR,0x32,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_OILO1_THR	0x32

/*
 * EVENT_CNT_UVLO0_THR,0x33,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_UVLO0_THR	0x33

/*
 * EVENT_CNT_UVLO1_THR,0x34,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_UVLO1_THR	0x34

/*
 * EVENT_CNT_OILO0,0x35,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_OILO0	0x35

/*
 * EVENT_CNT_OILO1,0x36,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_OILO1	0x36

/*
 * EVENT_CNT_UVLO0,0x37,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_UVLO0	0x37

/*
 * EVENT_CNT_UVLO1,0x38,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_EVENT_CNT_UVLO1	0x38

/*
 * I2C_CNFG,0x40,0b00000000,0x0,Reset_Type:O
 * HS_EXT_EN,SPR_3_1[1:3],PAIR[4:3],SPR_7
 */
#define MAX77779_I2C_CNFG	0x40
#define MAX77779_I2C_CNFG_HS_EXT_EN_SHIFT	0
#define MAX77779_I2C_CNFG_HS_EXT_EN_MASK	(0x1 << 0)
#define MAX77779_I2C_CNFG_HS_EXT_EN_CLEAR	(~(0x1 << 0))
#define MAX77779_I2C_CNFG_SPR_3_1_SHIFT	1
#define MAX77779_I2C_CNFG_SPR_3_1_MASK	(0x7 << 1)
#define MAX77779_I2C_CNFG_SPR_3_1_CLEAR	(~(0x7 << 1))
#define MAX77779_I2C_CNFG_PAIR_SHIFT	4
#define MAX77779_I2C_CNFG_PAIR_MASK	(0x7 << 4)
#define MAX77779_I2C_CNFG_PAIR_CLEAR	(~(0x7 << 4))
#define MAX77779_I2C_CNFG_SPR_7_SHIFT	7
#define MAX77779_I2C_CNFG_SPR_7_MASK	(0x1 << 7)
#define MAX77779_I2C_CNFG_SPR_7_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_i2c_cnfg_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " HS_EXT_EN=%x",
		FIELD2VALUE(MAX77779_HS_EXT_EN, val));
	i += scnprintf(&buff[i], len - i, " SPR_3_1=%x",
		FIELD2VALUE(MAX77779_SPR_3_1, val));
	i += scnprintf(&buff[i], len - i, " PAIR=%x",
		FIELD2VALUE(MAX77779_PAIR, val));
	i += scnprintf(&buff[i], len - i, " SPR_7=%x",
		FIELD2VALUE(MAX77779_SPR_7, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2c_cnfg_hs_ext_en,0,0)
MAX77779_BFF(max77779_i2c_cnfg_spr_3_1,3,1)
MAX77779_BFF(max77779_i2c_cnfg_pair,6,4)
MAX77779_BFF(max77779_i2c_cnfg_spr_7,7,7)

/*
 * SPMI_CNFG,0x41,0b00000010,0x2,OTP:SHADOW, Reset_Type:O
 * CLOAD[0:2],SPR_3_2[2:2],VIO_HIGH,SPR_7_5[5:3]
 */
#define MAX77779_SPMI_CNFG	0x41
#define MAX77779_SPMI_CNFG_CLOAD_SHIFT	0
#define MAX77779_SPMI_CNFG_CLOAD_MASK	(0x3 << 0)
#define MAX77779_SPMI_CNFG_CLOAD_CLEAR	(~(0x3 << 0))
#define MAX77779_SPMI_CNFG_SPR_3_2_SHIFT	2
#define MAX77779_SPMI_CNFG_SPR_3_2_MASK	(0x3 << 2)
#define MAX77779_SPMI_CNFG_SPR_3_2_CLEAR	(~(0x3 << 2))
#define MAX77779_SPMI_CNFG_VIO_HIGH_SHIFT	4
#define MAX77779_SPMI_CNFG_VIO_HIGH_MASK	(0x1 << 4)
#define MAX77779_SPMI_CNFG_VIO_HIGH_CLEAR	(~(0x1 << 4))
#define MAX77779_SPMI_CNFG_SPR_7_5_SHIFT	5
#define MAX77779_SPMI_CNFG_SPR_7_5_MASK	(0x7 << 5)
#define MAX77779_SPMI_CNFG_SPR_7_5_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_spmi_cnfg_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CLOAD=%x",
		FIELD2VALUE(MAX77779_CLOAD, val));
	i += scnprintf(&buff[i], len - i, " SPR_3_2=%x",
		FIELD2VALUE(MAX77779_SPR_3_2, val));
	i += scnprintf(&buff[i], len - i, " VIO_HIGH=%x",
		FIELD2VALUE(MAX77779_VIO_HIGH, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_5=%x",
		FIELD2VALUE(MAX77779_SPR_7_5, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_spmi_cnfg_cload,1,0)
MAX77779_BFF(max77779_spmi_cnfg_spr_3_2,3,2)
MAX77779_BFF(max77779_spmi_cnfg_vio_high,4,4)
MAX77779_BFF(max77779_spmi_cnfg_spr_7_5,7,5)

/*
 * SPMI_MID,0x42,0b00000010,0x2,Reset_Type:O
 * SPMI_MID[0:2],SPR_6_2[2:5],SPMI_MSG_REPEAT
 */
#define MAX77779_SPMI_MID	0x42
#define MAX77779_SPMI_MID_SPMI_MID_SHIFT	0
#define MAX77779_SPMI_MID_SPMI_MID_MASK	(0x3 << 0)
#define MAX77779_SPMI_MID_SPMI_MID_CLEAR	(~(0x3 << 0))
#define MAX77779_SPMI_MID_SPR_6_2_SHIFT	2
#define MAX77779_SPMI_MID_SPR_6_2_MASK	(0x1f << 2)
#define MAX77779_SPMI_MID_SPR_6_2_CLEAR	(~(0x1f << 2))
#define MAX77779_SPMI_MID_SPMI_MSG_REPEAT_SHIFT	7
#define MAX77779_SPMI_MID_SPMI_MSG_REPEAT_MASK	(0x1 << 7)
#define MAX77779_SPMI_MID_SPMI_MSG_REPEAT_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_spmi_mid_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SPMI_MID=%x",
		FIELD2VALUE(MAX77779_SPMI_MID, val));
	i += scnprintf(&buff[i], len - i, " SPR_6_2=%x",
		FIELD2VALUE(MAX77779_SPR_6_2, val));
	i += scnprintf(&buff[i], len - i, " SPMI_MSG_REPEAT=%x",
		FIELD2VALUE(MAX77779_SPMI_MSG_REPEAT, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_spmi_mid_spmi_mid,1,0)
MAX77779_BFF(max77779_spmi_mid_spr_6_2,6,2)
MAX77779_BFF(max77779_spmi_mid_spmi_msg_repeat,7,7)

/*
 * SPMI_MADDR,0x43,0b00000000,0x0,Reset_Type:O
 */
#define MAX77779_SPMI_MADDR	0x43

/*
 * SWRESET,0x50,0b00000000,0x0,Reset_Type:S, (Exception: SWR_RST bits are not register type which can retain data)
 * SWR_RST[0:6],IC_RST_MASK,VIO_OK_MASK
 */
#define MAX77779_SWRESET	0x50
#define MAX77779_SWRESET_SWR_RST_SHIFT	0
#define MAX77779_SWRESET_SWR_RST_MASK	(0x3f << 0)
#define MAX77779_SWRESET_SWR_RST_CLEAR	(~(0x3f << 0))
#define MAX77779_SWRESET_IC_RST_MASK_SHIFT	6
#define MAX77779_SWRESET_IC_RST_MASK_MASK	(0x1 << 6)
#define MAX77779_SWRESET_IC_RST_MASK_CLEAR	(~(0x1 << 6))
#define MAX77779_SWRESET_VIO_OK_MASK_SHIFT	7
#define MAX77779_SWRESET_VIO_OK_MASK_MASK	(0x1 << 7)
#define MAX77779_SWRESET_VIO_OK_MASK_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_swreset_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SWR_RST=%x",
		FIELD2VALUE(MAX77779_SWR_RST, val));
	i += scnprintf(&buff[i], len - i, " IC_RST_MASK=%x",
		FIELD2VALUE(MAX77779_IC_RST_MASK, val));
	i += scnprintf(&buff[i], len - i, " VIO_OK_MASK=%x",
		FIELD2VALUE(MAX77779_VIO_OK_MASK, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_swreset_swr_rst,5,0)
MAX77779_BFF(max77779_swreset_ic_rst_mask,6,6)
MAX77779_BFF(max77779_swreset_vio_ok_mask,7,7)

/*
 * CONTROL_FG,0x51,0b00010000,0x10,Reset_Type:F
 * SPR_3_0[0:4],TSHDN_DIS,SPR_7_5[5:3]
 */
#define MAX77779_CONTROL_FG	0x51
#define MAX77779_CONTROL_FG_SPR_3_0_SHIFT	0
#define MAX77779_CONTROL_FG_SPR_3_0_MASK	(0xf << 0)
#define MAX77779_CONTROL_FG_SPR_3_0_CLEAR	(~(0xf << 0))
#define MAX77779_CONTROL_FG_TSHDN_DIS_SHIFT	4
#define MAX77779_CONTROL_FG_TSHDN_DIS_MASK	(0x1 << 4)
#define MAX77779_CONTROL_FG_TSHDN_DIS_CLEAR	(~(0x1 << 4))
#define MAX77779_CONTROL_FG_SPR_7_5_SHIFT	5
#define MAX77779_CONTROL_FG_SPR_7_5_MASK	(0x7 << 5)
#define MAX77779_CONTROL_FG_SPR_7_5_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_control_fg_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SPR_3_0=%x",
		FIELD2VALUE(MAX77779_SPR_3_0, val));
	i += scnprintf(&buff[i], len - i, " TSHDN_DIS=%x",
		FIELD2VALUE(MAX77779_TSHDN_DIS, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_5=%x",
		FIELD2VALUE(MAX77779_SPR_7_5, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_control_fg_spr_3_0,3,0)
MAX77779_BFF(max77779_control_fg_tshdn_dis,4,4)
MAX77779_BFF(max77779_control_fg_spr_7_5,7,5)
/*
 * Section: RISCV_FUNC 0x60 8
 */


/*
 * DEVICE_ID,0x00,0b01111001,0x79,Reset_Type:S
 */
#define MAX77779_DEVICE_ID	0x60

/*
 * DEVICE_REV,0x01,0b00000001,0x1,Reset_Type:S
 */
#define MAX77779_DEVICE_REV	0x61

/*
 * FW_REV,0x02,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_FW_REV	0x62

/*
 * FW_SUB_REV,0x03,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_FW_SUB_REV	0x63

/*
 * SysMsg,0x09,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_SysMsg	0x69

/*
 * AP_DATAOUT1,0x21,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT1	0x81

/*
 * AP_DATAOUT2,0x22,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT2	0x82

/*
 * AP_DATAOUT3,0x23,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT3	0x83

/*
 * AP_DATAOUT4,0x24,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT4	0x84

/*
 * AP_DATAOUT5,0x25,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT5	0x85

/*
 * AP_DATAOUT6,0x26,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT6	0x86

/*
 * AP_DATAOUT7,0x27,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT7	0x87

/*
 * AP_DATAOUT8,0x28,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT8	0x88

/*
 * AP_DATAOUT_OPCODE,0x29,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAOUT_OPCODE	0x89

/*
 * AP_DATAIN0,0x51,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN0	0xb1

/*
 * AP_DATAIN1,0x52,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN1	0xb2

/*
 * AP_DATAIN2,0x53,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN2	0xb3

/*
 * AP_DATAIN3,0x54,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN3	0xb4

/*
 * AP_DATAIN4,0x55,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN4	0xb5

/*
 * AP_DATAIN5,0x56,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN5	0xb6

/*
 * AP_DATAIN6,0x57,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN6	0xb7

/*
 * AP_DATAIN7,0x58,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN7	0xb8

/*
 * AP_DATAIN8,0x59,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN8	0xb9

/*
 * AP_DATAIN9,0x5A,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN9	0xba

/*
 * AP_DATAIN10,0x5B,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN10	0xbb

/*
 * AP_DATAIN11,0x5C,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_AP_DATAIN11	0xbc

/*
 * COMMAND_HW,0x80,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_COMMAND_HW	0xe0
/*
 * Section: GPIO 0xE8 8
 */


/*
 * SGPIO_INT,0x00,0b00000000,0x0,Reset_Type:S
 * SGPIO0_STS,SGPIO1_STS,SGPIO2_STS,SGPIO3_STS,SGPIO4_STS,SGPIO5_STS,
 * SGPIO6_STS,SGPIO7_STS
 */
#define MAX77779_PMICSGPIO_INT	0xe8
#define MAX77779_PMICSGPIO_INT_SGPIO0_STS_SHIFT	0
#define MAX77779_PMICSGPIO_INT_SGPIO0_STS_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_INT_SGPIO0_STS_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_INT_SGPIO1_STS_SHIFT	1
#define MAX77779_PMICSGPIO_INT_SGPIO1_STS_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_INT_SGPIO1_STS_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_INT_SGPIO2_STS_SHIFT	2
#define MAX77779_PMICSGPIO_INT_SGPIO2_STS_MASK	(0x1 << 2)
#define MAX77779_PMICSGPIO_INT_SGPIO2_STS_CLEAR	(~(0x1 << 2))
#define MAX77779_PMICSGPIO_INT_SGPIO3_STS_SHIFT	3
#define MAX77779_PMICSGPIO_INT_SGPIO3_STS_MASK	(0x1 << 3)
#define MAX77779_PMICSGPIO_INT_SGPIO3_STS_CLEAR	(~(0x1 << 3))
#define MAX77779_PMICSGPIO_INT_SGPIO4_STS_SHIFT	4
#define MAX77779_PMICSGPIO_INT_SGPIO4_STS_MASK	(0x1 << 4)
#define MAX77779_PMICSGPIO_INT_SGPIO4_STS_CLEAR	(~(0x1 << 4))
#define MAX77779_PMICSGPIO_INT_SGPIO5_STS_SHIFT	5
#define MAX77779_PMICSGPIO_INT_SGPIO5_STS_MASK	(0x1 << 5)
#define MAX77779_PMICSGPIO_INT_SGPIO5_STS_CLEAR	(~(0x1 << 5))
#define MAX77779_PMICSGPIO_INT_SGPIO6_STS_SHIFT	6
#define MAX77779_PMICSGPIO_INT_SGPIO6_STS_MASK	(0x1 << 6)
#define MAX77779_PMICSGPIO_INT_SGPIO6_STS_CLEAR	(~(0x1 << 6))
#define MAX77779_PMICSGPIO_INT_SGPIO7_STS_SHIFT	7
#define MAX77779_PMICSGPIO_INT_SGPIO7_STS_MASK	(0x1 << 7)
#define MAX77779_PMICSGPIO_INT_SGPIO7_STS_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_pmicsgpio_int_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SGPIO0_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO0_STS, val));
	i += scnprintf(&buff[i], len - i, " SGPIO1_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO1_STS, val));
	i += scnprintf(&buff[i], len - i, " SGPIO2_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO2_STS, val));
	i += scnprintf(&buff[i], len - i, " SGPIO3_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO3_STS, val));
	i += scnprintf(&buff[i], len - i, " SGPIO4_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO4_STS, val));
	i += scnprintf(&buff[i], len - i, " SGPIO5_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO5_STS, val));
	i += scnprintf(&buff[i], len - i, " SGPIO6_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO6_STS, val));
	i += scnprintf(&buff[i], len - i, " SGPIO7_STS=%x",
		FIELD2VALUE(MAX77779_SGPIO7_STS, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_int_sgpio0_sts,0,0)
MAX77779_BFF(max77779_pmicsgpio_int_sgpio1_sts,1,1)
MAX77779_BFF(max77779_pmicsgpio_int_sgpio2_sts,2,2)
MAX77779_BFF(max77779_pmicsgpio_int_sgpio3_sts,3,3)
MAX77779_BFF(max77779_pmicsgpio_int_sgpio4_sts,4,4)
MAX77779_BFF(max77779_pmicsgpio_int_sgpio5_sts,5,5)
MAX77779_BFF(max77779_pmicsgpio_int_sgpio6_sts,6,6)
MAX77779_BFF(max77779_pmicsgpio_int_sgpio7_sts,7,7)

/*
 * SGPIO_PU,0x01,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * PU0,PU1,PU2,PU3,PU4,PU5,PU6,PU7
 */
#define MAX77779_PMICSGPIO_PU	0xe9
#define MAX77779_PMICSGPIO_PU_PU0_SHIFT	0
#define MAX77779_PMICSGPIO_PU_PU0_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_PU_PU0_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_PU_PU1_SHIFT	1
#define MAX77779_PMICSGPIO_PU_PU1_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_PU_PU1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_PU_PU2_SHIFT	2
#define MAX77779_PMICSGPIO_PU_PU2_MASK	(0x1 << 2)
#define MAX77779_PMICSGPIO_PU_PU2_CLEAR	(~(0x1 << 2))
#define MAX77779_PMICSGPIO_PU_PU3_SHIFT	3
#define MAX77779_PMICSGPIO_PU_PU3_MASK	(0x1 << 3)
#define MAX77779_PMICSGPIO_PU_PU3_CLEAR	(~(0x1 << 3))
#define MAX77779_PMICSGPIO_PU_PU4_SHIFT	4
#define MAX77779_PMICSGPIO_PU_PU4_MASK	(0x1 << 4)
#define MAX77779_PMICSGPIO_PU_PU4_CLEAR	(~(0x1 << 4))
#define MAX77779_PMICSGPIO_PU_PU5_SHIFT	5
#define MAX77779_PMICSGPIO_PU_PU5_MASK	(0x1 << 5)
#define MAX77779_PMICSGPIO_PU_PU5_CLEAR	(~(0x1 << 5))
#define MAX77779_PMICSGPIO_PU_PU6_SHIFT	6
#define MAX77779_PMICSGPIO_PU_PU6_MASK	(0x1 << 6)
#define MAX77779_PMICSGPIO_PU_PU6_CLEAR	(~(0x1 << 6))
#define MAX77779_PMICSGPIO_PU_PU7_SHIFT	7
#define MAX77779_PMICSGPIO_PU_PU7_MASK	(0x1 << 7)
#define MAX77779_PMICSGPIO_PU_PU7_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_pmicsgpio_pu_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " PU0=%x",
		FIELD2VALUE(MAX77779_PU0, val));
	i += scnprintf(&buff[i], len - i, " PU1=%x",
		FIELD2VALUE(MAX77779_PU1, val));
	i += scnprintf(&buff[i], len - i, " PU2=%x",
		FIELD2VALUE(MAX77779_PU2, val));
	i += scnprintf(&buff[i], len - i, " PU3=%x",
		FIELD2VALUE(MAX77779_PU3, val));
	i += scnprintf(&buff[i], len - i, " PU4=%x",
		FIELD2VALUE(MAX77779_PU4, val));
	i += scnprintf(&buff[i], len - i, " PU5=%x",
		FIELD2VALUE(MAX77779_PU5, val));
	i += scnprintf(&buff[i], len - i, " PU6=%x",
		FIELD2VALUE(MAX77779_PU6, val));
	i += scnprintf(&buff[i], len - i, " PU7=%x",
		FIELD2VALUE(MAX77779_PU7, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_pu_pu0,0,0)
MAX77779_BFF(max77779_pmicsgpio_pu_pu1,1,1)
MAX77779_BFF(max77779_pmicsgpio_pu_pu2,2,2)
MAX77779_BFF(max77779_pmicsgpio_pu_pu3,3,3)
MAX77779_BFF(max77779_pmicsgpio_pu_pu4,4,4)
MAX77779_BFF(max77779_pmicsgpio_pu_pu5,5,5)
MAX77779_BFF(max77779_pmicsgpio_pu_pu6,6,6)
MAX77779_BFF(max77779_pmicsgpio_pu_pu7,7,7)

/*
 * SGPIO_PD,0x02,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * PD0,PD1,PD2,PD3,PD4,PD5,PD6,PD7
 */
#define MAX77779_PMICSGPIO_PD	0xea
#define MAX77779_PMICSGPIO_PD_PD0_SHIFT	0
#define MAX77779_PMICSGPIO_PD_PD0_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_PD_PD0_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_PD_PD1_SHIFT	1
#define MAX77779_PMICSGPIO_PD_PD1_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_PD_PD1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_PD_PD2_SHIFT	2
#define MAX77779_PMICSGPIO_PD_PD2_MASK	(0x1 << 2)
#define MAX77779_PMICSGPIO_PD_PD2_CLEAR	(~(0x1 << 2))
#define MAX77779_PMICSGPIO_PD_PD3_SHIFT	3
#define MAX77779_PMICSGPIO_PD_PD3_MASK	(0x1 << 3)
#define MAX77779_PMICSGPIO_PD_PD3_CLEAR	(~(0x1 << 3))
#define MAX77779_PMICSGPIO_PD_PD4_SHIFT	4
#define MAX77779_PMICSGPIO_PD_PD4_MASK	(0x1 << 4)
#define MAX77779_PMICSGPIO_PD_PD4_CLEAR	(~(0x1 << 4))
#define MAX77779_PMICSGPIO_PD_PD5_SHIFT	5
#define MAX77779_PMICSGPIO_PD_PD5_MASK	(0x1 << 5)
#define MAX77779_PMICSGPIO_PD_PD5_CLEAR	(~(0x1 << 5))
#define MAX77779_PMICSGPIO_PD_PD6_SHIFT	6
#define MAX77779_PMICSGPIO_PD_PD6_MASK	(0x1 << 6)
#define MAX77779_PMICSGPIO_PD_PD6_CLEAR	(~(0x1 << 6))
#define MAX77779_PMICSGPIO_PD_PD7_SHIFT	7
#define MAX77779_PMICSGPIO_PD_PD7_MASK	(0x1 << 7)
#define MAX77779_PMICSGPIO_PD_PD7_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_pmicsgpio_pd_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " PD0=%x",
		FIELD2VALUE(MAX77779_PD0, val));
	i += scnprintf(&buff[i], len - i, " PD1=%x",
		FIELD2VALUE(MAX77779_PD1, val));
	i += scnprintf(&buff[i], len - i, " PD2=%x",
		FIELD2VALUE(MAX77779_PD2, val));
	i += scnprintf(&buff[i], len - i, " PD3=%x",
		FIELD2VALUE(MAX77779_PD3, val));
	i += scnprintf(&buff[i], len - i, " PD4=%x",
		FIELD2VALUE(MAX77779_PD4, val));
	i += scnprintf(&buff[i], len - i, " PD5=%x",
		FIELD2VALUE(MAX77779_PD5, val));
	i += scnprintf(&buff[i], len - i, " PD6=%x",
		FIELD2VALUE(MAX77779_PD6, val));
	i += scnprintf(&buff[i], len - i, " PD7=%x",
		FIELD2VALUE(MAX77779_PD7, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_pd_pd0,0,0)
MAX77779_BFF(max77779_pmicsgpio_pd_pd1,1,1)
MAX77779_BFF(max77779_pmicsgpio_pd_pd2,2,2)
MAX77779_BFF(max77779_pmicsgpio_pd_pd3,3,3)
MAX77779_BFF(max77779_pmicsgpio_pd_pd4,4,4)
MAX77779_BFF(max77779_pmicsgpio_pd_pd5,5,5)
MAX77779_BFF(max77779_pmicsgpio_pd_pd6,6,6)
MAX77779_BFF(max77779_pmicsgpio_pd_pd7,7,7)

/*
 * AGPIO_PU,0x03,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * PU0,PU1,PU2,PU3,SPR_7_4[4:4]
 */
#define MAX77779_PMICAGPIO_PU	0xeb
#define MAX77779_PMICAGPIO_PU_PU0_SHIFT	0
#define MAX77779_PMICAGPIO_PU_PU0_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_PU_PU0_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_PU_PU1_SHIFT	1
#define MAX77779_PMICAGPIO_PU_PU1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_PU_PU1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_PU_PU2_SHIFT	2
#define MAX77779_PMICAGPIO_PU_PU2_MASK	(0x1 << 2)
#define MAX77779_PMICAGPIO_PU_PU2_CLEAR	(~(0x1 << 2))
#define MAX77779_PMICAGPIO_PU_PU3_SHIFT	3
#define MAX77779_PMICAGPIO_PU_PU3_MASK	(0x1 << 3)
#define MAX77779_PMICAGPIO_PU_PU3_CLEAR	(~(0x1 << 3))
#define MAX77779_PMICAGPIO_PU_SPR_7_4_SHIFT	4
#define MAX77779_PMICAGPIO_PU_SPR_7_4_MASK	(0xf << 4)
#define MAX77779_PMICAGPIO_PU_SPR_7_4_CLEAR	(~(0xf << 4))
static inline const char *
max77779_pmicagpio_pu_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " PU0=%x",
		FIELD2VALUE(MAX77779_PU0, val));
	i += scnprintf(&buff[i], len - i, " PU1=%x",
		FIELD2VALUE(MAX77779_PU1, val));
	i += scnprintf(&buff[i], len - i, " PU2=%x",
		FIELD2VALUE(MAX77779_PU2, val));
	i += scnprintf(&buff[i], len - i, " PU3=%x",
		FIELD2VALUE(MAX77779_PU3, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_4=%x",
		FIELD2VALUE(MAX77779_SPR_7_4, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_pu_pu0,0,0)
MAX77779_BFF(max77779_pmicagpio_pu_pu1,1,1)
MAX77779_BFF(max77779_pmicagpio_pu_pu2,2,2)
MAX77779_BFF(max77779_pmicagpio_pu_pu3,3,3)
MAX77779_BFF(max77779_pmicagpio_pu_spr_7_4,7,4)

/*
 * AGPIO_PD,0x04,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * PD0,PD1,PD2,PD3,SPR_7_4[4:4]
 */
#define MAX77779_PMICAGPIO_PD	0xec
#define MAX77779_PMICAGPIO_PD_PD0_SHIFT	0
#define MAX77779_PMICAGPIO_PD_PD0_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_PD_PD0_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_PD_PD1_SHIFT	1
#define MAX77779_PMICAGPIO_PD_PD1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_PD_PD1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_PD_PD2_SHIFT	2
#define MAX77779_PMICAGPIO_PD_PD2_MASK	(0x1 << 2)
#define MAX77779_PMICAGPIO_PD_PD2_CLEAR	(~(0x1 << 2))
#define MAX77779_PMICAGPIO_PD_PD3_SHIFT	3
#define MAX77779_PMICAGPIO_PD_PD3_MASK	(0x1 << 3)
#define MAX77779_PMICAGPIO_PD_PD3_CLEAR	(~(0x1 << 3))
#define MAX77779_PMICAGPIO_PD_SPR_7_4_SHIFT	4
#define MAX77779_PMICAGPIO_PD_SPR_7_4_MASK	(0xf << 4)
#define MAX77779_PMICAGPIO_PD_SPR_7_4_CLEAR	(~(0xf << 4))
static inline const char *
max77779_pmicagpio_pd_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " PD0=%x",
		FIELD2VALUE(MAX77779_PD0, val));
	i += scnprintf(&buff[i], len - i, " PD1=%x",
		FIELD2VALUE(MAX77779_PD1, val));
	i += scnprintf(&buff[i], len - i, " PD2=%x",
		FIELD2VALUE(MAX77779_PD2, val));
	i += scnprintf(&buff[i], len - i, " PD3=%x",
		FIELD2VALUE(MAX77779_PD3, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_4=%x",
		FIELD2VALUE(MAX77779_SPR_7_4, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_pd_pd0,0,0)
MAX77779_BFF(max77779_pmicagpio_pd_pd1,1,1)
MAX77779_BFF(max77779_pmicagpio_pd_pd2,2,2)
MAX77779_BFF(max77779_pmicagpio_pd_pd3,3,3)
MAX77779_BFF(max77779_pmicagpio_pd_spr_7_4,7,4)

/*
 * SGPIO_CNFG0,0x05,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG0	0xed
#define MAX77779_PMICSGPIO_CNFG0_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG0_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG0_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG0_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG0_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG0_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG0_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG0_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG0_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG0_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG0_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG0_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG0_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG0_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG0_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg0_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg0_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg0_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg0_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg0_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg0_irq_sel,7,6)

/*
 * SGPIO_CNFG1,0x06,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG1	0xee
#define MAX77779_PMICSGPIO_CNFG1_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG1_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG1_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG1_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG1_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG1_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG1_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG1_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG1_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG1_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG1_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG1_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG1_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG1_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG1_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg1_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg1_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg1_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg1_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg1_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg1_irq_sel,7,6)

/*
 * SGPIO_CNFG2,0x07,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG2	0xef
#define MAX77779_PMICSGPIO_CNFG2_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG2_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG2_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG2_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG2_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG2_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG2_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG2_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG2_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG2_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG2_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG2_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG2_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG2_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG2_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg2_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg2_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg2_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg2_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg2_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg2_irq_sel,7,6)

/*
 * SGPIO_CNFG3,0x08,0b00000100,0x4,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG3	0xf0
#define MAX77779_PMICSGPIO_CNFG3_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG3_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG3_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG3_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG3_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG3_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG3_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG3_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG3_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG3_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG3_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG3_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG3_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG3_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG3_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg3_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg3_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg3_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg3_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg3_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg3_irq_sel,7,6)

/*
 * SGPIO_CNFG4,0x09,0b00000100,0x4,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG4	0xf1
#define MAX77779_PMICSGPIO_CNFG4_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG4_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG4_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG4_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG4_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG4_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG4_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG4_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG4_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG4_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG4_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG4_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG4_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG4_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG4_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg4_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg4_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg4_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg4_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg4_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg4_irq_sel,7,6)

/*
 * SGPIO_CNFG5,0x0A,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG5	0xf2
#define MAX77779_PMICSGPIO_CNFG5_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG5_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG5_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG5_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG5_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG5_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG5_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG5_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG5_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG5_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG5_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG5_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG5_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG5_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG5_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg5_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg5_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg5_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg5_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg5_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg5_irq_sel,7,6)

/*
 * SGPIO_CNFG6,0x0B,0b00000100,0x4,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG6	0xf3
#define MAX77779_PMICSGPIO_CNFG6_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG6_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG6_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG6_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG6_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG6_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG6_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG6_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG6_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG6_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG6_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG6_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG6_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG6_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG6_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg6_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg6_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg6_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg6_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg6_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg6_irq_sel,7,6)

/*
 * SGPIO_CNFG7,0x0C,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,VGPI_EN,MODE[2:2],DBNC_SEL[4:2],IRQ_SEL[6:2]
 */
#define MAX77779_PMICSGPIO_CNFG7	0xf4
#define MAX77779_PMICSGPIO_CNFG7_DATA_SHIFT	0
#define MAX77779_PMICSGPIO_CNFG7_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICSGPIO_CNFG7_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICSGPIO_CNFG7_VGPI_EN_SHIFT	1
#define MAX77779_PMICSGPIO_CNFG7_VGPI_EN_MASK	(0x1 << 1)
#define MAX77779_PMICSGPIO_CNFG7_VGPI_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICSGPIO_CNFG7_MODE_SHIFT	2
#define MAX77779_PMICSGPIO_CNFG7_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICSGPIO_CNFG7_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICSGPIO_CNFG7_DBNC_SEL_SHIFT	4
#define MAX77779_PMICSGPIO_CNFG7_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICSGPIO_CNFG7_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICSGPIO_CNFG7_IRQ_SEL_SHIFT	6
#define MAX77779_PMICSGPIO_CNFG7_IRQ_SEL_MASK	(0x3 << 6)
#define MAX77779_PMICSGPIO_CNFG7_IRQ_SEL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicsgpio_cnfg7_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " VGPI_EN=%x",
		FIELD2VALUE(MAX77779_VGPI_EN, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " IRQ_SEL=%x",
		FIELD2VALUE(MAX77779_IRQ_SEL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicsgpio_cnfg7_data,0,0)
MAX77779_BFF(max77779_pmicsgpio_cnfg7_vgpi_en,1,1)
MAX77779_BFF(max77779_pmicsgpio_cnfg7_mode,3,2)
MAX77779_BFF(max77779_pmicsgpio_cnfg7_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicsgpio_cnfg7_irq_sel,7,6)

/*
 * AGPIO_CNFG0,0x0D,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,SPR_1,MODE[2:2],DBNC_SEL[4:2],RSVD_7_6[6:2]
 */
#define MAX77779_PMICAGPIO_CNFG0	0xf5
#define MAX77779_PMICAGPIO_CNFG0_DATA_SHIFT	0
#define MAX77779_PMICAGPIO_CNFG0_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_CNFG0_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_CNFG0_SPR_1_SHIFT	1
#define MAX77779_PMICAGPIO_CNFG0_SPR_1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_CNFG0_SPR_1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_CNFG0_MODE_SHIFT	2
#define MAX77779_PMICAGPIO_CNFG0_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICAGPIO_CNFG0_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICAGPIO_CNFG0_DBNC_SEL_SHIFT	4
#define MAX77779_PMICAGPIO_CNFG0_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICAGPIO_CNFG0_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICAGPIO_CNFG0_RSVD_7_6_SHIFT	6
#define MAX77779_PMICAGPIO_CNFG0_RSVD_7_6_MASK	(0x3 << 6)
#define MAX77779_PMICAGPIO_CNFG0_RSVD_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicagpio_cnfg0_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " SPR_1=%x",
		FIELD2VALUE(MAX77779_SPR_1, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " RSVD_7_6=%x",
		FIELD2VALUE(MAX77779_RSVD_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_cnfg0_data,0,0)
MAX77779_BFF(max77779_pmicagpio_cnfg0_spr_1,1,1)
MAX77779_BFF(max77779_pmicagpio_cnfg0_mode,3,2)
MAX77779_BFF(max77779_pmicagpio_cnfg0_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicagpio_cnfg0_rsvd_7_6,7,6)

/*
 * AGPIO_CNFG1,0x0E,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,SPR_1,MODE[2:2],DBNC_SEL[4:2],RSVD_7_6[6:2]
 */
#define MAX77779_PMICAGPIO_CNFG1	0xf6
#define MAX77779_PMICAGPIO_CNFG1_DATA_SHIFT	0
#define MAX77779_PMICAGPIO_CNFG1_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_CNFG1_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_CNFG1_SPR_1_SHIFT	1
#define MAX77779_PMICAGPIO_CNFG1_SPR_1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_CNFG1_SPR_1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_CNFG1_MODE_SHIFT	2
#define MAX77779_PMICAGPIO_CNFG1_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICAGPIO_CNFG1_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICAGPIO_CNFG1_DBNC_SEL_SHIFT	4
#define MAX77779_PMICAGPIO_CNFG1_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICAGPIO_CNFG1_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICAGPIO_CNFG1_RSVD_7_6_SHIFT	6
#define MAX77779_PMICAGPIO_CNFG1_RSVD_7_6_MASK	(0x3 << 6)
#define MAX77779_PMICAGPIO_CNFG1_RSVD_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicagpio_cnfg1_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " SPR_1=%x",
		FIELD2VALUE(MAX77779_SPR_1, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " RSVD_7_6=%x",
		FIELD2VALUE(MAX77779_RSVD_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_cnfg1_data,0,0)
MAX77779_BFF(max77779_pmicagpio_cnfg1_spr_1,1,1)
MAX77779_BFF(max77779_pmicagpio_cnfg1_mode,3,2)
MAX77779_BFF(max77779_pmicagpio_cnfg1_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicagpio_cnfg1_rsvd_7_6,7,6)

/*
 * AGPIO_CNFG2,0x0F,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,SPR_1,MODE[2:2],DBNC_SEL[4:2],RSVD_7_6[6:2]
 */
#define MAX77779_PMICAGPIO_CNFG2	0xf7
#define MAX77779_PMICAGPIO_CNFG2_DATA_SHIFT	0
#define MAX77779_PMICAGPIO_CNFG2_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_CNFG2_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_CNFG2_SPR_1_SHIFT	1
#define MAX77779_PMICAGPIO_CNFG2_SPR_1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_CNFG2_SPR_1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_CNFG2_MODE_SHIFT	2
#define MAX77779_PMICAGPIO_CNFG2_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICAGPIO_CNFG2_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICAGPIO_CNFG2_DBNC_SEL_SHIFT	4
#define MAX77779_PMICAGPIO_CNFG2_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICAGPIO_CNFG2_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICAGPIO_CNFG2_RSVD_7_6_SHIFT	6
#define MAX77779_PMICAGPIO_CNFG2_RSVD_7_6_MASK	(0x3 << 6)
#define MAX77779_PMICAGPIO_CNFG2_RSVD_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicagpio_cnfg2_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " SPR_1=%x",
		FIELD2VALUE(MAX77779_SPR_1, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " RSVD_7_6=%x",
		FIELD2VALUE(MAX77779_RSVD_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_cnfg2_data,0,0)
MAX77779_BFF(max77779_pmicagpio_cnfg2_spr_1,1,1)
MAX77779_BFF(max77779_pmicagpio_cnfg2_mode,3,2)
MAX77779_BFF(max77779_pmicagpio_cnfg2_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicagpio_cnfg2_rsvd_7_6,7,6)

/*
 * AGPIO_CNFG3,0x10,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,SPR_1,MODE[2:2],DBNC_SEL[4:2],RSVD_7_6[6:2]
 */
#define MAX77779_PMICAGPIO_CNFG3	0xf8
#define MAX77779_PMICAGPIO_CNFG3_DATA_SHIFT	0
#define MAX77779_PMICAGPIO_CNFG3_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_CNFG3_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_CNFG3_SPR_1_SHIFT	1
#define MAX77779_PMICAGPIO_CNFG3_SPR_1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_CNFG3_SPR_1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_CNFG3_MODE_SHIFT	2
#define MAX77779_PMICAGPIO_CNFG3_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICAGPIO_CNFG3_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICAGPIO_CNFG3_DBNC_SEL_SHIFT	4
#define MAX77779_PMICAGPIO_CNFG3_DBNC_SEL_MASK	(0x3 << 4)
#define MAX77779_PMICAGPIO_CNFG3_DBNC_SEL_CLEAR	(~(0x3 << 4))
#define MAX77779_PMICAGPIO_CNFG3_RSVD_7_6_SHIFT	6
#define MAX77779_PMICAGPIO_CNFG3_RSVD_7_6_MASK	(0x3 << 6)
#define MAX77779_PMICAGPIO_CNFG3_RSVD_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_pmicagpio_cnfg3_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " SPR_1=%x",
		FIELD2VALUE(MAX77779_SPR_1, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " DBNC_SEL=%x",
		FIELD2VALUE(MAX77779_DBNC_SEL, val));
	i += scnprintf(&buff[i], len - i, " RSVD_7_6=%x",
		FIELD2VALUE(MAX77779_RSVD_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_cnfg3_data,0,0)
MAX77779_BFF(max77779_pmicagpio_cnfg3_spr_1,1,1)
MAX77779_BFF(max77779_pmicagpio_cnfg3_mode,3,2)
MAX77779_BFF(max77779_pmicagpio_cnfg3_dbnc_sel,5,4)
MAX77779_BFF(max77779_pmicagpio_cnfg3_rsvd_7_6,7,6)

/*
 * AGPIO_CNFG4,0x11,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,SPR_1,MODE[2:2],RSVD_7_4[4:4]
 */
#define MAX77779_PMICAGPIO_CNFG4	0xf9
#define MAX77779_PMICAGPIO_CNFG4_DATA_SHIFT	0
#define MAX77779_PMICAGPIO_CNFG4_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_CNFG4_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_CNFG4_SPR_1_SHIFT	1
#define MAX77779_PMICAGPIO_CNFG4_SPR_1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_CNFG4_SPR_1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_CNFG4_MODE_SHIFT	2
#define MAX77779_PMICAGPIO_CNFG4_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICAGPIO_CNFG4_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICAGPIO_CNFG4_RSVD_7_4_SHIFT	4
#define MAX77779_PMICAGPIO_CNFG4_RSVD_7_4_MASK	(0xf << 4)
#define MAX77779_PMICAGPIO_CNFG4_RSVD_7_4_CLEAR	(~(0xf << 4))
static inline const char *
max77779_pmicagpio_cnfg4_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " SPR_1=%x",
		FIELD2VALUE(MAX77779_SPR_1, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " RSVD_7_4=%x",
		FIELD2VALUE(MAX77779_RSVD_7_4, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_cnfg4_data,0,0)
MAX77779_BFF(max77779_pmicagpio_cnfg4_spr_1,1,1)
MAX77779_BFF(max77779_pmicagpio_cnfg4_mode,3,2)
MAX77779_BFF(max77779_pmicagpio_cnfg4_rsvd_7_4,7,4)

/*
 * AGPIO_CNFG5,0x12,0b00000000,0x0,OTP:SHADOW, Reset_Type:S
 * DATA,SPR_1,MODE[2:2],RSVD_7_4[4:4]
 */
#define MAX77779_PMICAGPIO_CNFG5	0xfa
#define MAX77779_PMICAGPIO_CNFG5_DATA_SHIFT	0
#define MAX77779_PMICAGPIO_CNFG5_DATA_MASK	(0x1 << 0)
#define MAX77779_PMICAGPIO_CNFG5_DATA_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICAGPIO_CNFG5_SPR_1_SHIFT	1
#define MAX77779_PMICAGPIO_CNFG5_SPR_1_MASK	(0x1 << 1)
#define MAX77779_PMICAGPIO_CNFG5_SPR_1_CLEAR	(~(0x1 << 1))
#define MAX77779_PMICAGPIO_CNFG5_MODE_SHIFT	2
#define MAX77779_PMICAGPIO_CNFG5_MODE_MASK	(0x3 << 2)
#define MAX77779_PMICAGPIO_CNFG5_MODE_CLEAR	(~(0x3 << 2))
#define MAX77779_PMICAGPIO_CNFG5_RSVD_7_4_SHIFT	4
#define MAX77779_PMICAGPIO_CNFG5_RSVD_7_4_MASK	(0xf << 4)
#define MAX77779_PMICAGPIO_CNFG5_RSVD_7_4_CLEAR	(~(0xf << 4))
static inline const char *
max77779_pmicagpio_cnfg5_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DATA=%x",
		FIELD2VALUE(MAX77779_DATA, val));
	i += scnprintf(&buff[i], len - i, " SPR_1=%x",
		FIELD2VALUE(MAX77779_SPR_1, val));
	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " RSVD_7_4=%x",
		FIELD2VALUE(MAX77779_RSVD_7_4, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicagpio_cnfg5_data,0,0)
MAX77779_BFF(max77779_pmicagpio_cnfg5_spr_1,1,1)
MAX77779_BFF(max77779_pmicagpio_cnfg5_mode,3,2)
MAX77779_BFF(max77779_pmicagpio_cnfg5_rsvd_7_4,7,4)

/*
 * VGPI_CNFG,0x13,0b00000001,0x1,Reset_Type:S
 * VGPI_PR,SPR_7_1[1:7]
 */
#define MAX77779_PMICVGPI_CNFG	0xfb
#define MAX77779_PMICVGPI_CNFG_VGPI_PR_SHIFT	0
#define MAX77779_PMICVGPI_CNFG_VGPI_PR_MASK	(0x1 << 0)
#define MAX77779_PMICVGPI_CNFG_VGPI_PR_CLEAR	(~(0x1 << 0))
#define MAX77779_PMICVGPI_CNFG_SPR_7_1_SHIFT	1
#define MAX77779_PMICVGPI_CNFG_SPR_7_1_MASK	(0x7f << 1)
#define MAX77779_PMICVGPI_CNFG_SPR_7_1_CLEAR	(~(0x7f << 1))
static inline const char *
max77779_pmicvgpi_cnfg_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " VGPI_PR=%x",
		FIELD2VALUE(MAX77779_VGPI_PR, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_1=%x",
		FIELD2VALUE(MAX77779_SPR_7_1, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_pmicvgpi_cnfg_vgpi_pr,0,0)
MAX77779_BFF(max77779_pmicvgpi_cnfg_spr_7_1,7,1)
/*
 * Section: JEITA_FUNC 0x00 8
 */


/*
 * CHGIN_I_ADC_L,0x00,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCHGIN_I_ADC_L	0x00

/*
 * CHGIN_I_ADC_H,0x01,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCHGIN_I_ADC_H	0x01

/*
 * CHGIN_V_ADC_L,0x02,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCHGIN_V_ADC_L	0x02

/*
 * CHGIN_V_ADC_H,0x03,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCHGIN_V_ADC_H	0x03

/*
 * WCIN_I_ADC_L,0x04,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGWCIN_I_ADC_L	0x04

/*
 * WCIN_I_ADC_H,0x05,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGWCIN_I_ADC_H	0x05

/*
 * WCIN_V_ADC_L,0x06,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGWCIN_V_ADC_L	0x06

/*
 * WCIN_V_ADC_H,0x07,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGWCIN_V_ADC_H	0x07

/*
 * THM1_TEMP_L,0x08,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGTHM1_TEMP_L	0x08

/*
 * THM1_TEMP_H,0x09,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGTHM1_TEMP_H	0x09

/*
 * THM2_TEMP_L,0x0A,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGTHM2_TEMP_L	0x0a

/*
 * THM2_TEMP_H,0x0B,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGTHM2_TEMP_H	0x0b

/*
 * THM3_TEMP_L,0x0C,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGTHM3_TEMP_L	0x0c

/*
 * THM3_TEMP_H,0x0D,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGTHM3_TEMP_H	0x0d

/*
 * BATT_ID1_ADC_L,0x0E,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGBATT_ID1_ADC_L	0x0e

/*
 * BATT_ID1_ADC_H,0x0F,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGBATT_ID1_ADC_H	0x0f

/*
 * BATT_ID2_ADC_L,0x10,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGBATT_ID2_ADC_L	0x10

/*
 * BATT_ID2_ADC_H,0x11,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGBATT_ID2_ADC_H	0x11

/*
 * JEITA_CTRL,0x12,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 * IN_ADC_FORCE,THM2_TEMP_FORCE,THM3_CHECK_EN,BATT_ID1_DO,BATT_ID2_DO,SPR_7_5[5:3]
 */
#define MAX77779_CHGJEITA_CTRL	0x12
#define MAX77779_CHGJEITA_CTRL_IN_ADC_FORCE_SHIFT	0
#define MAX77779_CHGJEITA_CTRL_IN_ADC_FORCE_MASK	(0x1 << 0)
#define MAX77779_CHGJEITA_CTRL_IN_ADC_FORCE_CLEAR	(~(0x1 << 0))
#define MAX77779_CHGJEITA_CTRL_THM2_TEMP_FORCE_SHIFT	1
#define MAX77779_CHGJEITA_CTRL_THM2_TEMP_FORCE_MASK	(0x1 << 1)
#define MAX77779_CHGJEITA_CTRL_THM2_TEMP_FORCE_CLEAR	(~(0x1 << 1))
#define MAX77779_CHGJEITA_CTRL_THM3_CHECK_EN_SHIFT	2
#define MAX77779_CHGJEITA_CTRL_THM3_CHECK_EN_MASK	(0x1 << 2)
#define MAX77779_CHGJEITA_CTRL_THM3_CHECK_EN_CLEAR	(~(0x1 << 2))
#define MAX77779_CHGJEITA_CTRL_BATT_ID1_DO_SHIFT	3
#define MAX77779_CHGJEITA_CTRL_BATT_ID1_DO_MASK	(0x1 << 3)
#define MAX77779_CHGJEITA_CTRL_BATT_ID1_DO_CLEAR	(~(0x1 << 3))
#define MAX77779_CHGJEITA_CTRL_BATT_ID2_DO_SHIFT	4
#define MAX77779_CHGJEITA_CTRL_BATT_ID2_DO_MASK	(0x1 << 4)
#define MAX77779_CHGJEITA_CTRL_BATT_ID2_DO_CLEAR	(~(0x1 << 4))
#define MAX77779_CHGJEITA_CTRL_SPR_7_5_SHIFT	5
#define MAX77779_CHGJEITA_CTRL_SPR_7_5_MASK	(0x7 << 5)
#define MAX77779_CHGJEITA_CTRL_SPR_7_5_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_chgjeita_ctrl_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " IN_ADC_FORCE=%x",
		FIELD2VALUE(MAX77779_IN_ADC_FORCE, val));
	i += scnprintf(&buff[i], len - i, " THM2_TEMP_FORCE=%x",
		FIELD2VALUE(MAX77779_THM2_TEMP_FORCE, val));
	i += scnprintf(&buff[i], len - i, " THM3_CHECK_EN=%x",
		FIELD2VALUE(MAX77779_THM3_CHECK_EN, val));
	i += scnprintf(&buff[i], len - i, " BATT_ID1_DO=%x",
		FIELD2VALUE(MAX77779_BATT_ID1_DO, val));
	i += scnprintf(&buff[i], len - i, " BATT_ID2_DO=%x",
		FIELD2VALUE(MAX77779_BATT_ID2_DO, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_5=%x",
		FIELD2VALUE(MAX77779_SPR_7_5, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chgjeita_ctrl_in_adc_force,0,0)
MAX77779_BFF(max77779_chgjeita_ctrl_thm2_temp_force,1,1)
MAX77779_BFF(max77779_chgjeita_ctrl_thm3_check_en,2,2)
MAX77779_BFF(max77779_chgjeita_ctrl_batt_id1_do,3,3)
MAX77779_BFF(max77779_chgjeita_ctrl_batt_id2_do,4,4)
MAX77779_BFF(max77779_chgjeita_ctrl_spr_7_5,7,5)

/*
 * JEITA_FLAGS,0x13,0b00000000,0x0,Reset_Type:S
 * CHGIN_I_ADC_ON,WCIN_I_ADC_ON,THM3_TEMP_ON,THM2_TEMP_ON,THM1_TEMP_ON,
 * BATT_ID2_ADC_OK,BATT_ID1_ADC_OK,SPR_7
 */
#define MAX77779_CHGJEITA_FLAGS	0x13
#define MAX77779_CHGJEITA_FLAGS_CHGIN_I_ADC_ON_SHIFT	0
#define MAX77779_CHGJEITA_FLAGS_CHGIN_I_ADC_ON_MASK	(0x1 << 0)
#define MAX77779_CHGJEITA_FLAGS_CHGIN_I_ADC_ON_CLEAR	(~(0x1 << 0))
#define MAX77779_CHGJEITA_FLAGS_WCIN_I_ADC_ON_SHIFT	1
#define MAX77779_CHGJEITA_FLAGS_WCIN_I_ADC_ON_MASK	(0x1 << 1)
#define MAX77779_CHGJEITA_FLAGS_WCIN_I_ADC_ON_CLEAR	(~(0x1 << 1))
#define MAX77779_CHGJEITA_FLAGS_THM3_TEMP_ON_SHIFT	2
#define MAX77779_CHGJEITA_FLAGS_THM3_TEMP_ON_MASK	(0x1 << 2)
#define MAX77779_CHGJEITA_FLAGS_THM3_TEMP_ON_CLEAR	(~(0x1 << 2))
#define MAX77779_CHGJEITA_FLAGS_THM2_TEMP_ON_SHIFT	3
#define MAX77779_CHGJEITA_FLAGS_THM2_TEMP_ON_MASK	(0x1 << 3)
#define MAX77779_CHGJEITA_FLAGS_THM2_TEMP_ON_CLEAR	(~(0x1 << 3))
#define MAX77779_CHGJEITA_FLAGS_THM1_TEMP_ON_SHIFT	4
#define MAX77779_CHGJEITA_FLAGS_THM1_TEMP_ON_MASK	(0x1 << 4)
#define MAX77779_CHGJEITA_FLAGS_THM1_TEMP_ON_CLEAR	(~(0x1 << 4))
#define MAX77779_CHGJEITA_FLAGS_BATT_ID2_ADC_OK_SHIFT	5
#define MAX77779_CHGJEITA_FLAGS_BATT_ID2_ADC_OK_MASK	(0x1 << 5)
#define MAX77779_CHGJEITA_FLAGS_BATT_ID2_ADC_OK_CLEAR	(~(0x1 << 5))
#define MAX77779_CHGJEITA_FLAGS_BATT_ID1_ADC_OK_SHIFT	6
#define MAX77779_CHGJEITA_FLAGS_BATT_ID1_ADC_OK_MASK	(0x1 << 6)
#define MAX77779_CHGJEITA_FLAGS_BATT_ID1_ADC_OK_CLEAR	(~(0x1 << 6))
#define MAX77779_CHGJEITA_FLAGS_SPR_7_SHIFT	7
#define MAX77779_CHGJEITA_FLAGS_SPR_7_MASK	(0x1 << 7)
#define MAX77779_CHGJEITA_FLAGS_SPR_7_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chgjeita_flags_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CHGIN_I_ADC_ON=%x",
		FIELD2VALUE(MAX77779_CHGIN_I_ADC_ON, val));
	i += scnprintf(&buff[i], len - i, " WCIN_I_ADC_ON=%x",
		FIELD2VALUE(MAX77779_WCIN_I_ADC_ON, val));
	i += scnprintf(&buff[i], len - i, " THM3_TEMP_ON=%x",
		FIELD2VALUE(MAX77779_THM3_TEMP_ON, val));
	i += scnprintf(&buff[i], len - i, " THM2_TEMP_ON=%x",
		FIELD2VALUE(MAX77779_THM2_TEMP_ON, val));
	i += scnprintf(&buff[i], len - i, " THM1_TEMP_ON=%x",
		FIELD2VALUE(MAX77779_THM1_TEMP_ON, val));
	i += scnprintf(&buff[i], len - i, " BATT_ID2_ADC_OK=%x",
		FIELD2VALUE(MAX77779_BATT_ID2_ADC_OK, val));
	i += scnprintf(&buff[i], len - i, " BATT_ID1_ADC_OK=%x",
		FIELD2VALUE(MAX77779_BATT_ID1_ADC_OK, val));
	i += scnprintf(&buff[i], len - i, " SPR_7=%x",
		FIELD2VALUE(MAX77779_SPR_7, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chgjeita_flags_chgin_i_adc_on,0,0)
MAX77779_BFF(max77779_chgjeita_flags_wcin_i_adc_on,1,1)
MAX77779_BFF(max77779_chgjeita_flags_thm3_temp_on,2,2)
MAX77779_BFF(max77779_chgjeita_flags_thm2_temp_on,3,3)
MAX77779_BFF(max77779_chgjeita_flags_thm1_temp_on,4,4)
MAX77779_BFF(max77779_chgjeita_flags_batt_id2_adc_ok,5,5)
MAX77779_BFF(max77779_chgjeita_flags_batt_id1_adc_ok,6,6)
MAX77779_BFF(max77779_chgjeita_flags_spr_7,7,7)

/*
 * COP_CTRL,0x20,0b00000000,0x0,Reset_Type:S
 * COP_EN,COP_LIMIT_WD_EN,SPR_7_2[2:6]
 */
#define MAX77779_CHGCOP_CTRL	0x20
#define MAX77779_CHGCOP_CTRL_COP_EN_SHIFT	0
#define MAX77779_CHGCOP_CTRL_COP_EN_MASK	(0x1 << 0)
#define MAX77779_CHGCOP_CTRL_COP_EN_CLEAR	(~(0x1 << 0))
#define MAX77779_CHGCOP_CTRL_COP_LIMIT_WD_EN_SHIFT	1
#define MAX77779_CHGCOP_CTRL_COP_LIMIT_WD_EN_MASK	(0x1 << 1)
#define MAX77779_CHGCOP_CTRL_COP_LIMIT_WD_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_CHGCOP_CTRL_SPR_7_2_SHIFT	2
#define MAX77779_CHGCOP_CTRL_SPR_7_2_MASK	(0x3f << 2)
#define MAX77779_CHGCOP_CTRL_SPR_7_2_CLEAR	(~(0x3f << 2))
static inline const char *
max77779_chgcop_ctrl_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " COP_EN=%x",
		FIELD2VALUE(MAX77779_COP_EN, val));
	i += scnprintf(&buff[i], len - i, " COP_LIMIT_WD_EN=%x",
		FIELD2VALUE(MAX77779_COP_LIMIT_WD_EN, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_2=%x",
		FIELD2VALUE(MAX77779_SPR_7_2, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chgcop_ctrl_cop_en,0,0)
MAX77779_BFF(max77779_chgcop_ctrl_cop_limit_wd_en,1,1)
MAX77779_BFF(max77779_chgcop_ctrl_spr_7_2,7,2)

/*
 * COP_DEBOUNCE,0x21,0b00000000,0x0,Reset_Type:S
 * COP_DB_TIME[0:3],SPR_7_3[3:5]
 */
#define MAX77779_CHGCOP_DEBOUNCE	0x21
#define MAX77779_CHGCOP_DEBOUNCE_COP_DB_TIME_SHIFT	0
#define MAX77779_CHGCOP_DEBOUNCE_COP_DB_TIME_MASK	(0x7 << 0)
#define MAX77779_CHGCOP_DEBOUNCE_COP_DB_TIME_CLEAR	(~(0x7 << 0))
#define MAX77779_CHGCOP_DEBOUNCE_SPR_7_3_SHIFT	3
#define MAX77779_CHGCOP_DEBOUNCE_SPR_7_3_MASK	(0x1f << 3)
#define MAX77779_CHGCOP_DEBOUNCE_SPR_7_3_CLEAR	(~(0x1f << 3))
static inline const char *
max77779_chgcop_debounce_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " COP_DB_TIME=%x",
		FIELD2VALUE(MAX77779_COP_DB_TIME, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_3=%x",
		FIELD2VALUE(MAX77779_SPR_7_3, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chgcop_debounce_cop_db_time,2,0)
MAX77779_BFF(max77779_chgcop_debounce_spr_7_3,7,3)

/*
 * COP_WARN_L,0x22,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCOP_WARN_L	0x22

/*
 * COP_WARN_H,0x23,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCOP_WARN_H	0x23

/*
 * COP_LIMIT_L,0x24,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCOP_LIMIT_L	0x24

/*
 * COP_LIMIT_H,0x25,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_CHGCOP_LIMIT_H	0x25
/*
 * Section: CHARGER_FUNC 0xB0 8
 */


/*
 * CHG_INT,0x00,0b00000000,0x0,Reset_Type:S
 * BYP_I,THM2_I,INLIM_I,BAT_I,CHG_I,WCIN_I,CHGIN_I,AICL_I
 */
#define MAX77779_CHG_INT	0xb0
#define MAX77779_CHG_INT_BYP_I_SHIFT	0
#define MAX77779_CHG_INT_BYP_I_MASK	(0x1 << 0)
#define MAX77779_CHG_INT_BYP_I_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_INT_THM2_I_SHIFT	1
#define MAX77779_CHG_INT_THM2_I_MASK	(0x1 << 1)
#define MAX77779_CHG_INT_THM2_I_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_INT_INLIM_I_SHIFT	2
#define MAX77779_CHG_INT_INLIM_I_MASK	(0x1 << 2)
#define MAX77779_CHG_INT_INLIM_I_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_INT_BAT_I_SHIFT	3
#define MAX77779_CHG_INT_BAT_I_MASK	(0x1 << 3)
#define MAX77779_CHG_INT_BAT_I_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_INT_CHG_I_SHIFT	4
#define MAX77779_CHG_INT_CHG_I_MASK	(0x1 << 4)
#define MAX77779_CHG_INT_CHG_I_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_INT_WCIN_I_SHIFT	5
#define MAX77779_CHG_INT_WCIN_I_MASK	(0x1 << 5)
#define MAX77779_CHG_INT_WCIN_I_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_INT_CHGIN_I_SHIFT	6
#define MAX77779_CHG_INT_CHGIN_I_MASK	(0x1 << 6)
#define MAX77779_CHG_INT_CHGIN_I_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_INT_AICL_I_SHIFT	7
#define MAX77779_CHG_INT_AICL_I_MASK	(0x1 << 7)
#define MAX77779_CHG_INT_AICL_I_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_int_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BYP_I=%x",
		FIELD2VALUE(MAX77779_BYP_I, val));
	i += scnprintf(&buff[i], len - i, " THM2_I=%x",
		FIELD2VALUE(MAX77779_THM2_I, val));
	i += scnprintf(&buff[i], len - i, " INLIM_I=%x",
		FIELD2VALUE(MAX77779_INLIM_I, val));
	i += scnprintf(&buff[i], len - i, " BAT_I=%x",
		FIELD2VALUE(MAX77779_BAT_I, val));
	i += scnprintf(&buff[i], len - i, " CHG_I=%x",
		FIELD2VALUE(MAX77779_CHG_I, val));
	i += scnprintf(&buff[i], len - i, " WCIN_I=%x",
		FIELD2VALUE(MAX77779_WCIN_I, val));
	i += scnprintf(&buff[i], len - i, " CHGIN_I=%x",
		FIELD2VALUE(MAX77779_CHGIN_I, val));
	i += scnprintf(&buff[i], len - i, " AICL_I=%x",
		FIELD2VALUE(MAX77779_AICL_I, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_int_byp_i,0,0)
MAX77779_BFF(max77779_chg_int_thm2_i,1,1)
MAX77779_BFF(max77779_chg_int_inlim_i,2,2)
MAX77779_BFF(max77779_chg_int_bat_i,3,3)
MAX77779_BFF(max77779_chg_int_chg_i,4,4)
MAX77779_BFF(max77779_chg_int_wcin_i,5,5)
MAX77779_BFF(max77779_chg_int_chgin_i,6,6)
MAX77779_BFF(max77779_chg_int_aicl_i,7,7)

/*
 * CHG_INT2,0x01,0b00000000,0x0,Reset_Type:S
 * CHG_STA_DONE_I,CHG_STA_TO_I,CHG_STA_CV_I,CHG_STA_CC_I,COP_WARN_I,COP_ALERT_I,
 * COP_LIMIT_WD_I,INSEL_I
 */
#define MAX77779_CHG_INT2	0xb1
#define MAX77779_CHG_INT2_CHG_STA_DONE_I_SHIFT	0
#define MAX77779_CHG_INT2_CHG_STA_DONE_I_MASK	(0x1 << 0)
#define MAX77779_CHG_INT2_CHG_STA_DONE_I_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_INT2_CHG_STA_TO_I_SHIFT	1
#define MAX77779_CHG_INT2_CHG_STA_TO_I_MASK	(0x1 << 1)
#define MAX77779_CHG_INT2_CHG_STA_TO_I_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_INT2_CHG_STA_CV_I_SHIFT	2
#define MAX77779_CHG_INT2_CHG_STA_CV_I_MASK	(0x1 << 2)
#define MAX77779_CHG_INT2_CHG_STA_CV_I_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_INT2_CHG_STA_CC_I_SHIFT	3
#define MAX77779_CHG_INT2_CHG_STA_CC_I_MASK	(0x1 << 3)
#define MAX77779_CHG_INT2_CHG_STA_CC_I_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_INT2_COP_WARN_I_SHIFT	4
#define MAX77779_CHG_INT2_COP_WARN_I_MASK	(0x1 << 4)
#define MAX77779_CHG_INT2_COP_WARN_I_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_INT2_COP_ALERT_I_SHIFT	5
#define MAX77779_CHG_INT2_COP_ALERT_I_MASK	(0x1 << 5)
#define MAX77779_CHG_INT2_COP_ALERT_I_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_INT2_COP_LIMIT_WD_I_SHIFT	6
#define MAX77779_CHG_INT2_COP_LIMIT_WD_I_MASK	(0x1 << 6)
#define MAX77779_CHG_INT2_COP_LIMIT_WD_I_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_INT2_INSEL_I_SHIFT	7
#define MAX77779_CHG_INT2_INSEL_I_MASK	(0x1 << 7)
#define MAX77779_CHG_INT2_INSEL_I_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_int2_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CHG_STA_DONE_I=%x",
		FIELD2VALUE(MAX77779_CHG_STA_DONE_I, val));
	i += scnprintf(&buff[i], len - i, " CHG_STA_TO_I=%x",
		FIELD2VALUE(MAX77779_CHG_STA_TO_I, val));
	i += scnprintf(&buff[i], len - i, " CHG_STA_CV_I=%x",
		FIELD2VALUE(MAX77779_CHG_STA_CV_I, val));
	i += scnprintf(&buff[i], len - i, " CHG_STA_CC_I=%x",
		FIELD2VALUE(MAX77779_CHG_STA_CC_I, val));
	i += scnprintf(&buff[i], len - i, " COP_WARN_I=%x",
		FIELD2VALUE(MAX77779_COP_WARN_I, val));
	i += scnprintf(&buff[i], len - i, " COP_ALERT_I=%x",
		FIELD2VALUE(MAX77779_COP_ALERT_I, val));
	i += scnprintf(&buff[i], len - i, " COP_LIMIT_WD_I=%x",
		FIELD2VALUE(MAX77779_COP_LIMIT_WD_I, val));
	i += scnprintf(&buff[i], len - i, " INSEL_I=%x",
		FIELD2VALUE(MAX77779_INSEL_I, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_int2_chg_sta_done_i,0,0)
MAX77779_BFF(max77779_chg_int2_chg_sta_to_i,1,1)
MAX77779_BFF(max77779_chg_int2_chg_sta_cv_i,2,2)
MAX77779_BFF(max77779_chg_int2_chg_sta_cc_i,3,3)
MAX77779_BFF(max77779_chg_int2_cop_warn_i,4,4)
MAX77779_BFF(max77779_chg_int2_cop_alert_i,5,5)
MAX77779_BFF(max77779_chg_int2_cop_limit_wd_i,6,6)
MAX77779_BFF(max77779_chg_int2_insel_i,7,7)

/*
 * CHG_INT_MASK,0x03,0b11111111,0xff,OTP:SHADOW, Reset_Type:O
 * BYP_M,THM2_M,INLIM_M,BAT_M,CHG_M,WCIN_M,CHGIN_M,AICL_M
 */
#define MAX77779_CHG_INT_MASK	0xb3
#define MAX77779_CHG_INT_MASK_BYP_M_SHIFT	0
#define MAX77779_CHG_INT_MASK_BYP_M_MASK	(0x1 << 0)
#define MAX77779_CHG_INT_MASK_BYP_M_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_INT_MASK_THM2_M_SHIFT	1
#define MAX77779_CHG_INT_MASK_THM2_M_MASK	(0x1 << 1)
#define MAX77779_CHG_INT_MASK_THM2_M_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_INT_MASK_INLIM_M_SHIFT	2
#define MAX77779_CHG_INT_MASK_INLIM_M_MASK	(0x1 << 2)
#define MAX77779_CHG_INT_MASK_INLIM_M_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_INT_MASK_BAT_M_SHIFT	3
#define MAX77779_CHG_INT_MASK_BAT_M_MASK	(0x1 << 3)
#define MAX77779_CHG_INT_MASK_BAT_M_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_INT_MASK_CHG_M_SHIFT	4
#define MAX77779_CHG_INT_MASK_CHG_M_MASK	(0x1 << 4)
#define MAX77779_CHG_INT_MASK_CHG_M_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_INT_MASK_WCIN_M_SHIFT	5
#define MAX77779_CHG_INT_MASK_WCIN_M_MASK	(0x1 << 5)
#define MAX77779_CHG_INT_MASK_WCIN_M_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_INT_MASK_CHGIN_M_SHIFT	6
#define MAX77779_CHG_INT_MASK_CHGIN_M_MASK	(0x1 << 6)
#define MAX77779_CHG_INT_MASK_CHGIN_M_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_INT_MASK_AICL_M_SHIFT	7
#define MAX77779_CHG_INT_MASK_AICL_M_MASK	(0x1 << 7)
#define MAX77779_CHG_INT_MASK_AICL_M_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_int_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BYP_M=%x",
		FIELD2VALUE(MAX77779_BYP_M, val));
	i += scnprintf(&buff[i], len - i, " THM2_M=%x",
		FIELD2VALUE(MAX77779_THM2_M, val));
	i += scnprintf(&buff[i], len - i, " INLIM_M=%x",
		FIELD2VALUE(MAX77779_INLIM_M, val));
	i += scnprintf(&buff[i], len - i, " BAT_M=%x",
		FIELD2VALUE(MAX77779_BAT_M, val));
	i += scnprintf(&buff[i], len - i, " CHG_M=%x",
		FIELD2VALUE(MAX77779_CHG_M, val));
	i += scnprintf(&buff[i], len - i, " WCIN_M=%x",
		FIELD2VALUE(MAX77779_WCIN_M, val));
	i += scnprintf(&buff[i], len - i, " CHGIN_M=%x",
		FIELD2VALUE(MAX77779_CHGIN_M, val));
	i += scnprintf(&buff[i], len - i, " AICL_M=%x",
		FIELD2VALUE(MAX77779_AICL_M, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_int_mask_byp_m,0,0)
MAX77779_BFF(max77779_chg_int_mask_thm2_m,1,1)
MAX77779_BFF(max77779_chg_int_mask_inlim_m,2,2)
MAX77779_BFF(max77779_chg_int_mask_bat_m,3,3)
MAX77779_BFF(max77779_chg_int_mask_chg_m,4,4)
MAX77779_BFF(max77779_chg_int_mask_wcin_m,5,5)
MAX77779_BFF(max77779_chg_int_mask_chgin_m,6,6)
MAX77779_BFF(max77779_chg_int_mask_aicl_m,7,7)

/*
 * CHG_INT2_MASK,0x04,0b11111111,0xff,OTP:SHADOW, Reset_Type:O
 * CHG_STA_DONE_M,CHG_STA_TO_M,CHG_STA_CV_M,CHG_STA_CC_M,COP_WARN_M,COP_ALERT_M,
 * COP_LIMIT_WD_M,INSEL_M
 */
#define MAX77779_CHG_INT2_MASK	0xb4
#define MAX77779_CHG_INT2_MASK_CHG_STA_DONE_M_SHIFT	0
#define MAX77779_CHG_INT2_MASK_CHG_STA_DONE_M_MASK	(0x1 << 0)
#define MAX77779_CHG_INT2_MASK_CHG_STA_DONE_M_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_INT2_MASK_CHG_STA_TO_M_SHIFT	1
#define MAX77779_CHG_INT2_MASK_CHG_STA_TO_M_MASK	(0x1 << 1)
#define MAX77779_CHG_INT2_MASK_CHG_STA_TO_M_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_INT2_MASK_CHG_STA_CV_M_SHIFT	2
#define MAX77779_CHG_INT2_MASK_CHG_STA_CV_M_MASK	(0x1 << 2)
#define MAX77779_CHG_INT2_MASK_CHG_STA_CV_M_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_INT2_MASK_CHG_STA_CC_M_SHIFT	3
#define MAX77779_CHG_INT2_MASK_CHG_STA_CC_M_MASK	(0x1 << 3)
#define MAX77779_CHG_INT2_MASK_CHG_STA_CC_M_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_INT2_MASK_COP_WARN_M_SHIFT	4
#define MAX77779_CHG_INT2_MASK_COP_WARN_M_MASK	(0x1 << 4)
#define MAX77779_CHG_INT2_MASK_COP_WARN_M_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_INT2_MASK_COP_ALERT_M_SHIFT	5
#define MAX77779_CHG_INT2_MASK_COP_ALERT_M_MASK	(0x1 << 5)
#define MAX77779_CHG_INT2_MASK_COP_ALERT_M_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_INT2_MASK_COP_LIMIT_WD_M_SHIFT	6
#define MAX77779_CHG_INT2_MASK_COP_LIMIT_WD_M_MASK	(0x1 << 6)
#define MAX77779_CHG_INT2_MASK_COP_LIMIT_WD_M_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_INT2_MASK_INSEL_M_SHIFT	7
#define MAX77779_CHG_INT2_MASK_INSEL_M_MASK	(0x1 << 7)
#define MAX77779_CHG_INT2_MASK_INSEL_M_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_int2_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CHG_STA_DONE_M=%x",
		FIELD2VALUE(MAX77779_CHG_STA_DONE_M, val));
	i += scnprintf(&buff[i], len - i, " CHG_STA_TO_M=%x",
		FIELD2VALUE(MAX77779_CHG_STA_TO_M, val));
	i += scnprintf(&buff[i], len - i, " CHG_STA_CV_M=%x",
		FIELD2VALUE(MAX77779_CHG_STA_CV_M, val));
	i += scnprintf(&buff[i], len - i, " CHG_STA_CC_M=%x",
		FIELD2VALUE(MAX77779_CHG_STA_CC_M, val));
	i += scnprintf(&buff[i], len - i, " COP_WARN_M=%x",
		FIELD2VALUE(MAX77779_COP_WARN_M, val));
	i += scnprintf(&buff[i], len - i, " COP_ALERT_M=%x",
		FIELD2VALUE(MAX77779_COP_ALERT_M, val));
	i += scnprintf(&buff[i], len - i, " COP_LIMIT_WD_M=%x",
		FIELD2VALUE(MAX77779_COP_LIMIT_WD_M, val));
	i += scnprintf(&buff[i], len - i, " INSEL_M=%x",
		FIELD2VALUE(MAX77779_INSEL_M, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_int2_mask_chg_sta_done_m,0,0)
MAX77779_BFF(max77779_chg_int2_mask_chg_sta_to_m,1,1)
MAX77779_BFF(max77779_chg_int2_mask_chg_sta_cv_m,2,2)
MAX77779_BFF(max77779_chg_int2_mask_chg_sta_cc_m,3,3)
MAX77779_BFF(max77779_chg_int2_mask_cop_warn_m,4,4)
MAX77779_BFF(max77779_chg_int2_mask_cop_alert_m,5,5)
MAX77779_BFF(max77779_chg_int2_mask_cop_limit_wd_m,6,6)
MAX77779_BFF(max77779_chg_int2_mask_insel_m,7,7)

/*
 * CHG_INT_OK,0x06,0b10011111,0x9f,Reset_Type:S
 * BYP_OK,THM2_OK,INLIM_OK,BAT_OK,CHG_OK,WCIN_OK,CHGIN_OK,AICL_OK
 */
#define MAX77779_CHG_INT_OK	0xb6
#define MAX77779_CHG_INT_OK_BYP_OK_SHIFT	0
#define MAX77779_CHG_INT_OK_BYP_OK_MASK	(0x1 << 0)
#define MAX77779_CHG_INT_OK_BYP_OK_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_INT_OK_THM2_OK_SHIFT	1
#define MAX77779_CHG_INT_OK_THM2_OK_MASK	(0x1 << 1)
#define MAX77779_CHG_INT_OK_THM2_OK_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_INT_OK_INLIM_OK_SHIFT	2
#define MAX77779_CHG_INT_OK_INLIM_OK_MASK	(0x1 << 2)
#define MAX77779_CHG_INT_OK_INLIM_OK_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_INT_OK_BAT_OK_SHIFT	3
#define MAX77779_CHG_INT_OK_BAT_OK_MASK	(0x1 << 3)
#define MAX77779_CHG_INT_OK_BAT_OK_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_INT_OK_CHG_OK_SHIFT	4
#define MAX77779_CHG_INT_OK_CHG_OK_MASK	(0x1 << 4)
#define MAX77779_CHG_INT_OK_CHG_OK_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_INT_OK_WCIN_OK_SHIFT	5
#define MAX77779_CHG_INT_OK_WCIN_OK_MASK	(0x1 << 5)
#define MAX77779_CHG_INT_OK_WCIN_OK_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_INT_OK_CHGIN_OK_SHIFT	6
#define MAX77779_CHG_INT_OK_CHGIN_OK_MASK	(0x1 << 6)
#define MAX77779_CHG_INT_OK_CHGIN_OK_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_INT_OK_AICL_OK_SHIFT	7
#define MAX77779_CHG_INT_OK_AICL_OK_MASK	(0x1 << 7)
#define MAX77779_CHG_INT_OK_AICL_OK_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_int_ok_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BYP_OK=%x",
		FIELD2VALUE(MAX77779_BYP_OK, val));
	i += scnprintf(&buff[i], len - i, " THM2_OK=%x",
		FIELD2VALUE(MAX77779_THM2_OK, val));
	i += scnprintf(&buff[i], len - i, " INLIM_OK=%x",
		FIELD2VALUE(MAX77779_INLIM_OK, val));
	i += scnprintf(&buff[i], len - i, " BAT_OK=%x",
		FIELD2VALUE(MAX77779_BAT_OK, val));
	i += scnprintf(&buff[i], len - i, " CHG_OK=%x",
		FIELD2VALUE(MAX77779_CHG_OK, val));
	i += scnprintf(&buff[i], len - i, " WCIN_OK=%x",
		FIELD2VALUE(MAX77779_WCIN_OK, val));
	i += scnprintf(&buff[i], len - i, " CHGIN_OK=%x",
		FIELD2VALUE(MAX77779_CHGIN_OK, val));
	i += scnprintf(&buff[i], len - i, " AICL_OK=%x",
		FIELD2VALUE(MAX77779_AICL_OK, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_int_ok_byp_ok,0,0)
MAX77779_BFF(max77779_chg_int_ok_thm2_ok,1,1)
MAX77779_BFF(max77779_chg_int_ok_inlim_ok,2,2)
MAX77779_BFF(max77779_chg_int_ok_bat_ok,3,3)
MAX77779_BFF(max77779_chg_int_ok_chg_ok,4,4)
MAX77779_BFF(max77779_chg_int_ok_wcin_ok,5,5)
MAX77779_BFF(max77779_chg_int_ok_chgin_ok,6,6)
MAX77779_BFF(max77779_chg_int_ok_aicl_ok,7,7)

/*
 * CHG_DETAILS_00,0x07,0b10000000,0x80,Reset_Type:S
 * TREG,SPSN_DTLS[1:2],WCIN_DTLS[3:2],CHGIN_DTLS[5:2],VDROOP1_OK
 */
#define MAX77779_CHG_DETAILS_00	0xb7
#define MAX77779_CHG_DETAILS_00_TREG_SHIFT	0
#define MAX77779_CHG_DETAILS_00_TREG_MASK	(0x1 << 0)
#define MAX77779_CHG_DETAILS_00_TREG_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_DETAILS_00_SPSN_DTLS_SHIFT	1
#define MAX77779_CHG_DETAILS_00_SPSN_DTLS_MASK	(0x3 << 1)
#define MAX77779_CHG_DETAILS_00_SPSN_DTLS_CLEAR	(~(0x3 << 1))
#define MAX77779_CHG_DETAILS_00_WCIN_DTLS_SHIFT	3
#define MAX77779_CHG_DETAILS_00_WCIN_DTLS_MASK	(0x3 << 3)
#define MAX77779_CHG_DETAILS_00_WCIN_DTLS_CLEAR	(~(0x3 << 3))
#define MAX77779_CHG_DETAILS_00_CHGIN_DTLS_SHIFT	5
#define MAX77779_CHG_DETAILS_00_CHGIN_DTLS_MASK	(0x3 << 5)
#define MAX77779_CHG_DETAILS_00_CHGIN_DTLS_CLEAR	(~(0x3 << 5))
#define MAX77779_CHG_DETAILS_00_VDROOP1_OK_SHIFT	7
#define MAX77779_CHG_DETAILS_00_VDROOP1_OK_MASK	(0x1 << 7)
#define MAX77779_CHG_DETAILS_00_VDROOP1_OK_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_details_00_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " TREG=%x",
		FIELD2VALUE(MAX77779_TREG, val));
	i += scnprintf(&buff[i], len - i, " SPSN_DTLS=%x",
		FIELD2VALUE(MAX77779_SPSN_DTLS, val));
	i += scnprintf(&buff[i], len - i, " WCIN_DTLS=%x",
		FIELD2VALUE(MAX77779_WCIN_DTLS, val));
	i += scnprintf(&buff[i], len - i, " CHGIN_DTLS=%x",
		FIELD2VALUE(MAX77779_CHGIN_DTLS, val));
	i += scnprintf(&buff[i], len - i, " VDROOP1_OK=%x",
		FIELD2VALUE(MAX77779_VDROOP1_OK, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_details_00_treg,0,0)
MAX77779_BFF(max77779_chg_details_00_spsn_dtls,2,1)
MAX77779_BFF(max77779_chg_details_00_wcin_dtls,4,3)
MAX77779_BFF(max77779_chg_details_00_chgin_dtls,6,5)
MAX77779_BFF(max77779_chg_details_00_vdroop1_ok,7,7)

/*
 * CHG_DETAILS_01,0x08,0b11111000,0xf8,Reset_Type:S
 * CHG_DTLS[0:4],BAT_DTLS[4:3],VDROOP2_OK
 */
#define MAX77779_CHG_DETAILS_01	0xb8
#define MAX77779_CHG_DETAILS_01_CHG_DTLS_SHIFT	0
#define MAX77779_CHG_DETAILS_01_CHG_DTLS_MASK	(0xf << 0)
#define MAX77779_CHG_DETAILS_01_CHG_DTLS_CLEAR	(~(0xf << 0))
#define MAX77779_CHG_DETAILS_01_BAT_DTLS_SHIFT	4
#define MAX77779_CHG_DETAILS_01_BAT_DTLS_MASK	(0x7 << 4)
#define MAX77779_CHG_DETAILS_01_BAT_DTLS_CLEAR	(~(0x7 << 4))
#define MAX77779_CHG_DETAILS_01_VDROOP2_OK_SHIFT	7
#define MAX77779_CHG_DETAILS_01_VDROOP2_OK_MASK	(0x1 << 7)
#define MAX77779_CHG_DETAILS_01_VDROOP2_OK_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_details_01_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CHG_DTLS=%x",
		FIELD2VALUE(MAX77779_CHG_DTLS, val));
	i += scnprintf(&buff[i], len - i, " BAT_DTLS=%x",
		FIELD2VALUE(MAX77779_BAT_DTLS, val));
	i += scnprintf(&buff[i], len - i, " VDROOP2_OK=%x",
		FIELD2VALUE(MAX77779_VDROOP2_OK, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_details_01_chg_dtls,3,0)
MAX77779_BFF(max77779_chg_details_01_bat_dtls,6,4)
MAX77779_BFF(max77779_chg_details_01_vdroop2_ok,7,7)

/*
 * CHG_DETAILS_02,0x09,0b00000000,0x0,Reset_Type:S
 * BYP_DTLS[0:4],WCIN_STS,CHGIN_STS,NXT_BCK_INPUT[6:2]
 */
#define MAX77779_CHG_DETAILS_02	0xb9
#define MAX77779_CHG_DETAILS_02_BYP_DTLS_SHIFT	0
#define MAX77779_CHG_DETAILS_02_BYP_DTLS_MASK	(0xf << 0)
#define MAX77779_CHG_DETAILS_02_BYP_DTLS_CLEAR	(~(0xf << 0))
#define MAX77779_CHG_DETAILS_02_WCIN_STS_SHIFT	4
#define MAX77779_CHG_DETAILS_02_WCIN_STS_MASK	(0x1 << 4)
#define MAX77779_CHG_DETAILS_02_WCIN_STS_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_DETAILS_02_CHGIN_STS_SHIFT	5
#define MAX77779_CHG_DETAILS_02_CHGIN_STS_MASK	(0x1 << 5)
#define MAX77779_CHG_DETAILS_02_CHGIN_STS_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_DETAILS_02_NXT_BCK_INPUT_SHIFT	6
#define MAX77779_CHG_DETAILS_02_NXT_BCK_INPUT_MASK	(0x3 << 6)
#define MAX77779_CHG_DETAILS_02_NXT_BCK_INPUT_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_details_02_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BYP_DTLS=%x",
		FIELD2VALUE(MAX77779_BYP_DTLS, val));
	i += scnprintf(&buff[i], len - i, " WCIN_STS=%x",
		FIELD2VALUE(MAX77779_WCIN_STS, val));
	i += scnprintf(&buff[i], len - i, " CHGIN_STS=%x",
		FIELD2VALUE(MAX77779_CHGIN_STS, val));
	i += scnprintf(&buff[i], len - i, " NXT_BCK_INPUT=%x",
		FIELD2VALUE(MAX77779_NXT_BCK_INPUT, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_details_02_byp_dtls,3,0)
MAX77779_BFF(max77779_chg_details_02_wcin_sts,4,4)
MAX77779_BFF(max77779_chg_details_02_chgin_sts,5,5)
MAX77779_BFF(max77779_chg_details_02_nxt_bck_input,7,6)

/*
 * CHG_DETAILS_03,0x0A,0b00010010,0x12,Reset_Type:S
 * THM1_DTLS[0:3],THM3_DTLS[3:3],JEITA_AUX_DTLS[6:2]
 */
#define MAX77779_CHG_DETAILS_03	0xba
#define MAX77779_CHG_DETAILS_03_THM1_DTLS_SHIFT	0
#define MAX77779_CHG_DETAILS_03_THM1_DTLS_MASK	(0x7 << 0)
#define MAX77779_CHG_DETAILS_03_THM1_DTLS_CLEAR	(~(0x7 << 0))
#define MAX77779_CHG_DETAILS_03_THM3_DTLS_SHIFT	3
#define MAX77779_CHG_DETAILS_03_THM3_DTLS_MASK	(0x7 << 3)
#define MAX77779_CHG_DETAILS_03_THM3_DTLS_CLEAR	(~(0x7 << 3))
#define MAX77779_CHG_DETAILS_03_JEITA_AUX_DTLS_SHIFT	6
#define MAX77779_CHG_DETAILS_03_JEITA_AUX_DTLS_MASK	(0x3 << 6)
#define MAX77779_CHG_DETAILS_03_JEITA_AUX_DTLS_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_details_03_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " THM1_DTLS=%x",
		FIELD2VALUE(MAX77779_THM1_DTLS, val));
	i += scnprintf(&buff[i], len - i, " THM3_DTLS=%x",
		FIELD2VALUE(MAX77779_THM3_DTLS, val));
	i += scnprintf(&buff[i], len - i, " JEITA_AUX_DTLS=%x",
		FIELD2VALUE(MAX77779_JEITA_AUX_DTLS, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_details_03_thm1_dtls,2,0)
MAX77779_BFF(max77779_chg_details_03_thm3_dtls,5,3)
MAX77779_BFF(max77779_chg_details_03_jeita_aux_dtls,7,6)

/*
 * CHG_DETAILS_04,0x0B,0b00000000,0x0,Reset_Type:S
 * FSHIP_EXIT_DTLS[0:2],MD_DTLS[2:2],BAT_OILO1_OPEN,BAT_OILO2_OPEN,SPR_7_6[6:2]
 */
#define MAX77779_CHG_DETAILS_04	0xbb
#define MAX77779_CHG_DETAILS_04_FSHIP_EXIT_DTLS_SHIFT	0
#define MAX77779_CHG_DETAILS_04_FSHIP_EXIT_DTLS_MASK	(0x3 << 0)
#define MAX77779_CHG_DETAILS_04_FSHIP_EXIT_DTLS_CLEAR	(~(0x3 << 0))
#define MAX77779_CHG_DETAILS_04_MD_DTLS_SHIFT	2
#define MAX77779_CHG_DETAILS_04_MD_DTLS_MASK	(0x3 << 2)
#define MAX77779_CHG_DETAILS_04_MD_DTLS_CLEAR	(~(0x3 << 2))
#define MAX77779_CHG_DETAILS_04_BAT_OILO1_OPEN_SHIFT	4
#define MAX77779_CHG_DETAILS_04_BAT_OILO1_OPEN_MASK	(0x1 << 4)
#define MAX77779_CHG_DETAILS_04_BAT_OILO1_OPEN_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_DETAILS_04_BAT_OILO2_OPEN_SHIFT	5
#define MAX77779_CHG_DETAILS_04_BAT_OILO2_OPEN_MASK	(0x1 << 5)
#define MAX77779_CHG_DETAILS_04_BAT_OILO2_OPEN_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_DETAILS_04_SPR_7_6_SHIFT	6
#define MAX77779_CHG_DETAILS_04_SPR_7_6_MASK	(0x3 << 6)
#define MAX77779_CHG_DETAILS_04_SPR_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_details_04_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " FSHIP_EXIT_DTLS=%x",
		FIELD2VALUE(MAX77779_FSHIP_EXIT_DTLS, val));
	i += scnprintf(&buff[i], len - i, " MD_DTLS=%x",
		FIELD2VALUE(MAX77779_MD_DTLS, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_OPEN=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_OPEN, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_OPEN=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_OPEN, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_6=%x",
		FIELD2VALUE(MAX77779_SPR_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_details_04_fship_exit_dtls,1,0)
MAX77779_BFF(max77779_chg_details_04_md_dtls,3,2)
MAX77779_BFF(max77779_chg_details_04_bat_oilo1_open,4,4)
MAX77779_BFF(max77779_chg_details_04_bat_oilo2_open,5,5)
MAX77779_BFF(max77779_chg_details_04_spr_7_6,7,6)

/*
 * CHG_CNFG_00,0x0C,0b00000100,0x4,OTP:SHADOW, Reset_Type:O
 * MODE[0:4],BYPV_RAMP_BYPASS,CP_EN,WDTCLR[6:2]
 */
#define MAX77779_CHG_CNFG_00	0xbc
#define MAX77779_CHG_CNFG_00_MODE_SHIFT	0
#define MAX77779_CHG_CNFG_00_MODE_MASK	(0xf << 0)
#define MAX77779_CHG_CNFG_00_MODE_CLEAR	(~(0xf << 0))
#define MAX77779_CHG_CNFG_00_BYPV_RAMP_BYPASS_SHIFT	4
#define MAX77779_CHG_CNFG_00_BYPV_RAMP_BYPASS_MASK	(0x1 << 4)
#define MAX77779_CHG_CNFG_00_BYPV_RAMP_BYPASS_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_CNFG_00_CP_EN_SHIFT	5
#define MAX77779_CHG_CNFG_00_CP_EN_MASK	(0x1 << 5)
#define MAX77779_CHG_CNFG_00_CP_EN_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_CNFG_00_WDTCLR_SHIFT	6
#define MAX77779_CHG_CNFG_00_WDTCLR_MASK	(0x3 << 6)
#define MAX77779_CHG_CNFG_00_WDTCLR_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_cnfg_00_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " MODE=%x",
		FIELD2VALUE(MAX77779_MODE, val));
	i += scnprintf(&buff[i], len - i, " BYPV_RAMP_BYPASS=%x",
		FIELD2VALUE(MAX77779_BYPV_RAMP_BYPASS, val));
	i += scnprintf(&buff[i], len - i, " CP_EN=%x",
		FIELD2VALUE(MAX77779_CP_EN, val));
	i += scnprintf(&buff[i], len - i, " WDTCLR=%x",
		FIELD2VALUE(MAX77779_WDTCLR, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_00_mode,3,0)
MAX77779_BFF(max77779_chg_cnfg_00_bypv_ramp_bypass,4,4)
MAX77779_BFF(max77779_chg_cnfg_00_cp_en,5,5)
MAX77779_BFF(max77779_chg_cnfg_00_wdtclr,7,6)

/*
 * CHG_CNFG_01,0x0D,0b10011001,0x99,OTP:SHADOW, Reset_Type:O
 * FCHGTIME[0:3],RECYCLE_EN,CHG_RSTRT[4:2],LSEL,PQEN
 */
#define MAX77779_CHG_CNFG_01	0xbd
#define MAX77779_CHG_CNFG_01_FCHGTIME_SHIFT	0
#define MAX77779_CHG_CNFG_01_FCHGTIME_MASK	(0x7 << 0)
#define MAX77779_CHG_CNFG_01_FCHGTIME_CLEAR	(~(0x7 << 0))
#define MAX77779_CHG_CNFG_01_RECYCLE_EN_SHIFT	3
#define MAX77779_CHG_CNFG_01_RECYCLE_EN_MASK	(0x1 << 3)
#define MAX77779_CHG_CNFG_01_RECYCLE_EN_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_CNFG_01_CHG_RSTRT_SHIFT	4
#define MAX77779_CHG_CNFG_01_CHG_RSTRT_MASK	(0x3 << 4)
#define MAX77779_CHG_CNFG_01_CHG_RSTRT_CLEAR	(~(0x3 << 4))
#define MAX77779_CHG_CNFG_01_LSEL_SHIFT	6
#define MAX77779_CHG_CNFG_01_LSEL_MASK	(0x1 << 6)
#define MAX77779_CHG_CNFG_01_LSEL_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_CNFG_01_PQEN_SHIFT	7
#define MAX77779_CHG_CNFG_01_PQEN_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_01_PQEN_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_01_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " FCHGTIME=%x",
		FIELD2VALUE(MAX77779_FCHGTIME, val));
	i += scnprintf(&buff[i], len - i, " RECYCLE_EN=%x",
		FIELD2VALUE(MAX77779_RECYCLE_EN, val));
	i += scnprintf(&buff[i], len - i, " CHG_RSTRT=%x",
		FIELD2VALUE(MAX77779_CHG_RSTRT, val));
	i += scnprintf(&buff[i], len - i, " LSEL=%x",
		FIELD2VALUE(MAX77779_LSEL, val));
	i += scnprintf(&buff[i], len - i, " PQEN=%x",
		FIELD2VALUE(MAX77779_PQEN, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_01_fchgtime,2,0)
MAX77779_BFF(max77779_chg_cnfg_01_recycle_en,3,3)
MAX77779_BFF(max77779_chg_cnfg_01_chg_rstrt,5,4)
MAX77779_BFF(max77779_chg_cnfg_01_lsel,6,6)
MAX77779_BFF(max77779_chg_cnfg_01_pqen,7,7)

/*
 * CHG_CNFG_02,0x0E,0b00000111,0x7,OTP:SHADOW, Reset_Type:O
 * CHGCC[0:6],SPR_7_6[6:2]
 */
#define MAX77779_CHG_CNFG_02	0xbe
#define MAX77779_CHG_CNFG_02_CHGCC_SHIFT	0
#define MAX77779_CHG_CNFG_02_CHGCC_MASK	(0x3f << 0)
#define MAX77779_CHG_CNFG_02_CHGCC_CLEAR	(~(0x3f << 0))
#define MAX77779_CHG_CNFG_02_SPR_7_6_SHIFT	6
#define MAX77779_CHG_CNFG_02_SPR_7_6_MASK	(0x3 << 6)
#define MAX77779_CHG_CNFG_02_SPR_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_cnfg_02_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CHGCC=%x",
		FIELD2VALUE(MAX77779_CHGCC, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_6=%x",
		FIELD2VALUE(MAX77779_SPR_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_02_chgcc,5,0)
MAX77779_BFF(max77779_chg_cnfg_02_spr_7_6,7,6)

/*
 * CHG_CNFG_03,0x0F,0b10011001,0x99,OTP:SHADOW, Reset_Type:O
 * TO_ITH[0:3],TO_TIME[3:3],AUTO_FSHIP_MODE_EN,SYS_TRACK_DIS
 */
#define MAX77779_CHG_CNFG_03	0xbf
#define MAX77779_CHG_CNFG_03_TO_ITH_SHIFT	0
#define MAX77779_CHG_CNFG_03_TO_ITH_MASK	(0x7 << 0)
#define MAX77779_CHG_CNFG_03_TO_ITH_CLEAR	(~(0x7 << 0))
#define MAX77779_CHG_CNFG_03_TO_TIME_SHIFT	3
#define MAX77779_CHG_CNFG_03_TO_TIME_MASK	(0x7 << 3)
#define MAX77779_CHG_CNFG_03_TO_TIME_CLEAR	(~(0x7 << 3))
#define MAX77779_CHG_CNFG_03_AUTO_FSHIP_MODE_EN_SHIFT	6
#define MAX77779_CHG_CNFG_03_AUTO_FSHIP_MODE_EN_MASK	(0x1 << 6)
#define MAX77779_CHG_CNFG_03_AUTO_FSHIP_MODE_EN_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_CNFG_03_SYS_TRACK_DIS_SHIFT	7
#define MAX77779_CHG_CNFG_03_SYS_TRACK_DIS_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_03_SYS_TRACK_DIS_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_03_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " TO_ITH=%x",
		FIELD2VALUE(MAX77779_TO_ITH, val));
	i += scnprintf(&buff[i], len - i, " TO_TIME=%x",
		FIELD2VALUE(MAX77779_TO_TIME, val));
	i += scnprintf(&buff[i], len - i, " AUTO_FSHIP_MODE_EN=%x",
		FIELD2VALUE(MAX77779_AUTO_FSHIP_MODE_EN, val));
	i += scnprintf(&buff[i], len - i, " SYS_TRACK_DIS=%x",
		FIELD2VALUE(MAX77779_SYS_TRACK_DIS, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_03_to_ith,2,0)
MAX77779_BFF(max77779_chg_cnfg_03_to_time,5,3)
MAX77779_BFF(max77779_chg_cnfg_03_auto_fship_mode_en,6,6)
MAX77779_BFF(max77779_chg_cnfg_03_sys_track_dis,7,7)

/*
 * CHG_CNFG_04,0x10,0b00010100,0x14,OTP:SHADOW, Reset_Type:O
 * CHG_CV_PRM[0:6],SPR_7_6[6:2]
 */
#define MAX77779_CHG_CNFG_04	0xc0
#define MAX77779_CHG_CNFG_04_CHG_CV_PRM_SHIFT	0
#define MAX77779_CHG_CNFG_04_CHG_CV_PRM_MASK	(0x3f << 0)
#define MAX77779_CHG_CNFG_04_CHG_CV_PRM_CLEAR	(~(0x3f << 0))
#define MAX77779_CHG_CNFG_04_SPR_7_6_SHIFT	6
#define MAX77779_CHG_CNFG_04_SPR_7_6_MASK	(0x3 << 6)
#define MAX77779_CHG_CNFG_04_SPR_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_cnfg_04_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CHG_CV_PRM=%x",
		FIELD2VALUE(MAX77779_CHG_CV_PRM, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_6=%x",
		FIELD2VALUE(MAX77779_SPR_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_04_chg_cv_prm,5,0)
MAX77779_BFF(max77779_chg_cnfg_04_spr_7_6,7,6)

/*
 * CHG_CNFG_05,0x11,0b00110110,0x36,OTP:SHADOW, Reset_Type:O
 * OTG_ILIM[0:4],WCSM_ILIM[4:4]
 */
#define MAX77779_CHG_CNFG_05	0xc1
#define MAX77779_CHG_CNFG_05_OTG_ILIM_SHIFT	0
#define MAX77779_CHG_CNFG_05_OTG_ILIM_MASK	(0xf << 0)
#define MAX77779_CHG_CNFG_05_OTG_ILIM_CLEAR	(~(0xf << 0))
#define MAX77779_CHG_CNFG_05_WCSM_ILIM_SHIFT	4
#define MAX77779_CHG_CNFG_05_WCSM_ILIM_MASK	(0xf << 4)
#define MAX77779_CHG_CNFG_05_WCSM_ILIM_CLEAR	(~(0xf << 4))
static inline const char *
max77779_chg_cnfg_05_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " OTG_ILIM=%x",
		FIELD2VALUE(MAX77779_OTG_ILIM, val));
	i += scnprintf(&buff[i], len - i, " WCSM_ILIM=%x",
		FIELD2VALUE(MAX77779_WCSM_ILIM, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_05_otg_ilim,3,0)
MAX77779_BFF(max77779_chg_cnfg_05_wcsm_ilim,7,4)

/*
 * CHG_CNFG_06,0x12,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 * SPR_1_0[0:2],CHGPROT[2:2],CHG_CTM_KEY[4:4]
 */
#define MAX77779_CHG_CNFG_06	0xc2
#define MAX77779_CHG_CNFG_06_SPR_1_0_SHIFT	0
#define MAX77779_CHG_CNFG_06_SPR_1_0_MASK	(0x3 << 0)
#define MAX77779_CHG_CNFG_06_SPR_1_0_CLEAR	(~(0x3 << 0))
#define MAX77779_CHG_CNFG_06_CHGPROT_SHIFT	2
#define MAX77779_CHG_CNFG_06_CHGPROT_MASK	(0x3 << 2)
#define MAX77779_CHG_CNFG_06_CHGPROT_CLEAR	(~(0x3 << 2))
#define MAX77779_CHG_CNFG_06_CHG_CTM_KEY_SHIFT	4
#define MAX77779_CHG_CNFG_06_CHG_CTM_KEY_MASK	(0xf << 4)
#define MAX77779_CHG_CNFG_06_CHG_CTM_KEY_CLEAR	(~(0xf << 4))
static inline const char *
max77779_chg_cnfg_06_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SPR_1_0=%x",
		FIELD2VALUE(MAX77779_SPR_1_0, val));
	i += scnprintf(&buff[i], len - i, " CHGPROT=%x",
		FIELD2VALUE(MAX77779_CHGPROT, val));
	i += scnprintf(&buff[i], len - i, " CHG_CTM_KEY=%x",
		FIELD2VALUE(MAX77779_CHG_CTM_KEY, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_06_spr_1_0,1,0)
MAX77779_BFF(max77779_chg_cnfg_06_chgprot,3,2)
MAX77779_BFF(max77779_chg_cnfg_06_chg_ctm_key,7,4)

/*
 * CHG_CNFG_07,0x13,0b00110000,0x30,OTP:SHADOW, Reset_Type:O
 * FSHIP_MODE,SPR_2_1[1:2],REGTEMP[3:4],WD_QBATOFF
 */
#define MAX77779_CHG_CNFG_07	0xc3
#define MAX77779_CHG_CNFG_07_FSHIP_MODE_SHIFT	0
#define MAX77779_CHG_CNFG_07_FSHIP_MODE_MASK	(0x1 << 0)
#define MAX77779_CHG_CNFG_07_FSHIP_MODE_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_CNFG_07_SPR_2_1_SHIFT	1
#define MAX77779_CHG_CNFG_07_SPR_2_1_MASK	(0x3 << 1)
#define MAX77779_CHG_CNFG_07_SPR_2_1_CLEAR	(~(0x3 << 1))
#define MAX77779_CHG_CNFG_07_REGTEMP_SHIFT	3
#define MAX77779_CHG_CNFG_07_REGTEMP_MASK	(0xf << 3)
#define MAX77779_CHG_CNFG_07_REGTEMP_CLEAR	(~(0xf << 3))
#define MAX77779_CHG_CNFG_07_WD_QBATOFF_SHIFT	7
#define MAX77779_CHG_CNFG_07_WD_QBATOFF_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_07_WD_QBATOFF_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_07_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " FSHIP_MODE=%x",
		FIELD2VALUE(MAX77779_FSHIP_MODE, val));
	i += scnprintf(&buff[i], len - i, " SPR_2_1=%x",
		FIELD2VALUE(MAX77779_SPR_2_1, val));
	i += scnprintf(&buff[i], len - i, " REGTEMP=%x",
		FIELD2VALUE(MAX77779_REGTEMP, val));
	i += scnprintf(&buff[i], len - i, " WD_QBATOFF=%x",
		FIELD2VALUE(MAX77779_WD_QBATOFF, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_07_fship_mode,0,0)
MAX77779_BFF(max77779_chg_cnfg_07_spr_2_1,2,1)
MAX77779_BFF(max77779_chg_cnfg_07_regtemp,6,3)
MAX77779_BFF(max77779_chg_cnfg_07_wd_qbatoff,7,7)

/*
 * CHG_CNFG_08,0x14,0b00000101,0x5,OTP:SHADOW, Reset_Type:O
 * FSW[0:2],THM1_JEITA_EN,ICHGCC_COOL,VCHGCV_COOL,ICHGCC_WARM,VCHGCV_WARM,SPR_7
 */
#define MAX77779_CHG_CNFG_08	0xc4
#define MAX77779_CHG_CNFG_08_FSW_SHIFT	0
#define MAX77779_CHG_CNFG_08_FSW_MASK	(0x3 << 0)
#define MAX77779_CHG_CNFG_08_FSW_CLEAR	(~(0x3 << 0))
#define MAX77779_CHG_CNFG_08_THM1_JEITA_EN_SHIFT	2
#define MAX77779_CHG_CNFG_08_THM1_JEITA_EN_MASK	(0x1 << 2)
#define MAX77779_CHG_CNFG_08_THM1_JEITA_EN_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_CNFG_08_ICHGCC_COOL_SHIFT	3
#define MAX77779_CHG_CNFG_08_ICHGCC_COOL_MASK	(0x1 << 3)
#define MAX77779_CHG_CNFG_08_ICHGCC_COOL_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_CNFG_08_VCHGCV_COOL_SHIFT	4
#define MAX77779_CHG_CNFG_08_VCHGCV_COOL_MASK	(0x1 << 4)
#define MAX77779_CHG_CNFG_08_VCHGCV_COOL_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_CNFG_08_ICHGCC_WARM_SHIFT	5
#define MAX77779_CHG_CNFG_08_ICHGCC_WARM_MASK	(0x1 << 5)
#define MAX77779_CHG_CNFG_08_ICHGCC_WARM_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_CNFG_08_VCHGCV_WARM_SHIFT	6
#define MAX77779_CHG_CNFG_08_VCHGCV_WARM_MASK	(0x1 << 6)
#define MAX77779_CHG_CNFG_08_VCHGCV_WARM_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_CNFG_08_SPR_7_SHIFT	7
#define MAX77779_CHG_CNFG_08_SPR_7_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_08_SPR_7_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_08_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " FSW=%x",
		FIELD2VALUE(MAX77779_FSW, val));
	i += scnprintf(&buff[i], len - i, " THM1_JEITA_EN=%x",
		FIELD2VALUE(MAX77779_THM1_JEITA_EN, val));
	i += scnprintf(&buff[i], len - i, " ICHGCC_COOL=%x",
		FIELD2VALUE(MAX77779_ICHGCC_COOL, val));
	i += scnprintf(&buff[i], len - i, " VCHGCV_COOL=%x",
		FIELD2VALUE(MAX77779_VCHGCV_COOL, val));
	i += scnprintf(&buff[i], len - i, " ICHGCC_WARM=%x",
		FIELD2VALUE(MAX77779_ICHGCC_WARM, val));
	i += scnprintf(&buff[i], len - i, " VCHGCV_WARM=%x",
		FIELD2VALUE(MAX77779_VCHGCV_WARM, val));
	i += scnprintf(&buff[i], len - i, " SPR_7=%x",
		FIELD2VALUE(MAX77779_SPR_7, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_08_fsw,1,0)
MAX77779_BFF(max77779_chg_cnfg_08_thm1_jeita_en,2,2)
MAX77779_BFF(max77779_chg_cnfg_08_ichgcc_cool,3,3)
MAX77779_BFF(max77779_chg_cnfg_08_vchgcv_cool,4,4)
MAX77779_BFF(max77779_chg_cnfg_08_ichgcc_warm,5,5)
MAX77779_BFF(max77779_chg_cnfg_08_vchgcv_warm,6,6)
MAX77779_BFF(max77779_chg_cnfg_08_spr_7,7,7)

/*
 * CHG_CNFG_09,0x15,0b00010011,0x13,OTP:SHADOW, Reset_Type:O
 * CHGIN_ILIM[0:7],NO_AUTOIBUS
 */
#define MAX77779_CHG_CNFG_09	0xc5
#define MAX77779_CHG_CNFG_09_CHGIN_ILIM_SHIFT	0
#define MAX77779_CHG_CNFG_09_CHGIN_ILIM_MASK	(0x7f << 0)
#define MAX77779_CHG_CNFG_09_CHGIN_ILIM_CLEAR	(~(0x7f << 0))
#define MAX77779_CHG_CNFG_09_NO_AUTOIBUS_SHIFT	7
#define MAX77779_CHG_CNFG_09_NO_AUTOIBUS_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_09_NO_AUTOIBUS_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_09_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CHGIN_ILIM=%x",
		FIELD2VALUE(MAX77779_CHGIN_ILIM, val));
	i += scnprintf(&buff[i], len - i, " NO_AUTOIBUS=%x",
		FIELD2VALUE(MAX77779_NO_AUTOIBUS, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_09_chgin_ilim,6,0)
MAX77779_BFF(max77779_chg_cnfg_09_no_autoibus,7,7)

/*
 * CHG_CNFG_10,0x16,0b00010011,0x13,OTP:SHADOW, Reset_Type:O
 * WCIN_ILIM[0:7],CHGIN_ILIM_SPEED
 */
#define MAX77779_CHG_CNFG_10	0xc6
#define MAX77779_CHG_CNFG_10_WCIN_ILIM_SHIFT	0
#define MAX77779_CHG_CNFG_10_WCIN_ILIM_MASK	(0x7f << 0)
#define MAX77779_CHG_CNFG_10_WCIN_ILIM_CLEAR	(~(0x7f << 0))
#define MAX77779_CHG_CNFG_10_CHGIN_ILIM_SPEED_SHIFT	7
#define MAX77779_CHG_CNFG_10_CHGIN_ILIM_SPEED_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_10_CHGIN_ILIM_SPEED_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_10_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " WCIN_ILIM=%x",
		FIELD2VALUE(MAX77779_WCIN_ILIM, val));
	i += scnprintf(&buff[i], len - i, " CHGIN_ILIM_SPEED=%x",
		FIELD2VALUE(MAX77779_CHGIN_ILIM_SPEED, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_10_wcin_ilim,6,0)
MAX77779_BFF(max77779_chg_cnfg_10_chgin_ilim_speed,7,7)

/*
 * CHG_CNFG_11,0x17,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 */
#define MAX77779_CHG_CNFG_11	0xc7

/*
 * CHG_CNFG_12,0x18,0b01101010,0x6a,OTP:SHADOW, Reset_Type:O
 * DISKIP,WCIN_REG[1:2],VCHGIN_REG[3:2],CHGINSEL,WCINSEL,CHG_EN
 */
#define MAX77779_CHG_CNFG_12	0xc8
#define MAX77779_CHG_CNFG_12_DISKIP_SHIFT	0
#define MAX77779_CHG_CNFG_12_DISKIP_MASK	(0x1 << 0)
#define MAX77779_CHG_CNFG_12_DISKIP_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_CNFG_12_WCIN_REG_SHIFT	1
#define MAX77779_CHG_CNFG_12_WCIN_REG_MASK	(0x3 << 1)
#define MAX77779_CHG_CNFG_12_WCIN_REG_CLEAR	(~(0x3 << 1))
#define MAX77779_CHG_CNFG_12_VCHGIN_REG_SHIFT	3
#define MAX77779_CHG_CNFG_12_VCHGIN_REG_MASK	(0x3 << 3)
#define MAX77779_CHG_CNFG_12_VCHGIN_REG_CLEAR	(~(0x3 << 3))
#define MAX77779_CHG_CNFG_12_CHGINSEL_SHIFT	5
#define MAX77779_CHG_CNFG_12_CHGINSEL_MASK	(0x1 << 5)
#define MAX77779_CHG_CNFG_12_CHGINSEL_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_CNFG_12_WCINSEL_SHIFT	6
#define MAX77779_CHG_CNFG_12_WCINSEL_MASK	(0x1 << 6)
#define MAX77779_CHG_CNFG_12_WCINSEL_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_CNFG_12_CHG_EN_SHIFT	7
#define MAX77779_CHG_CNFG_12_CHG_EN_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_12_CHG_EN_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_12_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DISKIP=%x",
		FIELD2VALUE(MAX77779_DISKIP, val));
	i += scnprintf(&buff[i], len - i, " WCIN_REG=%x",
		FIELD2VALUE(MAX77779_WCIN_REG, val));
	i += scnprintf(&buff[i], len - i, " VCHGIN_REG=%x",
		FIELD2VALUE(MAX77779_VCHGIN_REG, val));
	i += scnprintf(&buff[i], len - i, " CHGINSEL=%x",
		FIELD2VALUE(MAX77779_CHGINSEL, val));
	i += scnprintf(&buff[i], len - i, " WCINSEL=%x",
		FIELD2VALUE(MAX77779_WCINSEL, val));
	i += scnprintf(&buff[i], len - i, " CHG_EN=%x",
		FIELD2VALUE(MAX77779_CHG_EN, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_12_diskip,0,0)
MAX77779_BFF(max77779_chg_cnfg_12_wcin_reg,2,1)
MAX77779_BFF(max77779_chg_cnfg_12_vchgin_reg,4,3)
MAX77779_BFF(max77779_chg_cnfg_12_chginsel,5,5)
MAX77779_BFF(max77779_chg_cnfg_12_wcinsel,6,6)
MAX77779_BFF(max77779_chg_cnfg_12_chg_en,7,7)

/*
 * CHG_CNFG_13,0x19,0b00000011,0x3,OTP:SHADOW, Reset_Type:O
 * USB_TEMP_THR[0:3],THM2_HW_CTRL,THM_BUCK_DIS,THM_CC_HZ,THM_CHR_RSTART,SPR_7
 */
#define MAX77779_CHG_CNFG_13	0xc9
#define MAX77779_CHG_CNFG_13_USB_TEMP_THR_SHIFT	0
#define MAX77779_CHG_CNFG_13_USB_TEMP_THR_MASK	(0x7 << 0)
#define MAX77779_CHG_CNFG_13_USB_TEMP_THR_CLEAR	(~(0x7 << 0))
#define MAX77779_CHG_CNFG_13_THM2_HW_CTRL_SHIFT	3
#define MAX77779_CHG_CNFG_13_THM2_HW_CTRL_MASK	(0x1 << 3)
#define MAX77779_CHG_CNFG_13_THM2_HW_CTRL_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_CNFG_13_THM_BUCK_DIS_SHIFT	4
#define MAX77779_CHG_CNFG_13_THM_BUCK_DIS_MASK	(0x1 << 4)
#define MAX77779_CHG_CNFG_13_THM_BUCK_DIS_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_CNFG_13_THM_CC_HZ_SHIFT	5
#define MAX77779_CHG_CNFG_13_THM_CC_HZ_MASK	(0x1 << 5)
#define MAX77779_CHG_CNFG_13_THM_CC_HZ_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_CNFG_13_THM_CHR_RSTART_SHIFT	6
#define MAX77779_CHG_CNFG_13_THM_CHR_RSTART_MASK	(0x1 << 6)
#define MAX77779_CHG_CNFG_13_THM_CHR_RSTART_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_CNFG_13_SPR_7_SHIFT	7
#define MAX77779_CHG_CNFG_13_SPR_7_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_13_SPR_7_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_13_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " USB_TEMP_THR=%x",
		FIELD2VALUE(MAX77779_USB_TEMP_THR, val));
	i += scnprintf(&buff[i], len - i, " THM2_HW_CTRL=%x",
		FIELD2VALUE(MAX77779_THM2_HW_CTRL, val));
	i += scnprintf(&buff[i], len - i, " THM_BUCK_DIS=%x",
		FIELD2VALUE(MAX77779_THM_BUCK_DIS, val));
	i += scnprintf(&buff[i], len - i, " THM_CC_HZ=%x",
		FIELD2VALUE(MAX77779_THM_CC_HZ, val));
	i += scnprintf(&buff[i], len - i, " THM_CHR_RSTART=%x",
		FIELD2VALUE(MAX77779_THM_CHR_RSTART, val));
	i += scnprintf(&buff[i], len - i, " SPR_7=%x",
		FIELD2VALUE(MAX77779_SPR_7, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_13_usb_temp_thr,2,0)
MAX77779_BFF(max77779_chg_cnfg_13_thm2_hw_ctrl,3,3)
MAX77779_BFF(max77779_chg_cnfg_13_thm_buck_dis,4,4)
MAX77779_BFF(max77779_chg_cnfg_13_thm_cc_hz,5,5)
MAX77779_BFF(max77779_chg_cnfg_13_thm_chr_rstart,6,6)
MAX77779_BFF(max77779_chg_cnfg_13_spr_7,7,7)

/*
 * CHG_CNFG_14,0x1A,0b01001000,0x48,OTP:SHADOW, Reset_Type:O
 * SPR_2_0[0:3],TPWROUT[3:3],AICL[6:2]
 */
#define MAX77779_CHG_CNFG_14	0xca
#define MAX77779_CHG_CNFG_14_SPR_2_0_SHIFT	0
#define MAX77779_CHG_CNFG_14_SPR_2_0_MASK	(0x7 << 0)
#define MAX77779_CHG_CNFG_14_SPR_2_0_CLEAR	(~(0x7 << 0))
#define MAX77779_CHG_CNFG_14_TPWROUT_SHIFT	3
#define MAX77779_CHG_CNFG_14_TPWROUT_MASK	(0x7 << 3)
#define MAX77779_CHG_CNFG_14_TPWROUT_CLEAR	(~(0x7 << 3))
#define MAX77779_CHG_CNFG_14_AICL_SHIFT	6
#define MAX77779_CHG_CNFG_14_AICL_MASK	(0x3 << 6)
#define MAX77779_CHG_CNFG_14_AICL_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_cnfg_14_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SPR_2_0=%x",
		FIELD2VALUE(MAX77779_SPR_2_0, val));
	i += scnprintf(&buff[i], len - i, " TPWROUT=%x",
		FIELD2VALUE(MAX77779_TPWROUT, val));
	i += scnprintf(&buff[i], len - i, " AICL=%x",
		FIELD2VALUE(MAX77779_AICL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_14_spr_2_0,2,0)
MAX77779_BFF(max77779_chg_cnfg_14_tpwrout,5,3)
MAX77779_BFF(max77779_chg_cnfg_14_aicl,7,6)

/*
 * CHG_CNFG_15,0x1B,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 * WDTEN,MASTER_DC,SPSN_DET_EN,OTG_V_PGM,MINVSYS[4:2],SPR_7_6[6:2]
 */
#define MAX77779_CHG_CNFG_15	0xcb
#define MAX77779_CHG_CNFG_15_WDTEN_SHIFT	0
#define MAX77779_CHG_CNFG_15_WDTEN_MASK	(0x1 << 0)
#define MAX77779_CHG_CNFG_15_WDTEN_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_CNFG_15_MASTER_DC_SHIFT	1
#define MAX77779_CHG_CNFG_15_MASTER_DC_MASK	(0x1 << 1)
#define MAX77779_CHG_CNFG_15_MASTER_DC_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_CNFG_15_SPSN_DET_EN_SHIFT	2
#define MAX77779_CHG_CNFG_15_SPSN_DET_EN_MASK	(0x1 << 2)
#define MAX77779_CHG_CNFG_15_SPSN_DET_EN_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_CNFG_15_OTG_V_PGM_SHIFT	3
#define MAX77779_CHG_CNFG_15_OTG_V_PGM_MASK	(0x1 << 3)
#define MAX77779_CHG_CNFG_15_OTG_V_PGM_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_CNFG_15_MINVSYS_SHIFT	4
#define MAX77779_CHG_CNFG_15_MINVSYS_MASK	(0x3 << 4)
#define MAX77779_CHG_CNFG_15_MINVSYS_CLEAR	(~(0x3 << 4))
#define MAX77779_CHG_CNFG_15_SPR_7_6_SHIFT	6
#define MAX77779_CHG_CNFG_15_SPR_7_6_MASK	(0x3 << 6)
#define MAX77779_CHG_CNFG_15_SPR_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_cnfg_15_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " WDTEN=%x",
		FIELD2VALUE(MAX77779_WDTEN, val));
	i += scnprintf(&buff[i], len - i, " MASTER_DC=%x",
		FIELD2VALUE(MAX77779_MASTER_DC, val));
	i += scnprintf(&buff[i], len - i, " SPSN_DET_EN=%x",
		FIELD2VALUE(MAX77779_SPSN_DET_EN, val));
	i += scnprintf(&buff[i], len - i, " OTG_V_PGM=%x",
		FIELD2VALUE(MAX77779_OTG_V_PGM, val));
	i += scnprintf(&buff[i], len - i, " MINVSYS=%x",
		FIELD2VALUE(MAX77779_MINVSYS, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_6=%x",
		FIELD2VALUE(MAX77779_SPR_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_15_wdten,0,0)
MAX77779_BFF(max77779_chg_cnfg_15_master_dc,1,1)
MAX77779_BFF(max77779_chg_cnfg_15_spsn_det_en,2,2)
MAX77779_BFF(max77779_chg_cnfg_15_otg_v_pgm,3,3)
MAX77779_BFF(max77779_chg_cnfg_15_minvsys,5,4)
MAX77779_BFF(max77779_chg_cnfg_15_spr_7_6,7,6)

/*
 * CHG_CNFG_16,0x1C,0b10010000,0x90,OTP:SHADOW, Reset_Type:O
 * SLOWLX[0:2],DIS_IR_CTRL,INLIM_CLK[3:2],SPR_5,AUTO_FSHIP_TIME[6:2]
 */
#define MAX77779_CHG_CNFG_16	0xcc
#define MAX77779_CHG_CNFG_16_SLOWLX_SHIFT	0
#define MAX77779_CHG_CNFG_16_SLOWLX_MASK	(0x3 << 0)
#define MAX77779_CHG_CNFG_16_SLOWLX_CLEAR	(~(0x3 << 0))
#define MAX77779_CHG_CNFG_16_DIS_IR_CTRL_SHIFT	2
#define MAX77779_CHG_CNFG_16_DIS_IR_CTRL_MASK	(0x1 << 2)
#define MAX77779_CHG_CNFG_16_DIS_IR_CTRL_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_CNFG_16_INLIM_CLK_SHIFT	3
#define MAX77779_CHG_CNFG_16_INLIM_CLK_MASK	(0x3 << 3)
#define MAX77779_CHG_CNFG_16_INLIM_CLK_CLEAR	(~(0x3 << 3))
#define MAX77779_CHG_CNFG_16_SPR_5_SHIFT	5
#define MAX77779_CHG_CNFG_16_SPR_5_MASK	(0x1 << 5)
#define MAX77779_CHG_CNFG_16_SPR_5_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_CNFG_16_AUTO_FSHIP_TIME_SHIFT	6
#define MAX77779_CHG_CNFG_16_AUTO_FSHIP_TIME_MASK	(0x3 << 6)
#define MAX77779_CHG_CNFG_16_AUTO_FSHIP_TIME_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_chg_cnfg_16_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SLOWLX=%x",
		FIELD2VALUE(MAX77779_SLOWLX, val));
	i += scnprintf(&buff[i], len - i, " DIS_IR_CTRL=%x",
		FIELD2VALUE(MAX77779_DIS_IR_CTRL, val));
	i += scnprintf(&buff[i], len - i, " INLIM_CLK=%x",
		FIELD2VALUE(MAX77779_INLIM_CLK, val));
	i += scnprintf(&buff[i], len - i, " SPR_5=%x",
		FIELD2VALUE(MAX77779_SPR_5, val));
	i += scnprintf(&buff[i], len - i, " AUTO_FSHIP_TIME=%x",
		FIELD2VALUE(MAX77779_AUTO_FSHIP_TIME, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_16_slowlx,1,0)
MAX77779_BFF(max77779_chg_cnfg_16_dis_ir_ctrl,2,2)
MAX77779_BFF(max77779_chg_cnfg_16_inlim_clk,4,3)
MAX77779_BFF(max77779_chg_cnfg_16_spr_5,5,5)
MAX77779_BFF(max77779_chg_cnfg_16_auto_fship_time,7,6)

/*
 * CHG_CNFG_17,0x1D,0b11000011,0xc3,OTP:SHADOW, Reset_Type:O
 * THM3_JEITA_EN,JEITA_AUX_EN,JEITA_AUX_ZONE,ICHGCC_JAUX,VCHGCV_JAUX,
 * SPR_5,VDP1_STP_BST,VDP2_STP_BST
 */
#define MAX77779_CHG_CNFG_17	0xcd
#define MAX77779_CHG_CNFG_17_THM3_JEITA_EN_SHIFT	0
#define MAX77779_CHG_CNFG_17_THM3_JEITA_EN_MASK	(0x1 << 0)
#define MAX77779_CHG_CNFG_17_THM3_JEITA_EN_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_CNFG_17_JEITA_AUX_EN_SHIFT	1
#define MAX77779_CHG_CNFG_17_JEITA_AUX_EN_MASK	(0x1 << 1)
#define MAX77779_CHG_CNFG_17_JEITA_AUX_EN_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_CNFG_17_JEITA_AUX_ZONE_SHIFT	2
#define MAX77779_CHG_CNFG_17_JEITA_AUX_ZONE_MASK	(0x1 << 2)
#define MAX77779_CHG_CNFG_17_JEITA_AUX_ZONE_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_CNFG_17_ICHGCC_JAUX_SHIFT	3
#define MAX77779_CHG_CNFG_17_ICHGCC_JAUX_MASK	(0x1 << 3)
#define MAX77779_CHG_CNFG_17_ICHGCC_JAUX_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_CNFG_17_VCHGCV_JAUX_SHIFT	4
#define MAX77779_CHG_CNFG_17_VCHGCV_JAUX_MASK	(0x1 << 4)
#define MAX77779_CHG_CNFG_17_VCHGCV_JAUX_CLEAR	(~(0x1 << 4))
#define MAX77779_CHG_CNFG_17_SPR_5_SHIFT	5
#define MAX77779_CHG_CNFG_17_SPR_5_MASK	(0x1 << 5)
#define MAX77779_CHG_CNFG_17_SPR_5_CLEAR	(~(0x1 << 5))
#define MAX77779_CHG_CNFG_17_VDP1_STP_BST_SHIFT	6
#define MAX77779_CHG_CNFG_17_VDP1_STP_BST_MASK	(0x1 << 6)
#define MAX77779_CHG_CNFG_17_VDP1_STP_BST_CLEAR	(~(0x1 << 6))
#define MAX77779_CHG_CNFG_17_VDP2_STP_BST_SHIFT	7
#define MAX77779_CHG_CNFG_17_VDP2_STP_BST_MASK	(0x1 << 7)
#define MAX77779_CHG_CNFG_17_VDP2_STP_BST_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_chg_cnfg_17_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " THM3_JEITA_EN=%x",
		FIELD2VALUE(MAX77779_THM3_JEITA_EN, val));
	i += scnprintf(&buff[i], len - i, " JEITA_AUX_EN=%x",
		FIELD2VALUE(MAX77779_JEITA_AUX_EN, val));
	i += scnprintf(&buff[i], len - i, " JEITA_AUX_ZONE=%x",
		FIELD2VALUE(MAX77779_JEITA_AUX_ZONE, val));
	i += scnprintf(&buff[i], len - i, " ICHGCC_JAUX=%x",
		FIELD2VALUE(MAX77779_ICHGCC_JAUX, val));
	i += scnprintf(&buff[i], len - i, " VCHGCV_JAUX=%x",
		FIELD2VALUE(MAX77779_VCHGCV_JAUX, val));
	i += scnprintf(&buff[i], len - i, " SPR_5=%x",
		FIELD2VALUE(MAX77779_SPR_5, val));
	i += scnprintf(&buff[i], len - i, " VDP1_STP_BST=%x",
		FIELD2VALUE(MAX77779_VDP1_STP_BST, val));
	i += scnprintf(&buff[i], len - i, " VDP2_STP_BST=%x",
		FIELD2VALUE(MAX77779_VDP2_STP_BST, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cnfg_17_thm3_jeita_en,0,0)
MAX77779_BFF(max77779_chg_cnfg_17_jeita_aux_en,1,1)
MAX77779_BFF(max77779_chg_cnfg_17_jeita_aux_zone,2,2)
MAX77779_BFF(max77779_chg_cnfg_17_ichgcc_jaux,3,3)
MAX77779_BFF(max77779_chg_cnfg_17_vchgcv_jaux,4,4)
MAX77779_BFF(max77779_chg_cnfg_17_spr_5,5,5)
MAX77779_BFF(max77779_chg_cnfg_17_vdp1_stp_bst,6,6)
MAX77779_BFF(max77779_chg_cnfg_17_vdp2_stp_bst,7,7)

/*
 * SYS_UVLO1_CNFG_0,0x1E,0b00001000,0x8,OTP:SHADOW, Reset_Type:O
 * SYS_UVLO1[0:4],SYS_UVLO1_HYST[4:2],SPR_7_6[6:2]
 */
#define MAX77779_SYS_UVLO1_CNFG_0	0xce
#define MAX77779_SYS_UVLO1_CNFG_0_SYS_UVLO1_SHIFT	0
#define MAX77779_SYS_UVLO1_CNFG_0_SYS_UVLO1_MASK	(0xf << 0)
#define MAX77779_SYS_UVLO1_CNFG_0_SYS_UVLO1_CLEAR	(~(0xf << 0))
#define MAX77779_SYS_UVLO1_CNFG_0_SYS_UVLO1_HYST_SHIFT	4
#define MAX77779_SYS_UVLO1_CNFG_0_SYS_UVLO1_HYST_MASK	(0x3 << 4)
#define MAX77779_SYS_UVLO1_CNFG_0_SYS_UVLO1_HYST_CLEAR	(~(0x3 << 4))
#define MAX77779_SYS_UVLO1_CNFG_0_SPR_7_6_SHIFT	6
#define MAX77779_SYS_UVLO1_CNFG_0_SPR_7_6_MASK	(0x3 << 6)
#define MAX77779_SYS_UVLO1_CNFG_0_SPR_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_sys_uvlo1_cnfg_0_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SYS_UVLO1=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_HYST=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_HYST, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_6=%x",
		FIELD2VALUE(MAX77779_SPR_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_sys_uvlo1_cnfg_0_sys_uvlo1,3,0)
MAX77779_BFF(max77779_sys_uvlo1_cnfg_0_sys_uvlo1_hyst,5,4)
MAX77779_BFF(max77779_sys_uvlo1_cnfg_0_spr_7_6,7,6)

/*
 * SYS_UVLO1_CNFG_1,0x1F,0b10000000,0x80,OTP:SHADOW, Reset_Type:O
 * SYS_UVLO1_REL[0:2],SPR_3_2[2:2],SYS_UVLO1_DET,SPR_6_5[5:2],SYS_UVLO1_VDRP1_EN
 */
#define MAX77779_SYS_UVLO1_CNFG_1	0xcf
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_REL_SHIFT	0
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_REL_MASK	(0x3 << 0)
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_REL_CLEAR	(~(0x3 << 0))
#define MAX77779_SYS_UVLO1_CNFG_1_SPR_3_2_SHIFT	2
#define MAX77779_SYS_UVLO1_CNFG_1_SPR_3_2_MASK	(0x3 << 2)
#define MAX77779_SYS_UVLO1_CNFG_1_SPR_3_2_CLEAR	(~(0x3 << 2))
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_DET_SHIFT	4
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_DET_MASK	(0x1 << 4)
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_DET_CLEAR	(~(0x1 << 4))
#define MAX77779_SYS_UVLO1_CNFG_1_SPR_6_5_SHIFT	5
#define MAX77779_SYS_UVLO1_CNFG_1_SPR_6_5_MASK	(0x3 << 5)
#define MAX77779_SYS_UVLO1_CNFG_1_SPR_6_5_CLEAR	(~(0x3 << 5))
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_VDRP1_EN_SHIFT	7
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_VDRP1_EN_MASK	(0x1 << 7)
#define MAX77779_SYS_UVLO1_CNFG_1_SYS_UVLO1_VDRP1_EN_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_sys_uvlo1_cnfg_1_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_REL=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_REL, val));
	i += scnprintf(&buff[i], len - i, " SPR_3_2=%x",
		FIELD2VALUE(MAX77779_SPR_3_2, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_DET=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_DET, val));
	i += scnprintf(&buff[i], len - i, " SPR_6_5=%x",
		FIELD2VALUE(MAX77779_SPR_6_5, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_VDRP1_EN=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_VDRP1_EN, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_sys_uvlo1_cnfg_1_sys_uvlo1_rel,1,0)
MAX77779_BFF(max77779_sys_uvlo1_cnfg_1_spr_3_2,3,2)
MAX77779_BFF(max77779_sys_uvlo1_cnfg_1_sys_uvlo1_det,4,4)
MAX77779_BFF(max77779_sys_uvlo1_cnfg_1_spr_6_5,6,5)
MAX77779_BFF(max77779_sys_uvlo1_cnfg_1_sys_uvlo1_vdrp1_en,7,7)

/*
 * SYS_UVLO2_CNFG_0,0x20,0b00000100,0x4,OTP:SHADOW, Reset_Type:O
 * SYS_UVLO2[0:4],SYS_UVLO2_HYST[4:2],SPR_7_6[6:2]
 */
#define MAX77779_SYS_UVLO2_CNFG_0	0xd0
#define MAX77779_SYS_UVLO2_CNFG_0_SYS_UVLO2_SHIFT	0
#define MAX77779_SYS_UVLO2_CNFG_0_SYS_UVLO2_MASK	(0xf << 0)
#define MAX77779_SYS_UVLO2_CNFG_0_SYS_UVLO2_CLEAR	(~(0xf << 0))
#define MAX77779_SYS_UVLO2_CNFG_0_SYS_UVLO2_HYST_SHIFT	4
#define MAX77779_SYS_UVLO2_CNFG_0_SYS_UVLO2_HYST_MASK	(0x3 << 4)
#define MAX77779_SYS_UVLO2_CNFG_0_SYS_UVLO2_HYST_CLEAR	(~(0x3 << 4))
#define MAX77779_SYS_UVLO2_CNFG_0_SPR_7_6_SHIFT	6
#define MAX77779_SYS_UVLO2_CNFG_0_SPR_7_6_MASK	(0x3 << 6)
#define MAX77779_SYS_UVLO2_CNFG_0_SPR_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_sys_uvlo2_cnfg_0_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SYS_UVLO2=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_HYST=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_HYST, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_6=%x",
		FIELD2VALUE(MAX77779_SPR_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_sys_uvlo2_cnfg_0_sys_uvlo2,3,0)
MAX77779_BFF(max77779_sys_uvlo2_cnfg_0_sys_uvlo2_hyst,5,4)
MAX77779_BFF(max77779_sys_uvlo2_cnfg_0_spr_7_6,7,6)

/*
 * SYS_UVLO2_CNFG_1,0x21,0b10000000,0x80,OTP:SHADOW, Reset_Type:O
 * SYS_UVLO2_REL[0:2],SPR_3_2[2:2],SYS_UVLO2_DET,SPR_6_5[5:2],SYS_UVLO2_VDRP2_EN
 */
#define MAX77779_SYS_UVLO2_CNFG_1	0xd1
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_REL_SHIFT	0
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_REL_MASK	(0x3 << 0)
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_REL_CLEAR	(~(0x3 << 0))
#define MAX77779_SYS_UVLO2_CNFG_1_SPR_3_2_SHIFT	2
#define MAX77779_SYS_UVLO2_CNFG_1_SPR_3_2_MASK	(0x3 << 2)
#define MAX77779_SYS_UVLO2_CNFG_1_SPR_3_2_CLEAR	(~(0x3 << 2))
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_DET_SHIFT	4
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_DET_MASK	(0x1 << 4)
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_DET_CLEAR	(~(0x1 << 4))
#define MAX77779_SYS_UVLO2_CNFG_1_SPR_6_5_SHIFT	5
#define MAX77779_SYS_UVLO2_CNFG_1_SPR_6_5_MASK	(0x3 << 5)
#define MAX77779_SYS_UVLO2_CNFG_1_SPR_6_5_CLEAR	(~(0x3 << 5))
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_VDRP2_EN_SHIFT	7
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_VDRP2_EN_MASK	(0x1 << 7)
#define MAX77779_SYS_UVLO2_CNFG_1_SYS_UVLO2_VDRP2_EN_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_sys_uvlo2_cnfg_1_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_REL=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_REL, val));
	i += scnprintf(&buff[i], len - i, " SPR_3_2=%x",
		FIELD2VALUE(MAX77779_SPR_3_2, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_DET=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_DET, val));
	i += scnprintf(&buff[i], len - i, " SPR_6_5=%x",
		FIELD2VALUE(MAX77779_SPR_6_5, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_VDRP2_EN=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_VDRP2_EN, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_sys_uvlo2_cnfg_1_sys_uvlo2_rel,1,0)
MAX77779_BFF(max77779_sys_uvlo2_cnfg_1_spr_3_2,3,2)
MAX77779_BFF(max77779_sys_uvlo2_cnfg_1_sys_uvlo2_det,4,4)
MAX77779_BFF(max77779_sys_uvlo2_cnfg_1_spr_6_5,6,5)
MAX77779_BFF(max77779_sys_uvlo2_cnfg_1_sys_uvlo2_vdrp2_en,7,7)

/*
 * BAT_OILO1_CNFG_0,0x22,0b00010000,0x10,OTP:SHADOW, Reset_Type:O
 * BAT_OILO1[0:5],SPR_7_5[5:3]
 */
#define MAX77779_BAT_OILO1_CNFG_0	0xd2
#define MAX77779_BAT_OILO1_CNFG_0_BAT_OILO1_SHIFT	0
#define MAX77779_BAT_OILO1_CNFG_0_BAT_OILO1_MASK	(0x1f << 0)
#define MAX77779_BAT_OILO1_CNFG_0_BAT_OILO1_CLEAR	(~(0x1f << 0))
#define MAX77779_BAT_OILO1_CNFG_0_SPR_7_5_SHIFT	5
#define MAX77779_BAT_OILO1_CNFG_0_SPR_7_5_MASK	(0x7 << 5)
#define MAX77779_BAT_OILO1_CNFG_0_SPR_7_5_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_bat_oilo1_cnfg_0_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OILO1=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_5=%x",
		FIELD2VALUE(MAX77779_SPR_7_5, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo1_cnfg_0_bat_oilo1,4,0)
MAX77779_BFF(max77779_bat_oilo1_cnfg_0_spr_7_5,7,5)

/*
 * BAT_OILO1_CNFG_1,0x23,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 * BAT_OILO1_DET[0:5],BAT_OILO1_REL[5:3]
 */
#define MAX77779_BAT_OILO1_CNFG_1	0xd3
#define MAX77779_BAT_OILO1_CNFG_1_BAT_OILO1_DET_SHIFT	0
#define MAX77779_BAT_OILO1_CNFG_1_BAT_OILO1_DET_MASK	(0x1f << 0)
#define MAX77779_BAT_OILO1_CNFG_1_BAT_OILO1_DET_CLEAR	(~(0x1f << 0))
#define MAX77779_BAT_OILO1_CNFG_1_BAT_OILO1_REL_SHIFT	5
#define MAX77779_BAT_OILO1_CNFG_1_BAT_OILO1_REL_MASK	(0x7 << 5)
#define MAX77779_BAT_OILO1_CNFG_1_BAT_OILO1_REL_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_bat_oilo1_cnfg_1_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OILO1_DET=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_DET, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_REL=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_REL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo1_cnfg_1_bat_oilo1_det,4,0)
MAX77779_BFF(max77779_bat_oilo1_cnfg_1_bat_oilo1_rel,7,5)

/*
 * BAT_OILO1_CNFG_2,0x24,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 * BAT_OILO1_INT_DET[0:5],BAT_OILO1_INT_REL[5:3]
 */
#define MAX77779_BAT_OILO1_CNFG_2	0xd4
#define MAX77779_BAT_OILO1_CNFG_2_BAT_OILO1_INT_DET_SHIFT	0
#define MAX77779_BAT_OILO1_CNFG_2_BAT_OILO1_INT_DET_MASK	(0x1f << 0)
#define MAX77779_BAT_OILO1_CNFG_2_BAT_OILO1_INT_DET_CLEAR	(~(0x1f << 0))
#define MAX77779_BAT_OILO1_CNFG_2_BAT_OILO1_INT_REL_SHIFT	5
#define MAX77779_BAT_OILO1_CNFG_2_BAT_OILO1_INT_REL_MASK	(0x7 << 5)
#define MAX77779_BAT_OILO1_CNFG_2_BAT_OILO1_INT_REL_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_bat_oilo1_cnfg_2_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OILO1_INT_DET=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_INT_DET, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_INT_REL=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_INT_REL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo1_cnfg_2_bat_oilo1_int_det,4,0)
MAX77779_BFF(max77779_bat_oilo1_cnfg_2_bat_oilo1_int_rel,7,5)

/*
 * BAT_OILO1_CNFG_3,0x25,0b10000010,0x82,OTP:SHADOW, Reset_Type:O
 * BAT_OPEN_TO_1[0:4],SPR_3,BAT_OILO_INT_CLR,BAT_OILO1_VDRP1_EN,BAT_OILO1_VDRP2_EN
 */
#define MAX77779_BAT_OILO1_CNFG_3	0xd5
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OPEN_TO_1_SHIFT	0
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OPEN_TO_1_MASK	(0xf << 0)
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OPEN_TO_1_CLEAR	(~(0xf << 0))
#define MAX77779_BAT_OILO1_CNFG_3_SPR_3_SHIFT	4
#define MAX77779_BAT_OILO1_CNFG_3_SPR_3_MASK	(0x1 << 4)
#define MAX77779_BAT_OILO1_CNFG_3_SPR_3_CLEAR	(~(0x1 << 4))
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO_INT_CLR_SHIFT	5
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO_INT_CLR_MASK	(0x1 << 5)
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO_INT_CLR_CLEAR	(~(0x1 << 5))
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO1_VDRP1_EN_SHIFT	6
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO1_VDRP1_EN_MASK	(0x1 << 6)
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO1_VDRP1_EN_CLEAR	(~(0x1 << 6))
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO1_VDRP2_EN_SHIFT	7
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO1_VDRP2_EN_MASK	(0x1 << 7)
#define MAX77779_BAT_OILO1_CNFG_3_BAT_OILO1_VDRP2_EN_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_bat_oilo1_cnfg_3_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OPEN_TO_1=%x",
		FIELD2VALUE(MAX77779_BAT_OPEN_TO_1, val));
	i += scnprintf(&buff[i], len - i, " SPR_3=%x",
		FIELD2VALUE(MAX77779_SPR_3, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO_INT_CLR=%x",
		FIELD2VALUE(MAX77779_BAT_OILO_INT_CLR, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_VDRP1_EN=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_VDRP1_EN, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_VDRP2_EN=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_VDRP2_EN, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo1_cnfg_3_bat_open_to_1,3,0)
MAX77779_BFF(max77779_bat_oilo1_cnfg_3_spr_3,4,4)
MAX77779_BFF(max77779_bat_oilo1_cnfg_3_bat_oilo_int_clr,5,5)
MAX77779_BFF(max77779_bat_oilo1_cnfg_3_bat_oilo1_vdrp1_en,6,6)
MAX77779_BFF(max77779_bat_oilo1_cnfg_3_bat_oilo1_vdrp2_en,7,7)

/*
 * BAT_OILO2_CNFG_0,0x26,0b00010000,0x10,OTP:SHADOW, Reset_Type:O
 * BAT_OILO2[0:5],SPR_7_5[5:3]
 */
#define MAX77779_BAT_OILO2_CNFG_0	0xd6
#define MAX77779_BAT_OILO2_CNFG_0_BAT_OILO2_SHIFT	0
#define MAX77779_BAT_OILO2_CNFG_0_BAT_OILO2_MASK	(0x1f << 0)
#define MAX77779_BAT_OILO2_CNFG_0_BAT_OILO2_CLEAR	(~(0x1f << 0))
#define MAX77779_BAT_OILO2_CNFG_0_SPR_7_5_SHIFT	5
#define MAX77779_BAT_OILO2_CNFG_0_SPR_7_5_MASK	(0x7 << 5)
#define MAX77779_BAT_OILO2_CNFG_0_SPR_7_5_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_bat_oilo2_cnfg_0_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OILO2=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_5=%x",
		FIELD2VALUE(MAX77779_SPR_7_5, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo2_cnfg_0_bat_oilo2,4,0)
MAX77779_BFF(max77779_bat_oilo2_cnfg_0_spr_7_5,7,5)

/*
 * BAT_OILO2_CNFG_1,0x27,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 * BAT_OILO2_DET[0:5],BAT_OILO2_REL[5:3]
 */
#define MAX77779_BAT_OILO2_CNFG_1	0xd7
#define MAX77779_BAT_OILO2_CNFG_1_BAT_OILO2_DET_SHIFT	0
#define MAX77779_BAT_OILO2_CNFG_1_BAT_OILO2_DET_MASK	(0x1f << 0)
#define MAX77779_BAT_OILO2_CNFG_1_BAT_OILO2_DET_CLEAR	(~(0x1f << 0))
#define MAX77779_BAT_OILO2_CNFG_1_BAT_OILO2_REL_SHIFT	5
#define MAX77779_BAT_OILO2_CNFG_1_BAT_OILO2_REL_MASK	(0x7 << 5)
#define MAX77779_BAT_OILO2_CNFG_1_BAT_OILO2_REL_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_bat_oilo2_cnfg_1_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OILO2_DET=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_DET, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_REL=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_REL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo2_cnfg_1_bat_oilo2_det,4,0)
MAX77779_BFF(max77779_bat_oilo2_cnfg_1_bat_oilo2_rel,7,5)

/*
 * BAT_OILO2_CNFG_2,0x28,0b00000000,0x0,OTP:SHADOW, Reset_Type:O
 * BAT_OILO2_INT_DET[0:5],BAT_OILO2_INT_REL[5:3]
 */
#define MAX77779_BAT_OILO2_CNFG_2	0xd8
#define MAX77779_BAT_OILO2_CNFG_2_BAT_OILO2_INT_DET_SHIFT	0
#define MAX77779_BAT_OILO2_CNFG_2_BAT_OILO2_INT_DET_MASK	(0x1f << 0)
#define MAX77779_BAT_OILO2_CNFG_2_BAT_OILO2_INT_DET_CLEAR	(~(0x1f << 0))
#define MAX77779_BAT_OILO2_CNFG_2_BAT_OILO2_INT_REL_SHIFT	5
#define MAX77779_BAT_OILO2_CNFG_2_BAT_OILO2_INT_REL_MASK	(0x7 << 5)
#define MAX77779_BAT_OILO2_CNFG_2_BAT_OILO2_INT_REL_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_bat_oilo2_cnfg_2_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OILO2_INT_DET=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_INT_DET, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_INT_REL=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_INT_REL, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo2_cnfg_2_bat_oilo2_int_det,4,0)
MAX77779_BFF(max77779_bat_oilo2_cnfg_2_bat_oilo2_int_rel,7,5)

/*
 * BAT_OILO2_CNFG_3,0x29,0b00000010,0x2,OTP:SHADOW, Reset_Type:O
 * BAT_OPEN_TO_2[0:4],SPR_5_4[4:2],BAT_OILO2_VDRP1_EN,BAT_OILO2_VDRP2_EN
 */
#define MAX77779_BAT_OILO2_CNFG_3	0xd9
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OPEN_TO_2_SHIFT	0
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OPEN_TO_2_MASK	(0xf << 0)
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OPEN_TO_2_CLEAR	(~(0xf << 0))
#define MAX77779_BAT_OILO2_CNFG_3_SPR_5_4_SHIFT	4
#define MAX77779_BAT_OILO2_CNFG_3_SPR_5_4_MASK	(0x3 << 4)
#define MAX77779_BAT_OILO2_CNFG_3_SPR_5_4_CLEAR	(~(0x3 << 4))
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OILO2_VDRP1_EN_SHIFT	6
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OILO2_VDRP1_EN_MASK	(0x1 << 6)
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OILO2_VDRP1_EN_CLEAR	(~(0x1 << 6))
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OILO2_VDRP2_EN_SHIFT	7
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OILO2_VDRP2_EN_MASK	(0x1 << 7)
#define MAX77779_BAT_OILO2_CNFG_3_BAT_OILO2_VDRP2_EN_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_bat_oilo2_cnfg_3_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OPEN_TO_2=%x",
		FIELD2VALUE(MAX77779_BAT_OPEN_TO_2, val));
	i += scnprintf(&buff[i], len - i, " SPR_5_4=%x",
		FIELD2VALUE(MAX77779_SPR_5_4, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_VDRP1_EN=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_VDRP1_EN, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO2_VDRP2_EN=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_VDRP2_EN, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bat_oilo2_cnfg_3_bat_open_to_2,3,0)
MAX77779_BFF(max77779_bat_oilo2_cnfg_3_spr_5_4,5,4)
MAX77779_BFF(max77779_bat_oilo2_cnfg_3_bat_oilo2_vdrp1_en,6,6)
MAX77779_BFF(max77779_bat_oilo2_cnfg_3_bat_oilo2_vdrp2_en,7,7)

/*
 * CHG_CUST_TM,0x2E,0b00000000,0x0,Reset_Type:O
 * BAT_OILO2_CTM,BAT_OILO1_CTM,SYS_UVLO2_CTM,SYS_UVLO1_CTM,SPR_7_4[4:4]
 */
#define MAX77779_CHG_CUST_TM	0xde
#define MAX77779_CHG_CUST_TM_BAT_OILO2_CTM_SHIFT	0
#define MAX77779_CHG_CUST_TM_BAT_OILO2_CTM_MASK	(0x1 << 0)
#define MAX77779_CHG_CUST_TM_BAT_OILO2_CTM_CLEAR	(~(0x1 << 0))
#define MAX77779_CHG_CUST_TM_BAT_OILO1_CTM_SHIFT	1
#define MAX77779_CHG_CUST_TM_BAT_OILO1_CTM_MASK	(0x1 << 1)
#define MAX77779_CHG_CUST_TM_BAT_OILO1_CTM_CLEAR	(~(0x1 << 1))
#define MAX77779_CHG_CUST_TM_SYS_UVLO2_CTM_SHIFT	2
#define MAX77779_CHG_CUST_TM_SYS_UVLO2_CTM_MASK	(0x1 << 2)
#define MAX77779_CHG_CUST_TM_SYS_UVLO2_CTM_CLEAR	(~(0x1 << 2))
#define MAX77779_CHG_CUST_TM_SYS_UVLO1_CTM_SHIFT	3
#define MAX77779_CHG_CUST_TM_SYS_UVLO1_CTM_MASK	(0x1 << 3)
#define MAX77779_CHG_CUST_TM_SYS_UVLO1_CTM_CLEAR	(~(0x1 << 3))
#define MAX77779_CHG_CUST_TM_SPR_7_4_SHIFT	4
#define MAX77779_CHG_CUST_TM_SPR_7_4_MASK	(0xf << 4)
#define MAX77779_CHG_CUST_TM_SPR_7_4_CLEAR	(~(0xf << 4))
static inline const char *
max77779_chg_cust_tm_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BAT_OILO2_CTM=%x",
		FIELD2VALUE(MAX77779_BAT_OILO2_CTM, val));
	i += scnprintf(&buff[i], len - i, " BAT_OILO1_CTM=%x",
		FIELD2VALUE(MAX77779_BAT_OILO1_CTM, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO2_CTM=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO2_CTM, val));
	i += scnprintf(&buff[i], len - i, " SYS_UVLO1_CTM=%x",
		FIELD2VALUE(MAX77779_SYS_UVLO1_CTM, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_4=%x",
		FIELD2VALUE(MAX77779_SPR_7_4, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_chg_cust_tm_bat_oilo2_ctm,0,0)
MAX77779_BFF(max77779_chg_cust_tm_bat_oilo1_ctm,1,1)
MAX77779_BFF(max77779_chg_cust_tm_sys_uvlo2_ctm,2,2)
MAX77779_BFF(max77779_chg_cust_tm_sys_uvlo1_ctm,3,3)
MAX77779_BFF(max77779_chg_cust_tm_spr_7_4,7,4)
/*
 * Section: FG_RAM 0x00 16
 */

/*
 * Section: FG_FUNC 0xE0 16
 */


/*
 * FG_INT_STS,0x00,0b00000010,0x2,Reset_Type:F
 * Spare_int_0,PONR,Imn,Bst,CmdFwDone,ThmHot,Imx,dSOCi,Vmn,Tmn,Smn,Bi,
 * Vmx,Tmx,Smx,Br
 */
#define MAX77779_M5_FG_INT_STS	0xe0
#define MAX77779_M5_FG_INT_STS_Spare_int_0_SHIFT	0
#define MAX77779_M5_FG_INT_STS_Spare_int_0_MASK	(0x1 << 0)
#define MAX77779_M5_FG_INT_STS_Spare_int_0_CLEAR	(~(0x1 << 0))
#define MAX77779_M5_FG_INT_STS_PONR_SHIFT	1
#define MAX77779_M5_FG_INT_STS_PONR_MASK	(0x1 << 1)
#define MAX77779_M5_FG_INT_STS_PONR_CLEAR	(~(0x1 << 1))
#define MAX77779_M5_FG_INT_STS_Imn_SHIFT	2
#define MAX77779_M5_FG_INT_STS_Imn_MASK	(0x1 << 2)
#define MAX77779_M5_FG_INT_STS_Imn_CLEAR	(~(0x1 << 2))
#define MAX77779_M5_FG_INT_STS_Bst_SHIFT	3
#define MAX77779_M5_FG_INT_STS_Bst_MASK	(0x1 << 3)
#define MAX77779_M5_FG_INT_STS_Bst_CLEAR	(~(0x1 << 3))
#define MAX77779_M5_FG_INT_STS_CmdFwDone_SHIFT	4
#define MAX77779_M5_FG_INT_STS_CmdFwDone_MASK	(0x1 << 4)
#define MAX77779_M5_FG_INT_STS_CmdFwDone_CLEAR	(~(0x1 << 4))
#define MAX77779_M5_FG_INT_STS_ThmHot_SHIFT	5
#define MAX77779_M5_FG_INT_STS_ThmHot_MASK	(0x1 << 5)
#define MAX77779_M5_FG_INT_STS_ThmHot_CLEAR	(~(0x1 << 5))
#define MAX77779_M5_FG_INT_STS_Imx_SHIFT	6
#define MAX77779_M5_FG_INT_STS_Imx_MASK	(0x1 << 6)
#define MAX77779_M5_FG_INT_STS_Imx_CLEAR	(~(0x1 << 6))
#define MAX77779_M5_FG_INT_STS_dSOCi_SHIFT	7
#define MAX77779_M5_FG_INT_STS_dSOCi_MASK	(0x1 << 7)
#define MAX77779_M5_FG_INT_STS_dSOCi_CLEAR	(~(0x1 << 7))
#define MAX77779_M5_FG_INT_STS_Vmn_SHIFT	8
#define MAX77779_M5_FG_INT_STS_Vmn_MASK	(0x1 << 8)
#define MAX77779_M5_FG_INT_STS_Vmn_CLEAR	(~(0x1 << 8))
#define MAX77779_M5_FG_INT_STS_Tmn_SHIFT	9
#define MAX77779_M5_FG_INT_STS_Tmn_MASK	(0x1 << 9)
#define MAX77779_M5_FG_INT_STS_Tmn_CLEAR	(~(0x1 << 9))
#define MAX77779_M5_FG_INT_STS_Smn_SHIFT	10
#define MAX77779_M5_FG_INT_STS_Smn_MASK	(0x1 << 10)
#define MAX77779_M5_FG_INT_STS_Smn_CLEAR	(~(0x1 << 10))
#define MAX77779_M5_FG_INT_STS_Bi_SHIFT	11
#define MAX77779_M5_FG_INT_STS_Bi_MASK	(0x1 << 11)
#define MAX77779_M5_FG_INT_STS_Bi_CLEAR	(~(0x1 << 11))
#define MAX77779_M5_FG_INT_STS_Vmx_SHIFT	12
#define MAX77779_M5_FG_INT_STS_Vmx_MASK	(0x1 << 12)
#define MAX77779_M5_FG_INT_STS_Vmx_CLEAR	(~(0x1 << 12))
#define MAX77779_M5_FG_INT_STS_Tmx_SHIFT	13
#define MAX77779_M5_FG_INT_STS_Tmx_MASK	(0x1 << 13)
#define MAX77779_M5_FG_INT_STS_Tmx_CLEAR	(~(0x1 << 13))
#define MAX77779_M5_FG_INT_STS_Smx_SHIFT	14
#define MAX77779_M5_FG_INT_STS_Smx_MASK	(0x1 << 14)
#define MAX77779_M5_FG_INT_STS_Smx_CLEAR	(~(0x1 << 14))
#define MAX77779_M5_FG_INT_STS_Br_SHIFT	15
#define MAX77779_M5_FG_INT_STS_Br_MASK	(0x1 << 15)
#define MAX77779_M5_FG_INT_STS_Br_CLEAR	(~(0x1 << 15))
static inline const char *
max77779_m5_fg_int_sts_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " Spare_int_0=%x",
		FIELD2VALUE(MAX77779_Spare_int_0, val));
	i += scnprintf(&buff[i], len - i, " PONR=%x",
		FIELD2VALUE(MAX77779_PONR, val));
	i += scnprintf(&buff[i], len - i, " Imn=%x",
		FIELD2VALUE(MAX77779_Imn, val));
	i += scnprintf(&buff[i], len - i, " Bst=%x",
		FIELD2VALUE(MAX77779_Bst, val));
	i += scnprintf(&buff[i], len - i, " CmdFwDone=%x",
		FIELD2VALUE(MAX77779_CmdFwDone, val));
	i += scnprintf(&buff[i], len - i, " ThmHot=%x",
		FIELD2VALUE(MAX77779_ThmHot, val));
	i += scnprintf(&buff[i], len - i, " Imx=%x",
		FIELD2VALUE(MAX77779_Imx, val));
	i += scnprintf(&buff[i], len - i, " dSOCi=%x",
		FIELD2VALUE(MAX77779_dSOCi, val));
	i += scnprintf(&buff[i], len - i, " Vmn=%x",
		FIELD2VALUE(MAX77779_Vmn, val));
	i += scnprintf(&buff[i], len - i, " Tmn=%x",
		FIELD2VALUE(MAX77779_Tmn, val));
	i += scnprintf(&buff[i], len - i, " Smn=%x",
		FIELD2VALUE(MAX77779_Smn, val));
	i += scnprintf(&buff[i], len - i, " Bi=%x",
		FIELD2VALUE(MAX77779_Bi, val));
	i += scnprintf(&buff[i], len - i, " Vmx=%x",
		FIELD2VALUE(MAX77779_Vmx, val));
	i += scnprintf(&buff[i], len - i, " Tmx=%x",
		FIELD2VALUE(MAX77779_Tmx, val));
	i += scnprintf(&buff[i], len - i, " Smx=%x",
		FIELD2VALUE(MAX77779_Smx, val));
	i += scnprintf(&buff[i], len - i, " Br=%x",
		FIELD2VALUE(MAX77779_Br, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_m5_fg_int_sts_spare_int_0,0,0)
MAX77779_BFF(max77779_m5_fg_int_sts_ponr,1,1)
MAX77779_BFF(max77779_m5_fg_int_sts_imn,2,2)
MAX77779_BFF(max77779_m5_fg_int_sts_bst,3,3)
MAX77779_BFF(max77779_m5_fg_int_sts_cmdfwdone,4,4)
MAX77779_BFF(max77779_m5_fg_int_sts_thmhot,5,5)
MAX77779_BFF(max77779_m5_fg_int_sts_imx,6,6)
MAX77779_BFF(max77779_m5_fg_int_sts_dsoci,7,7)
MAX77779_BFF(max77779_m5_fg_int_sts_vmn,8,8)
MAX77779_BFF(max77779_m5_fg_int_sts_tmn,9,9)
MAX77779_BFF(max77779_m5_fg_int_sts_smn,10,10)
MAX77779_BFF(max77779_m5_fg_int_sts_bi,11,11)
MAX77779_BFF(max77779_m5_fg_int_sts_vmx,12,12)
MAX77779_BFF(max77779_m5_fg_int_sts_tmx,13,13)
MAX77779_BFF(max77779_m5_fg_int_sts_smx,14,14)
MAX77779_BFF(max77779_m5_fg_int_sts_br,15,15)

/*
 * Mask,0x01,0b00000010,0x2,Reset_Type:F
 * Spare_int_0_m,POR_m,Imn_m,Bst_m,CmdFwDone_m,ThmHot_m,Imx_m,dSOCi_m,
 * Vmn_m,Tmn_m,Smn_m,Bi_m,Vmx_m,Tmx_m,Smx_m,Br_m
 */
#define MAX77779_M5_Mask	0xe1
#define MAX77779_M5_Mask_Spare_int_0_m_SHIFT	0
#define MAX77779_M5_Mask_Spare_int_0_m_MASK	(0x1 << 0)
#define MAX77779_M5_Mask_Spare_int_0_m_CLEAR	(~(0x1 << 0))
#define MAX77779_M5_Mask_POR_m_SHIFT	1
#define MAX77779_M5_Mask_POR_m_MASK	(0x1 << 1)
#define MAX77779_M5_Mask_POR_m_CLEAR	(~(0x1 << 1))
#define MAX77779_M5_Mask_Imn_m_SHIFT	2
#define MAX77779_M5_Mask_Imn_m_MASK	(0x1 << 2)
#define MAX77779_M5_Mask_Imn_m_CLEAR	(~(0x1 << 2))
#define MAX77779_M5_Mask_Bst_m_SHIFT	3
#define MAX77779_M5_Mask_Bst_m_MASK	(0x1 << 3)
#define MAX77779_M5_Mask_Bst_m_CLEAR	(~(0x1 << 3))
#define MAX77779_M5_Mask_CmdFwDone_m_SHIFT	4
#define MAX77779_M5_Mask_CmdFwDone_m_MASK	(0x1 << 4)
#define MAX77779_M5_Mask_CmdFwDone_m_CLEAR	(~(0x1 << 4))
#define MAX77779_M5_Mask_ThmHot_m_SHIFT	5
#define MAX77779_M5_Mask_ThmHot_m_MASK	(0x1 << 5)
#define MAX77779_M5_Mask_ThmHot_m_CLEAR	(~(0x1 << 5))
#define MAX77779_M5_Mask_Imx_m_SHIFT	6
#define MAX77779_M5_Mask_Imx_m_MASK	(0x1 << 6)
#define MAX77779_M5_Mask_Imx_m_CLEAR	(~(0x1 << 6))
#define MAX77779_M5_Mask_dSOCi_m_SHIFT	7
#define MAX77779_M5_Mask_dSOCi_m_MASK	(0x1 << 7)
#define MAX77779_M5_Mask_dSOCi_m_CLEAR	(~(0x1 << 7))
#define MAX77779_M5_Mask_Vmn_m_SHIFT	8
#define MAX77779_M5_Mask_Vmn_m_MASK	(0x1 << 8)
#define MAX77779_M5_Mask_Vmn_m_CLEAR	(~(0x1 << 8))
#define MAX77779_M5_Mask_Tmn_m_SHIFT	9
#define MAX77779_M5_Mask_Tmn_m_MASK	(0x1 << 9)
#define MAX77779_M5_Mask_Tmn_m_CLEAR	(~(0x1 << 9))
#define MAX77779_M5_Mask_Smn_m_SHIFT	10
#define MAX77779_M5_Mask_Smn_m_MASK	(0x1 << 10)
#define MAX77779_M5_Mask_Smn_m_CLEAR	(~(0x1 << 10))
#define MAX77779_M5_Mask_Bi_m_SHIFT	11
#define MAX77779_M5_Mask_Bi_m_MASK	(0x1 << 11)
#define MAX77779_M5_Mask_Bi_m_CLEAR	(~(0x1 << 11))
#define MAX77779_M5_Mask_Vmx_m_SHIFT	12
#define MAX77779_M5_Mask_Vmx_m_MASK	(0x1 << 12)
#define MAX77779_M5_Mask_Vmx_m_CLEAR	(~(0x1 << 12))
#define MAX77779_M5_Mask_Tmx_m_SHIFT	13
#define MAX77779_M5_Mask_Tmx_m_MASK	(0x1 << 13)
#define MAX77779_M5_Mask_Tmx_m_CLEAR	(~(0x1 << 13))
#define MAX77779_M5_Mask_Smx_m_SHIFT	14
#define MAX77779_M5_Mask_Smx_m_MASK	(0x1 << 14)
#define MAX77779_M5_Mask_Smx_m_CLEAR	(~(0x1 << 14))
#define MAX77779_M5_Mask_Br_m_SHIFT	15
#define MAX77779_M5_Mask_Br_m_MASK	(0x1 << 15)
#define MAX77779_M5_Mask_Br_m_CLEAR	(~(0x1 << 15))
static inline const char *
max77779_m5_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " Spare_int_0_m=%x",
		FIELD2VALUE(MAX77779_Spare_int_0_m, val));
	i += scnprintf(&buff[i], len - i, " POR_m=%x",
		FIELD2VALUE(MAX77779_POR_m, val));
	i += scnprintf(&buff[i], len - i, " Imn_m=%x",
		FIELD2VALUE(MAX77779_Imn_m, val));
	i += scnprintf(&buff[i], len - i, " Bst_m=%x",
		FIELD2VALUE(MAX77779_Bst_m, val));
	i += scnprintf(&buff[i], len - i, " CmdFwDone_m=%x",
		FIELD2VALUE(MAX77779_CmdFwDone_m, val));
	i += scnprintf(&buff[i], len - i, " ThmHot_m=%x",
		FIELD2VALUE(MAX77779_ThmHot_m, val));
	i += scnprintf(&buff[i], len - i, " Imx_m=%x",
		FIELD2VALUE(MAX77779_Imx_m, val));
	i += scnprintf(&buff[i], len - i, " dSOCi_m=%x",
		FIELD2VALUE(MAX77779_dSOCi_m, val));
	i += scnprintf(&buff[i], len - i, " Vmn_m=%x",
		FIELD2VALUE(MAX77779_Vmn_m, val));
	i += scnprintf(&buff[i], len - i, " Tmn_m=%x",
		FIELD2VALUE(MAX77779_Tmn_m, val));
	i += scnprintf(&buff[i], len - i, " Smn_m=%x",
		FIELD2VALUE(MAX77779_Smn_m, val));
	i += scnprintf(&buff[i], len - i, " Bi_m=%x",
		FIELD2VALUE(MAX77779_Bi_m, val));
	i += scnprintf(&buff[i], len - i, " Vmx_m=%x",
		FIELD2VALUE(MAX77779_Vmx_m, val));
	i += scnprintf(&buff[i], len - i, " Tmx_m=%x",
		FIELD2VALUE(MAX77779_Tmx_m, val));
	i += scnprintf(&buff[i], len - i, " Smx_m=%x",
		FIELD2VALUE(MAX77779_Smx_m, val));
	i += scnprintf(&buff[i], len - i, " Br_m=%x",
		FIELD2VALUE(MAX77779_Br_m, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_m5_mask_spare_int_0_m,0,0)
MAX77779_BFF(max77779_m5_mask_por_m,1,1)
MAX77779_BFF(max77779_m5_mask_imn_m,2,2)
MAX77779_BFF(max77779_m5_mask_bst_m,3,3)
MAX77779_BFF(max77779_m5_mask_cmdfwdone_m,4,4)
MAX77779_BFF(max77779_m5_mask_thmhot_m,5,5)
MAX77779_BFF(max77779_m5_mask_imx_m,6,6)
MAX77779_BFF(max77779_m5_mask_dsoci_m,7,7)
MAX77779_BFF(max77779_m5_mask_vmn_m,8,8)
MAX77779_BFF(max77779_m5_mask_tmn_m,9,9)
MAX77779_BFF(max77779_m5_mask_smn_m,10,10)
MAX77779_BFF(max77779_m5_mask_bi_m,11,11)
MAX77779_BFF(max77779_m5_mask_vmx_m,12,12)
MAX77779_BFF(max77779_m5_mask_tmx_m,13,13)
MAX77779_BFF(max77779_m5_mask_smx_m,14,14)
MAX77779_BFF(max77779_m5_mask_br_m,15,15)

/*
 * Command_fw,0x09,0b00000000,0x0,Reset_Type:F
 */
#define MAX77779_M5_Command_fw	0xe9

/*
 * Command_ack,0x0A,0b00000000,0x0,Reset_Type:F
 * CMD_FW_BUSY
 */
#define MAX77779_M5_Command_ack	0xea
#define MAX77779_M5_Command_ack_CMD_FW_BUSY_SHIFT	0
#define MAX77779_M5_Command_ack_CMD_FW_BUSY_MASK	(0x1 << 0)
#define MAX77779_M5_Command_ack_CMD_FW_BUSY_CLEAR	(~(0x1 << 0))
static inline const char *
max77779_m5_command_ack_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " CMD_FW_BUSY=%x",
		FIELD2VALUE(MAX77779_CMD_FW_BUSY, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_m5_command_ack_cmd_fw_busy,0,0)

/*
 * USR,0x1F,0b00001110,0xe,Reset_Type:F
 * NLOCK,VLOCK,RLOCK,SPR_USR_0[4:12]
 */
#define MAX77779_M5_USR	0xff
#define MAX77779_M5_USR_NLOCK_SHIFT	1
#define MAX77779_M5_USR_NLOCK_MASK	(0x1 << 1)
#define MAX77779_M5_USR_NLOCK_CLEAR	(~(0x1 << 1))
#define MAX77779_M5_USR_VLOCK_SHIFT	2
#define MAX77779_M5_USR_VLOCK_MASK	(0x1 << 2)
#define MAX77779_M5_USR_VLOCK_CLEAR	(~(0x1 << 2))
#define MAX77779_M5_USR_RLOCK_SHIFT	3
#define MAX77779_M5_USR_RLOCK_MASK	(0x1 << 3)
#define MAX77779_M5_USR_RLOCK_CLEAR	(~(0x1 << 3))
#define MAX77779_M5_USR_SPR_USR_0_SHIFT	4
#define MAX77779_M5_USR_SPR_USR_0_MASK	(0xfff << 4)
#define MAX77779_M5_USR_SPR_USR_0_CLEAR	(~(0xfff << 4))
static inline const char *
max77779_m5_usr_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " NLOCK=%x",
		FIELD2VALUE(MAX77779_NLOCK, val));
	i += scnprintf(&buff[i], len - i, " VLOCK=%x",
		FIELD2VALUE(MAX77779_VLOCK, val));
	i += scnprintf(&buff[i], len - i, " RLOCK=%x",
		FIELD2VALUE(MAX77779_RLOCK, val));
	i += scnprintf(&buff[i], len - i, " SPR_USR_0=%x",
		FIELD2VALUE(MAX77779_SPR_USR_0, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_m5_usr_nlock,1,1)
MAX77779_BFF(max77779_m5_usr_vlock,2,2)
MAX77779_BFF(max77779_m5_usr_rlock,3,3)
MAX77779_BFF(max77779_m5_usr_spr_usr_0,15,4)
/*
 * Section: I2C_Master 0x00 8
 */


/*
 * I2CM_INTERRUPT,0x00,0b00000000,0x0,Reset_Type:IM
 * DONEI,SPR_6_1[1:6],ERRI
 */
#define MAX77779_I2CM_INTERRUPT	0x00
#define MAX77779_I2CM_INTERRUPT_DONEI_SHIFT	0
#define MAX77779_I2CM_INTERRUPT_DONEI_MASK	(0x1 << 0)
#define MAX77779_I2CM_INTERRUPT_DONEI_CLEAR	(~(0x1 << 0))
#define MAX77779_I2CM_INTERRUPT_SPR_6_1_SHIFT	1
#define MAX77779_I2CM_INTERRUPT_SPR_6_1_MASK	(0x3f << 1)
#define MAX77779_I2CM_INTERRUPT_SPR_6_1_CLEAR	(~(0x3f << 1))
#define MAX77779_I2CM_INTERRUPT_ERRI_SHIFT	7
#define MAX77779_I2CM_INTERRUPT_ERRI_MASK	(0x1 << 7)
#define MAX77779_I2CM_INTERRUPT_ERRI_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_i2cm_interrupt_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DONEI=%x",
		FIELD2VALUE(MAX77779_DONEI, val));
	i += scnprintf(&buff[i], len - i, " SPR_6_1=%x",
		FIELD2VALUE(MAX77779_SPR_6_1, val));
	i += scnprintf(&buff[i], len - i, " ERRI=%x",
		FIELD2VALUE(MAX77779_ERRI, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_interrupt_donei,0,0)
MAX77779_BFF(max77779_i2cm_interrupt_spr_6_1,6,1)
MAX77779_BFF(max77779_i2cm_interrupt_erri,7,7)

/*
 * I2CM_INTMASK,0x01,0b00000000,0x0,Reset_Type:IM
 * DONEIM,SPR_6_1[1:6],ERRIM
 */
#define MAX77779_I2CM_INTMASK	0x01
#define MAX77779_I2CM_INTMASK_DONEIM_SHIFT	0
#define MAX77779_I2CM_INTMASK_DONEIM_MASK	(0x1 << 0)
#define MAX77779_I2CM_INTMASK_DONEIM_CLEAR	(~(0x1 << 0))
#define MAX77779_I2CM_INTMASK_SPR_6_1_SHIFT	1
#define MAX77779_I2CM_INTMASK_SPR_6_1_MASK	(0x3f << 1)
#define MAX77779_I2CM_INTMASK_SPR_6_1_CLEAR	(~(0x3f << 1))
#define MAX77779_I2CM_INTMASK_ERRIM_SHIFT	7
#define MAX77779_I2CM_INTMASK_ERRIM_MASK	(0x1 << 7)
#define MAX77779_I2CM_INTMASK_ERRIM_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_i2cm_intmask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " DONEIM=%x",
		FIELD2VALUE(MAX77779_DONEIM, val));
	i += scnprintf(&buff[i], len - i, " SPR_6_1=%x",
		FIELD2VALUE(MAX77779_SPR_6_1, val));
	i += scnprintf(&buff[i], len - i, " ERRIM=%x",
		FIELD2VALUE(MAX77779_ERRIM, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_intmask_doneim,0,0)
MAX77779_BFF(max77779_i2cm_intmask_spr_6_1,6,1)
MAX77779_BFF(max77779_i2cm_intmask_errim,7,7)

/*
 * I2CM_STATUS,0x02,0b00000000,0x0,Reset_Type:IM
 * ERROR[0:7],BUS
 */
#define MAX77779_I2CM_STATUS	0x02
#define MAX77779_I2CM_STATUS_ERROR_SHIFT	0
#define MAX77779_I2CM_STATUS_ERROR_MASK	(0x7f << 0)
#define MAX77779_I2CM_STATUS_ERROR_CLEAR	(~(0x7f << 0))
#define MAX77779_I2CM_STATUS_BUS_SHIFT	7
#define MAX77779_I2CM_STATUS_BUS_MASK	(0x1 << 7)
#define MAX77779_I2CM_STATUS_BUS_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_i2cm_status_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " ERROR=%x",
		FIELD2VALUE(MAX77779_ERROR, val));
	i += scnprintf(&buff[i], len - i, " BUS=%x",
		FIELD2VALUE(MAX77779_BUS, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_status_error,6,0)
MAX77779_BFF(max77779_i2cm_status_bus,7,7)

/*
 * I2CM_TIMEOUT,0x03,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TIMEOUT	0x03

/*
 * I2CM_CONTROL,0x04,0b11000000,0xc0,Reset_Type:IM
 * I2CEN,CLOCK_SPEED[1:2],OEN,SDA,SCL,SCLO,SDAO
 */
#define MAX77779_I2CM_CONTROL	0x04
#define MAX77779_I2CM_CONTROL_I2CEN_SHIFT	0
#define MAX77779_I2CM_CONTROL_I2CEN_MASK	(0x1 << 0)
#define MAX77779_I2CM_CONTROL_I2CEN_CLEAR	(~(0x1 << 0))
#define MAX77779_I2CM_CONTROL_CLOCK_SPEED_SHIFT	1
#define MAX77779_I2CM_CONTROL_CLOCK_SPEED_MASK	(0x3 << 1)
#define MAX77779_I2CM_CONTROL_CLOCK_SPEED_CLEAR	(~(0x3 << 1))
#define MAX77779_I2CM_CONTROL_OEN_SHIFT	3
#define MAX77779_I2CM_CONTROL_OEN_MASK	(0x1 << 3)
#define MAX77779_I2CM_CONTROL_OEN_CLEAR	(~(0x1 << 3))
#define MAX77779_I2CM_CONTROL_SDA_SHIFT	4
#define MAX77779_I2CM_CONTROL_SDA_MASK	(0x1 << 4)
#define MAX77779_I2CM_CONTROL_SDA_CLEAR	(~(0x1 << 4))
#define MAX77779_I2CM_CONTROL_SCL_SHIFT	5
#define MAX77779_I2CM_CONTROL_SCL_MASK	(0x1 << 5)
#define MAX77779_I2CM_CONTROL_SCL_CLEAR	(~(0x1 << 5))
#define MAX77779_I2CM_CONTROL_SCLO_SHIFT	6
#define MAX77779_I2CM_CONTROL_SCLO_MASK	(0x1 << 6)
#define MAX77779_I2CM_CONTROL_SCLO_CLEAR	(~(0x1 << 6))
#define MAX77779_I2CM_CONTROL_SDAO_SHIFT	7
#define MAX77779_I2CM_CONTROL_SDAO_MASK	(0x1 << 7)
#define MAX77779_I2CM_CONTROL_SDAO_CLEAR	(~(0x1 << 7))
static inline const char *
max77779_i2cm_control_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " I2CEN=%x",
		FIELD2VALUE(MAX77779_I2CEN, val));
	i += scnprintf(&buff[i], len - i, " CLOCK_SPEED=%x",
		FIELD2VALUE(MAX77779_CLOCK_SPEED, val));
	i += scnprintf(&buff[i], len - i, " OEN=%x",
		FIELD2VALUE(MAX77779_OEN, val));
	i += scnprintf(&buff[i], len - i, " SDA=%x",
		FIELD2VALUE(MAX77779_SDA, val));
	i += scnprintf(&buff[i], len - i, " SCL=%x",
		FIELD2VALUE(MAX77779_SCL, val));
	i += scnprintf(&buff[i], len - i, " SCLO=%x",
		FIELD2VALUE(MAX77779_SCLO, val));
	i += scnprintf(&buff[i], len - i, " SDAO=%x",
		FIELD2VALUE(MAX77779_SDAO, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_control_i2cen,0,0)
MAX77779_BFF(max77779_i2cm_control_clock_speed,2,1)
MAX77779_BFF(max77779_i2cm_control_oen,3,3)
MAX77779_BFF(max77779_i2cm_control_sda,4,4)
MAX77779_BFF(max77779_i2cm_control_scl,5,5)
MAX77779_BFF(max77779_i2cm_control_sclo,6,6)
MAX77779_BFF(max77779_i2cm_control_sdao,7,7)

/*
 * I2CM_SLADD,0x05,0b00000000,0x0,Reset_Type:IM
 * SPR_0,SLAVE_ID[1:7]
 */
#define MAX77779_I2CM_SLADD	0x05
#define MAX77779_I2CM_SLADD_SPR_0_SHIFT	0
#define MAX77779_I2CM_SLADD_SPR_0_MASK	(0x1 << 0)
#define MAX77779_I2CM_SLADD_SPR_0_CLEAR	(~(0x1 << 0))
#define MAX77779_I2CM_SLADD_SLAVE_ID_SHIFT	1
#define MAX77779_I2CM_SLADD_SLAVE_ID_MASK	(0x7f << 1)
#define MAX77779_I2CM_SLADD_SLAVE_ID_CLEAR	(~(0x7f << 1))
static inline const char *
max77779_i2cm_sladd_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SPR_0=%x",
		FIELD2VALUE(MAX77779_SPR_0, val));
	i += scnprintf(&buff[i], len - i, " SLAVE_ID=%x",
		FIELD2VALUE(MAX77779_SLAVE_ID, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_sladd_spr_0,0,0)
MAX77779_BFF(max77779_i2cm_sladd_slave_id,7,1)

/*
 * I2CM_TXDATA_CNT,0x06,0b00000000,0x0,Reset_Type:IM
 * TXCNT[0:6],SPR_7_6[6:2]
 */
#define MAX77779_I2CM_TXDATA_CNT	0x06
#define MAX77779_I2CM_TXDATA_CNT_TXCNT_SHIFT	0
#define MAX77779_I2CM_TXDATA_CNT_TXCNT_MASK	(0x3f << 0)
#define MAX77779_I2CM_TXDATA_CNT_TXCNT_CLEAR	(~(0x3f << 0))
#define MAX77779_I2CM_TXDATA_CNT_SPR_7_6_SHIFT	6
#define MAX77779_I2CM_TXDATA_CNT_SPR_7_6_MASK	(0x3 << 6)
#define MAX77779_I2CM_TXDATA_CNT_SPR_7_6_CLEAR	(~(0x3 << 6))
static inline const char *
max77779_i2cm_txdata_cnt_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " TXCNT=%x",
		FIELD2VALUE(MAX77779_TXCNT, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_6=%x",
		FIELD2VALUE(MAX77779_SPR_7_6, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_txdata_cnt_txcnt,5,0)
MAX77779_BFF(max77779_i2cm_txdata_cnt_spr_7_6,7,6)

/*
 * I2CM_TX_BUFFER_0,0x07,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_0	0x07

/*
 * I2CM_TX_BUFFER_1,0x08,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_1	0x08

/*
 * I2CM_TX_BUFFER_2,0x09,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_2	0x09

/*
 * I2CM_TX_BUFFER_3,0x0A,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_3	0x0a

/*
 * I2CM_TX_BUFFER_4,0x0B,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_4	0x0b

/*
 * I2CM_TX_BUFFER_5,0x0C,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_5	0x0c

/*
 * I2CM_TX_BUFFER_6,0x0D,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_6	0x0d

/*
 * I2CM_TX_BUFFER_7,0x0E,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_7	0x0e

/*
 * I2CM_TX_BUFFER_8,0x0F,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_8	0x0f

/*
 * I2CM_TX_BUFFER_9,0x10,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_9	0x10

/*
 * I2CM_TX_BUFFER_10,0x11,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_10	0x11

/*
 * I2CM_TX_BUFFER_11,0x12,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_11	0x12

/*
 * I2CM_TX_BUFFER_12,0x13,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_12	0x13

/*
 * I2CM_TX_BUFFER_13,0x14,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_13	0x14

/*
 * I2CM_TX_BUFFER_14,0x15,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_14	0x15

/*
 * I2CM_TX_BUFFER_15,0x16,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_15	0x16

/*
 * I2CM_TX_BUFFER_16,0x17,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_16	0x17

/*
 * I2CM_TX_BUFFER_17,0x18,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_17	0x18

/*
 * I2CM_TX_BUFFER_18,0x19,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_18	0x19

/*
 * I2CM_TX_BUFFER_19,0x1A,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_19	0x1a

/*
 * I2CM_TX_BUFFER_20,0x1B,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_20	0x1b

/*
 * I2CM_TX_BUFFER_21,0x1C,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_21	0x1c

/*
 * I2CM_TX_BUFFER_22,0x1D,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_22	0x1d

/*
 * I2CM_TX_BUFFER_23,0x1E,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_23	0x1e

/*
 * I2CM_TX_BUFFER_24,0x1F,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_24	0x1f

/*
 * I2CM_TX_BUFFER_25,0x20,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_25	0x20

/*
 * I2CM_TX_BUFFER_26,0x21,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_26	0x21

/*
 * I2CM_TX_BUFFER_27,0x22,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_27	0x22

/*
 * I2CM_TX_BUFFER_28,0x23,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_28	0x23

/*
 * I2CM_TX_BUFFER_29,0x24,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_29	0x24

/*
 * I2CM_TX_BUFFER_30,0x25,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_30	0x25

/*
 * I2CM_TX_BUFFER_31,0x26,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_31	0x26

/*
 * I2CM_TX_BUFFER_32,0x27,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_32	0x27

/*
 * I2CM_TX_BUFFER_33,0x28,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_TX_BUFFER_33	0x28

/*
 * I2CM_RXDATA_CNT,0x29,0b00000000,0x0,Reset_Type:IM
 * RXCNT[0:5],SPR_7_5[5:3]
 */
#define MAX77779_I2CM_RXDATA_CNT	0x29
#define MAX77779_I2CM_RXDATA_CNT_RXCNT_SHIFT	0
#define MAX77779_I2CM_RXDATA_CNT_RXCNT_MASK	(0x1f << 0)
#define MAX77779_I2CM_RXDATA_CNT_RXCNT_CLEAR	(~(0x1f << 0))
#define MAX77779_I2CM_RXDATA_CNT_SPR_7_5_SHIFT	5
#define MAX77779_I2CM_RXDATA_CNT_SPR_7_5_MASK	(0x7 << 5)
#define MAX77779_I2CM_RXDATA_CNT_SPR_7_5_CLEAR	(~(0x7 << 5))
static inline const char *
max77779_i2cm_rxdata_cnt_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " RXCNT=%x",
		FIELD2VALUE(MAX77779_RXCNT, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_5=%x",
		FIELD2VALUE(MAX77779_SPR_7_5, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_rxdata_cnt_rxcnt,4,0)
MAX77779_BFF(max77779_i2cm_rxdata_cnt_spr_7_5,7,5)

/*
 * I2CM_CMD,0x2A,0b00000000,0x0,Reset_Type:IM
 * I2CMWRITE,I2CMREAD,SPR_7_2[2:6]
 */
#define MAX77779_I2CM_CMD	0x2a
#define MAX77779_I2CM_CMD_I2CMWRITE_SHIFT	0
#define MAX77779_I2CM_CMD_I2CMWRITE_MASK	(0x1 << 0)
#define MAX77779_I2CM_CMD_I2CMWRITE_CLEAR	(~(0x1 << 0))
#define MAX77779_I2CM_CMD_I2CMREAD_SHIFT	1
#define MAX77779_I2CM_CMD_I2CMREAD_MASK	(0x1 << 1)
#define MAX77779_I2CM_CMD_I2CMREAD_CLEAR	(~(0x1 << 1))
#define MAX77779_I2CM_CMD_SPR_7_2_SHIFT	2
#define MAX77779_I2CM_CMD_SPR_7_2_MASK	(0x3f << 2)
#define MAX77779_I2CM_CMD_SPR_7_2_CLEAR	(~(0x3f << 2))
static inline const char *
max77779_i2cm_cmd_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " I2CMWRITE=%x",
		FIELD2VALUE(MAX77779_I2CMWRITE, val));
	i += scnprintf(&buff[i], len - i, " I2CMREAD=%x",
		FIELD2VALUE(MAX77779_I2CMREAD, val));
	i += scnprintf(&buff[i], len - i, " SPR_7_2=%x",
		FIELD2VALUE(MAX77779_SPR_7_2, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_i2cm_cmd_i2cmwrite,0,0)
MAX77779_BFF(max77779_i2cm_cmd_i2cmread,1,1)
MAX77779_BFF(max77779_i2cm_cmd_spr_7_2,7,2)

/*
 * I2CM_RX_BUFFER_0,0x2B,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_0	0x2b

/*
 * I2CM_RX_BUFFER_1,0x2C,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_1	0x2c

/*
 * I2CM_RX_BUFFER_2,0x2D,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_2	0x2d

/*
 * I2CM_RX_BUFFER_3,0x2E,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_3	0x2e

/*
 * I2CM_RX_BUFFER_4,0x2F,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_4	0x2f

/*
 * I2CM_RX_BUFFER_5,0x30,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_5	0x30

/*
 * I2CM_RX_BUFFER_6,0x31,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_6	0x31

/*
 * I2CM_RX_BUFFER_7,0x32,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_7	0x32

/*
 * I2CM_RX_BUFFER_8,0x33,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_8	0x33

/*
 * I2CM_RX_BUFFER_9,0x34,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_9	0x34

/*
 * I2CM_RX_BUFFER_10,0x35,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_10	0x35

/*
 * I2CM_RX_BUFFER_11,0x36,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_11	0x36

/*
 * I2CM_RX_BUFFER_12,0x37,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_12	0x37

/*
 * I2CM_RX_BUFFER_13,0x38,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_13	0x38

/*
 * I2CM_RX_BUFFER_14,0x39,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_14	0x39

/*
 * I2CM_RX_BUFFER_15,0x3A,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_15	0x3a

/*
 * I2CM_RX_BUFFER_16,0x3B,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_16	0x3b

/*
 * I2CM_RX_BUFFER_17,0x3C,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_17	0x3c

/*
 * I2CM_RX_BUFFER_18,0x3D,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_18	0x3d

/*
 * I2CM_RX_BUFFER_19,0x3E,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_19	0x3e

/*
 * I2CM_RX_BUFFER_20,0x3F,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_20	0x3f

/*
 * I2CM_RX_BUFFER_21,0x40,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_21	0x40

/*
 * I2CM_RX_BUFFER_22,0x41,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_22	0x41

/*
 * I2CM_RX_BUFFER_23,0x42,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_23	0x42

/*
 * I2CM_RX_BUFFER_24,0x43,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_24	0x43

/*
 * I2CM_RX_BUFFER_25,0x44,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_25	0x44

/*
 * I2CM_RX_BUFFER_26,0x45,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_26	0x45

/*
 * I2CM_RX_BUFFER_27,0x46,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_27	0x46

/*
 * I2CM_RX_BUFFER_28,0x47,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_28	0x47

/*
 * I2CM_RX_BUFFER_29,0x48,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_29	0x48

/*
 * I2CM_RX_BUFFER_30,0x49,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_30	0x49

/*
 * I2CM_RX_BUFFER_31,0x4A,0b00000000,0x0,Reset_Type:IM
 */
#define MAX77779_I2CM_RX_BUFFER_31	0x4a
/*
 * Section: BATTVIMON_FUNC 0x00 16
 */


/*
 * BVIM_INT_STS,0x00,0b00000000,0x0,Reset_Type:F
 * BVIM_Samples_Rdy
 */
#define MAX77779_BVIMBVIM_INT_STS	0x00
#define MAX77779_BVIMBVIM_INT_STS_BVIM_Samples_Rdy_SHIFT	0
#define MAX77779_BVIMBVIM_INT_STS_BVIM_Samples_Rdy_MASK	(0x1 << 0)
#define MAX77779_BVIMBVIM_INT_STS_BVIM_Samples_Rdy_CLEAR	(~(0x1 << 0))
static inline const char *
max77779_bvimbvim_int_sts_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BVIM_Samples_Rdy=%x",
		FIELD2VALUE(MAX77779_BVIM_Samples_Rdy, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_int_sts_bvim_samples_rdy,0,0)

/*
 * BVIM_MASK,0x01,0b00000001,0x1,Reset_Type:F
 * BVIM_Samples_Rdy_m
 */
#define MAX77779_BVIMBVIM_MASK	0x01
#define MAX77779_BVIMBVIM_MASK_BVIM_Samples_Rdy_m_SHIFT	0
#define MAX77779_BVIMBVIM_MASK_BVIM_Samples_Rdy_m_MASK	(0x1 << 0)
#define MAX77779_BVIMBVIM_MASK_BVIM_Samples_Rdy_m_CLEAR	(~(0x1 << 0))
static inline const char *
max77779_bvimbvim_mask_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BVIM_Samples_Rdy_m=%x",
		FIELD2VALUE(MAX77779_BVIM_Samples_Rdy_m, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_mask_bvim_samples_rdy_m,0,0)

/*
 * BVIM_CTRL,0x10,0b00000000,0x0,Reset_Type:F
 * BVIMON_TRIG
 */
#define MAX77779_BVIMBVIM_CTRL	0x10
#define MAX77779_BVIMBVIM_CTRL_BVIMON_TRIG_SHIFT	0
#define MAX77779_BVIMBVIM_CTRL_BVIMON_TRIG_MASK	(0x1 << 0)
#define MAX77779_BVIMBVIM_CTRL_BVIMON_TRIG_CLEAR	(~(0x1 << 0))
static inline const char *
max77779_bvimbvim_ctrl_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BVIMON_TRIG=%x",
		FIELD2VALUE(MAX77779_BVIMON_TRIG, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_ctrl_bvimon_trig,0,0)
/*
 * Section: BVIM_CONTROL 0x60 16
 */


/*
 * bvim_cfg,0x00,0b00000000,0x0,Reset_Type:S
 * smpl_n[0:8],smpl_m[8:3],cnt_run
 */
#define MAX77779_BVIMbvim_cfg	0x60
#define MAX77779_BVIMbvim_cfg_smpl_n_SHIFT	0
#define MAX77779_BVIMbvim_cfg_smpl_n_MASK	(0xff << 0)
#define MAX77779_BVIMbvim_cfg_smpl_n_CLEAR	(~(0xff << 0))
#define MAX77779_BVIMbvim_cfg_smpl_m_SHIFT	8
#define MAX77779_BVIMbvim_cfg_smpl_m_MASK	(0x7 << 8)
#define MAX77779_BVIMbvim_cfg_smpl_m_CLEAR	(~(0x7 << 8))
#define MAX77779_BVIMbvim_cfg_cnt_run_SHIFT	11
#define MAX77779_BVIMbvim_cfg_cnt_run_MASK	(0x1 << 11)
#define MAX77779_BVIMbvim_cfg_cnt_run_CLEAR	(~(0x1 << 11))
static inline const char *
max77779_bvimbvim_cfg_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " smpl_n=%x",
		FIELD2VALUE(MAX77779_smpl_n, val));
	i += scnprintf(&buff[i], len - i, " smpl_m=%x",
		FIELD2VALUE(MAX77779_smpl_m, val));
	i += scnprintf(&buff[i], len - i, " cnt_run=%x",
		FIELD2VALUE(MAX77779_cnt_run, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_cfg_smpl_n,7,0)
MAX77779_BFF(max77779_bvimbvim_cfg_smpl_m,10,8)
MAX77779_BFF(max77779_bvimbvim_cfg_cnt_run,11,11)

/*
 * smpl_math,0x01,0b00000000,0x0,Reset_Type:S
 * math_avg,math_min,math_max,smpl_start_add[7:9]
 */
#define MAX77779_BVIMsmpl_math	0x61
#define MAX77779_BVIMsmpl_math_math_avg_SHIFT	0
#define MAX77779_BVIMsmpl_math_math_avg_MASK	(0x1 << 0)
#define MAX77779_BVIMsmpl_math_math_avg_CLEAR	(~(0x1 << 0))
#define MAX77779_BVIMsmpl_math_math_min_SHIFT	1
#define MAX77779_BVIMsmpl_math_math_min_MASK	(0x1 << 1)
#define MAX77779_BVIMsmpl_math_math_min_CLEAR	(~(0x1 << 1))
#define MAX77779_BVIMsmpl_math_math_max_SHIFT	2
#define MAX77779_BVIMsmpl_math_math_max_MASK	(0x1 << 2)
#define MAX77779_BVIMsmpl_math_math_max_CLEAR	(~(0x1 << 2))
#define MAX77779_BVIMsmpl_math_smpl_start_add_SHIFT	7
#define MAX77779_BVIMsmpl_math_smpl_start_add_MASK	(0x1ff << 7)
#define MAX77779_BVIMsmpl_math_smpl_start_add_CLEAR	(~(0x1ff << 7))
static inline const char *
max77779_bvimsmpl_math_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " math_avg=%x",
		FIELD2VALUE(MAX77779_math_avg, val));
	i += scnprintf(&buff[i], len - i, " math_min=%x",
		FIELD2VALUE(MAX77779_math_min, val));
	i += scnprintf(&buff[i], len - i, " math_max=%x",
		FIELD2VALUE(MAX77779_math_max, val));
	i += scnprintf(&buff[i], len - i, " smpl_start_add=%x",
		FIELD2VALUE(MAX77779_smpl_start_add, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimsmpl_math_math_avg,0,0)
MAX77779_BFF(max77779_bvimsmpl_math_math_min,1,1)
MAX77779_BFF(max77779_bvimsmpl_math_math_max,2,2)
MAX77779_BFF(max77779_bvimsmpl_math_smpl_start_add,15,7)

/*
 * bvim_trig,0x02,0b00000000,0x0,Reset_Type:S
 * trig_now,batoilo_tr,sysuvlo1_tr,sysuvlo2_tr,vbatt_tr,ibatt_tr,vbatt,
 * vbatt_avg_tr,vbatt_min_tr,vbatt_max_tr,ibatt_avg_tr,ibatt_min_tr,ibatt_max_tr
 */
#define MAX77779_BVIMbvim_trig	0x62
#define MAX77779_BVIMbvim_trig_trig_now_SHIFT	0
#define MAX77779_BVIMbvim_trig_trig_now_MASK	(0x1 << 0)
#define MAX77779_BVIMbvim_trig_trig_now_CLEAR	(~(0x1 << 0))
#define MAX77779_BVIMbvim_trig_batoilo_tr_SHIFT	1
#define MAX77779_BVIMbvim_trig_batoilo_tr_MASK	(0x1 << 1)
#define MAX77779_BVIMbvim_trig_batoilo_tr_CLEAR	(~(0x1 << 1))
#define MAX77779_BVIMbvim_trig_sysuvlo1_tr_SHIFT	2
#define MAX77779_BVIMbvim_trig_sysuvlo1_tr_MASK	(0x1 << 2)
#define MAX77779_BVIMbvim_trig_sysuvlo1_tr_CLEAR	(~(0x1 << 2))
#define MAX77779_BVIMbvim_trig_sysuvlo2_tr_SHIFT	3
#define MAX77779_BVIMbvim_trig_sysuvlo2_tr_MASK	(0x1 << 3)
#define MAX77779_BVIMbvim_trig_sysuvlo2_tr_CLEAR	(~(0x1 << 3))
#define MAX77779_BVIMbvim_trig_vbatt_tr_SHIFT	4
#define MAX77779_BVIMbvim_trig_vbatt_tr_MASK	(0x1 << 4)
#define MAX77779_BVIMbvim_trig_vbatt_tr_CLEAR	(~(0x1 << 4))
#define MAX77779_BVIMbvim_trig_ibatt_tr_SHIFT	5
#define MAX77779_BVIMbvim_trig_ibatt_tr_MASK	(0x1 << 5)
#define MAX77779_BVIMbvim_trig_ibatt_tr_CLEAR	(~(0x1 << 5))
#define MAX77779_BVIMbvim_trig_vbatt_SHIFT	6
#define MAX77779_BVIMbvim_trig_vbatt_MASK	(0x1 << 6)
#define MAX77779_BVIMbvim_trig_vbatt_CLEAR	(~(0x1 << 6))
#define MAX77779_BVIMbvim_trig_vbatt_avg_tr_SHIFT	7
#define MAX77779_BVIMbvim_trig_vbatt_avg_tr_MASK	(0x1 << 7)
#define MAX77779_BVIMbvim_trig_vbatt_avg_tr_CLEAR	(~(0x1 << 7))
#define MAX77779_BVIMbvim_trig_vbatt_min_tr_SHIFT	8
#define MAX77779_BVIMbvim_trig_vbatt_min_tr_MASK	(0x1 << 8)
#define MAX77779_BVIMbvim_trig_vbatt_min_tr_CLEAR	(~(0x1 << 8))
#define MAX77779_BVIMbvim_trig_vbatt_max_tr_SHIFT	9
#define MAX77779_BVIMbvim_trig_vbatt_max_tr_MASK	(0x1 << 9)
#define MAX77779_BVIMbvim_trig_vbatt_max_tr_CLEAR	(~(0x1 << 9))
#define MAX77779_BVIMbvim_trig_ibatt_avg_tr_SHIFT	10
#define MAX77779_BVIMbvim_trig_ibatt_avg_tr_MASK	(0x1 << 10)
#define MAX77779_BVIMbvim_trig_ibatt_avg_tr_CLEAR	(~(0x1 << 10))
#define MAX77779_BVIMbvim_trig_ibatt_min_tr_SHIFT	11
#define MAX77779_BVIMbvim_trig_ibatt_min_tr_MASK	(0x1 << 11)
#define MAX77779_BVIMbvim_trig_ibatt_min_tr_CLEAR	(~(0x1 << 11))
#define MAX77779_BVIMbvim_trig_ibatt_max_tr_SHIFT	12
#define MAX77779_BVIMbvim_trig_ibatt_max_tr_MASK	(0x1 << 12)
#define MAX77779_BVIMbvim_trig_ibatt_max_tr_CLEAR	(~(0x1 << 12))
static inline const char *
max77779_bvimbvim_trig_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " trig_now=%x",
		FIELD2VALUE(MAX77779_trig_now, val));
	i += scnprintf(&buff[i], len - i, " batoilo_tr=%x",
		FIELD2VALUE(MAX77779_batoilo_tr, val));
	i += scnprintf(&buff[i], len - i, " sysuvlo1_tr=%x",
		FIELD2VALUE(MAX77779_sysuvlo1_tr, val));
	i += scnprintf(&buff[i], len - i, " sysuvlo2_tr=%x",
		FIELD2VALUE(MAX77779_sysuvlo2_tr, val));
	i += scnprintf(&buff[i], len - i, " vbatt_tr=%x",
		FIELD2VALUE(MAX77779_vbatt_tr, val));
	i += scnprintf(&buff[i], len - i, " ibatt_tr=%x",
		FIELD2VALUE(MAX77779_ibatt_tr, val));
	i += scnprintf(&buff[i], len - i, " vbatt=%x",
		FIELD2VALUE(MAX77779_vbatt, val));
	i += scnprintf(&buff[i], len - i, " vbatt_avg_tr=%x",
		FIELD2VALUE(MAX77779_vbatt_avg_tr, val));
	i += scnprintf(&buff[i], len - i, " vbatt_min_tr=%x",
		FIELD2VALUE(MAX77779_vbatt_min_tr, val));
	i += scnprintf(&buff[i], len - i, " vbatt_max_tr=%x",
		FIELD2VALUE(MAX77779_vbatt_max_tr, val));
	i += scnprintf(&buff[i], len - i, " ibatt_avg_tr=%x",
		FIELD2VALUE(MAX77779_ibatt_avg_tr, val));
	i += scnprintf(&buff[i], len - i, " ibatt_min_tr=%x",
		FIELD2VALUE(MAX77779_ibatt_min_tr, val));
	i += scnprintf(&buff[i], len - i, " ibatt_max_tr=%x",
		FIELD2VALUE(MAX77779_ibatt_max_tr, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_trig_trig_now,0,0)
MAX77779_BFF(max77779_bvimbvim_trig_batoilo_tr,1,1)
MAX77779_BFF(max77779_bvimbvim_trig_sysuvlo1_tr,2,2)
MAX77779_BFF(max77779_bvimbvim_trig_sysuvlo2_tr,3,3)
MAX77779_BFF(max77779_bvimbvim_trig_vbatt_tr,4,4)
MAX77779_BFF(max77779_bvimbvim_trig_ibatt_tr,5,5)
MAX77779_BFF(max77779_bvimbvim_trig_vbatt,6,6)
MAX77779_BFF(max77779_bvimbvim_trig_vbatt_avg_tr,7,7)
MAX77779_BFF(max77779_bvimbvim_trig_vbatt_min_tr,8,8)
MAX77779_BFF(max77779_bvimbvim_trig_vbatt_max_tr,9,9)
MAX77779_BFF(max77779_bvimbvim_trig_ibatt_avg_tr,10,10)
MAX77779_BFF(max77779_bvimbvim_trig_ibatt_min_tr,11,11)
MAX77779_BFF(max77779_bvimbvim_trig_ibatt_max_tr,12,12)

/*
 * bvimtr,0x03,0b00000000,0x0,Reset_Type:S
 * v_md[0:2],i_md[2:2],v_savg_md[4:2],v_smin_md[6:2],v_smax_md[8:2],i_savg_md[10:2],
 * i_smin_md[12:2],i_smax_md[14:2]
 */
#define MAX77779_BVIMbvimtr	0x63
#define MAX77779_BVIMbvimtr_v_md_SHIFT	0
#define MAX77779_BVIMbvimtr_v_md_MASK	(0x3 << 0)
#define MAX77779_BVIMbvimtr_v_md_CLEAR	(~(0x3 << 0))
#define MAX77779_BVIMbvimtr_i_md_SHIFT	2
#define MAX77779_BVIMbvimtr_i_md_MASK	(0x3 << 2)
#define MAX77779_BVIMbvimtr_i_md_CLEAR	(~(0x3 << 2))
#define MAX77779_BVIMbvimtr_v_savg_md_SHIFT	4
#define MAX77779_BVIMbvimtr_v_savg_md_MASK	(0x3 << 4)
#define MAX77779_BVIMbvimtr_v_savg_md_CLEAR	(~(0x3 << 4))
#define MAX77779_BVIMbvimtr_v_smin_md_SHIFT	6
#define MAX77779_BVIMbvimtr_v_smin_md_MASK	(0x3 << 6)
#define MAX77779_BVIMbvimtr_v_smin_md_CLEAR	(~(0x3 << 6))
#define MAX77779_BVIMbvimtr_v_smax_md_SHIFT	8
#define MAX77779_BVIMbvimtr_v_smax_md_MASK	(0x3 << 8)
#define MAX77779_BVIMbvimtr_v_smax_md_CLEAR	(~(0x3 << 8))
#define MAX77779_BVIMbvimtr_i_savg_md_SHIFT	10
#define MAX77779_BVIMbvimtr_i_savg_md_MASK	(0x3 << 10)
#define MAX77779_BVIMbvimtr_i_savg_md_CLEAR	(~(0x3 << 10))
#define MAX77779_BVIMbvimtr_i_smin_md_SHIFT	12
#define MAX77779_BVIMbvimtr_i_smin_md_MASK	(0x3 << 12)
#define MAX77779_BVIMbvimtr_i_smin_md_CLEAR	(~(0x3 << 12))
#define MAX77779_BVIMbvimtr_i_smax_md_SHIFT	14
#define MAX77779_BVIMbvimtr_i_smax_md_MASK	(0x3 << 14)
#define MAX77779_BVIMbvimtr_i_smax_md_CLEAR	(~(0x3 << 14))
static inline const char *
max77779_bvimbvimtr_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " v_md=%x",
		FIELD2VALUE(MAX77779_v_md, val));
	i += scnprintf(&buff[i], len - i, " i_md=%x",
		FIELD2VALUE(MAX77779_i_md, val));
	i += scnprintf(&buff[i], len - i, " v_savg_md=%x",
		FIELD2VALUE(MAX77779_v_savg_md, val));
	i += scnprintf(&buff[i], len - i, " v_smin_md=%x",
		FIELD2VALUE(MAX77779_v_smin_md, val));
	i += scnprintf(&buff[i], len - i, " v_smax_md=%x",
		FIELD2VALUE(MAX77779_v_smax_md, val));
	i += scnprintf(&buff[i], len - i, " i_savg_md=%x",
		FIELD2VALUE(MAX77779_i_savg_md, val));
	i += scnprintf(&buff[i], len - i, " i_smin_md=%x",
		FIELD2VALUE(MAX77779_i_smin_md, val));
	i += scnprintf(&buff[i], len - i, " i_smax_md=%x",
		FIELD2VALUE(MAX77779_i_smax_md, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvimtr_v_md,1,0)
MAX77779_BFF(max77779_bvimbvimtr_i_md,3,2)
MAX77779_BFF(max77779_bvimbvimtr_v_savg_md,5,4)
MAX77779_BFF(max77779_bvimbvimtr_v_smin_md,7,6)
MAX77779_BFF(max77779_bvimbvimtr_v_smax_md,9,8)
MAX77779_BFF(max77779_bvimbvimtr_i_savg_md,11,10)
MAX77779_BFF(max77779_bvimbvimtr_i_smin_md,13,12)
MAX77779_BFF(max77779_bvimbvimtr_i_smax_md,15,14)

/*
 * bvim_vtr_mth,0x04,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_vtr_mth	0x64

/*
 * bvim_itr_mth,0x05,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_itr_mth	0x65

/*
 * bvim_vtr_smin_th,0x06,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_vtr_smin_th	0x66

/*
 * bvim_vtr_smax_th,0x07,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_vtr_smax_th	0x67

/*
 * bvim_vtr_savg_th,0x08,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_vtr_savg_th	0x68

/*
 * bvim_itr_smin_th,0x09,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_itr_smin_th	0x69

/*
 * bvim_itr_smax_th,0x0A,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_itr_smax_th	0x6a

/*
 * bvim_itr_savg_th,0x0B,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_itr_savg_th	0x6b

/*
 * bvim_sts,0x0C,0b00000000,0x0,Reset_Type:S
 * bvim_osc[0:9],tr_sts[9:2]
 */
#define MAX77779_BVIMbvim_sts	0x6c
#define MAX77779_BVIMbvim_sts_bvim_osc_SHIFT	0
#define MAX77779_BVIMbvim_sts_bvim_osc_MASK	(0x1ff << 0)
#define MAX77779_BVIMbvim_sts_bvim_osc_CLEAR	(~(0x1ff << 0))
#define MAX77779_BVIMbvim_sts_tr_sts_SHIFT	9
#define MAX77779_BVIMbvim_sts_tr_sts_MASK	(0x3 << 9)
#define MAX77779_BVIMbvim_sts_tr_sts_CLEAR	(~(0x3 << 9))
static inline const char *
max77779_bvimbvim_sts_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " bvim_osc=%x",
		FIELD2VALUE(MAX77779_bvim_osc, val));
	i += scnprintf(&buff[i], len - i, " tr_sts=%x",
		FIELD2VALUE(MAX77779_tr_sts, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_sts_bvim_osc,8,0)
MAX77779_BFF(max77779_bvimbvim_sts_tr_sts,10,9)

/*
 * bvim_rs,0x0D,0b00000000,0x0,Reset_Type:S
 * rsc[0:9],bvim_rts[9:4]
 */
#define MAX77779_BVIMbvim_rs	0x6d
#define MAX77779_BVIMbvim_rs_rsc_SHIFT	0
#define MAX77779_BVIMbvim_rs_rsc_MASK	(0x1ff << 0)
#define MAX77779_BVIMbvim_rs_rsc_CLEAR	(~(0x1ff << 0))
#define MAX77779_BVIMbvim_rs_bvim_rts_SHIFT	9
#define MAX77779_BVIMbvim_rs_bvim_rts_MASK	(0xf << 9)
#define MAX77779_BVIMbvim_rs_bvim_rts_CLEAR	(~(0xf << 9))
static inline const char *
max77779_bvimbvim_rs_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " rsc=%x",
		FIELD2VALUE(MAX77779_rsc, val));
	i += scnprintf(&buff[i], len - i, " bvim_rts=%x",
		FIELD2VALUE(MAX77779_bvim_rts, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_rs_rsc,8,0)
MAX77779_BFF(max77779_bvimbvim_rs_bvim_rts,12,9)

/*
 * bvim_rlap,0x0E,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_rlap	0x6e

/*
 * bvim_rfap,0x0F,0b00000000,0x0,Reset_Type:S
 */
#define MAX77779_BVIMbvim_rfap	0x6f
/*
 * Section: BVIM_PAGE 0x70 16
 */


/*
 * BVIM_PAGE_CTRL,0x00,0b00000000,0x0,Reset_Type:F
 * BVIM_DATA_PAGE[0:2]
 */
#define MAX77779_BVIMBVIM_PAGE_CTRL	0x70
#define MAX77779_BVIMBVIM_PAGE_CTRL_BVIM_DATA_PAGE_SHIFT	0
#define MAX77779_BVIMBVIM_PAGE_CTRL_BVIM_DATA_PAGE_MASK	(0x3 << 0)
#define MAX77779_BVIMBVIM_PAGE_CTRL_BVIM_DATA_PAGE_CLEAR	(~(0x3 << 0))
static inline const char *
max77779_bvimbvim_page_ctrl_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " BVIM_DATA_PAGE=%x",
		FIELD2VALUE(MAX77779_BVIM_DATA_PAGE, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_bvimbvim_page_ctrl_bvim_data_page,1,0)
/*
 * Section: BVIM_DATA 0x80 16
 */

/*
 * Section: SP_PAGE 0x70 16
 */


/*
 * SP_PAGE_CTRL,0x00,0b00000000,0x0,Reset_Type:SP
 * SP_DATA_PAGE[0:2]
 */
#define MAX77779_SP_PAGE_CTRL	0x70
#define MAX77779_SP_PAGE_CTRL_SP_DATA_PAGE_SHIFT	0
#define MAX77779_SP_PAGE_CTRL_SP_DATA_PAGE_MASK	(0x3 << 0)
#define MAX77779_SP_PAGE_CTRL_SP_DATA_PAGE_CLEAR	(~(0x3 << 0))
static inline const char *
max77779_sp_page_ctrl_cstr(char *buff, size_t len, int val)
{
#ifdef CONFIG_SCNPRINTF_DEBUG
	int i = 0;

	i += scnprintf(&buff[i], len - i, " SP_DATA_PAGE=%x",
		FIELD2VALUE(MAX77779_SP_DATA_PAGE, val));
#else
	buff[0] = 0;
#endif
	return buff;
}

MAX77779_BFF(max77779_sp_page_ctrl_sp_data_page,1,0)
/*
 * Section: SP_DATA 0x80 16
 */


#endif /* SEQUOIA_REGMAP_CUSTOMER_060_REG_H_ */
