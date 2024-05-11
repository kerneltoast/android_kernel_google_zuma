// SPDX-License-Identifier: GPL-2.0-only
/*
 * Structures and helpers for managing GXP MicroController Unit.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/delay.h>
#include <linux/sizes.h>

#include <gcip/gcip-mem-pool.h>

#include "gxp-config.h"
#include "gxp-internal.h"
#include "gxp-lpm.h"
#include "gxp-mcu-firmware.h"
#include "gxp-mcu.h"
#include "gxp-uci.h"

/* Setting bit 15 and 16 of GPOUT_LO_WRT register to 0 will hold MCU reset. */
#define GPOUT_LO_MCU_RESET (3u << 15)
#define GPOUT_LO_MCU_PSTATE (1u << 2)
#define GPOUT_LO_MCU_PREG (1u << 3)
#define GPIN_LO_MCU_PACCEPT (1u << 2)
#define GPIN_LO_MCU_PDENY (1u << 3)

/* Allocates the MCU <-> cores shared buffer region. */
static int gxp_alloc_shared_buffer(struct gxp_dev *gxp, struct gxp_mcu *mcu)
{
	const size_t size = GXP_SHARED_BUFFER_SIZE;
	phys_addr_t paddr;
	struct gxp_mapped_resource *res = &mcu->gxp->shared_buf;
	size_t offset;
	void *vaddr;

	paddr = gcip_mem_pool_alloc(&mcu->remap_data_pool, size);
	if (!paddr)
		return -ENOMEM;
	res->paddr = paddr;
	res->size = size;
	res->daddr = GXP_IOVA_SHARED_BUFFER;

	/* clear shared buffer to make sure it's clean */
	offset = gcip_mem_pool_offset(&mcu->remap_data_pool, paddr);
	vaddr = offset + (mcu->fw.image_buf.vaddr + GXP_IREMAP_DATA_OFFSET);
	memset(vaddr, 0, size);
	res->vaddr = vaddr;

	return 0;
}

static void gxp_free_shared_buffer(struct gxp_mcu *mcu)
{
	struct gxp_mapped_resource *res = &mcu->gxp->shared_buf;

	gcip_mem_pool_free(&mcu->remap_data_pool, res->paddr, res->size);
}

/*
 * Initializes memory pools, must be called after @mcu->fw has been initialized
 * to have a valid image_buf.
 */
static int gxp_mcu_mem_pools_init(struct gxp_dev *gxp, struct gxp_mcu *mcu)
{
	phys_addr_t iremap_paddr = mcu->fw.image_buf.paddr;
	int ret;

	ret = gcip_mem_pool_init(&mcu->remap_data_pool, gxp->dev,
				 iremap_paddr + GXP_IREMAP_DATA_OFFSET,
				 GXP_IREMAP_DATA_SIZE, SZ_4K);
	if (ret)
		return ret;
	ret = gcip_mem_pool_init(&mcu->remap_secure_pool, gxp->dev,
				 iremap_paddr + GXP_IREMAP_SECURE_OFFSET,
				 GXP_IREMAP_SECURE_SIZE, SZ_4K);
	if (ret) {
		gcip_mem_pool_exit(&mcu->remap_data_pool);
		return ret;
	}
	return 0;
}

static void gxp_mcu_mem_pools_exit(struct gxp_mcu *mcu)
{
	gcip_mem_pool_exit(&mcu->remap_secure_pool);
	gcip_mem_pool_exit(&mcu->remap_data_pool);
}

int gxp_mcu_mem_alloc_data(struct gxp_mcu *mcu, struct gxp_mapped_resource *mem, size_t size)
{
	size_t offset;
	phys_addr_t paddr;

	paddr = gcip_mem_pool_alloc(&mcu->remap_data_pool, size);
	if (!paddr)
		return -ENOMEM;
	offset = gcip_mem_pool_offset(&mcu->remap_data_pool, paddr);
	mem->size = size;
	mem->paddr = paddr;
	mem->vaddr = offset + (mcu->fw.image_buf.vaddr + GXP_IREMAP_DATA_OFFSET);
	mem->daddr = offset + (mcu->fw.image_buf.daddr + GXP_IREMAP_DATA_OFFSET);
	return 0;
}

void gxp_mcu_mem_free_data(struct gxp_mcu *mcu, struct gxp_mapped_resource *mem)
{
	gcip_mem_pool_free(&mcu->remap_data_pool, mem->paddr, mem->size);
	mem->size = 0;
	mem->paddr = 0;
	mem->vaddr = NULL;
	mem->daddr = 0;
}

int gxp_mcu_init(struct gxp_dev *gxp, struct gxp_mcu *mcu)
{
	int ret;

	mcu->gxp = gxp;
	ret = gxp_mcu_firmware_init(gxp, &mcu->fw);
	if (ret)
		return ret;
	ret = gxp_mcu_mem_pools_init(gxp, mcu);
	if (ret)
		goto err_fw_exit;
	ret = gxp_alloc_shared_buffer(gxp, mcu);
	if (ret)
		goto err_pools_exit;
	/*
	 * MCU telemetry must be initialized before UCI and KCI to match the
	 * .log_buffer address in the firmware linker.ld.
	 */
	ret = gxp_mcu_telemetry_init(mcu);
	if (ret)
		goto err_free_shared_buffer;
	ret = gxp_uci_init(mcu);
	if (ret)
		goto err_telemetry_exit;
	ret = gxp_kci_init(mcu);
	if (ret)
		goto err_uci_exit;
	return 0;

err_uci_exit:
	gxp_uci_exit(&mcu->uci);
err_telemetry_exit:
	gxp_mcu_telemetry_exit(mcu);
err_free_shared_buffer:
	gxp_free_shared_buffer(mcu);
err_pools_exit:
	gxp_mcu_mem_pools_exit(mcu);
err_fw_exit:
	gxp_mcu_firmware_exit(&mcu->fw);
	return ret;
}

void gxp_mcu_exit(struct gxp_mcu *mcu)
{
	gxp_kci_exit(&mcu->kci);
	gxp_uci_exit(&mcu->uci);
	gxp_mcu_telemetry_exit(mcu);
	gxp_free_shared_buffer(mcu);
	gxp_mcu_mem_pools_exit(mcu);
	gxp_mcu_firmware_exit(&mcu->fw);
}

int gxp_mcu_reset(struct gxp_dev *gxp, bool release_reset)
{
	u32 gpout_lo_rd, gpin_lo_rd, orig;
	int i, ret = 0;

	/* 1. Read gpout_lo_rd register. */
	orig = gpout_lo_rd =
		lpm_read_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), PSM_REG_GPOUT_LO_RD_OFFSET);

	/* 2. Toggle bit 15 and 16 of this register to '0'. */
	gpout_lo_rd &= ~GPOUT_LO_MCU_RESET;

	/* 3. Set psm in debug mode with debug_cfg.en=1 and debug_cfg.gpout_override=1. */
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), PSM_REG_DEBUG_CFG_OFFSET, 0b11);

	/* 4. Write the modified value from step2 to gpout_lo_wrt register. */
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), PSM_REG_GPOUT_LO_WRT_OFFSET,
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

	if (!release_reset)
		return 0;

	/*
	 * 6. Modify gpout_lo_wrt register locally to set bit [3:2]={1,0} to let MCU transit to
	 * RUN state.
	 */
	gpout_lo_rd = (gpout_lo_rd | GPOUT_LO_MCU_PREG) & ~GPOUT_LO_MCU_PSTATE;
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), PSM_REG_GPOUT_LO_WRT_OFFSET,
			 gpout_lo_rd);

	/* 7. Toggle bit 15 and 16 of gpout_lo_wrt register to '1' to release reset. */
	gpout_lo_rd |= GPOUT_LO_MCU_RESET;
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), PSM_REG_GPOUT_LO_WRT_OFFSET,
			 gpout_lo_rd);

	/* 8. Poll gpin_lo_rd for one of bit 2 (paccept) and 3 (pdeny) becoming non-zero. */
	for (i = 10000; i > 0; i--) {
		gpin_lo_rd = lpm_read_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID),
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
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), PSM_REG_GPOUT_LO_WRT_OFFSET, orig);

	/*
	 * 10. Move PSM back to func mode with gpout override disabled debug_cfg.en=0 and
	 * debug_cfg.gpout=0.
	 */
	lpm_write_32_psm(gxp, CORE_TO_PSM(GXP_MCU_CORE_ID), PSM_REG_DEBUG_CFG_OFFSET, 0);

	return ret;
}
