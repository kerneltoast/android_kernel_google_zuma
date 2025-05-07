// SPDX-License-Identifier: GPL-2.0
/*
 * Common platform interfaces for mobile TPU chips.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <gcip/gcip-iommu.h>
#include <iif/iif-manager.h>

#include "edgetpu-config.h"
#include "edgetpu-devfreq.h"
#include "edgetpu-dmabuf.h"
#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"
#include "edgetpu-mmu.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-thermal.h"

static struct edgetpu_dev *edgetpu_debug_pointer;

static int edgetpu_platform_setup_fw_region(struct edgetpu_mobile_platform_dev *etmdev)
{
	struct edgetpu_dev *etdev = &etmdev->edgetpu_dev;
	struct device *dev = etdev->dev;
	struct resource r;
	struct device_node *np;
	int ret;
	size_t region_map_size = EDGETPU_MAX_FW_LIMIT;

	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np) {
		dev_err(dev, "No memory region for firmware");
		return -ENODEV;
	}

	ret = of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (ret) {
		dev_err(dev, "No memory address assigned to firmware region");
		return ret;
	}

	if (resource_size(&r) < region_map_size) {
		dev_err(dev, "Memory region for firmware too small (%zu bytes needed, got %llu)",
			region_map_size, resource_size(&r));
		return -ENOSPC;
	}

	ret = edgetpu_firmware_setup_fw_region(etdev, r.start);
	if (ret)
		dev_err(dev, "setup firmware region failed: %d", ret);
	return ret;
}

static void edgetpu_platform_cleanup_fw_region(struct edgetpu_mobile_platform_dev *etmdev)
{
	struct edgetpu_dev *etdev = &etmdev->edgetpu_dev;

	edgetpu_firmware_cleanup_fw_region(etdev);
}

/* Handle mailbox response doorbell IRQ for mobile platform devices. */
static irqreturn_t edgetpu_platform_handle_mailbox_doorbell(struct edgetpu_dev *etdev, int irq)
{
	struct edgetpu_mailbox *mailbox;
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);
	struct edgetpu_mailbox_manager *mgr = etdev->mailbox_manager;
	uint i;

	if (!mgr)
		return IRQ_NONE;
	for (i = 0; i < etmdev->n_mailbox_irq; i++)
		if (etmdev->mailbox_irq[i] == irq)
			break;
	if (i == etmdev->n_mailbox_irq)
		return IRQ_NONE;
	read_lock(&mgr->mailboxes_lock);
	mailbox = mgr->mailboxes[i];
	if (!mailbox)
		goto out;
	if (!EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, doorbell_status))
		goto out;
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, doorbell_clear, 1);
	etdev_dbg(mgr->etdev, "mbox %u resp doorbell irq tail=%u\n", i,
		  EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, tail));
	if (mailbox->handle_irq)
		mailbox->handle_irq(mailbox);
out:
	read_unlock(&mgr->mailboxes_lock);
	return IRQ_HANDLED;
}

/* Handle a mailbox response doorbell interrupt. */
irqreturn_t edgetpu_mailbox_irq_handler(int irq, void *arg)
{
	struct edgetpu_dev *etdev = arg;

	edgetpu_telemetry_irq_handler(etdev);
	return edgetpu_platform_handle_mailbox_doorbell(etdev, irq);
}

static inline const char *get_driver_commit(void)
{
#if IS_ENABLED(CONFIG_MODULE_SCMVERSION)
	return THIS_MODULE->scmversion ?: "scmversion missing";
#elif defined(GIT_REPO_TAG)
	return GIT_REPO_TAG;
#else
	return "Unknown";
#endif
}

static void edgetpu_get_embedded_iif_mgr(struct edgetpu_dev *etdev)
{
	struct iif_manager *mgr;

	etdev_info(etdev, "Try to get an embedded IIF manager");

	mgr = iif_manager_init(etdev->dev->of_node);
	if (IS_ERR(mgr)) {
		etdev_warn(etdev, "Failed to init an embedded IIF manager: %ld", PTR_ERR(mgr));
		return;
	}

	etdev->iif_mgr = mgr;
}

static void edgetpu_get_iif_mgr(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev;
	struct device_node *node;
	struct iif_manager *mgr;

	node = of_parse_phandle(etdev->dev->of_node, "iif-device", 0);
	if (IS_ERR_OR_NULL(node)) {
		etdev_warn(etdev, "There is no iif-device node in the device tree");
		goto get_embed;
	}

	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev) {
		etdev_warn(etdev, "Failed to find the IIF device");
		goto get_embed;
	}

	mgr = platform_get_drvdata(pdev);
	if (!mgr) {
		etdev_warn(etdev, "Failed to get a manager from IIF device");
		goto put_device;
	}

	etdev_info(etdev, "Use the IIF manager of IIF device");

	/* We don't need to call `get_device` since `of_find_device_by_node` takes a refcount. */
	etdev->iif_dev = &pdev->dev;
	etdev->iif_mgr = iif_manager_get(mgr);
	return;

put_device:
	put_device(&pdev->dev);
get_embed:
	edgetpu_get_embedded_iif_mgr(etdev);
}

static void edgetpu_put_iif_mgr(struct edgetpu_dev *etdev)
{
	if (etdev->iif_mgr)
		iif_manager_put(etdev->iif_mgr);
	/* NO-OP if `etdev->iif_dev.dev` is NULL. */
	put_device(etdev->iif_dev);
}

static int edgetpu_mobile_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct edgetpu_mobile_platform_dev *etmdev;
	struct edgetpu_dev *etdev;
	struct resource *r;
	struct edgetpu_mapped_resource regs;
	int ret;
	struct edgetpu_iface_params iface_params[] = {
		/* Default interface  */
		{ .name = NULL },
		/* Common name for embedded SoC devices */
		{ .name = "edgetpu-soc" },
	};

	etmdev = devm_kzalloc(dev, sizeof(*etmdev), GFP_KERNEL);
	if (!etmdev)
		return -ENOMEM;
	mutex_init(&etmdev->tz_mailbox_lock);
	etdev = &etmdev->edgetpu_dev;
	platform_set_drvdata(pdev, etdev);
	etdev->dev = dev;
	etdev->num_cores = EDGETPU_NUM_CORES;
	etdev->num_telemetry_buffers = EDGETPU_NUM_CORES;
	etdev->log_buffer_size = EDGETPU_TELEMETRY_LOG_BUFFER_SIZE;
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
	etdev->trace_buffer_size = EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE;
#endif
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR_OR_NULL(r)) {
		dev_err(dev, "failed to get memory resource");
		return -ENODEV;
	}

	regs.phys = r->start;
	regs.size = resource_size(r);
	regs.mem = devm_ioremap_resource(dev, r);
	if (IS_ERR(regs.mem)) {
		ret = PTR_ERR(regs.mem);
		dev_err(dev, "failed to map TPU TOP registers: %d", ret);
		return ret;
	}

	/* Use 36-bit DMA mask for any default DMA API paths except coherent. */
	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));
	if (ret)
		dev_warn(dev, "dma_set_mask returned %d\n", ret);
	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(dev, "dma_set_coherent_mask returned %d\n", ret);

	ret = edgetpu_platform_setup_fw_region(etmdev);
	if (ret) {
		dev_err(dev, "setup fw regions failed: %d", ret);
		return ret;
	}

	mutex_init(&etdev->vii_format_uninitialized_lock);
	etdev->vii_format = EDGETPU_VII_FORMAT_UNKNOWN;
	ret = edgetpu_device_add(etdev, &regs, iface_params, ARRAY_SIZE(iface_params));
	if (ret) {
		dev_err(dev, "edgetpu device add failed: %d", ret);
		goto out_cleanup_fw_region;
	}

	ret = edgetpu_soc_setup_irqs(etdev);
	if (ret) {
		dev_err(dev, "IRQ setup failed: %d", ret);
		goto out_remove_device;
	}

	ret = edgetpu_firmware_create(etdev);
	if (ret) {
		dev_err(dev, "initialize firmware downloader failed: %d", ret);
		goto out_remove_device;
	}

	ret = edgetpu_thermal_create(etdev);
	if (ret)
		etdev_warn(etdev, "Failed to create thermal device: %d", ret);

	ret = edgetpu_devfreq_create(etdev);
	if (ret)
		etdev_warn(etdev, "Failed to create devfreq interface: %d", ret);

	ret = edgetpu_sync_fence_manager_create(etdev);
	if (ret) {
		etdev_err(etdev, "Failed to create DMA fence manager: %d", ret);
		goto out_destroy_thermal;
	}

	edgetpu_get_iif_mgr(etdev);
	edgetpu_soc_post_power_on_init(etdev);
	dev_info(dev, "%s edgetpu initialized. Build: %s", etdev->dev_name, get_driver_commit());

	/* Turn the device off unless a client request is already received. */
	edgetpu_pm_shutdown(etdev, false);

	edgetpu_debug_pointer = etdev;

	return 0;

out_destroy_thermal:
	edgetpu_devfreq_destroy(etdev);
	edgetpu_thermal_destroy(etdev);
	edgetpu_firmware_destroy(etdev);
out_remove_device:
	edgetpu_device_remove(etdev);
out_cleanup_fw_region:
	edgetpu_platform_cleanup_fw_region(etmdev);
	return ret;
}

static int edgetpu_mobile_platform_remove(struct platform_device *pdev)
{
	struct edgetpu_dev *etdev = platform_get_drvdata(pdev);
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(etdev);

	edgetpu_put_iif_mgr(etdev);
	edgetpu_devfreq_destroy(etdev);
	edgetpu_thermal_destroy(etdev);
	edgetpu_firmware_destroy(etdev);
	edgetpu_device_remove(etdev);
	edgetpu_platform_cleanup_fw_region(etmdev);

	edgetpu_debug_pointer = NULL;

	return 0;
}
