// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP MCU telemetry support
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/slab.h>
#include <linux/workqueue.h>

#include <gcip/gcip-config.h>
#include <gcip/gcip-telemetry.h>

#include "gxp-internal.h"
#include "gxp-kci.h"
#include "gxp-mcu-telemetry.h"
#include "gxp-mcu.h"
#include "gxp-notification.h"

static struct gcip_telemetry *
select_telemetry(struct gxp_mcu_telemetry_ctx *ctx,
		 enum gcip_telemetry_type type)
{
	switch (type) {
	case GCIP_TELEMETRY_TRACE:
		return &ctx->trace;
	case GCIP_TELEMETRY_LOG:
		return &ctx->log;
	default:
		WARN_ONCE(1, "Unrecognized GCIP telemetry type: %d", type);
		/* return a valid object, don't crash the kernel */
		return &ctx->log;
	}
}

static struct gxp_mapped_resource *
select_telemetry_mem(struct gxp_mcu_telemetry_ctx *ctx,
		     enum gcip_telemetry_type type)
{
	switch (type) {
	case GCIP_TELEMETRY_TRACE:
		return &ctx->trace_mem;
	case GCIP_TELEMETRY_LOG:
		return &ctx->log_mem;
	default:
		WARN_ONCE(1, "Unrecognized GCIP telemetry type: %d", type);
		/* return a valid object, don't crash the kernel */
		return &ctx->log_mem;
	}
}

int gxp_mcu_telemetry_init(struct gxp_mcu *mcu)
{
	struct gxp_mcu_telemetry_ctx *tel = &mcu->telemetry;
	int ret;

	ret = gxp_mcu_mem_alloc_data(mcu, &tel->log_mem,
				     GXP_MCU_TELEMETRY_LOG_BUFFER_SIZE);
	if (ret)
		return ret;

	ret = gcip_telemetry_init(mcu->gxp->dev, &tel->log, "telemetry_log",
				  tel->log_mem.vaddr,
				  GXP_MCU_TELEMETRY_LOG_BUFFER_SIZE,
				  gcip_telemetry_fw_log);
	if (ret)
		goto free_log_mem;

	ret = gxp_mcu_mem_alloc_data(mcu, &tel->trace_mem,
				     GXP_MCU_TELEMETRY_TRACE_BUFFER_SIZE);
	if (ret)
		goto uninit_log;

	ret = gcip_telemetry_init(mcu->gxp->dev, &tel->trace, "telemetry_trace",
				  tel->trace_mem.vaddr,
				  GXP_MCU_TELEMETRY_TRACE_BUFFER_SIZE,
				  gcip_telemetry_fw_trace);
	if (ret)
		goto free_trace_mem;

	return 0;

free_trace_mem:
	gxp_mcu_mem_free_data(mcu, &tel->trace_mem);

uninit_log:
	gcip_telemetry_exit(&tel->log);

free_log_mem:
	gxp_mcu_mem_free_data(mcu, &tel->log_mem);

	return ret;
}

void gxp_mcu_telemetry_exit(struct gxp_mcu *mcu)
{
	gcip_telemetry_exit(&mcu->telemetry.trace);
	gxp_mcu_mem_free_data(mcu, &mcu->telemetry.trace_mem);
	gcip_telemetry_exit(&mcu->telemetry.log);
	gxp_mcu_mem_free_data(mcu, &mcu->telemetry.log_mem);
}

void gxp_mcu_telemetry_irq_handler(struct gxp_mcu *mcu)
{
	struct gxp_mcu_telemetry_ctx *tel = &mcu->telemetry;

	gcip_telemetry_irq_handler(&tel->log);
	gcip_telemetry_irq_handler(&tel->trace);
}

int gxp_mcu_telemetry_kci(struct gxp_mcu *mcu)
{
	struct gcip_telemetry_kci_args args = {
		.kci = mcu->kci.mbx->mbx_impl.gcip_kci,
		.addr = mcu->telemetry.log_mem.daddr,
		.size = mcu->telemetry.log_mem.size,
	};
	int ret;

	ret = gcip_telemetry_kci(&mcu->telemetry.log,
				 gxp_kci_map_mcu_log_buffer, &args);
	if (ret)
		return ret;

	args.addr = mcu->telemetry.trace_mem.daddr,
	args.size = mcu->telemetry.trace_mem.size,
	ret = gcip_telemetry_kci(&mcu->telemetry.trace,
				 gxp_kci_map_mcu_trace_buffer, &args);

	return ret;
}

int gxp_mcu_telemetry_register_eventfd(struct gxp_mcu *mcu,
				       enum gcip_telemetry_type type,
				       u32 eventfd)
{
	int ret;

	ret = gcip_telemetry_set_event(select_telemetry(&mcu->telemetry, type),
				       eventfd);
	if (ret)
		gxp_mcu_telemetry_unregister_eventfd(mcu, type);

	return ret;
}

int gxp_mcu_telemetry_unregister_eventfd(struct gxp_mcu *mcu,
					 enum gcip_telemetry_type type)
{
	gcip_telemetry_unset_event(select_telemetry(&mcu->telemetry, type));

	return 0;
}

struct telemetry_vma_data {
	struct gcip_telemetry *tel;
	refcount_t ref_count;
};

static void telemetry_vma_open(struct vm_area_struct *vma)
{
	gxp_mcu_telemetry_vma_get(vma);
}

static void telemetry_vma_close(struct vm_area_struct *vma)
{
	gxp_mcu_telemetry_vma_put(vma);
}

static const struct vm_operations_struct telemetry_vma_ops = {
	.open = telemetry_vma_open,
	.close = telemetry_vma_close,
};

struct gxp_mcu_telemetry_mmap_args {
	struct gcip_telemetry *tel;
	struct gxp_mapped_resource *mem;
	struct vm_area_struct *vma;
};

static int telemetry_mmap_buffer(void *args)
{
	struct gxp_mcu_telemetry_mmap_args *data = args;
	struct gcip_telemetry *tel = data->tel;
	struct gxp_mapped_resource *mem = data->mem;
	struct vm_area_struct *vma = data->vma;
	struct telemetry_vma_data *vma_data =
		kmalloc(sizeof(*vma_data), GFP_KERNEL);
	unsigned long orig_pgoff = vma->vm_pgoff;
	int ret;

	if (!vma_data)
		return -ENOMEM;
	vma_data->tel = tel;
	refcount_set(&vma_data->ref_count, 1);

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
#if GCIP_HAS_VMA_FLAGS_API
	vm_flags_set(vma, VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP);
#else
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP;
#endif
	vma->vm_pgoff = 0;

	if (mem->size > vma->vm_end - vma->vm_start) {
		ret = -EINVAL;
		goto out;
	}

	ret = remap_pfn_range(vma, vma->vm_start, mem->paddr >> PAGE_SHIFT,
			      mem->size, vma->vm_page_prot);
	vma->vm_pgoff = orig_pgoff;

out:
	if (ret) {
		kfree(vma_data);
	} else {
		vma->vm_ops = &telemetry_vma_ops;
		vma->vm_private_data = vma_data;
	}

	return ret;
}

int gxp_mcu_telemetry_mmap_buffer(struct gxp_mcu *mcu,
				  enum gcip_telemetry_type type,
				  struct vm_area_struct *vma)
{
	struct gxp_mcu_telemetry_mmap_args args = {
		.mem = select_telemetry_mem(&mcu->telemetry, type),
		.tel = select_telemetry(&mcu->telemetry, type),
		.vma = vma,
	};

	return gcip_telemetry_mmap_buffer(select_telemetry(&mcu->telemetry,
							   type),
					  telemetry_mmap_buffer, &args);
}

void gxp_mcu_telemetry_vma_get(struct vm_area_struct *vma)
{
	struct telemetry_vma_data *vma_data = (struct telemetry_vma_data *)vma->vm_private_data;
	struct gcip_telemetry *tel = vma_data->tel;

	WARN_ON_ONCE(!refcount_inc_not_zero(&vma_data->ref_count));
	gcip_telemetry_inc_mmap_count(tel, 1);
}

void gxp_mcu_telemetry_vma_put(struct vm_area_struct *vma)
{
	struct telemetry_vma_data *vma_data = (struct telemetry_vma_data *)vma->vm_private_data;
	struct gcip_telemetry *tel = vma_data->tel;

	gcip_telemetry_inc_mmap_count(tel, -1);
	if (refcount_dec_and_test(&vma_data->ref_count))
		kfree(vma_data);
}
