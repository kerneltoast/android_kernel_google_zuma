// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU ML accelerator telemetry: logging and tracing.
 *
 * Copyright (C) 2019-2020 Google, Inc.
 */

#include <linux/mm_types.h>

#include <gcip/gcip-telemetry.h>

#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-mmu.h"
#include "edgetpu-telemetry.h"
#include "edgetpu.h"

static struct gcip_telemetry *select_telemetry(struct edgetpu_telemetry_ctx *ctx,
					       enum gcip_telemetry_type type)
{
	switch (type) {
	case GCIP_TELEMETRY_TRACE:
		return &ctx->trace;
	case GCIP_TELEMETRY_LOG:
		return &ctx->log;
	default:
		WARN_ONCE(1, "Unrecognized EdgeTPU telemetry type: %d", type);
		/* return a valid object, don't crash the kernel */
		return &ctx->log;
	}
}

static struct edgetpu_coherent_mem *select_telemetry_mem(struct edgetpu_telemetry_ctx *ctx,
							 enum gcip_telemetry_type type)
{
	switch (type) {
	case GCIP_TELEMETRY_TRACE:
		return &ctx->trace_mem;
	case GCIP_TELEMETRY_LOG:
		return &ctx->log_mem;
	default:
		WARN_ONCE(1, "Unrecognized EdgeTPU telemetry type: %d", type);
		/* return a valid object, don't crash the kernel */
		return &ctx->log_mem;
	}
}

static void set_telemetry_mem(struct edgetpu_dev *etdev)
{
	struct edgetpu_telemetry_ctx *telem = etdev->telemetry;
	int i, offset = 0;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		telem[i].log_mem.vaddr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		telem[i].log_mem.dma_addr = edgetpu_firmware_shared_data_daddr(etdev) + offset;
		telem[i].log_mem.host_addr = 0;
		telem[i].log_mem.size = etdev->log_buffer_size;
		telem[i].log_mem.vaddr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		offset += etdev->log_buffer_size;
		telem[i].trace_mem.vaddr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		telem[i].trace_mem.dma_addr = edgetpu_firmware_shared_data_daddr(etdev) + offset;
		telem[i].trace_mem.host_addr = 0;
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
		ret = gcip_telemetry_init(etdev->dev, &etdev->telemetry[i].log, "telemetry_log",
					  etdev->telemetry[i].log_mem.vaddr,
					  EDGETPU_TELEMETRY_LOG_BUFFER_SIZE,
					  gcip_telemetry_fw_log);
		if (ret)
			break;

#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
		ret = gcip_telemetry_init(etdev->dev, &etdev->telemetry[i].trace, "telemetry_trace",
					  etdev->telemetry[i].trace_mem.vaddr,
					  EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE,
					  gcip_telemetry_fw_trace);
		if (ret) {
			gcip_telemetry_exit(&etdev->telemetry[i].log);
			break;
		}
#endif
	}

	if (ret)
		while (i--) {
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
			gcip_telemetry_exit(&etdev->telemetry[i].trace);
#endif
			gcip_telemetry_exit(&etdev->telemetry[i].log);

		}

	return ret;
}

void edgetpu_telemetry_exit(struct edgetpu_dev *etdev)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
		gcip_telemetry_exit(&etdev->telemetry[i].trace);
#endif
		gcip_telemetry_exit(&etdev->telemetry[i].log);
	}
}

int edgetpu_telemetry_kci(struct edgetpu_dev *etdev)
{
	struct gcip_telemetry_kci_args log_args = {
		.kci = etdev->etkci->kci,
		.addr = etdev->telemetry[0].log_mem.dma_addr,
		.size = etdev->telemetry[0].log_mem.size,
	};
	struct gcip_telemetry_kci_args trace_args = {
		.kci = etdev->etkci->kci,
		.addr = etdev->telemetry[0].trace_mem.dma_addr,
		.size = etdev->telemetry[0].trace_mem.size,
	};
	int ret;

	/* Core 0 will notify other cores. */
	ret = gcip_telemetry_kci(&etdev->telemetry[0].log, edgetpu_kci_map_log_buffer, &log_args);
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
	ret = gcip_telemetry_kci(&etdev->telemetry[0].trace, edgetpu_kci_map_trace_buffer,
				 &trace_args);
	if (ret)
		return ret;
#endif

	return 0;
}

int edgetpu_telemetry_set_event(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				u32 eventfd)
{
	int i, ret;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		ret = gcip_telemetry_set_event(select_telemetry(&etdev->telemetry[i], type),
					       eventfd);
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
		gcip_telemetry_unset_event(select_telemetry(&etdev->telemetry[i], type));
}

void edgetpu_telemetry_irq_handler(struct edgetpu_dev *etdev)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		gcip_telemetry_irq_handler(&etdev->telemetry[i].log);
#if IS_ENABLED(CONFIG_EDGETPU_TELEMETRY_TRACE)
		gcip_telemetry_irq_handler(&etdev->telemetry[i].trace);
#endif
	}
}

static void telemetry_mappings_show(struct gcip_telemetry *tel, struct edgetpu_coherent_mem *mem,
				    struct seq_file *s)
{
	seq_printf(s, "  %pad %lu %s %#llx\n", &mem->dma_addr,
		   DIV_ROUND_UP(mem->size, PAGE_SIZE), tel->name, mem->host_addr);
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

struct edgetpu_telemetry_mmap_args {
	struct edgetpu_dev *etdev;
	struct edgetpu_coherent_mem *mem;
	struct vm_area_struct *vma;
};

static int telemetry_mmap_buffer(void *args)
{
	struct edgetpu_telemetry_mmap_args *data = args;
	int ret;

	ret = edgetpu_iremap_mmap(data->etdev, data->vma, data->mem);

	if (!ret)
		data->mem->host_addr = data->vma->vm_start;

	return ret;
}

int edgetpu_mmap_telemetry_buffer(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				  struct vm_area_struct *vma, int core_id)
{
	struct edgetpu_telemetry_mmap_args args = {
		.etdev = etdev,
		.mem = select_telemetry_mem(&etdev->telemetry[core_id], type),
		.vma = vma,
	};

	if (core_id > etdev->num_telemetry_buffers)
		return -EINVAL;

	return gcip_telemetry_mmap_buffer(select_telemetry(&etdev->telemetry[core_id], type),
					  telemetry_mmap_buffer, &args);
}

void edgetpu_telemetry_inc_mmap_count(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				      int core_id)
{
	gcip_telemetry_inc_mmap_count(select_telemetry(&etdev->telemetry[core_id], type), 1);
}

void edgetpu_telemetry_dec_mmap_count(struct edgetpu_dev *etdev, enum gcip_telemetry_type type,
				      int core_id)
{
	gcip_telemetry_inc_mmap_count(select_telemetry(&etdev->telemetry[core_id], type), -1);
}
