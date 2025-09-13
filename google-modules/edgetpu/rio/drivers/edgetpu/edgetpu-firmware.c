// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU firmware loader.
 *
 * Copyright (C) 2019-2021,2024 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <gcip/gcip-alloc-helper.h>
#include <gcip/gcip-common-image-header.h>
#include <gcip/gcip-fault-injection.h>
#include <gcip/gcip-image-config.h>
#include <gcip/gcip-iommu.h>
#include <gcip/gcip-thermal.h>

#include "edgetpu.h"
#include "edgetpu-config.h"
#include "edgetpu-debug.h"
#include "edgetpu-device-group.h"
#include "edgetpu-firmware.h"
#include "edgetpu-firmware-util.h"
#include "edgetpu-gsa.h"
#include "edgetpu-iif.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mmu.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-usage-stats.h"

/*
 * Log and trace buffers at the beginning of the remapped region, pool memory afterwards.
 *
 * Note this is default value when the number of cores equals the number of telemetry buffers
 * and may be adjusted at runtime when a firmware that specifies telemetry buffer config is loaded.
 */
#define EDGETPU_POOL_MEM_OFFSET                                                                    \
	((EDGETPU_TELEMETRY_LOG_BUFFER_SIZE + EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE) *               \
	 EDGETPU_NUM_CORES)

static char *firmware_name;
module_param(firmware_name, charp, 0660);

/*
 * Driver-managed firmware mappings from image config: non-secure memory allocations + IOVA mappings
 * and secure "shared" IOVA mappings to be established in all VII contexts.
 *
 * "Secure" mappings without the SHARED mapping flag don't need any extra driver management and are
 * not stored using these structs.
 */
struct edgetpu_image_config_mapping {
	/* DMA address specified by the firmware image config for this mapping. */
	dma_addr_t daddr;
	/* Physical address specified by the image config for "secure" IOVA map only. */
	phys_addr_t paddr;
	/* SG table for NS mapping memory allocated by our driver (or NULL if IOVA map only). */
	struct sg_table *sgt;
	/* Size of the mapping (and allocation if sgt != NULL) specified by image config. */
	size_t size;
	/* GCIP_IMAGE_CONFIG_MAP_* flags  specified by the image config for this mapping. */
	unsigned int cfg_map_flags;
	/* List of all driver-managed firmware image config mappings for the device. */
	struct list_head list;
};

struct edgetpu_firmware {
	struct edgetpu_dev *etdev;
	/* Physical address of the firmware image */
	phys_addr_t fw_region_paddr;
	/* Size of the firmware region */
	size_t fw_region_size;
	/* Shared data region in TPU's CPU address space */
	tpu_addr_t shared_data_daddr;
	/* Size of shared data region */
	size_t shared_data_size;
	/* Virtual address of the shared data region */
	void *shared_data_vaddr;
	/* Physical address of the shared data region */
	phys_addr_t shared_data_paddr;

	struct gcip_image_config_parser *img_cfg_parser;
	/* List of firmware image config mappings for the device that need special processing. */
	struct list_head image_config_map_list;
	/* Lock to protect @image_config_map_list. */
	struct mutex image_config_map_list_lock;

	/* Firmware state lock: load/unload disallowed while held. */
	struct mutex fw_state_lock;
	/* Name of the firmware image */
	const char *name;
	enum gcip_fw_status status;
	struct gcip_fw_info fw_info;

	/*
	 * Pointer to GSA device for firmware authentication.
	 * May be NULL if GSA not present (in which case firmware authentication is not supported).
	 */
	struct device *gsa_dev;

	struct gcip_fault_inject *fault_inject;
	int sanitizer_status;
};

/*
 * Add an image_config_map_list entry for a new firmware image config mapping that needs additional
 * processing by the driver beyond the image config parser map/unmap callbacks.
 * Only image config mapping entries that require "shared" mappings across all contexts, and
 * non-secure mappings of driver-allocated memory, are added here; "secure" non-shared mappings do
 * not require any additional driver processing beyond the config parser map/unmap operations.
 */
static int image_config_managed_mapping_add(struct edgetpu_dev *etdev, dma_addr_t daddr,
					    phys_addr_t paddr, size_t size,
					    struct sg_table *sgt, unsigned int cfg_map_flags)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct edgetpu_image_config_mapping *image_config_map;

	image_config_map = kzalloc(sizeof(*image_config_map), GFP_KERNEL);
	if (!image_config_map)
		return -ENOMEM;

	image_config_map->daddr = daddr;
	image_config_map->paddr = paddr;
	image_config_map->size = size;
	image_config_map->sgt = sgt;
	image_config_map->cfg_map_flags = cfg_map_flags;
	mutex_lock(&et_fw->image_config_map_list_lock);
	list_add_tail(&image_config_map->list, &et_fw->image_config_map_list);
	mutex_unlock(&et_fw->image_config_map_list_lock);
	return 0;
}

/*
 * Find the image_config_map_list entry for the provided dma addr + size and remove from the list.
 * Return a pointer to the entry if found, or NULL if not found.  If non-NULL, the caller is
 * expected to free the entry after processing is complete.
 */
static struct edgetpu_image_config_mapping *image_config_managed_mapping_del(
	struct edgetpu_dev *etdev, dma_addr_t daddr, size_t size)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct edgetpu_image_config_mapping *image_config_map = NULL, *cur;

	mutex_lock(&et_fw->image_config_map_list_lock);
	list_for_each_entry(cur, &et_fw->image_config_map_list, list) {
		if (cur->daddr == daddr && cur->size == size) {
			image_config_map = cur;
			list_del(&cur->list);
			break;
		}
	}
	mutex_unlock(&et_fw->image_config_map_list_lock);
	return image_config_map;
}

static int add_image_config_iova_translate(struct edgetpu_dev *etdev, dma_addr_t daddr,
					   phys_addr_t paddr, size_t size,
					   unsigned int cfg_map_flags,
					   struct edgetpu_iommu_domain *etdomain)
{
	u64 gcip_map_flags = GCIP_MAP_FLAGS_DMA_RW;

	if (GCIP_IMAGE_CONFIG_MAP_MMIO(cfg_map_flags))
		gcip_map_flags |= GCIP_MAP_FLAGS_MMIO_TO_FLAGS(1);

	return edgetpu_mmu_add_translation(etdev, daddr, paddr, size, gcip_map_flags, etdomain);
}

void edgetpu_firmware_shared_mappings_context_map(struct edgetpu_dev *etdev,
						  struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct edgetpu_image_config_mapping *image_config_map;
	int ret;

	mutex_lock(&et_fw->image_config_map_list_lock);
	list_for_each_entry(image_config_map, &et_fw->image_config_map_list, list) {
		if (!image_config_map->sgt &&
		    GCIP_IMAGE_CONFIG_MAP_SHARED(image_config_map->cfg_map_flags)) {
			ret = add_image_config_iova_translate(
					etdev, image_config_map->daddr, image_config_map->paddr,
					image_config_map->size, image_config_map->cfg_map_flags,
					etdomain);
			if (ret)
				etdev_warn(etdev, "img cfg map %pad -> %pap pasid=%u error=%d\n",
					   &image_config_map->daddr, &image_config_map->paddr,
					   etdomain->pasid, ret);
		}
	}
	mutex_unlock(&et_fw->image_config_map_list_lock);
}

void edgetpu_firmware_shared_mappings_context_unmap(struct edgetpu_dev *etdev,
						    struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct edgetpu_image_config_mapping *image_config_map;

	mutex_lock(&et_fw->image_config_map_list_lock);
	list_for_each_entry(image_config_map, &et_fw->image_config_map_list, list) {
		if (!image_config_map->sgt &&
		    GCIP_IMAGE_CONFIG_MAP_SHARED(image_config_map->cfg_map_flags)) {
			edgetpu_mmu_remove_translation(etdev, image_config_map->daddr,
						       image_config_map->size, etdomain);
		}
	}
	mutex_unlock(&et_fw->image_config_map_list_lock);
}

/*
 * All parameters are from the image_config_map callback except @sgt from the local caller.
 * @etdev: the device for which an image config map callback is being processed
 * @daddr: the DMA address specified by the image config virtual_address, with map flags removed
 * @paddr: the physical address of a "secure" IOVA map, or zero for "non-secure" memory alloc map
 * @size: the size of the mapping specified by the image config
 * @sgt: the scatter-gather table for a non-secure memory allocation map
 * @cfg_map_flags: map flags from the image config masked off from the virtual_address field
 *
 * Returns zero for success, or negative errno on error, in which case no list entry nor mapping
 * will have been created.
 */
static int image_config_do_map(struct edgetpu_dev *etdev, dma_addr_t daddr, phys_addr_t paddr,
			       size_t size, struct sg_table *sgt, unsigned int cfg_map_flags)
{
	bool managed = sgt || GCIP_IMAGE_CONFIG_MAP_SHARED(cfg_map_flags);
	int ret;

	/*
	 * Image config mappings that need additional processing by the driver beyond the image
	 * config parser map/unmap callbacks are kept in the image_config_map_list.
	 *
	 * Only image config mapping entries that require "shared" mappings across all contexts, and
	 * non-secure mappings of driver-allocated memory, are added to the list here.
	 * "Secure" non-shared mappings do not require any additional driver processing beyond the
	 * config parser map/unmap operations and are not added to the list.
	 */
	if (managed) {
		/* Add this mapping to the driver-managed list. */
		ret = image_config_managed_mapping_add(etdev, daddr, paddr, size, sgt,
						       cfg_map_flags);
		if (ret)
			return ret;
	}

	/*
	 * There are no VII contexts at firmware load time; any "shared"
	 * mappings will be established in a VII context at IOMMU domain attach time.
	 */

	if (GCIP_IMAGE_CONFIG_MAP_SHARED(cfg_map_flags)) {
		etdev_dbg(etdev, "Shared mapping @%pad skipped for default domain", &daddr);
		return ret;
	}

	/*
	 * Add KCI context mapping.
	 */
	if (!sgt)
		ret = add_image_config_iova_translate(etdev, daddr, paddr, size, cfg_map_flags,
						      edgetpu_mmu_default_domain(etdev));
	else
		ret = edgetpu_mmu_map_iova_sgt(etdev, daddr, sgt, DMA_BIDIRECTIONAL, 0,
					       edgetpu_mmu_default_domain(etdev));
	if (ret) {
		if (managed) {
			struct edgetpu_image_config_mapping *image_config_map =
				image_config_managed_mapping_del(etdev, daddr, size);

			kfree(image_config_map);
		}
	}

	return ret;
}

static int image_config_map(void *data, dma_addr_t daddr, phys_addr_t paddr, size_t size,
			    unsigned int cfg_map_flags, unsigned int cfg_op_flags)
{
	struct edgetpu_dev *etdev = data;
	struct sg_table *sgt = NULL;
	int ret;

	if (!(cfg_op_flags & GCIP_IMAGE_CONFIG_FLAGS_SECURE)) {
		sgt = gcip_alloc_noncontiguous(etdev->dev, size, GFP_KERNEL);
		if (!sgt)
			return -ENOMEM;
	}

	ret = image_config_do_map(etdev, daddr, paddr, size, sgt, cfg_map_flags);
	if (ret && sgt)
		gcip_free_noncontiguous(sgt);

	return ret;
}

static void image_config_unmap(void *data, dma_addr_t daddr, size_t size,
			       unsigned int cfg_map_flags, unsigned int cfg_op_flags)
{
	struct edgetpu_dev *etdev = data;
	struct edgetpu_image_config_mapping *image_config_map;

	/*
	 * If there is no "managed firmware image config mapping list" entry for this type of
	 * mapping (non-shared secure) then remove the mapping from KCI context and done.
	 */
	if (cfg_op_flags & GCIP_IMAGE_CONFIG_FLAGS_SECURE &&
	    !GCIP_IMAGE_CONFIG_MAP_SHARED(cfg_map_flags)) {
		edgetpu_mmu_remove_translation(etdev, daddr, size,
					       edgetpu_mmu_default_domain(etdev));
		return;
	}

	/* Lookup the "managed mapping" entry, remove from list, and free/unmap. */
	image_config_map = image_config_managed_mapping_del(etdev, daddr, size);
	if (!image_config_map) {
		etdev_warn(etdev, "Firmware image config mapping for %pad not found at unmap time.",
			   &daddr);
		return;
	}

	/* Unmap from KCI context. No VII contexts exist at firmware unload time. */
	if (image_config_map->sgt) {
		edgetpu_mmu_unmap_iova_sgt(etdev, daddr, image_config_map->sgt, DMA_BIDIRECTIONAL,
					   edgetpu_mmu_default_domain(etdev));
		gcip_free_noncontiguous(image_config_map->sgt);
	} else {
		/* Shared (all VII contexts) mappings are not mapped in KCI context, skip. */
		if (!GCIP_IMAGE_CONFIG_MAP_SHARED(cfg_map_flags))
			edgetpu_mmu_remove_translation(etdev, daddr, size,
						       edgetpu_mmu_default_domain(etdev));
	}

	kfree(image_config_map);
}

static int edgetpu_firmware_init_image_config(struct edgetpu_firmware *et_fw)
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

	edgetpu_firmware_set_img_cfg_parser(et_fw, data);
	return 0;
}

static void edgetpu_firmware_deinit_image_config(struct edgetpu_firmware *et_fw)
{
	struct gcip_image_config_parser *cfg_parser = edgetpu_firmware_get_img_cfg_parser(et_fw);

	if (cfg_parser)
		gcip_image_config_clear(cfg_parser);
	edgetpu_firmware_set_img_cfg_parser(et_fw, NULL);
	kfree(cfg_parser);
}

static struct gcip_image_config *edgetpu_firmware_get_image_config(struct edgetpu_dev *etdev)
{
	struct gcip_image_config_parser *cfg_parser =
		edgetpu_firmware_get_img_cfg_parser(etdev->firmware);

	return (cfg_parser && cfg_parser->last_config_valid) ? &cfg_parser->last_config : NULL;
}

static int edgetpu_firmware_gsa_authenticate(struct edgetpu_dev *etdev, const struct firmware *fw,
					     void *image_vaddr)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	void *header_vaddr;
	dma_addr_t header_dma_addr;
	int tpu_state;
	int ret = 0;

	tpu_state = gsa_send_tpu_cmd(et_fw->gsa_dev, GSA_TPU_GET_STATE);

	if (tpu_state < GSA_TPU_STATE_INACTIVE) {
		etdev_err(etdev, "GSA failed to retrieve current status: %d\n", tpu_state);
		return tpu_state;
	}

	etdev_dbg(etdev, "GSA Reports TPU state: %d\n", tpu_state);

	if (tpu_state > GSA_TPU_STATE_INACTIVE) {
		ret = gsa_unload_tpu_fw_image(et_fw->gsa_dev);
		if (ret) {
			etdev_warn(etdev, "GSA release failed: %d\n", ret);
			return -EIO;
		}
	}

	/* Copy the firmware image to the carveout, skipping the header */
	memcpy(image_vaddr, fw->data + EDGETPU_FW_HEADER_SIZE, fw->size - EDGETPU_FW_HEADER_SIZE);

	/* Allocate coherent memory for the image header */
	header_vaddr = dma_alloc_coherent(et_fw->gsa_dev, EDGETPU_FW_HEADER_SIZE, &header_dma_addr,
					  GFP_KERNEL);
	if (!header_vaddr) {
		etdev_err(etdev, "Failed to allocate coherent memory for header\n");
		return -ENOMEM;
	}

	memcpy(header_vaddr, fw->data, EDGETPU_FW_HEADER_SIZE);
	etdev_dbg(etdev, "Requesting GSA image load. meta = %pad payload = %pap", &header_dma_addr,
		  &et_fw->fw_region_paddr);
	ret = gsa_load_tpu_fw_image(et_fw->gsa_dev, header_dma_addr, et_fw->fw_region_paddr);
	if (ret)
		etdev_err(etdev, "GSA authentication failed: %d\n", ret);

	dma_free_coherent(et_fw->gsa_dev, EDGETPU_FW_HEADER_SIZE, header_vaddr, header_dma_addr);
	return ret;
}

static int edgetpu_firmware_update_remapped_data_region(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct gcip_image_config *config = edgetpu_firmware_get_image_config(etdev);
	bool i_has_shared_data_addr = config && config->shared_data_start &&
				      config->shared_data_size;
	/*
	 * If the image config provides a shared data physical address, calculate the DMA address
	 * by adding the offset from the carveout physical address to the instruction remap base.
	 * Otherwise, assume the shared data region starts right after firmware code.
	 */
	tpu_addr_t shared_data_daddr =
		config->shared_data_iova ? config->shared_data_iova :
					   (EDGETPU_INSTRUCTION_REMAP_BASE + et_fw->fw_region_size);
	phys_addr_t shared_data_paddr = i_has_shared_data_addr ?
						config->shared_data_start :
						et_fw->fw_region_paddr + et_fw->fw_region_size;
	size_t shared_data_size = i_has_shared_data_addr ? config->shared_data_size :
							   EDGETPU_DEFAULT_REMAPPED_DATA_SIZE;
	size_t firmware_size = (config) ? config->firmware_size : 0;
	u32 firmware_base = (config) ? config->firmware_base : 0;
	u32 secure_data_start = (config) ? config->secure_data_start : 0;
	struct gcip_telemetry_buffer_config telemetry_config;
	bool i_has_telemetry_config =
		config && gcip_image_config_get_telemetry_buffer_config(config, &telemetry_config);
	int ret;
	size_t iremap_pool_mem_offset = EDGETPU_POOL_MEM_OFFSET;

	if (et_fw->shared_data_daddr == shared_data_daddr &&
	    et_fw->shared_data_size == shared_data_size &&
	    et_fw->shared_data_paddr == shared_data_paddr)
		return 0;

	/* Allow shared data region to be placed after secure data if an address was provided */
	if (shared_data_daddr <
		    EDGETPU_INSTRUCTION_REMAP_BASE + firmware_size ||
	    (!i_has_shared_data_addr &&
	     (firmware_base + et_fw->fw_region_size + shared_data_size >
	      secure_data_start))) {
		etdev_err(etdev, "Firmware shared data address invalid");
		etdev_err(etdev, "Shared data @ %08llX (%zu bytes)", shared_data_daddr,
			  shared_data_size);
		etdev_err(etdev, "Firmware base @ %08X", firmware_base);
		etdev_err(etdev, "Firmware %s a shared data address",
			  i_has_shared_data_addr ? "provided" : "did not provide");
		return -EINVAL;
	}

	etdev_dbg(etdev, "Moving remapped data from %pad to %pad\n",
		  &et_fw->shared_data_daddr, &shared_data_daddr);

	if (et_fw->shared_data_vaddr) {
		/*
		 * No need to free user-space VII queues, since allocated groups will block fw from
		 * loading.
		 */
		edgetpu_kci_release(etdev, etdev->etkci);
		edgetpu_ikv_release(etdev, etdev->etikv);
		edgetpu_iif_release_mailbox(etdev->etiif);
		edgetpu_telemetry_exit(etdev);
		edgetpu_iremap_pool_destroy(etdev);
		memunmap(et_fw->shared_data_vaddr);
	}

	if (i_has_telemetry_config) {
		etdev_dbg(
			etdev,
			"Updating telemetry buffer config. Count = %zu log size = %zu trace size = %zu",
			telemetry_config.count, telemetry_config.log_buffer_size,
			telemetry_config.trace_buffer_size);
		etdev->num_telemetry_buffers = telemetry_config.count;
		etdev->log_buffer_size = telemetry_config.log_buffer_size;
		etdev->trace_buffer_size = telemetry_config.trace_buffer_size;
		iremap_pool_mem_offset = (etdev->log_buffer_size + etdev->trace_buffer_size) *
					 etdev->num_telemetry_buffers;
	} else {
		etdev->num_telemetry_buffers = EDGETPU_NUM_CORES;
		etdev->log_buffer_size = EDGETPU_TELEMETRY_LOG_BUFFER_SIZE;
		etdev->trace_buffer_size = EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE;
	}

	et_fw->shared_data_daddr = shared_data_daddr;
	et_fw->shared_data_size = shared_data_size;
	et_fw->shared_data_paddr = shared_data_paddr;
	et_fw->shared_data_vaddr =
		memremap(et_fw->shared_data_paddr, et_fw->shared_data_size, MEMREMAP_WC);
	if (!et_fw->shared_data_vaddr) {
		etdev_err(etdev, "Shared fw memory remap failed\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = edgetpu_iremap_pool_create(etdev,
					 /* Base virtual address (kernel address space) */
					 et_fw->shared_data_vaddr + iremap_pool_mem_offset,
					 /* Base DMA address */
					 et_fw->shared_data_daddr + iremap_pool_mem_offset,
					 /* Base physical address */
					 et_fw->shared_data_paddr + iremap_pool_mem_offset,
					 /* Size */
					 et_fw->shared_data_size - iremap_pool_mem_offset,
					 /* Granularity */
					 PAGE_SIZE);
	if (ret) {
		etdev_err(etdev, "failed to initialize remapped memory pool: %d", ret);
		goto out_memunmap;
	}

	ret = edgetpu_telemetry_init(etdev);
	if (ret)
		goto out_iremap_pool_destroy;

	ret = edgetpu_kci_init(etdev->mailbox_manager, etdev->etkci);
	if (ret)
		goto out_telemetry_exit;

	ret = edgetpu_ikv_init(etdev->mailbox_manager, etdev->etikv);
	if (ret)
		goto out_kci_release;

	ret = edgetpu_iif_init_mailbox(etdev->mailbox_manager, etdev->etiif);
	if (ret)
		goto out_ikv_release;

	return 0;

out_ikv_release:
	edgetpu_ikv_release(etdev, etdev->etikv);
out_kci_release:
	edgetpu_kci_release(etdev, etdev->etkci);
out_telemetry_exit:
	edgetpu_telemetry_exit(etdev);
out_iremap_pool_destroy:
	edgetpu_iremap_pool_destroy(etdev);
out_memunmap:
	memunmap(et_fw->shared_data_vaddr);

out:
	et_fw->shared_data_daddr = 0;
	et_fw->shared_data_size = 0;
	et_fw->shared_data_paddr = 0;
	et_fw->shared_data_vaddr = NULL;

	etdev->num_telemetry_buffers = EDGETPU_NUM_CORES;
	etdev->log_buffer_size = EDGETPU_TELEMETRY_LOG_BUFFER_SIZE;
	etdev->trace_buffer_size = EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE;

	return ret;
}

/* Return KVA of FW shared memory area. */
void *edgetpu_firmware_shared_data_vaddr(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw->shared_data_vaddr;
}

/* Return phys addr of FW shared memory area. */
phys_addr_t edgetpu_firmware_shared_data_paddr(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw->shared_data_paddr;
}

/* Return device DMA addr of FW shared memory area. */
dma_addr_t edgetpu_firmware_shared_data_daddr(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw->shared_data_daddr;
}

/* Return size of FW shared memory area. */
size_t edgetpu_firmware_shared_data_size(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw->shared_data_size;
}

/* Return phys addr of FW remap region start. */
phys_addr_t edgetpu_firmware_fw_region_paddr(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw->fw_region_paddr;
}

/* Return size of entire FW remap region. */
size_t edgetpu_firmware_fw_region_size(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw->fw_region_size;
}

static int edgetpu_firmware_prepare_run(struct edgetpu_firmware *et_fw)
{
	struct edgetpu_dev *etdev = et_fw->etdev;
	int ret;

	/* Reset KCI and IKV mailboxes before starting f/w, don't process anything old.*/
	edgetpu_mailbox_reset(etdev->etkci->mailbox);
	/* Need to check if in-kernel VII was enabled */
	if (etdev->etikv->mbx_hardware)
		edgetpu_mailbox_reset(etdev->etikv->mbx_hardware);

	ret = edgetpu_firmware_update_remapped_data_region(etdev);
	if (ret)
		return ret;

	edgetpu_soc_prepare_firmware(etdev);

	return edgetpu_firmware_reset_cpu(etdev, false);
}

static int edgetpu_firmware_restart(struct edgetpu_firmware *et_fw, bool force_reset)
{
	struct edgetpu_dev *etdev = et_fw->etdev;

	/*
	 * We are in a bad state, reset the CPU and hope the device recovers.
	 * Ignore failures in the reset assert request and proceed to reset release.
	 */
	if (force_reset)
		edgetpu_firmware_reset_cpu(etdev, true);

	edgetpu_soc_prepare_firmware(etdev);

	return edgetpu_firmware_reset_cpu(etdev, false);
}

/*
 * Copy image to carveout.  Process image_config.  Caller must hold edgetpu_firmware_load_lock().
 */
static int edgetpu_firmware_setup_image(struct edgetpu_firmware *et_fw, const struct firmware *fw)
{
	int ret = 0;
	void *image_vaddr;
	struct edgetpu_dev *etdev = et_fw->etdev;
	const struct gcip_image_config *image_config;
	struct gcip_image_config_parser *cfg_parser = edgetpu_firmware_get_img_cfg_parser(et_fw);
	phys_addr_t image_start, image_end, carveout_start, carveout_end;

	if (fw->size < EDGETPU_FW_HEADER_SIZE) {
		etdev_err(etdev, "Invalid firmware image size: %zu < %d\n",
			  fw->size, EDGETPU_FW_HEADER_SIZE);
		return -EINVAL;
	}

	if (!gcip_common_image_check_magic(fw->data, EDGETPU_FW_MAGIC)) {
		etdev_err(etdev, "Invalid firmware header magic value\n");
		return -EINVAL;
	}

	image_config = gcip_common_image_get_config_from_hdr(fw->data, EDGETPU_FW_MAGIC);
	if (!image_config) {
		etdev_err(etdev, "Invalid header generation identifier\n");
		return -EINVAL;
	}

	et_fw->fw_region_paddr = image_config->firmware_base;
	et_fw->fw_region_size =
		image_config->shared_data_start ?
			image_config->shared_data_start - image_config->firmware_base :
			EDGETPU_DEFAULT_FW_LIMIT;

	image_vaddr = memremap(et_fw->fw_region_paddr, et_fw->fw_region_size, MEMREMAP_WC);
	if (!image_vaddr) {
		etdev_err(etdev, "FW region remap failed %#08x %#08x\n",
			  image_config->shared_data_start, image_config->firmware_base);
		return -ENOMEM;
	}

	memcpy(&etdev->fw_version, &image_config->firmware_versions, sizeof(etdev->fw_version));

#if EDGETPU_ALLOW_NONSECURE_FW || IS_ENABLED(CONFIG_EDGETPU_TEST)
	if (gcip_image_config_is_ns(image_config))
		etdev_warn(etdev, "Loading non-secure firmware image\n");
#else
	if (gcip_image_config_is_ns(image_config)) {
		etdev_err(etdev, "Non-secure firmware image not allowed\n");
		ret = -EINVAL;
		goto out;
	}
#endif

	if (et_fw->gsa_dev) {
		/* The following also copies the image to the carveout. */
		ret = edgetpu_firmware_gsa_authenticate(etdev, fw, image_vaddr);
		if (ret && gcip_image_config_is_ns(image_config)) {
			etdev_warn(etdev, "GSA authenticate of non-secure image failed: %d\n", ret);
			ret = 0;
		}
	} else if (gcip_image_config_is_ns(image_config)) {
		etdev_dbg(etdev, "No GSA device available, but firmware is non-secure.");
		etdev_dbg(etdev, "Continuing without authentication.");
		/* Copy the firmware image to the target location, skipping the header. */
		memcpy(image_vaddr, fw->data + EDGETPU_FW_HEADER_SIZE,
		       fw->size - EDGETPU_FW_HEADER_SIZE);
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
	carveout_start = et_fw->fw_region_paddr;
	carveout_end = carveout_start + et_fw->fw_region_size - 1;

	/* Image must fit within the carveout */
	if (image_start < carveout_start || image_end > carveout_end) {
		etdev_err(etdev, "Firmware image doesn't fit in carveout\n");
		etdev_err(etdev, "Image config: %pap - %pap\n", &image_start, &image_end);
		etdev_err(etdev, "Carveout: %pap - %pap\n", &carveout_start, &carveout_end);
		ret = -ERANGE;
		goto out;
	}

	ret = gcip_image_config_parse(cfg_parser, image_config);
	et_fw->sanitizer_status =
		GCIP_IMAGE_CONFIG_SANITIZER_STATUS(image_config->sanitizer_config);
out:
	memunmap(image_vaddr);
	return ret;
}

static void program_iremap_csr(struct edgetpu_dev *etdev)
{
	int i;

	edgetpu_soc_set_tpu_cpu_security(etdev);

	for (i = 0; i < etdev->num_cores; i++) {
		edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_NEW_BASE + 8 * i,
				     EDGETPU_INSTRUCTION_REMAP_BASE);
		edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_LIMIT + 8 * i,
				     EDGETPU_INSTRUCTION_REMAP_BASE + SZ_32M);
		edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_CONTROL + 8 * i, 1);
	}
}

int edgetpu_firmware_reset_cpu(struct edgetpu_dev *etdev, bool assert_reset)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct gcip_image_config *image_config = edgetpu_firmware_get_image_config(etdev);
	int ret = 0;

	if (!image_config)
		return 0;

	if (gcip_image_config_is_ns(image_config)) {
		int i;

		if (!assert_reset)
			program_iremap_csr(etdev);
		for (i = 0; i < EDGETPU_NUM_CORES; i++)
			edgetpu_dev_write_32_sync(etdev, EDGETPU_REG_RESET_CONTROL + i * 8,
						  assert_reset ? 1 : 0);
	} else if (et_fw->gsa_dev) {
		ret = gsa_send_tpu_cmd(et_fw->gsa_dev,
				       assert_reset ? GSA_TPU_SHUTDOWN : GSA_TPU_START);
	} else {
		ret = -ENODEV;
	}

	etdev_dbg(etdev, "%s CPU reset result = %d", assert_reset ? "assert" : "release", ret);

	if (ret < 0) {
		etdev_err(etdev, "GSA CPU reset %s failed: %d\n",
			  assert_reset ? "assert" : "release", ret);
		return ret;
	}

	etdev->firmware_cpu_on = !assert_reset;
	return 0;
}

void edgetpu_firmware_set_img_cfg_parser(struct edgetpu_firmware *et_fw,
					 struct gcip_image_config_parser *parser)
{
	et_fw->img_cfg_parser = parser;
}

struct gcip_image_config_parser *edgetpu_firmware_get_img_cfg_parser(struct edgetpu_firmware *et_fw)
{
	return et_fw->img_cfg_parser;
}

static void edgetpu_firmware_image_clear(struct edgetpu_firmware *et_fw)
{
	et_fw->name = NULL;
}

/* Load firmware named @name.  Caller must hold edgetpu_firmware_load_lock(). */
static int edgetpu_firmware_load(struct edgetpu_firmware *et_fw, const char *name)
{
	struct edgetpu_dev *etdev = et_fw->etdev;
	struct device *dev = etdev->dev;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, name, dev);
	if (ret) {
		etdev_err(etdev, "request firmware '%s' failed: %d\n", name, ret);
		return ret;
	}

	/* May return NULL on out of memory, driver must handle properly */
	et_fw->name = devm_kstrdup(dev, name, GFP_KERNEL);
	ret = edgetpu_firmware_setup_image(et_fw, fw);
	release_firmware(fw);
	if (ret)
		edgetpu_firmware_image_clear(et_fw);
	return ret;
}

static int edgetpu_firmware_handshake(struct edgetpu_firmware *et_fw)
{
	struct edgetpu_dev *etdev = et_fw->etdev;
	enum gcip_fw_flavor fw_flavor;
	int ret;

	etdev_dbg(etdev, "Detecting firmware info...");
	et_fw->fw_info.fw_build_time = 0;
	et_fw->fw_info.fw_flavor = GCIP_FW_FLAVOR_UNKNOWN;
	et_fw->fw_info.fw_changelist = 0;
	fw_flavor = edgetpu_kci_fw_info(etdev->etkci, &et_fw->fw_info);
	etdev_info(etdev, "R52 boot stage: %u\n",
		   EDGETPU_MAILBOX_CONTEXT_READ(etdev->etkci->mailbox, config_spare_1));
	if (fw_flavor < 0) {
		etdev_err(etdev, "firmware handshake failed: %d", fw_flavor);
		et_fw->fw_info.fw_flavor = GCIP_FW_FLAVOR_UNKNOWN;
		et_fw->fw_info.fw_changelist = 0;
		et_fw->fw_info.fw_build_time = 0;
		/* Dump telemetry buffer in case there is any log. */
		edgetpu_telemetry_irq_handler(etdev);
		return fw_flavor;
	}

	etdev_info(etdev, "loaded %s firmware (%u.%u %u)",
		   gcip_fw_flavor_str(fw_flavor),
		   etdev->fw_version.major_version,
		   etdev->fw_version.minor_version,
		   et_fw->fw_info.fw_changelist);
	ret = edgetpu_telemetry_kci(etdev);
	if (ret)
		etdev_warn(etdev, "telemetry KCI error: %d", ret);

	ret = gcip_firmware_tracing_restore_on_powering(etdev->fw_tracing);
	if (ret)
		etdev_warn(etdev, "firmware tracing restore error: %d", ret);

	ret = gcip_thermal_restore_on_powering(etdev->thermal);
	if (ret)
		etdev_warn(etdev, "thermal restore error: %d", ret);

	/* set_device_properties not enabled in production b/405471390 */
	if (IS_ENABLED(CONFIG_EDGETPU_TEST)) {
		ret = edgetpu_kci_set_device_properties(etdev->etkci, &etdev->device_prop);
		if (ret)
			dev_warn(etdev->dev, "Failed to pass device_prop to fw: %d\n", ret);
	}
	/* If fw debug service is already activated, tell fw about it. */
	edgetpu_fw_debug_resetup(etdev);
	return 0;
}

/*
 * Do edgetpu_pm_get() but prevent it from running the loaded firmware.
 *
 * On success, caller must later call edgetpu_pm_put() to decrease the reference count.
 *
 * Caller holds firmware lock.
 */
static int edgetpu_firmware_pm_get(struct edgetpu_firmware *et_fw)
{
	enum gcip_fw_status prev = et_fw->status;
	int ret;

	/* Prevent platform-specific code from trying to run the previous firmware */
	et_fw->status = GCIP_FW_LOADING;
	etdev_dbg(et_fw->etdev, "Requesting power up for firmware run\n");
	ret = edgetpu_pm_get(et_fw->etdev);
	if (ret)
		et_fw->status = prev;
	return ret;
}

static void edgetpu_firmware_set_loading(struct edgetpu_firmware *et_fw)
{
	struct edgetpu_dev *etdev = et_fw->etdev;

	mutex_lock(&etdev->state_lock);
	etdev->state = ETDEV_STATE_FWLOADING;
	mutex_unlock(&etdev->state_lock);

	et_fw->status = GCIP_FW_LOADING;
}

/* Set firmware and etdev state according to @ret, which can be an errno or 0. */
static void edgetpu_firmware_set_state(struct edgetpu_firmware *et_fw, int ret)
{
	struct edgetpu_dev *etdev = et_fw->etdev;

	et_fw->status = ret ? GCIP_FW_INVALID : GCIP_FW_VALID;

	mutex_lock(&etdev->state_lock);
	if (ret == -EIO)
		etdev->state = ETDEV_STATE_BAD; /* f/w handshake error */
	else if (ret)
		etdev->state = ETDEV_STATE_NOFW; /* other errors */
	else
		etdev->state = ETDEV_STATE_GOOD; /* f/w handshake success */
	mutex_unlock(&etdev->state_lock);
}

uint32_t
edgetpu_firmware_get_cl(struct edgetpu_firmware *et_fw)
{
	return et_fw->fw_info.fw_changelist;
}

uint64_t
edgetpu_firmware_get_build_time(struct edgetpu_firmware *et_fw)
{
	return et_fw->fw_info.fw_build_time;
}

enum gcip_fw_flavor edgetpu_firmware_get_flavor(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return GCIP_FW_FLAVOR_UNKNOWN;
	return et_fw->fw_info.fw_flavor;
}

/*
 * Try edgetpu_firmware_lock() if it's not locked yet.
 *
 * Returns 1 if the lock is acquired successfully, 0 otherwise.
 */
int edgetpu_firmware_trylock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return 1;
	return mutex_trylock(&et_fw->fw_state_lock);
}

/*
 * Grab firmware lock to protect against firmware state changes.
 * Locks out firmware loading / unloading while caller performs ops that are
 * incompatible with a change in firmware status.  Does not care whether or not
 * the device is joined to a group.
 */
int edgetpu_firmware_lock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return -EINVAL;
	mutex_lock(&et_fw->fw_state_lock);
	return 0;
}

/* Drop f/w lock, let any pending firmware load proceed. */
void edgetpu_firmware_unlock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return;
	mutex_unlock(&et_fw->fw_state_lock);
}

/*
 * Lock firmware for loading.  Disallow group join for device during load.
 * Fails if device is already joined to a group and is in use.
 * See documentation for edgetpu_firmware_lock() for rules on locking vs. pm_lock.
 */
static int edgetpu_firmware_load_lock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw) {
		etdev_err(
			etdev,
			"Cannot load firmware when no loader is available\n");
		return -EINVAL;
	}
	mutex_lock(&et_fw->fw_state_lock);

	/* Disallow group join while loading, fail if already joined */
	if (!edgetpu_set_group_join_lockout(etdev, true)) {
		etdev_err(
			etdev,
			"Cannot load firmware because device is in use");
		mutex_unlock(&et_fw->fw_state_lock);
		return -EBUSY;
	}
	return 0;
}

/* Unlock firmware after lock held for loading, re-allow group join. */
static void edgetpu_firmware_load_unlock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw) {
		etdev_dbg(etdev,
			  "Unlock firmware when no loader available\n");
		return;
	}
	edgetpu_set_group_join_lockout(etdev, false);
	mutex_unlock(&et_fw->fw_state_lock);
}

static void edgetpu_firmware_reset_boot_stage(struct edgetpu_firmware *et_fw)
{
	EDGETPU_MAILBOX_CONTEXT_WRITE(et_fw->etdev->etkci->mailbox, config_spare_1, 0);
}

#define FLATBUFFER_MIN_VII_VERSION	0
#define LITEBUF_MIN_VII_VERSION	3
/*
 * Update the driver's current VII format, based on the current firmware's VII version.
 * This function must only be called during the process of loading new firmware.
 */
static int edgetpu_update_vii_format(struct edgetpu_dev *etdev)
{
	enum edgetpu_vii_format new_format;

	if (etdev->fw_version.vii_version >= LITEBUF_MIN_VII_VERSION)
		new_format = EDGETPU_VII_FORMAT_LITEBUF;
	else
		new_format = EDGETPU_VII_FORMAT_FLATBUFFER;

	if (etdev->vii_format != new_format) {
		etdev->vii_format = new_format;
		edgetpu_ikv_release(etdev, etdev->etikv);
		/* If IKV init fails, the firmware load will fail, and format set to UNKNOWN. */
		return edgetpu_ikv_init(etdev->mailbox_manager, etdev->etikv);
	}

	return 0;
}

/*
 * If the sanitizer is enabled, a carveout region is required as BCTCM since the
 * data size exceeds the real BCTCM. We have to clear this chunk of memory.
 */
static void edgetpu_firmware_clear_bctcm(struct edgetpu_dev *etdev)
{
	struct device *dev = etdev->dev;
	struct resource r;
	struct device_node *np;
	void *vaddr;
	int ret;

	np = of_parse_phandle(dev->of_node, "bctcm-region", 0);
	if (!np) {
		etdev_warn(etdev, "No bctcm carveout region for firmware");
		return;
	}

	ret = of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (ret) {
		etdev_warn(etdev, "No memory address assigned to bctcm carveout region");
		return;
	}

	vaddr = memremap(r.start, resource_size(&r), MEMREMAP_WC);
	if (!vaddr) {
		etdev_warn(etdev, "bctcm carveout region remap failed");
		return;
	}
	memset(vaddr, 0, resource_size(&r));
	memunmap(vaddr);
}

static int edgetpu_firmware_run_locked(struct edgetpu_firmware *et_fw, const char *name)
{
	struct edgetpu_dev *etdev = et_fw->etdev;
	int ret;

	edgetpu_firmware_set_loading(et_fw);
	edgetpu_sw_wdt_stop(et_fw->etdev);
	ret = edgetpu_firmware_load(et_fw, name);
	edgetpu_firmware_reset_boot_stage(et_fw);
	if (ret)
		goto out_failed;

	/*
	 * Update VII format now that the image_config has been read and before
	 * edgetpu_firmware_prepare_run() can reinitialize the in-Kernel VII stack.
	 */
	edgetpu_update_vii_format(etdev);
	if (ret)
		goto out_failed;

	etdev_dbg(etdev, "run fw %s", name);
	if (et_fw->sanitizer_status)
		edgetpu_firmware_clear_bctcm(etdev);
	ret = edgetpu_firmware_prepare_run(et_fw);
	if (ret)
		goto out_clear_image;

	gcip_fault_inject_send(et_fw->fault_inject);

	ret = edgetpu_firmware_handshake(et_fw);
	if (!ret)
		edgetpu_sw_wdt_start(et_fw->etdev);
	edgetpu_firmware_set_state(et_fw, ret);
	/* Reset metrics version sent to firmware, in case new fw supports a newer version. */
	if (etdev->usage_stats)
		etdev->usage_stats->ustats.version = EDGETPU_USAGE_METRIC_VERSION;
	return ret;

out_clear_image:
	edgetpu_firmware_image_clear(et_fw);
out_failed:
	edgetpu_firmware_set_state(et_fw, ret);
	etdev->vii_format = EDGETPU_VII_FORMAT_UNKNOWN;
	return ret;
}

int edgetpu_firmware_run(struct edgetpu_dev *etdev, const char *name)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;
	ret = edgetpu_firmware_pm_get(et_fw);
	if (ret)
		return ret;
	ret = edgetpu_firmware_load_lock(etdev);
	if (ret) {
		etdev_err(etdev, "firmware run: state lock failed: %d\n", ret);
	} else {
		/* will be overwritten when we successfully parse the f/w header */
		etdev->fw_version.kci_version = EDGETPU_INVALID_KCI_VERSION;
		ret = edgetpu_firmware_run_locked(et_fw, name);
		edgetpu_firmware_load_unlock(etdev);
	}

	edgetpu_pm_put(etdev);
	return ret;
}

int edgetpu_firmware_run_default_locked(struct edgetpu_dev *etdev)
{
	const char *run_firmware_name = EDGETPU_DEFAULT_FIRMWARE_NAME;

	if (firmware_name && *firmware_name)
		run_firmware_name = firmware_name;

	return edgetpu_firmware_run_locked(etdev->firmware, run_firmware_name);
}

int edgetpu_firmware_run_default(struct edgetpu_dev *etdev)
{
	const char *run_firmware_name = EDGETPU_DEFAULT_FIRMWARE_NAME;

	if (firmware_name && *firmware_name)
		run_firmware_name = firmware_name;

	return edgetpu_firmware_run(etdev, run_firmware_name);
}

bool edgetpu_firmware_is_loading(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	return et_fw && et_fw->status == GCIP_FW_LOADING;
}

/* Caller must hold firmware lock. */
enum gcip_fw_status edgetpu_firmware_status_locked(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return GCIP_FW_INVALID;
	return et_fw->status;
}

/* Caller must hold firmware lock. For unit tests. */
void edgetpu_firmware_set_status_locked(struct edgetpu_dev *etdev, enum gcip_fw_status status)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (et_fw)
		et_fw->status = status;
}

/* Caller must hold firmware lock for loading. */
int edgetpu_firmware_restart_locked(struct edgetpu_dev *etdev, bool force_reset)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret = -1;

	edgetpu_firmware_set_loading(et_fw);
	edgetpu_sw_wdt_stop(etdev);
	edgetpu_firmware_reset_boot_stage(et_fw);
	if (et_fw->sanitizer_status)
		edgetpu_firmware_clear_bctcm(etdev);
	/* Reset KCI mailbox before starting f/w, don't process anything old.*/
	edgetpu_mailbox_reset(etdev->etkci->mailbox);
	edgetpu_iif_reinit_mailbox(etdev->etiif);

	/*
	 * Try restarting the firmware first, fall back to normal firmware start
	 * if this fails.
	 */
	ret = edgetpu_firmware_restart(et_fw, force_reset);
	if (ret) {
		ret = edgetpu_firmware_prepare_run(et_fw);
		if (ret)
			goto out;
	}
	ret = edgetpu_firmware_handshake(et_fw);
	if (!ret)
		edgetpu_sw_wdt_start(etdev);
out:
	edgetpu_firmware_set_state(et_fw, ret);
	return ret;
}

ssize_t edgetpu_firmware_get_name(struct edgetpu_dev *etdev, char *buf,
				  size_t buflen)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		goto fw_none;

	mutex_lock(&et_fw->fw_state_lock);
	if (edgetpu_firmware_status_locked(etdev) != GCIP_FW_VALID)
		goto unlock_fw_none;
	if (!et_fw->name)
		goto unlock_fw_none;
	ret = scnprintf(buf, buflen, "%s\n", et_fw->name);
	mutex_unlock(&et_fw->fw_state_lock);
	return ret;

unlock_fw_none:
	mutex_unlock(&et_fw->fw_state_lock);
fw_none:
	return scnprintf(buf, buflen, "[none]\n");
}

static ssize_t load_firmware_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);

	return edgetpu_firmware_get_name(etdev, buf, PAGE_SIZE);
}

static ssize_t load_firmware_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;
	char *name;

	if (!et_fw)
		return -ENODEV;

	name = edgetpu_fwutil_name_from_attr_buf(buf);
	if (IS_ERR(name))
		return PTR_ERR(name);

	etdev_info(etdev, "loading firmware %s\n", name);
	ret = edgetpu_firmware_run(etdev, name);

	kfree(name);

	if (ret)
		return ret;
	return count;
}

static DEVICE_ATTR_RW(load_firmware);

static ssize_t firmware_type_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;
	ret = scnprintf(buf, PAGE_SIZE, "%s\n",
			gcip_fw_flavor_str(et_fw->fw_info.fw_flavor));
	return ret;
}
static DEVICE_ATTR_RO(firmware_type);

static ssize_t firmware_version_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;

	if (etdev->fw_version.kci_version == EDGETPU_INVALID_KCI_VERSION)
		ret = -ENODATA;
	else
		ret = scnprintf(buf, PAGE_SIZE, "%u.%u vii=%u kci=%u cl=%u\n",
				etdev->fw_version.major_version,
				etdev->fw_version.minor_version,
				etdev->fw_version.vii_version,
				etdev->fw_version.kci_version,
				et_fw->fw_info.fw_changelist);
	return ret;
}
static DEVICE_ATTR_RO(firmware_version);

static struct attribute *dev_attrs[] = {
	&dev_attr_load_firmware.attr,
	&dev_attr_firmware_type.attr,
	&dev_attr_firmware_version.attr,
	NULL,
};

static const struct attribute_group edgetpu_firmware_attr_group = {
	.attrs = dev_attrs,
};

void edgetpu_firmware_watchdog_restart(struct edgetpu_dev *etdev, bool in_powerdown)
{
	int ret;
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!in_powerdown) {
		/* Don't attempt f/w restart if device is off. */
		if (!edgetpu_pm_is_powered(etdev))
			return;
	}

	/*
	 * Zero the FW state of open mailboxes and enabled pasids so that when the runtime releases
	 * groups the CLOSE_DEVICE and RELEASE_VMBOX KCIs won't be sent.
	 */
	edgetpu_handshake_clear_fw_state(&etdev->mailbox_manager->open_devices);
	edgetpu_handshake_clear_fw_state(&etdev->mailbox_manager->enabled_pasids);

	if (!in_powerdown) {
		/* Another procedure is loading the firmware, let it do the work. */
		if (edgetpu_firmware_is_loading(etdev))
			return;
	}

	/* edgetpu_firmware_lock() here never fails */
	edgetpu_firmware_lock(etdev);

	if (!in_powerdown) {
		ret = edgetpu_firmware_pm_get(et_fw);
		if (ret)
			goto out;
	}

	ret = edgetpu_firmware_restart_locked(etdev, true);
	if (ret)
		etdev_warn(etdev, "restart returns %d\n", ret);

	if (!in_powerdown)
		edgetpu_pm_put(etdev);

out:
	edgetpu_firmware_unlock(etdev);
}

static int edgetpu_firmware_fault_inject_init(struct edgetpu_firmware *et_fw)
{
	struct edgetpu_dev *etdev = et_fw->etdev;
	const struct gcip_fault_inject_args args = { .dev = etdev->dev,
						     .parent_dentry = etdev->d_entry,
						     .pm = edgetpu_gcip_pm(etdev),
						     .send_kci = edgetpu_kci_fault_injection,
						     .kci_data = etdev->etkci };
	struct gcip_fault_inject *injection;

	injection = gcip_fault_inject_create(&args);

	if (IS_ERR(injection))
		return PTR_ERR(injection);

	et_fw->fault_inject = injection;

	return 0;
}

static void edgetpu_firmware_fault_inject_exit(struct edgetpu_firmware *et_fw)
{
	gcip_fault_inject_destroy(et_fw->fault_inject);
}

int edgetpu_firmware_create(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	INIT_LIST_HEAD(&et_fw->image_config_map_list);
	mutex_init(&et_fw->image_config_map_list_lock);

	ret = device_add_group(etdev->dev, &edgetpu_firmware_attr_group);
	if (ret)
		return ret;

	ret = edgetpu_firmware_init_image_config(et_fw);
	if (ret)
		goto out_device_remove_group;

	ret = edgetpu_firmware_fault_inject_init(et_fw);
	if (ret)
		etdev_warn(etdev, "Failed to init fault injection: %d\n", ret);


	ret = edgetpu_sw_wdt_create(etdev, EDGETPU_ACTIVE_DEV_BEAT_MS,
				    EDGETPU_DORMANT_DEV_BEAT_MS);
	if (ret)
		etdev_warn(etdev, "Failed to create software watchdog\n");
	return 0;

out_device_remove_group:
	device_remove_group(etdev->dev, &edgetpu_firmware_attr_group);
	return ret;
}

/* Must be called prior to edgetpu_firmware_cleanup_fw_region(). */
void edgetpu_firmware_destroy(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return;
	edgetpu_sw_wdt_destroy(etdev);

	edgetpu_firmware_fault_inject_exit(et_fw);
	edgetpu_firmware_deinit_image_config(et_fw);
	device_remove_group(etdev->dev, &edgetpu_firmware_attr_group);
	mutex_lock(&et_fw->fw_state_lock);
	edgetpu_firmware_image_clear(et_fw);
	et_fw->status = GCIP_FW_INVALID;
	mutex_unlock(&et_fw->fw_state_lock);
	/* Disallow further firmware run on power up. */
	mutex_lock(&etdev->state_lock);
	etdev->state = ETDEV_STATE_SHUTDOWN;
	mutex_unlock(&etdev->state_lock);
}

#if EDGETPU_HAS_GSA

static void edgetpu_firmware_setup_gsa(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct device *dev = etdev->dev;
	struct device_node *np;
	struct platform_device *gsa_pdev;

	/* Get GSA device from device tree */
	np = of_parse_phandle(dev->of_node, "gsa-device", 0);
	if (!np) {
		etdev_warn(etdev, "No \"gsa-device\" property in device tree, authentication not available.");
		return;
	}

	gsa_pdev = of_find_device_by_node(np);
	if (!gsa_pdev)
		etdev_warn(etdev,
			   "GSA device not found in device tree, authentication not available.");
	else
		et_fw->gsa_dev = &gsa_pdev->dev;

	of_node_put(np);
}

static void edgetpu_firmware_cleanup_gsa(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (et_fw->gsa_dev) {
		gsa_unload_tpu_fw_image(et_fw->gsa_dev);
		put_device(et_fw->gsa_dev);
	}
}

#if IS_ENABLED(CONFIG_EDGETPU_TEST)
/* Used by unit tests to set a mocked GSA device. */
void edgetpu_firmware_set_fake_gsa_dev(struct edgetpu_dev *etdev, struct device *gsa_dev)
{
	if (!etdev->firmware) {
		pr_err("set fake GSA dev called with no fw set\n");
	} else {
		edgetpu_firmware_cleanup_gsa(etdev);
		etdev->firmware->gsa_dev = get_device(gsa_dev);
	}
}
#endif /* CONFIG_EDGETPU_TEST */

#else /* EDGETPU_HAS_GSA */

static void edgetpu_firmware_setup_gsa(struct edgetpu_dev *etdev)
{
}

static void edgetpu_firmware_cleanup_gsa(struct edgetpu_dev *etdev)
{
}

#if IS_ENABLED(CONFIG_EDGETPU_TEST)
void edgetpu_firmware_set_fake_gsa_dev(struct edgetpu_dev *etdev, struct device *gsa_dev)
{
}
#endif /* CONFIG_EDGETPU_TEST */

#endif /* EDGETPU_HAS_GSA */

int edgetpu_firmware_setup_fw_region(struct edgetpu_dev *etdev, phys_addr_t fw_region_paddr)
{
	struct edgetpu_firmware *et_fw;
	int ret;

	et_fw = kzalloc(sizeof(*et_fw), GFP_KERNEL);
	if (!et_fw)
		return -ENOMEM;
	et_fw->etdev = etdev;
	mutex_init(&et_fw->fw_state_lock);

	et_fw->fw_region_paddr = fw_region_paddr;
	et_fw->fw_region_size = EDGETPU_DEFAULT_FW_LIMIT;
	et_fw->shared_data_daddr = EDGETPU_INSTRUCTION_REMAP_BASE + et_fw->fw_region_size;
	et_fw->shared_data_size = EDGETPU_DEFAULT_REMAPPED_DATA_SIZE;
	et_fw->shared_data_vaddr = memremap(et_fw->fw_region_paddr + et_fw->fw_region_size,
					 et_fw->shared_data_size, MEMREMAP_WC);
	if (!et_fw->shared_data_vaddr) {
		etdev_err(etdev, "Shared fw memory remap failed");
		ret = -ENOMEM;
		goto out_free_et_fw;
	}

	ret = edgetpu_iremap_pool_create(etdev,
					 /* Base virtual address (kernel address space) */
					 et_fw->shared_data_vaddr + EDGETPU_POOL_MEM_OFFSET,
					 /* Base DMA address */
					 et_fw->shared_data_daddr + EDGETPU_POOL_MEM_OFFSET,
					 /* Base physical address */
					 et_fw->shared_data_paddr + EDGETPU_POOL_MEM_OFFSET,
					 /* Size */
					 et_fw->shared_data_size - EDGETPU_POOL_MEM_OFFSET,
					 /* Granularity */
					 PAGE_SIZE);
	if (ret) {
		etdev_err(etdev, "failed to initialize fw remapped memory pool: %d", ret);
		goto out_unmap_fw;
	}

	etdev->firmware = et_fw;
	edgetpu_firmware_setup_gsa(etdev);
	return 0;

out_unmap_fw:
	memunmap(et_fw->shared_data_vaddr);
out_free_et_fw:
	kfree(et_fw);
	return ret;
}

void edgetpu_firmware_cleanup_fw_region(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	edgetpu_firmware_cleanup_gsa(etdev);
	edgetpu_iremap_pool_destroy(etdev);

	if (et_fw->shared_data_vaddr) {
		memunmap(et_fw->shared_data_vaddr);
		et_fw->shared_data_vaddr = NULL;
		et_fw->shared_data_daddr = 0;
		et_fw->shared_data_size = 0;
	}

	etdev->firmware = NULL;
	kfree(et_fw);
}

/* debugfs mappings dump */
void edgetpu_firmware_mappings_show(struct edgetpu_dev *etdev,
				    struct seq_file *s)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	dma_addr_t fw_carveout_daddr = EDGETPU_INSTRUCTION_REMAP_BASE;

	if (!et_fw)
		return;
	if (!et_fw->name)
		return;
	seq_printf(s, "  %pad %lu fw\n", &fw_carveout_daddr,
		   DIV_ROUND_UP(et_fw->fw_region_size, PAGE_SIZE));
}

#if IS_ENABLED(CONFIG_EDGETPU_TEST)
/* Return the gcip_fault_inject from the private firmware data for the device. */
struct gcip_fault_inject *edgetpu_firmware_get_fault_inject(struct edgetpu_dev *etdev)
{
	return etdev->firmware ? etdev->firmware->fault_inject : NULL;
}
#endif
