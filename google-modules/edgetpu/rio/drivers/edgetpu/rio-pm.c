// SPDX-License-Identifier: GPL-2.0
/*
 * Rio EdgeTPU power management support
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <soc/google/bcl.h>

#include "edgetpu-config.h"
#include "edgetpu-internal.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "mobile-soc-gsx01.h"
#include "mobile-pm.h"

#include "mobile-pm.c"

#define SHUTDOWN_DELAY_US_MIN 200
#define SHUTDOWN_DELAY_US_MAX 200
#define BOOTUP_DELAY_US_MIN 100
#define BOOTUP_DELAY_US_MAX 150
#define SHUTDOWN_MAX_DELAY_COUNT 20

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

static void rio_lpm_down(struct edgetpu_dev *etdev)
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
		// Log the issue then continue to perform the shutdown forcefully.
		etdev_warn(etdev, "LPM shutdown failure, continuing BLK shutdown\n");
}

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

static int rio_lpm_up(struct edgetpu_dev *etdev)
{
	int ret, i;
	u32 val;

	rio_patch_lpm(etdev);

	/* TODO(b/225804117) : Move core and cluster PSM setup to fw. */
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

static bool rio_is_block_down(struct edgetpu_dev *etdev)
{
	int timeout_cnt = 0;
	int curr_state;

	do {
		/* Delay 20us per retry till blk shutdown finished */
		usleep_range(SHUTDOWN_DELAY_US_MIN, SHUTDOWN_DELAY_US_MAX);
		curr_state = readl(etdev->pmu_status);
		if (!curr_state)
			return true;
		timeout_cnt++;
	} while (timeout_cnt < SHUTDOWN_MAX_DELAY_COUNT);

	return false;
}

static void rio_post_fw_start(struct edgetpu_dev *etdev)
{
	if (etdev->soc_data->bcl_dev)
		google_init_tpu_ratio(etdev->soc_data->bcl_dev);
}

int edgetpu_chip_pm_create(struct edgetpu_dev *etdev)
{
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_platform_pwr *platform_pwr = &etmdev->platform_pwr;

	platform_pwr->lpm_up = rio_lpm_up;
	platform_pwr->lpm_down = rio_lpm_down;
	if (etdev->pmu_status)
		platform_pwr->is_block_down = rio_is_block_down;
	platform_pwr->post_fw_start = rio_post_fw_start;

	etdev->soc_data->bcl_dev = google_retrieve_bcl_handle();

	return edgetpu_mobile_pm_create(etdev);
}
