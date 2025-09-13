// SPDX-License-Identifier: GPL-2.0-only
/*
 * Structures and helpers for managing GXP MicroController Unit.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/delay.h>
#include <linux/sizes.h>

#include <gcip/gcip-mem-pool.h>
#include <gcip/gcip-memory.h>

#include "gxp-config.h"
#include "gxp-internal.h"
#include "gxp-kci.h"
#include "gxp-lpm.h"
#include "gxp-mcu-firmware.h"
#include "gxp-mcu-platform.h"
#include "gxp-mcu.h"
#include "gxp-uci.h"

/* Allocates the MCU <-> cores shared buffer region. */
static int gxp_alloc_shared_buffer(struct gxp_dev *gxp, struct gxp_mcu *mcu)
{
	const size_t size = GXP_SHARED_BUFFER_SIZE;
	phys_addr_t paddr;
	struct gcip_memory *res = &mcu->gxp->shared_buf;
	size_t offset;
	void *vaddr;

	paddr = gcip_mem_pool_alloc(&mcu->remap_data_pool, size);
	if (!paddr)
		return -ENOMEM;
	res->phys_addr = paddr;
	res->size = size;
	res->dma_addr = GXP_IOVA_SHARED_BUFFER;

	/* clear shared buffer to make sure it's clean */
	offset = gcip_mem_pool_offset(&mcu->remap_data_pool, paddr);
	vaddr = offset + (mcu->fw.image_buf.virt_addr + GXP_IREMAP_DATA_OFFSET);
	memset(vaddr, 0, size);
	res->virt_addr = vaddr;

	return 0;
}

static void gxp_free_shared_buffer(struct gxp_mcu *mcu)
{
	struct gcip_memory *res = &mcu->gxp->shared_buf;

	gcip_mem_pool_free(&mcu->remap_data_pool, res->phys_addr, res->size);
}

/*
 * Initializes memory pools, must be called after @mcu->fw has been initialized
 * to have a valid image_buf.
 */
static int gxp_mcu_mem_pools_init(struct gxp_dev *gxp, struct gxp_mcu *mcu)
{
	phys_addr_t iremap_phys_addr = mcu->fw.image_buf.phys_addr;
	int ret;

	ret = gcip_mem_pool_init(&mcu->remap_data_pool, gxp->dev,
				 iremap_phys_addr + GXP_IREMAP_DATA_OFFSET, GXP_IREMAP_DATA_SIZE,
				 SZ_4K);
	if (ret)
		return ret;
	ret = gcip_mem_pool_init(&mcu->remap_secure_pool, gxp->dev,
				 iremap_phys_addr + GXP_IREMAP_SECURE_OFFSET,
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

int gxp_mcu_mem_alloc_data(struct gxp_mcu *mcu, struct gcip_memory *mem, size_t size)
{
	size_t offset;
	phys_addr_t paddr;

	paddr = gcip_mem_pool_alloc(&mcu->remap_data_pool, size);
	if (!paddr)
		return -ENOMEM;
	offset = gcip_mem_pool_offset(&mcu->remap_data_pool, paddr);
	mem->size = size;
	mem->phys_addr = paddr;
	mem->virt_addr = offset + (mcu->fw.image_buf.virt_addr + GXP_IREMAP_DATA_OFFSET);
	mem->dma_addr = offset + (mcu->fw.image_buf.dma_addr + GXP_IREMAP_DATA_OFFSET);
	return 0;
}

void gxp_mcu_mem_free_data(struct gxp_mcu *mcu, struct gcip_memory *mem)
{
	gcip_mem_pool_free(&mcu->remap_data_pool, mem->phys_addr, mem->size);
	mem->size = 0;
	mem->phys_addr = 0;
	mem->virt_addr = NULL;
	mem->dma_addr = 0;
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
	/*
	 * We should call IIF init after UCI is initialized since fence_unblocked operator
	 * (getting initialized in iif init) will try to send commands to the UCI mailbox.
	 */
	ret = gxp_iif_init(mcu);
	if (ret)
		goto err_kci_exit;
	return 0;

err_kci_exit:
	gxp_kci_exit(&mcu->kci);
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
	gxp_iif_release(mcu->giif);
	gxp_kci_exit(&mcu->kci);
	gxp_uci_exit(&mcu->uci);
	gxp_mcu_telemetry_exit(mcu);
	gxp_free_shared_buffer(mcu);
	gxp_mcu_mem_pools_exit(mcu);
	gxp_mcu_firmware_exit(&mcu->fw);
}


void gxp_mcu_reset_mailbox(struct gxp_mcu *mcu)
{
	gxp_uci_reinit(&mcu->uci);
	gxp_kci_reinit(&mcu->kci);
	gxp_iif_reinit(mcu->giif);
}

void gxp_mcu_set_boot_mode(struct gxp_mcu_firmware *mcu_fw, enum gxp_mcu_boot_mode mode)
{
	writel(mode, GXP_MCU_BOOT_MODE_OFFSET + mcu_fw->image_buf.virt_addr);
}

void gxp_mcu_set_debug_dump_config(struct gxp_mcu_firmware *mcu_fw, struct gcip_memory *mem)
{
	struct gxp_mcu_dump_config *dump_config =
		GXP_MCU_DUMP_CONFIG_OFFSET + mcu_fw->image_buf.virt_addr;
	dump_config->dump_dev_addr = mem->dma_addr;
	dump_config->dump_size = mem->size;
}
