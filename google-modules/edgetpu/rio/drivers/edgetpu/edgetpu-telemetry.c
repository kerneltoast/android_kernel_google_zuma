// SPDX-License-Identifier: GPL-2.0-only
/*
 * Edge TPU ML accelerator telemetry: logging and tracing.
 *
 * Copyright (C) 2019-2025 Google LLC
 */

#include <linux/minmax.h>
#include <linux/mm_types.h>

#include <gcip/gcip-memory.h>
#include <gcip/gcip-telemetry.h>

#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-mmu.h"
#include "edgetpu-telemetry.h"
#include "edgetpu.h"

static void set_telemetry_mem(struct edgetpu_dev *etdev)
{
	struct gcip_telemetry_ctx *telem = etdev->telemetry;
	int i, offset = 0;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		telem[i].log_mem.virt_addr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		telem[i].log_mem.dma_addr = edgetpu_firmware_shared_data_daddr(etdev) + offset;
		telem[i].log_mem.host_addr = 0;
		telem[i].log_mem.phys_addr = edgetpu_firmware_shared_data_paddr(etdev) + offset;
		telem[i].log_mem.size = etdev->log_buffer_size;
		telem[i].log_mem.virt_addr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		offset += etdev->log_buffer_size;
		telem[i].trace_mem.virt_addr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		telem[i].trace_mem.dma_addr = edgetpu_firmware_shared_data_daddr(etdev) + offset;
		telem[i].trace_mem.host_addr = 0;
		telem[i].trace_mem.phys_addr = edgetpu_firmware_shared_data_paddr(etdev) + offset;
		telem[i].trace_mem.size = etdev->trace_buffer_size;
		offset += etdev->trace_buffer_size;
	}
}

int edgetpu_telemetry_init(struct edgetpu_dev *etdev)
{
	int ret, i;

	if (!etdev->telemetry) {
		etdev->telemetry = devm_kcalloc(etdev->dev, etdev->num_telemetry_buffers,
						sizeof(*etdev->telemetry), GFP_KERNEL);
	} else {
		etdev->telemetry = devm_krealloc(
			etdev->dev, etdev->telemetry,
			etdev->num_telemetry_buffers * sizeof(*etdev->telemetry), GFP_KERNEL);
	}

	if (!etdev->telemetry)
		return -ENOMEM;

	set_telemetry_mem(etdev);

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		ret = gcip_telemetry_init(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_LOG,
					  etdev->dev);
		if (ret)
			break;

#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
		ret = gcip_telemetry_init(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_TRACE,
					  etdev->dev);
		if (ret) {
			gcip_telemetry_exit(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_LOG);
			break;
		}
#endif
	}

	if (ret)
		while (i--) {
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
			gcip_telemetry_exit(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_TRACE);
#endif
			gcip_telemetry_exit(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_LOG);
		}

	return ret;
}

void edgetpu_telemetry_exit(struct edgetpu_dev *etdev)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
		gcip_telemetry_exit(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_TRACE);
#endif
		gcip_telemetry_exit(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_LOG);
	}
}

int edgetpu_telemetry_kci(struct edgetpu_dev *etdev)
{
	int ret;

	/* Core 0 will notify other cores. */
	ret = gcip_telemetry_kci(&etdev->telemetry[0], GCIP_TELEMETRY_TYPE_LOG,
				 edgetpu_kci_map_log_buffer, etdev->etkci->kci);
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
	ret = gcip_telemetry_kci(&etdev->telemetry[0], GCIP_TELEMETRY_TYPE_TRACE,
				 edgetpu_kci_map_trace_buffer, etdev->etkci->kci);
	if (ret)
		return ret;
#endif

	return 0;
}

int edgetpu_telemetry_set_event(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				u32 eventfd)
{
	int ret;
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		ret = gcip_telemetry_set_event(&etdev->telemetry[i], type, eventfd);
		if (ret) {
			edgetpu_telemetry_unset_event(etdev, type);
			return ret;
		}
	}

	return 0;
}

void edgetpu_telemetry_unset_event(struct edgetpu_dev *etdev, enum gcip_telemetry_type type)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++)
		gcip_telemetry_unset_event(&etdev->telemetry[i], type);
}

void edgetpu_telemetry_irq_handler(struct edgetpu_dev *etdev)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		gcip_telemetry_irq_handler(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_LOG);
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
		gcip_telemetry_irq_handler(&etdev->telemetry[i], GCIP_TELEMETRY_TYPE_TRACE);
#endif
	}
}

static void telemetry_mappings_show(struct gcip_telemetry *tel, struct gcip_memory *mem,
				    struct seq_file *s)
{
	seq_printf(s, "  %pad %lu %s %#llx\n", &mem->dma_addr, DIV_ROUND_UP(mem->size, PAGE_SIZE),
		   tel->name, mem->host_addr);
}

void edgetpu_telemetry_mappings_show(struct edgetpu_dev *etdev,
				     struct seq_file *s)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		telemetry_mappings_show(&etdev->telemetry[i].log, &etdev->telemetry[i].log_mem, s);
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
		telemetry_mappings_show(&etdev->telemetry[i].trace, &etdev->telemetry[i].trace_mem,
					s);
#endif
	}
}

int edgetpu_mmap_telemetry_buffer(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				  struct vm_area_struct *vma, int core_id)
{
	int ret;

	if (core_id >= etdev->num_telemetry_buffers)
		return -EINVAL;

	ret = gcip_telemetry_mmap(&etdev->telemetry[core_id], type, vma);
	if (ret) {
		etdev_err(etdev, "Failed to mmap telemetry buffer: type=%d, ret=%d", type, ret);
		return ret;
	}

	return 0;
}
