// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU functions for GSX01 SoCs.
 *
 * Copyright (C) 2022-2023 Google LLC
 */

#include <bcl.h>
#include <linux/acpm_dvfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <soc/google/bts.h>
#include <soc/google/exynos_pm_qos.h>
#include <soc/google/gs_tmu_v3.h>

#include <gcip/gcip-kci.h>
#include <gcip/gcip-thermal.h>

#include "edgetpu-internal.h"
#include "edgetpu-firmware.h"
#include "edgetpu-gsa.h"
#include "edgetpu-kci.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "mobile-soc-gsx01.h"

#define TPU_ACPM_DOMAIN 9

#define MAX_VOLTAGE_VAL 1250000

#define TPU_DEBUG_REQ (1 << 31)

#define TPU_DEBUG_VALUE_SHIFT (27)
#define TPU_DEBUG_VALUE_MASK ((1 << TPU_DEBUG_VALUE_SHIFT) - 1)
#define TPU_VDD_TPU_DEBUG (0 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_VDD_TPU_M_DEBUG (1 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_VDD_INT_M_DEBUG (2 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_CLK_CORE_DEBUG (3 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_CLK_CTL_DEBUG (4 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_CLK_AXI_DEBUG (5 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_CLK_APB_DEBUG (6 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_CLK_UART_DEBUG (7 << TPU_DEBUG_VALUE_SHIFT)
#define TPU_CORE_PWR_DEBUG (8 << TPU_DEBUG_VALUE_SHIFT)

/*
 * Encode INT/MIF values as a 16 bit pair in the 32-bit return value
 * (in units of MHz, to provide enough range)
 */
#define PM_QOS_INT_SHIFT                (16)
#define PM_QOS_MIF_MASK                 (0xFFFF)
#define PM_QOS_FACTOR                   (1000)

#define SSMT_NS_READ_STREAM_VID_OFFSET(n)     (0x1000u + (0x4u * (n)))
#define SSMT_NS_WRITE_STREAM_VID_OFFSET(n)    (0x1200u + (0x4u * (n)))

#define SSMT_NS_READ_STREAM_VID_REG(base, n)  ((base) + SSMT_NS_READ_STREAM_VID_OFFSET(n))
#define SSMT_NS_WRITE_STREAM_VID_REG(base, n) ((base) + SSMT_NS_WRITE_STREAM_VID_OFFSET(n))

#define PLL_CON3_OFFSET 0x10c
#define PLL_DIV_M_POS 16
#define PLL_DIV_M_WIDTH 10
#define TO_PLL_DIV_M(val) (((val) >> PLL_DIV_M_POS) & (BIT(PLL_DIV_M_WIDTH) - 1))

/* Rio values */
#define EDGETPU_S2MPU_REG_CTRL_CLR 0x54
#define EDGEPTU_S2MPU_REG_NUM_CONTEXT 0x100

#define SHUTDOWN_DELAY_US_MIN 200
#define SHUTDOWN_DELAY_US_MAX 200
#define BOOTUP_DELAY_US_MIN 100
#define BOOTUP_DELAY_US_MAX 150
#define SHUTDOWN_MAX_DELAY_COUNT 20

/* Rio values */
#define EDGETPU_PSM0_CFG 0x1c1700
#define EDGETPU_PSM0_START 0x1c1704
#define EDGETPU_PSM0_STATUS 0x1c1708
#define EDGETPU_LPM_CONTROL_CSR 0x1d0020
#define EDGETPU_LPM_CORE_CSR 0x1d0028
#define EDGETPU_LPM_CLUSTER_CSR0 0x1d0030
#define EDGETPU_LPM_CLUSTER_CSR1 0x1d0038
#define EDGETPU_TOP_CLOCK_GATE_CONTROL_CSR 0x1d0068
#define EDGETPU_LPM_CHANGE_TIMEOUT 30000

#define EDGETPU_LPM_IMEM_OPS_SIZE 0x4
#define EDGETPU_LPM_IMEM_OPS(n) (0x1c0800 + (EDGETPU_LPM_IMEM_OPS_SIZE * (n)))
#define EDGETPU_LPM_IMEM_OPS_SET(etdev, n, value)                                                  \
	edgetpu_dev_write_32_sync(etdev, EDGETPU_LPM_IMEM_OPS(n), value)

static u32 _edgetpu_active_states[EDGETPU_NUM_STATES] = {
	TPU_ACTIVE_MIN, TPU_ACTIVE_ULTRA_LOW, TPU_ACTIVE_VERY_LOW, TPU_ACTIVE_SUB_LOW,
	TPU_ACTIVE_LOW, TPU_ACTIVE_MEDIUM,    TPU_ACTIVE_NOM,
};

static int gsx01_parse_ssmt(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev = to_platform_device(etdev->dev);
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	struct resource *res;
	int ret, i;
	void __iomem *ssmt_base;
	char ssmt_name[] = "ssmt_d0";

	soc_data->num_ssmts = EDGETPU_NUM_SSMTS;
	soc_data->ssmt_base = devm_kcalloc(etdev->dev, soc_data->num_ssmts,
					   sizeof(*soc_data->ssmt_base), GFP_KERNEL);

	if (!soc_data->ssmt_base)
		return -ENOMEM;

	if (unlikely(soc_data->num_ssmts > 9))
		return -EINVAL;

	for (i = 0; i < soc_data->num_ssmts; i++) {
		sprintf(ssmt_name, "ssmt_d%d", i);

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, ssmt_name);
		if (!res) {
			etdev_warn(etdev, "Failed to find SSMT_D%d register base", i);
			return -EINVAL;
		}
		ssmt_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(ssmt_base)) {
			ret = PTR_ERR(ssmt_base);
			etdev_warn(etdev, "Failed to map SSMT_D%d register base: %d", i, ret);
			return ret;
		}
		soc_data->ssmt_base[i] = ssmt_base;
	}
	return 0;
}

static int gsx01_parse_cmu(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev = to_platform_device(etdev->dev);
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	struct resource *res;
	void __iomem *cmu_base;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmu");
	if (!res) {
		etdev_warn(etdev, "Failed to find CMU register base");
		return -EINVAL;
	}
	cmu_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cmu_base)) {
		ret = PTR_ERR(cmu_base);
		etdev_warn(etdev, "Failed to map CMU register base: %d", ret);
		return ret;
	}
	soc_data->cmu_base = cmu_base;

	return 0;
}

static void edgetpu_gsx01_parse_pmu(struct edgetpu_dev *etdev)
{
	struct device *dev = etdev->dev;
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	u32 reg;

	if (of_find_property(dev->of_node, "pmu-status-base", NULL) &&
	    !of_property_read_u32_index(dev->of_node, "pmu-status-base", 0, &reg)) {
		soc_data->pmu_status = devm_ioremap(dev, reg, 0x4);
		if (!soc_data->pmu_status)
			etdev_err(etdev, "Using ACPM for blk status query\n");
	} else {
		etdev_warn(etdev, "Failed to find PMU register base\n");
	}
}

/*
 * Acquires the register base from OF property and map it.
 *
 * On success, caller calls iounmap() when the returned pointer is not required.
 *
 * Returns -ENODATA on reading property failure, typically caused by the property @prop doesn't
 * exist or doesn't have a 32-bit value.
 * Returns -ENOMEM on mapping register base failure.
 */
static void __iomem *reg_base_of_prop(struct device *dev, const char *prop, size_t size)
{
	u32 reg;
	void __iomem *addr;
	int ret;

	ret = of_property_read_u32_index(dev->of_node, prop, 0, &reg);
	if (ret)
		return ERR_PTR(-ENODATA);
	addr = ioremap(reg, size);
	if (!addr)
		return ERR_PTR(-ENOMEM);

	return addr;
}

int edgetpu_soc_check_supplier_devices(struct device *dev)
{
	/* gsx01 platforms have no supplier devices */
	return 0;
}

int edgetpu_soc_early_init(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev = to_platform_device(etdev->dev);
	int ret;

	etdev->soc_data = devm_kzalloc(&pdev->dev, sizeof(*etdev->soc_data), GFP_KERNEL);
	if (!etdev->soc_data)
		return -ENOMEM;

	etdev->num_active_states = EDGETPU_NUM_STATES;
	etdev->active_states = _edgetpu_active_states;
	etdev->max_active_state = TPU_ACTIVE_NOM;

	mutex_init(&etdev->soc_data->scenario_lock);
	ret = gsx01_parse_ssmt(etdev);
	if (ret)
		dev_warn(etdev->dev, "SSMT setup failed (%d). Context isolation not enforced", ret);

	ret = gsx01_parse_cmu(etdev);
	if (ret)
		dev_warn(etdev->dev, "CMU setup failed (%d). Can't query TPU core frequency.", ret);

	edgetpu_gsx01_parse_pmu(etdev);
	return 0;
}

/*
 * Set shareability for enabling IO coherency (for Rio with 2 MCUs).
 */
static int edgetpu_gsx01_mmu_set_shareability(struct device *dev)
{
	void __iomem *addr = reg_base_of_prop(dev, "edgetpu,shareability", PAGE_SIZE);

	if (IS_ERR(addr))
		return PTR_ERR(addr);

	writel_relaxed(SHAREABLE_WRITE | SHAREABLE_READ | INNER_SHAREABLE,
		       addr + EDGETPU_SYSREG_TPU0_SHAREABILITY);
	writel_relaxed(SHAREABLE_WRITE | SHAREABLE_READ | INNER_SHAREABLE,
		       addr + EDGETPU_SYSREG_TPU1_SHAREABILITY);
	iounmap(addr);
	return 0;
}

/*
 * Disables all contexts in S2MPU. Only required for platforms where bootloader doesn't disable it
 * for us.
 */
static int edgetpu_gsx01_disable_s2mpu(struct device *dev)
{
	void __iomem *addr = reg_base_of_prop(dev, "edgetpu,s2mpu", PAGE_SIZE);
	u32 num_context;

	if (IS_ERR(addr)) {
		/* ignore errors when the property doesn't exist */
		if (PTR_ERR(addr) == -ENODATA)
			return 0;
		return PTR_ERR(addr);
	}
	num_context = readl_relaxed(addr + EDGEPTU_S2MPU_REG_NUM_CONTEXT);
	writel_relaxed((1u << num_context) - 1, addr + EDGETPU_S2MPU_REG_CTRL_CLR);
	iounmap(addr);
	return 0;
}

int edgetpu_soc_post_power_on_init(struct edgetpu_dev *etdev)
{
	int ret;

	ret = edgetpu_gsx01_mmu_set_shareability(etdev->dev);
	if (ret)
		etdev_warn(etdev, "failed to enable shareability: %d", ret);
	ret = edgetpu_gsx01_disable_s2mpu(etdev->dev);
	if (ret)
		etdev_warn(etdev, "failed to disable S2MPU: %d", ret);
	return 0;
}

void edgetpu_soc_exit(struct edgetpu_dev *etdev)
{
}

/* Caller ensures vid < EDGETPU_MAX_STREAM_ID. */
static void set_ssmt_vid(struct edgetpu_dev *etdev, uint vid, uint val)
{
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	int i;

	for (i = 0; i < soc_data->num_ssmts; i++) {
		if (soc_data->ssmt_base[i]) {
			writel(val, SSMT_NS_READ_STREAM_VID_REG(soc_data->ssmt_base[i], vid));
			writel(val, SSMT_NS_WRITE_STREAM_VID_REG(soc_data->ssmt_base[i], vid));
		}
	}
}

static void gsx01_setup_ssmt(struct edgetpu_dev *etdev)
{
	int i;

	for (i = 0; i < EDGETPU_MAX_STREAM_ID; i++)
		set_ssmt_vid(etdev, i, 0);
}

int edgetpu_soc_prepare_firmware(struct edgetpu_dev *etdev)
{
	gsx01_setup_ssmt(etdev);
	return 0;
}

void edgetpu_soc_pm_post_fw_start(struct edgetpu_dev *etdev)
{
	if (etdev->soc_data->bcl_dev)
		google_init_tpu_ratio(etdev->soc_data->bcl_dev);
}

static void gsx01_cleanup_bts_scenario(struct edgetpu_dev *etdev)
{
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	int performance_scenario = soc_data->performance_scenario;

	if (!performance_scenario)
		return;

	mutex_lock(&soc_data->scenario_lock);
	while (soc_data->scenario_count) {
		int ret = bts_del_scenario(performance_scenario);

		if (ret) {
			soc_data->scenario_count = 0;
			etdev_warn_once(etdev, "error %d in cleaning up BTS scenario %u\n", ret,
					performance_scenario);
			break;
		}
		soc_data->scenario_count--;
	}
	mutex_unlock(&soc_data->scenario_lock);
}

static void gsx01_activate_bts_scenario(struct edgetpu_dev *etdev)
{
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	int performance_scenario = soc_data->performance_scenario;

	/* bts_add_scenario() keeps track of reference count internally.*/
	int ret;

	if (!performance_scenario)
		return;
	mutex_lock(&soc_data->scenario_lock);
	ret = bts_add_scenario(performance_scenario);
	if (ret)
		etdev_warn_once(etdev, "error %d adding BTS scenario %u\n", ret,
				performance_scenario);
	else
		soc_data->scenario_count++;

	etdev_dbg(etdev, "BTS Scenario activated: %d\n", soc_data->scenario_count);
	mutex_unlock(&soc_data->scenario_lock);
}

static void gsx01_deactivate_bts_scenario(struct edgetpu_dev *etdev)
{
	/* bts_del_scenario() keeps track of reference count internally.*/
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	int performance_scenario = soc_data->performance_scenario;
	int ret;

	if (!performance_scenario)
		return;
	mutex_lock(&soc_data->scenario_lock);
	if (!soc_data->scenario_count) {
		mutex_unlock(&soc_data->scenario_lock);
		return;
	}
	ret = bts_del_scenario(performance_scenario);
	if (ret)
		etdev_warn_once(etdev, "error %d deleting BTS scenario %u\n", ret,
				performance_scenario);
	else
		soc_data->scenario_count--;

	etdev_dbg(etdev, "BTS Scenario deactivated: %d\n", soc_data->scenario_count);
	mutex_unlock(&soc_data->scenario_lock);
}

static void gsx01_set_bts(struct edgetpu_dev *etdev, u16 bts_val)
{
	etdev_dbg(etdev, "%s: bts request - val = %u\n", __func__, bts_val);

	switch (bts_val) {
	case 0:
		gsx01_deactivate_bts_scenario(etdev);
		break;
	case 1:
		gsx01_activate_bts_scenario(etdev);
		break;
	default:
		etdev_warn(etdev, "invalid BTS request value: %u\n", bts_val);
		break;
	}
}

static void gsx01_set_pm_qos(struct edgetpu_dev *etdev, u32 pm_qos_val)
{
	s32 int_val = (pm_qos_val >> PM_QOS_INT_SHIFT) * PM_QOS_FACTOR;
	s32 mif_val = (pm_qos_val & PM_QOS_MIF_MASK) * PM_QOS_FACTOR;

	etdev_dbg(etdev, "%s: pm_qos request - int = %d mif = %d\n", __func__, int_val, mif_val);

	exynos_pm_qos_update_request(&etdev->soc_data->int_min, int_val);
	exynos_pm_qos_update_request(&etdev->soc_data->mif_min, mif_val);
}

void edgetpu_soc_handle_reverse_kci(struct edgetpu_dev *etdev,
				    struct gcip_kci_response_element *resp)
{
	int ret;

	switch (resp->code) {
	case RKCI_CODE_PM_QOS_BTS:
		/* FW indicates to ignore the request by setting them to undefined values. */
		if (resp->rkci_value2 != U32_MAX)
			gsx01_set_pm_qos(etdev, resp->rkci_value2);
		if (resp->rkci_value1 != U16_MAX)
			gsx01_set_bts(etdev, resp->rkci_value1);
		ret = edgetpu_kci_resp_rkci_ack(etdev, resp);
		if (ret)
			etdev_err(etdev, "failed to send rkci resp for %llu (%d)", resp->seq, ret);
		break;
	default:
		etdev_warn(etdev, "Unrecognized KCI request: %u\n", resp->code);
		break;
	}
}

static unsigned long edgetpu_pm_rate;

long edgetpu_soc_pm_get_rate(struct edgetpu_dev *etdev, int flags)
{
	void __iomem *cmu_base = etdev->soc_data->cmu_base;
	long curr_state;
	u32 pll_con3;

	if (IS_ENABLED(CONFIG_EDGETPU_TEST))
		return edgetpu_pm_rate;

	if (!cmu_base)
		return -EINVAL;

	/* We need to keep TPU being powered to ensure CMU read is valid. */
	if (edgetpu_pm_get_if_powered(etdev, true))
		return 0;
	pll_con3 = readl(cmu_base + PLL_CON3_OFFSET);
	edgetpu_pm_put(etdev);

	/*
	 * Below values must match the CMU PLL (pll_con3_pll_tpu) values in the spec and firmware.
	 * See [REDACTED] and
	 * power_manager.cc for more details.
	 */
	switch (TO_PLL_DIV_M(pll_con3)) {
	case 221:
		curr_state = TPU_ACTIVE_MIN;
		break;
	case 222:
		curr_state = TPU_ACTIVE_ULTRA_LOW;
		break;
	case 153:
		curr_state = TPU_ACTIVE_VERY_LOW;
		break;
	case 174:
		curr_state = TPU_ACTIVE_SUB_LOW;
		break;
	case 206:
		curr_state = TPU_ACTIVE_LOW;
		break;
	case 130:
		curr_state = TPU_ACTIVE_MEDIUM;
		break;
	case 182:
		curr_state = TPU_ACTIVE_NOM;
		break;
	default:
		etdev_err(etdev, "Invalid DIV_M read from PLL: %lu\n", TO_PLL_DIV_M(pll_con3));
		curr_state = -EINVAL;
	}

	etdev_dbg(etdev, "current tpu state: %ld\n", curr_state);

	return curr_state;
}

static int edgetpu_core_rate_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_CLK_CORE_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

static int edgetpu_ctl_rate_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_CLK_CTL_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

static int edgetpu_axi_rate_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_CLK_AXI_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

static int edgetpu_apb_rate_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_CLK_APB_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

static int edgetpu_uart_rate_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_CLK_UART_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

static int edgetpu_vdd_int_m_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_VDD_INT_M_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

static int edgetpu_vdd_tpu_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_VDD_TPU_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

static int edgetpu_vdd_tpu_m_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = (typeof(etdev))data;

	if (edgetpu_pm_get_if_powered(etdev, true)) {
		*val = 0;
	} else {
		*val = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_VDD_TPU_M_DEBUG);
		edgetpu_pm_put(etdev);
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_core_rate, edgetpu_core_rate_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_ctl_rate, edgetpu_ctl_rate_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_axi_rate, edgetpu_axi_rate_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_apb_rate, edgetpu_apb_rate_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_uart_rate, edgetpu_uart_rate_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_vdd_int_m, edgetpu_vdd_int_m_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_vdd_tpu, edgetpu_vdd_tpu_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_tpu_vdd_tpu_m, edgetpu_vdd_tpu_m_get, NULL, "%llu\n");

void edgetpu_soc_pm_power_down(struct edgetpu_dev *etdev)
{
	/* Remove our vote for INT/MIF state (if any) */
	exynos_pm_qos_update_request(&etdev->soc_data->int_min, 0);
	exynos_pm_qos_update_request(&etdev->soc_data->mif_min, 0);

	gsx01_cleanup_bts_scenario(etdev);
}

bool edgetpu_soc_pm_is_block_off(struct edgetpu_dev *etdev)
{
	return etdev->soc_data->pmu_status ? !readl(etdev->soc_data->pmu_status) : false;
}

void edgetpu_soc_pm_lpm_down(struct edgetpu_dev *etdev)
{
	int timeout_cnt = 0;
	u32 val;

	do {
		/* Manually delay 200us per retry till LPM shutdown finished */
		usleep_range(SHUTDOWN_DELAY_US_MIN, SHUTDOWN_DELAY_US_MAX);
		val = edgetpu_dev_read_32_sync(etdev, EDGETPU_REG_LPM_CONTROL);
		if ((val & 0x100) || (val == 0))
			break;
		timeout_cnt++;
	} while (timeout_cnt < SHUTDOWN_MAX_DELAY_COUNT);
	if (timeout_cnt == SHUTDOWN_MAX_DELAY_COUNT)
		/* Log the issue then continue to perform the shutdown forcefully. */
		etdev_warn(etdev, "LPM shutdown failure, continuing BLK shutdown\n");
}

#if IS_ENABLED(CONFIG_RIO)
static void rio_patch_lpm(struct edgetpu_dev *etdev)
{
	/* Pchannel handshake fix for b/210907864. */
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 190, 0x11061104);

	/* FRC retention fix for cl/412125437. */
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 3, 0x10051101);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 13, 0x11051001);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 22, 0x22001005);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 28, 0x21261004);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 32, 0x100b1104);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 33, 0x100f2126);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 41, 0x10090011);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 135, 0x110e100b);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 138, 0x21251110);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 164, 0x100b1109);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 174, 0x22b32125);

	/* DVFS fix for b/211411986. */
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 197, 0x00241002);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 198, 0x0bc75301);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 199, 0x00241102);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 200, 0x0bc95001);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 201, 0x14171018);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 202, 0x02001118);

	/* psm_1_state_table_0_trans_1_next_state */
	edgetpu_dev_write_32_sync(etdev, 0x1c2020, 0x00000000);
	/* psm_1_state_table_0_trans_1_seq_addr */
	edgetpu_dev_write_32_sync(etdev, 0x1c2024, 0x000000c5);
	/* psm_1_state_table_0_trans_1_trigger_num */
	edgetpu_dev_write_32_sync(etdev, 0x1c2030, 0x00000005);
	/* psm_1_state_table_0_trans_1_trigger_en */
	edgetpu_dev_write_32_sync(etdev, 0x1c2034, 0x00000001);
	/* trigger_csr_events_en_5_hi */
	edgetpu_dev_write_32_sync(etdev, 0x1c012c, 0x00000003);

	/*
	 * FRC clocking fix for b/287661979.
	 *
	 * Increases the delay between cluster clock enablement and logic
	 * retention/restore activation.
	 */
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 3, 0x21261101);
	EDGETPU_LPM_IMEM_OPS_SET(etdev, 4, 0x11111005);
}
#endif /* CONFIG_RIO */

int edgetpu_soc_pm_lpm_up(struct edgetpu_dev *etdev)
{
	int ret, i;
	u32 val;

#if IS_ENABLED(CONFIG_RIO)
	rio_patch_lpm(etdev);
#endif

	/* set coreNewPwrState = 0x4, coreNewPowerStateReq = 0x1 */
	val = edgetpu_dev_read_32_sync(etdev, EDGETPU_LPM_CORE_CSR);
	val = val | (4 << 3);
	val = val | (1 << 6);
	edgetpu_dev_write_32_sync(etdev, EDGETPU_LPM_CORE_CSR, val);
	/* set clusterNewPwrState = 0x1 */
	edgetpu_dev_write_32_sync(etdev, EDGETPU_LPM_CLUSTER_CSR0, 1 << 1);
	edgetpu_dev_write_32_sync(etdev, EDGETPU_LPM_CLUSTER_CSR1, 1 << 1);

	/* If lpmCtlOffState is set, clear tpuPowerOff and leave */
	val = edgetpu_dev_read_32_sync(etdev, EDGETPU_LPM_CONTROL_CSR);
	if (val & (1 << 8)) {
		edgetpu_dev_write_32_sync(etdev, EDGETPU_LPM_CONTROL_CSR, val & ~(1 << 5));
		return 0;
	}
	for (i = 0; i < 4; i++) {
		val = edgetpu_dev_read_32_sync(etdev, EDGETPU_PSM0_STATUS + i * 0x1000);
		/* only bring up LPM when it's in an invalid state */
		if ((val & 0x10) == 0)
			edgetpu_dev_write_32_sync(etdev, EDGETPU_PSM0_START + i * 0x1000, 1);
	}
	for (i = 0; i < 4; i++) {
		ret = readl_poll_timeout(etdev->regs.mem + EDGETPU_PSM0_STATUS + i * 0x1000, val,
					 val & 0x80, 5, EDGETPU_LPM_CHANGE_TIMEOUT);
		if (ret) {
			etdev_err(etdev, "Set PSM%d failed: %d\n", i, ret);
			return ret;
		}
	}
	for (i = 0; i < 4; i++)
		edgetpu_dev_write_32_sync(etdev, EDGETPU_PSM0_CFG + i * 0x1000, 0);
	/* set clockGateAllowed = 0x1 */
	edgetpu_dev_write_32_sync(etdev, EDGETPU_LPM_CONTROL_CSR, 1 << 3);
	/* set axiReorderClockGateEn = 0x1, tpuTopClockGateEn = 0x1 */
	edgetpu_dev_write_32_sync(etdev, EDGETPU_TOP_CLOCK_GATE_CONTROL_CSR, 3);

	return 0;
}

/*
 * Log TPU block power state for debugging.  The block is not required to be powered up for
 * this function on this SoC family.
 */
void edgetpu_soc_pm_dump_block_state(struct edgetpu_dev *etdev)
{
	if (IS_ENABLED(CONFIG_EDGETPU_TEST))
		return;

	if (etdev->soc_data->pmu_status)
		etdev_warn(etdev, "pmu_status=%#x\n", readl(etdev->soc_data->pmu_status));
}

int edgetpu_soc_pm_init(struct edgetpu_dev *etdev)
{
	etdev->soc_data->bcl_dev = google_retrieve_bcl_handle();

	exynos_pm_qos_add_request(&etdev->soc_data->int_min, PM_QOS_DEVICE_THROUGHPUT, 0);
	exynos_pm_qos_add_request(&etdev->soc_data->mif_min, PM_QOS_BUS_THROUGHPUT, 0);

	etdev->soc_data->performance_scenario = bts_get_scenindex("tpu_performance");
	if (!etdev->soc_data->performance_scenario)
		dev_warn(etdev->dev, "tpu_performance BTS scenario not found\n");
	etdev->soc_data->scenario_count = 0;

	debugfs_create_file("vdd_tpu", 0660, etdev->pm->debugfs_dir, etdev, &fops_tpu_vdd_tpu);
	debugfs_create_file("vdd_tpu_m", 0660, etdev->pm->debugfs_dir, etdev,
			    &fops_tpu_vdd_tpu_m);
	debugfs_create_file("vdd_int_m", 0660, etdev->pm->debugfs_dir, etdev,
			    &fops_tpu_vdd_int_m);
	debugfs_create_file("core_rate", 0660, etdev->pm->debugfs_dir, etdev,
			    &fops_tpu_core_rate);
	debugfs_create_file("ctl_rate", 0660, etdev->pm->debugfs_dir, etdev, &fops_tpu_ctl_rate);
	debugfs_create_file("axi_rate", 0660, etdev->pm->debugfs_dir, etdev, &fops_tpu_axi_rate);
	debugfs_create_file("apb_rate", 0440, etdev->pm->debugfs_dir, etdev, &fops_tpu_apb_rate);
	debugfs_create_file("uart_rate", 0440, etdev->pm->debugfs_dir, etdev,
			    &fops_tpu_uart_rate);
	return 0;
}

void edgetpu_soc_pm_exit(struct edgetpu_dev *etdev)
{
	gsx01_cleanup_bts_scenario(etdev);
	exynos_pm_qos_remove_request(&etdev->soc_data->int_min);
	exynos_pm_qos_remove_request(&etdev->soc_data->mif_min);
}

static int tpu_pause_callback(enum thermal_pause_state action, void *data)
{
	struct gcip_thermal *thermal = data;
	int ret = -EINVAL;

	if (!thermal)
		return ret;

	if (action == THERMAL_SUSPEND)
		ret = gcip_thermal_suspend_device(thermal);
	else if (action == THERMAL_RESUME)
		ret = gcip_thermal_resume_device(thermal);

	return ret;
}

void edgetpu_soc_thermal_init(struct edgetpu_dev *etdev)
{
	struct gcip_thermal *thermal = etdev->thermal;
	struct notifier_block *nb = gcip_thermal_get_notifier_block(thermal);

	register_tpu_thermal_pause_cb(tpu_pause_callback, thermal);

	if (etdev->soc_data->bcl_dev)
		exynos_pm_qos_add_notifier(PM_QOS_TPU_FREQ_MAX, nb);
}

void edgetpu_soc_thermal_exit(struct edgetpu_dev *etdev)
{
	struct gcip_thermal *thermal = etdev->thermal;
	struct notifier_block *nb = gcip_thermal_get_notifier_block(thermal);

	if (etdev->soc_data->bcl_dev)
		exynos_pm_qos_remove_notifier(PM_QOS_TPU_FREQ_MAX, nb);
}

int edgetpu_soc_activate_context(struct edgetpu_dev *etdev, int pasid)
{
	const uint vid = pasid;

	if (vid >= EDGETPU_MAX_STREAM_ID)
		return -EINVAL;

	set_ssmt_vid(etdev, vid, vid);

	return 0;
}

void edgetpu_soc_deactivate_context(struct edgetpu_dev *etdev, int pasid)
{
	const uint vid = pasid;

	if (vid >= EDGETPU_MAX_STREAM_ID)
		return;

	set_ssmt_vid(etdev, vid, 0);
}

void edgetpu_soc_set_tpu_cpu_security(struct edgetpu_dev *etdev)
{
	const int ctx_id = 0, sid0 = 0x30, sid1 = 0x34;

	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_SECURITY, (ctx_id << 16) | sid0);
	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_SECURITY + 8,
			     (ctx_id << 16) | sid1);
}

int edgetpu_soc_setup_irqs(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev = to_platform_device(etdev->dev);
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	int n = platform_irq_count(pdev);
	int ret;
	int i;

	if (n < 0) {
		dev_err(etdev->dev, "Error retrieving IRQ count: %d\n", n);
		return n;
	}

	etmdev->mailbox_irq = devm_kmalloc_array(etdev->dev, n, sizeof(*etmdev->mailbox_irq),
						 GFP_KERNEL);
	if (!etmdev->mailbox_irq)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		etmdev->mailbox_irq[i] = platform_get_irq(pdev, i);
		ret = devm_request_irq(etdev->dev, etmdev->mailbox_irq[i],
				       edgetpu_mailbox_irq_handler, IRQF_ONESHOT, etdev->dev_name,
				       etdev);
		if (ret) {
			dev_err(etdev->dev, "%s: failed to request mailbox irq %d: %d\n",
				etdev->dev_name, etmdev->mailbox_irq[i], ret);
			return ret;
		}
	}
	etmdev->n_mailbox_irq = n;
	return 0;
}
