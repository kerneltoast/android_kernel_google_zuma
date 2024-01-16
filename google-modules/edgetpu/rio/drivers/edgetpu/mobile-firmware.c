// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU firmware management for mobile chipsets.
 *
 * Copyright (C) 2021-2022 Google LLC
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/gsa/gsa_tpu.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include <gcip/gcip-alloc-helper.h>
#include <gcip/gcip-image-config.h>

#include "edgetpu.h"
#include "edgetpu-config.h"
#include "edgetpu-firmware.h"
#include "edgetpu-firmware-util.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mmu.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-soc.h"
#include "edgetpu-telemetry.h"
#include "mobile-firmware.h"

static int image_config_map(void *data, dma_addr_t daddr, phys_addr_t paddr, size_t size,
			    unsigned int flags)
{
	struct edgetpu_dev *etdev = data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_fw_ctx *fw_ctx;
	int ret;

	if (flags & GCIP_IMAGE_CONFIG_FLAGS_SECURE)
		return edgetpu_mmu_add_translation(etdev, daddr, paddr, size,
						   IOMMU_READ | IOMMU_WRITE, EDGETPU_CONTEXT_KCI);

	fw_ctx = kzalloc(sizeof(*fw_ctx), GFP_KERNEL);
	if (!fw_ctx)
		return -ENOMEM;

	fw_ctx->daddr = daddr;
	fw_ctx->size = size;
	fw_ctx->sgt = gcip_alloc_noncontiguous(etdev->dev, size, GFP_KERNEL);
	if (!fw_ctx->sgt) {
		kfree(fw_ctx);
		return -ENOMEM;
	}

	ret = edgetpu_mmu_map_iova_sgt(etdev, daddr, fw_ctx->sgt, DMA_BIDIRECTIONAL, 0,
				       EDGETPU_CONTEXT_KCI);
	if (ret) {
		gcip_free_noncontiguous(fw_ctx->sgt);
		kfree(fw_ctx);
		return ret;
	}

	mutex_lock(&etmdev->fw_ctx_list_lock);
	list_add_tail(&fw_ctx->list, &etmdev->fw_ctx_list);
	mutex_unlock(&etmdev->fw_ctx_list_lock);

	return 0;
}

static void image_config_unmap(void *data, dma_addr_t daddr, size_t size, unsigned int flags)
{
	struct edgetpu_dev *etdev = data;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mobile_fw_ctx *fw_ctx = NULL, *cur;

	if (flags & GCIP_IMAGE_CONFIG_FLAGS_SECURE) {
		edgetpu_mmu_remove_translation(etdev, daddr, size, EDGETPU_CONTEXT_KCI);
		return;
	}

	mutex_lock(&etmdev->fw_ctx_list_lock);
	list_for_each_entry(cur, &etmdev->fw_ctx_list, list) {
		if (cur->daddr == daddr && cur->size == size) {
			fw_ctx = cur;
			list_del(&cur->list);
			break;
		}
	}
	mutex_unlock(&etmdev->fw_ctx_list_lock);

	if (fw_ctx) {
		edgetpu_mmu_unmap_iova_sgt(etdev, daddr, fw_ctx->sgt, DMA_BIDIRECTIONAL,
					   EDGETPU_CONTEXT_KCI);
		gcip_free_noncontiguous(fw_ctx->sgt);
		kfree(fw_ctx);
	} else {
		etdev_warn(etdev, "Firmware context region SG table not found.");
	}
}

static int mobile_firmware_after_create(struct edgetpu_firmware *et_fw)
{
	struct gcip_image_config_parser *data;
	struct edgetpu_dev *etdev = et_fw->etdev;
	int ret;
	static const struct gcip_image_config_ops image_config_parser_ops = {
		.map = image_config_map,
		.unmap = image_config_unmap,
	};

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = gcip_image_config_parser_init(data, &image_config_parser_ops,
					    get_dev_for_logging(etdev), etdev);
	if (ret) {
		etdev_err(etdev, "Image config parser init failed: %d", ret);
		kfree(data);
		return ret;
	}

	edgetpu_firmware_set_data(et_fw, data);
	return 0;
}

static void mobile_firmware_before_destroy(struct edgetpu_firmware *et_fw)
{
	struct gcip_image_config_parser *cfg_parser = edgetpu_firmware_get_data(et_fw);

	gcip_image_config_clear(cfg_parser);
	edgetpu_firmware_set_data(et_fw, NULL);
	kfree(cfg_parser);
}

static int mobile_firmware_alloc_buffer(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_buffer *fw_buf)
{
	struct edgetpu_dev *etdev = et_fw->etdev;

	/* Allocate extra space the image header */
	size_t buffer_size = EDGETPU_MAX_FW_LIMIT + MOBILE_FW_HEADER_SIZE;

	fw_buf->vaddr = vmalloc(buffer_size);
	if (!fw_buf->vaddr) {
		etdev_err(etdev, "%s: failed to allocate buffer (%zu bytes)\n",
			  __func__, buffer_size);
		return -ENOMEM;
	}
	fw_buf->dma_addr = 0;
	fw_buf->alloc_size = buffer_size;
	fw_buf->used_size_align = 16;
	return 0;
}

static void mobile_firmware_free_buffer(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_buffer *fw_buf)
{
	vfree(fw_buf->vaddr);
	fw_buf->vaddr = NULL;
	fw_buf->dma_addr = 0;
	fw_buf->alloc_size = 0;
	fw_buf->used_size_align = 0;
}

static struct gcip_image_config *mobile_firmware_get_image_config(struct edgetpu_dev *etdev)
{
	struct gcip_image_config_parser *cfg_parser = edgetpu_firmware_get_data(etdev->firmware);

	return &cfg_parser->last_config;
}

static int mobile_firmware_gsa_authenticate(struct edgetpu_mobile_platform_dev *etmdev,
					    struct edgetpu_firmware_buffer *fw_buf,
					    void *image_vaddr)
{
	struct edgetpu_dev *etdev = &etmdev->edgetpu_dev;
	void *header_vaddr;
	dma_addr_t header_dma_addr;
	int tpu_state;
	int ret = 0;

	tpu_state = gsa_send_tpu_cmd(etmdev->gsa_dev, GSA_TPU_GET_STATE);

	if (tpu_state < GSA_TPU_STATE_INACTIVE) {
		etdev_err(etdev, "GSA failed to retrieve current status: %d\n", tpu_state);
		return tpu_state;
	}

	etdev_dbg(etdev, "GSA Reports TPU state: %d\n", tpu_state);

	if (tpu_state > GSA_TPU_STATE_INACTIVE) {
		ret = gsa_unload_tpu_fw_image(etmdev->gsa_dev);
		if (ret) {
			etdev_warn(etdev, "GSA release failed: %d\n", ret);
			return -EIO;
		}
	}

	/* Copy the firmware image to the target location, skipping the header */
	memcpy(image_vaddr, fw_buf->vaddr + MOBILE_FW_HEADER_SIZE,
	       fw_buf->used_size - MOBILE_FW_HEADER_SIZE);

	/* Allocate coherent memory for the image header */
	header_vaddr = dma_alloc_coherent(etmdev->gsa_dev,
					  MOBILE_FW_HEADER_SIZE,
					  &header_dma_addr, GFP_KERNEL);
	if (!header_vaddr) {
		etdev_err(etdev,
			  "Failed to allocate coherent memory for header\n");
		return -ENOMEM;
	}

	memcpy(header_vaddr, fw_buf->vaddr, MOBILE_FW_HEADER_SIZE);
	etdev_dbg(etdev, "Requesting GSA image load. meta = %llX payload = %llX", header_dma_addr,
		  (u64)etmdev->fw_region_paddr);

	ret = gsa_load_tpu_fw_image(etmdev->gsa_dev, header_dma_addr,
				    etmdev->fw_region_paddr);
	if (ret)
		etdev_err(etdev, "GSA authentication failed: %d\n", ret);

	dma_free_coherent(etmdev->gsa_dev, MOBILE_FW_HEADER_SIZE, header_vaddr, header_dma_addr);

	return ret;
}

static int mobile_firmware_update_remapped_data_region(struct edgetpu_dev *etdev)
{
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct gcip_image_config *config = mobile_firmware_get_image_config(etdev);
	tpu_addr_t remapped_data_addr = EDGETPU_INSTRUCTION_REMAP_BASE + etmdev->fw_region_size;
	size_t remapped_data_size = config->remapped_data_size ? config->remapped_data_size :
								 EDGETPU_DEFAULT_REMAPPED_DATA_SIZE;
	int ret;

	if (etmdev->remapped_data_addr == remapped_data_addr &&
	    etmdev->remapped_data_size == remapped_data_size)
		return 0;

	if (remapped_data_addr < EDGETPU_INSTRUCTION_REMAP_BASE + config->firmware_size ||
	    config->firmware_base + etmdev->fw_region_size + remapped_data_size >
		    config->remapped_region_start)
		return -EINVAL;

	etdev_dbg(etdev, "Moving remapped data from %#llx to %#llx\n", etmdev->remapped_data_addr,
		  remapped_data_addr);

	if (etmdev->shared_mem_vaddr) {
		/* No need to free the VII queues since allocated groups will block fw loading. */
		edgetpu_kci_release(etdev, etdev->etkci);
		edgetpu_telemetry_exit(etdev);
		edgetpu_iremap_pool_destroy(etdev);
		memunmap(etmdev->shared_mem_vaddr);
	}

	etmdev->remapped_data_addr = remapped_data_addr;
	etmdev->remapped_data_size = remapped_data_size;

	etmdev->shared_mem_paddr = etmdev->fw_region_paddr + etmdev->fw_region_size;
	etmdev->shared_mem_vaddr =
		memremap(etmdev->shared_mem_paddr, etmdev->remapped_data_size, MEMREMAP_WC);
	if (!etmdev->shared_mem_vaddr) {
		etdev_err(etdev, "Shared memory remap failed\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = edgetpu_iremap_pool_create(etdev,
					 /* Base virtual address (kernel address space) */
					 etmdev->shared_mem_vaddr + EDGETPU_POOL_MEM_OFFSET,
					 /* Base DMA address */
					 etmdev->remapped_data_addr + EDGETPU_POOL_MEM_OFFSET,
					 /* Base TPU address */
					 etmdev->remapped_data_addr + EDGETPU_POOL_MEM_OFFSET,
					 /* Base physical address */
					 etmdev->shared_mem_paddr + EDGETPU_POOL_MEM_OFFSET,
					 /* Size */
					 etmdev->remapped_data_size - EDGETPU_POOL_MEM_OFFSET,
					 /* Granularity */
					 PAGE_SIZE);
	if (ret) {
		etdev_err(etdev, "failed to initialize remapped memory pool: %d", ret);
		goto out_memunmap;
	}

#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
	edgetpu_mobile_set_telemetry_mem(etmdev);
	ret = edgetpu_telemetry_init(etdev, etmdev->log_mem, etmdev->trace_mem);
	if (ret)
		goto out_iremap_pool_destroy;
#endif

	ret = edgetpu_kci_init(etdev->mailbox_manager, etdev->etkci);
	if (ret)
		goto out_telemetry_exit;

	return 0;

out_telemetry_exit:
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
	edgetpu_telemetry_exit(etdev);
out_iremap_pool_destroy:
#endif
	edgetpu_iremap_pool_destroy(etdev);
out_memunmap:
	memunmap(etmdev->shared_mem_vaddr);

out:
	etmdev->remapped_data_addr = 0;
	etmdev->remapped_data_size = 0;
	etmdev->shared_mem_paddr = 0;
	etmdev->shared_mem_vaddr = NULL;

	return ret;
}

static int mobile_firmware_prepare_run(struct edgetpu_firmware *et_fw,
				       struct edgetpu_firmware_buffer *fw_buf)
{
	struct edgetpu_dev *etdev = et_fw->etdev;
	int ret;

	/* Reset KCI mailbox before starting f/w, don't process anything old.*/
	edgetpu_mailbox_reset(etdev->etkci->mailbox);

	ret = mobile_firmware_update_remapped_data_region(etdev);
	if (ret)
		return ret;

	edgetpu_soc_prepare_firmware(etdev);

	return edgetpu_mobile_firmware_reset_cpu(etdev, false);
}

static int mobile_firmware_restart(struct edgetpu_firmware *et_fw, bool force_reset)
{
	struct edgetpu_dev *etdev = et_fw->etdev;

	/*
	 * We are in a bad state, reset the CPU and hope the device recovers.
	 * Ignore failures in the reset assert request and proceed to reset release.
	 */
	if (force_reset)
		edgetpu_mobile_firmware_reset_cpu(etdev, true);

	edgetpu_soc_prepare_firmware(etdev);

	return edgetpu_mobile_firmware_reset_cpu(etdev, false);
}

static int mobile_firmware_setup_buffer(struct edgetpu_firmware *et_fw,
					struct edgetpu_firmware_buffer *fw_buf)
{
	int ret = 0;
	void *image_vaddr;
	struct edgetpu_dev *etdev = et_fw->etdev;
	struct gcip_image_config *image_config;
	struct gcip_image_config_parser *cfg_parser = edgetpu_firmware_get_data(et_fw);
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	phys_addr_t image_start, image_end, carveout_start, carveout_end;
	struct mobile_image_header *hdr;

	if (fw_buf->used_size < MOBILE_FW_HEADER_SIZE) {
		etdev_err(etdev, "Invalid buffer size: %zu < %d\n",
			  fw_buf->used_size, MOBILE_FW_HEADER_SIZE);
		return -EINVAL;
	}

	hdr = (struct mobile_image_header *)fw_buf->vaddr;
	if (hdr->common.Magic != EDGETPU_MOBILE_FW_MAGIC) {
		etdev_err(etdev, "Invalid firmware header magic value %#08x\n", hdr->common.Magic);
		return -EINVAL;
	}

	switch (hdr->common.Generation) {
	case 1:
		image_config = &hdr->gen1.ImageConfig;
		break;
	case 2:
		image_config = &hdr->gen2.ImageConfig;
		break;
	default:
		etdev_err(etdev, "Invalid header generation identifier (%d)\n",
			  hdr->common.Generation);
		return -EINVAL;
	}

	etmdev->fw_region_paddr = image_config->firmware_base;
	etmdev->fw_region_size =
		image_config->remapped_data_start ?
			image_config->remapped_data_start - image_config->firmware_base :
			EDGETPU_DEFAULT_FW_LIMIT;

	image_vaddr = memremap(etmdev->fw_region_paddr, etmdev->fw_region_size, MEMREMAP_WC);
	if (!image_vaddr) {
		etdev_err(etdev, "FW region memremap failed\n");
		return -ENOMEM;
	}

	memcpy(&etdev->fw_version, &image_config->firmware_versions, sizeof(etdev->fw_version));

	/*
	 * TODO(b/244103549) re-enable authentication of non-secure images when
	 * GSA and TPU updates for Zuma land
	 */
	if (etmdev->gsa_dev && !gcip_image_config_is_ns(image_config)) {
		ret = mobile_firmware_gsa_authenticate(etmdev, fw_buf, image_vaddr);
	} else if (gcip_image_config_is_ns(image_config)) {
		etdev_dbg(etdev, "Loading unauthenticated non-secure firmware\n");
		/* Copy the firmware image to the target location, skipping the header */
		memcpy(image_vaddr, fw_buf->vaddr + MOBILE_FW_HEADER_SIZE,
		       fw_buf->used_size - MOBILE_FW_HEADER_SIZE);
	} else {
		etdev_err(etdev,
			  "Cannot load firmware at privilege level %d with no authentication\n",
			  image_config->privilege_level);
		ret = -EINVAL;
	}

	if (ret)
		goto out;

	image_start = (phys_addr_t)image_config->carveout_base;
	image_end = (phys_addr_t)(image_config->firmware_base + image_config->firmware_size - 1);
	carveout_start = etmdev->fw_region_paddr;
	carveout_end = carveout_start + etmdev->fw_region_size - 1;

	/* Image must fit within the carveout */
	if (image_start < carveout_start || image_end > carveout_end) {
		etdev_err(etdev, "Firmware image doesn't fit in carveout\n");
		etdev_err(etdev, "Image config: %pap - %pap\n", &image_start, &image_end);
		etdev_err(etdev, "Carveout: %pap - %pap\n", &carveout_start, &carveout_end);
		ret = -ERANGE;
		goto out;
	}

	ret = gcip_image_config_parse(cfg_parser, image_config);
out:
	memunmap(image_vaddr);
	return ret;
}

/* TODO: Get the SIDs and CSRs form chip config. */
static void program_iremap_csr(struct edgetpu_dev *etdev)
{
	const int ctx_id = 0, sid0 = 0x30, sid1 = 0x34;

	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_SECURITY, (ctx_id << 16) | sid0);
	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_SECURITY + 8,
			     (ctx_id << 16) | sid1);
	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_NEW_BASE,
			     EDGETPU_INSTRUCTION_REMAP_BASE);
	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_NEW_BASE + 8,
			     EDGETPU_INSTRUCTION_REMAP_BASE);

	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_LIMIT,
			     EDGETPU_INSTRUCTION_REMAP_BASE + SZ_32M);
	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_LIMIT + 8,
			     EDGETPU_INSTRUCTION_REMAP_BASE + SZ_32M);

	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_CONTROL, 1);
	edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_CONTROL + 8, 1);
}

int edgetpu_mobile_firmware_reset_cpu(struct edgetpu_dev *etdev, bool assert_reset)
{
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct gcip_image_config *image_config = mobile_firmware_get_image_config(etdev);
	int ret = 0;

	if (gcip_image_config_is_ns(image_config)) {
		int i;

		if (!assert_reset)
			program_iremap_csr(etdev);
		for (i = 0; i < EDGETPU_NUM_CORES; i++)
			edgetpu_dev_write_32_sync(etdev, EDGETPU_REG_RESET_CONTROL + i * 8,
						  assert_reset ? 1 : 0);
	}
	else if (etmdev->gsa_dev)
		ret = gsa_send_tpu_cmd(etmdev->gsa_dev,
				       assert_reset ? GSA_TPU_SHUTDOWN : GSA_TPU_START);
	else
		ret = -ENODEV;

	etdev_dbg(etdev, "%s CPU reset result = %d", assert_reset ? "assert" : "release", ret);

	if (ret < 0)
		return ret;

	return 0;
}

static const struct edgetpu_firmware_chip_data mobile_firmware_chip_data = {
	.default_firmware_name = EDGETPU_DEFAULT_FIRMWARE_NAME,
	.after_create = mobile_firmware_after_create,
	.before_destroy = mobile_firmware_before_destroy,
	.alloc_buffer = mobile_firmware_alloc_buffer,
	.free_buffer = mobile_firmware_free_buffer,
	.setup_buffer = mobile_firmware_setup_buffer,
	.prepare_run = mobile_firmware_prepare_run,
	.restart = mobile_firmware_restart,
};

int edgetpu_mobile_firmware_create(struct edgetpu_dev *etdev)
{
	return edgetpu_firmware_create(etdev, &mobile_firmware_chip_data);
}

void edgetpu_mobile_firmware_destroy(struct edgetpu_dev *etdev)
{
	edgetpu_firmware_destroy(etdev);
}

unsigned long edgetpu_chip_firmware_iova(struct edgetpu_dev *etdev)
{
	/*
	 * On mobile platforms, firmware address translation may happen in 1 or 2 stages:
	 * 1.- Instruction remap registers.
	 * 2.- IOMMU translation (when not running in GSA privilege).
	 * In either case, the address seen by the TPU's CPU core will remain constant, and
	 * equal to the macro below.
	 */
	return EDGETPU_INSTRUCTION_REMAP_BASE;
}
