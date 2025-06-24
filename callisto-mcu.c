// SPDX-License-Identifier: GPL-2.0-only
/*
 * Callisto chip specific GXP MicroController Unit management.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/delay.h>

#include "gxp-internal.h"
#include "gxp-lpm.h"
#include "gxp-mcu.h"
#include "gxp-mcu-firmware.h"
#include "gxp-mcu-platform.h"

/* Setting bit 15 and 16 of GPOUT_LO_WRT register to 0 will hold MCU reset. */
#define GPOUT_LO_MCU_RESET (3u << 15)
#define GPOUT_LO_MCU_PSTATE (1u << 2)
#define GPOUT_LO_MCU_PREG (1u << 3)
#define GPIN_LO_MCU_PACCEPT (1u << 2)
#define GPIN_LO_MCU_PDENY (1u << 3)

int gxp_mcu_reset(struct gxp_dev *gxp, bool release_reset)
{
	struct gxp_mcu *mcu = &to_mcu_dev(gxp)->mcu;
	u32 gpout_lo_rd, gpin_lo_rd, orig;
	int i, ret = 0;

	/* 1. Read gpout_lo_rd register. */
	orig = gpout_lo_rd =
		lpm_read_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), PSM_REG_GPOUT_LO_RD_OFFSET);

	/* 2. Toggle bit 15 and 16 of this register to '0'. */
	gpout_lo_rd &= ~GPOUT_LO_MCU_RESET;

	/* 3. Set psm in debug mode with debug_cfg.en=1 and debug_cfg.gpout_override=1. */
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), PSM_REG_DEBUG_CFG_OFFSET, 0b11);

	/* 4. Write the modified value from step2 to gpout_lo_wrt register. */
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), PSM_REG_GPOUT_LO_WRT_OFFSET,
			 gpout_lo_rd);

	/*
	 * 5. Wait for MCU being reset.
	 *
	 * Basically, to verify the MCU reset, we should poll bit 0 of MCU_RESET_STATUS register
	 * (CORERESET_N) to become 0.
	 *
	 * However, as we cannot access the register for the security reason, there is no way to
	 * poll it. Based on the experiment, resetting MCU was already done when the step 4 above
	 * is finished which took under 5 us. Therefore, waiting 1~2 ms as a margin should be
	 * enough.
	 */
	usleep_range(1000, 2000);

	gxp_mcu_reset_mailbox(mcu);

	if (!release_reset)
		return 0;

	/*
	 * 6. Modify gpout_lo_wrt register locally to set bit [3:2]={1,0} to let MCU transit to
	 * RUN state.
	 */
	gpout_lo_rd = (gpout_lo_rd | GPOUT_LO_MCU_PREG) & ~GPOUT_LO_MCU_PSTATE;
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), PSM_REG_GPOUT_LO_WRT_OFFSET,
			 gpout_lo_rd);

	/* 7. Toggle bit 15 and 16 of gpout_lo_wrt register to '1' to release reset. */
	gpout_lo_rd |= GPOUT_LO_MCU_RESET;
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), PSM_REG_GPOUT_LO_WRT_OFFSET,
			 gpout_lo_rd);

	/* 8. Poll gpin_lo_rd for one of bit 2 (paccept) and 3 (pdeny) becoming non-zero. */
	for (i = 10000; i > 0; i--) {
		gpin_lo_rd = lpm_read_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID),
					     PSM_REG_GPIN_LO_RD_OFFSET);
		if (gpin_lo_rd & (GPIN_LO_MCU_PACCEPT | GPIN_LO_MCU_PDENY))
			break;
		udelay(GXP_TIME_DELAY_FACTOR);
	}

	if (!i) {
		dev_warn(gxp->dev, "MCU is not responding to the power control");
		ret = -ETIMEDOUT;
	} else if (gpin_lo_rd & GPIN_LO_MCU_PDENY) {
		dev_warn(gxp->dev, "MCU denied the power control for reset");
		ret = -EAGAIN;
	}

	/* 9. Write gpout_lo_wrt the same as gpout_lo_rd of step 1. */
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), PSM_REG_GPOUT_LO_WRT_OFFSET, orig);

	/*
	 * 10. Move PSM back to func mode with gpout override disabled debug_cfg.en=0 and
	 * debug_cfg.gpout=0.
	 */
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), PSM_REG_DEBUG_CFG_OFFSET, 0);

	return ret;
}

/* Time(us) to boot MCU in recovery mode. */
#define GXP_MCU_RECOVERY_BOOT_DELAY 100

bool gxp_mcu_recovery_boot_shutdown(struct gxp_dev *gxp, bool force)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	int try = 3, ret;
	bool status = false;

	lockdep_assert_held(&mcu_fw->lock);

	do {
		if (force) {
			dev_dbg(gxp->dev, "Trying recovery boot");
			gxp_mcu_set_boot_mode(mcu_fw, GXP_MCU_BOOT_MODE_RECOVERY);
			ret = gxp_mcu_reset(gxp, true);
			udelay(GXP_MCU_RECOVERY_BOOT_DELAY);
			if (ret) {
				dev_err(gxp->dev, "Failed to reset MCU (ret=%d)", ret);
				continue;
			}
		}

		if (gxp_lpm_wait_state_eq(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), LPM_PG_STATE)) {
			status = true;
			break;
		}

		dev_warn(gxp->dev, "MCU PSM transition to PS3 fails, current state: %u, try: %d",
			 gxp_lpm_get_state(gxp, CORE_TO_PSM(GXP_REG_MCU_ID)), try);
		/*
		 * If PG transition fails, MCU will not fall into WFI after the reset.
		 * Therefore, we must boot into recovery to force WFI transition.
		 */
		force = true;
	} while (--try > 0);

	if (status)
		gxp_mcu_firmware_shutdown(mcu_fw);

	return status;
}
