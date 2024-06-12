// SPDX-License-Identifier: GPL-2.0
/*
 * google_bcl_core.c Google bcl driver
 *
 * Copyright (c) 2022, Google LLC. All rights reserved.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mfd/samsung/s2mpg1415.h>
#include <linux/mfd/samsung/s2mpg1415-register.h>
#include <soc/google/exynos-cpupm.h>
#include <soc/google/exynos-pm.h>
#include <soc/google/exynos-pmu-if.h>
#include <soc/google/bcl.h>

const unsigned int subsystem_pmu[] = {
	PMU_ALIVE_CPU0_STATES,
	PMU_ALIVE_CPU1_STATES,
	PMU_ALIVE_CPU2_STATES,
	PMU_ALIVE_TPU_STATES,
	PMU_ALIVE_GPU_STATES,
	PMU_ALIVE_AUR_STATES
};

int meter_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return s2mpg15_write_reg((bcl_dev)->sub_meter_i2c, reg, value);
	case CORE_PMIC_MAIN:
		return s2mpg14_write_reg((bcl_dev)->main_meter_i2c, reg, value);
	}
	return 0;
}

int meter_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return s2mpg15_read_reg((bcl_dev)->sub_meter_i2c, reg, value);
	case CORE_PMIC_MAIN:
		return s2mpg14_read_reg((bcl_dev)->main_meter_i2c, reg, value);
	}
	return 0;
}

int pmic_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return s2mpg15_write_reg((bcl_dev)->sub_pmic_i2c, reg, value);
	case CORE_PMIC_MAIN:
		return s2mpg14_write_reg((bcl_dev)->main_pmic_i2c, reg, value);
	}
	return 0;
}

int pmic_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return s2mpg15_read_reg((bcl_dev)->sub_pmic_i2c, reg, value);
	case CORE_PMIC_MAIN:
		return s2mpg14_read_reg((bcl_dev)->main_pmic_i2c, reg, value);
	}
	return 0;
}

bool bcl_is_cluster_on(int cluster)
{
#if IS_ENABLED(CONFIG_SOC_ZUMA)
	unsigned int addr, value = 0;

	if (cluster < CPU2_CLUSTER_MIN) {
		addr = CLUSTER1_NONCPU_STATES;
		exynos_pmu_read(addr, &value);
		return value & BIT(4);
	}
	if (cluster == CPU2_CLUSTER_MIN) {
		addr = CLUSTER2_NONCPU_STATES;
		exynos_pmu_read(addr, &value);
		return value & BIT(4);
	}
#endif
	return false;
}

bool bcl_is_subsystem_on(unsigned int addr)
{
	unsigned int value;

	switch (addr) {
	case PMU_ALIVE_TPU_STATES:
	case PMU_ALIVE_GPU_STATES:
	case PMU_ALIVE_AUR_STATES:
		exynos_pmu_read(addr, &value);
		return !(value & BIT(7));
	case PMU_ALIVE_CPU0_STATES:
		return true;
	case PMU_ALIVE_CPU1_STATES:
		return bcl_is_cluster_on(CPU1_CLUSTER_MIN);
	case PMU_ALIVE_CPU2_STATES:
		return bcl_is_cluster_on(CPU2_CLUSTER_MIN);
	}
	return false;
}

bool bcl_disable_power(int cluster)
{
	int i;
#if IS_ENABLED(CONFIG_SOC_ZUMA)
	if (cluster == SUBSYSTEM_CPU1) {
		for (i = CPU1_CLUSTER_MIN; i < CPU2_CLUSTER_MIN; i++) {
			if (bcl_is_cluster_on(i) == true) {
				disable_power_mode(i, POWERMODE_TYPE_CLUSTER);
			} else
				return false;
		}
	}
	else if (cluster == SUBSYSTEM_CPU2) {
		if (bcl_is_cluster_on(CPU2_CLUSTER_MIN) == true) {
			disable_power_mode(CPU2_CLUSTER_MIN, POWERMODE_TYPE_CLUSTER);
		} else {
			return false;
		}
	}
#else
	if (cluster == SUBSYSTEM_CPU1)
		for (i = CPU1_CLUSTER_MIN; i < CPU2_CLUSTER_MIN; i++)
			disable_power_mode(i, POWERMODE_TYPE_CLUSTER);
	else if (cluster == SUBSYSTEM_CPU2)
		disable_power_mode(CPU2_CLUSTER_MIN, POWERMODE_TYPE_CLUSTER);
#endif
	return true;
}

bool bcl_enable_power(int cluster)
{
	int i;
#if IS_ENABLED(CONFIG_SOC_ZUMA)
	if (cluster == SUBSYSTEM_CPU1) {
		for (i = CPU1_CLUSTER_MIN; i < CPU2_CLUSTER_MIN; i++) {
			if (bcl_is_cluster_on(i) == true) {
				enable_power_mode(i, POWERMODE_TYPE_CLUSTER);
			} else
				return false;
		}
	}
	else if (cluster == SUBSYSTEM_CPU2) {
		if (bcl_is_cluster_on(CPU2_CLUSTER_MIN) == true) {
			enable_power_mode(CPU2_CLUSTER_MIN, POWERMODE_TYPE_CLUSTER);
		} else
			return false;
	}
#else
	if (cluster == SUBSYSTEM_CPU1)
		for (i = CPU1_CLUSTER_MIN; i < CPU2_CLUSTER_MIN; i++)
			enable_power_mode(i, POWERMODE_TYPE_CLUSTER);
	else if (cluster == SUBSYSTEM_CPU2)
		enable_power_mode(CPU2_CLUSTER_MIN, POWERMODE_TYPE_CLUSTER);
#endif
	return true;
}
