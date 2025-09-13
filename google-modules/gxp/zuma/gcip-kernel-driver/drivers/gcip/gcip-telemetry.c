// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP telemetry: logging and tracing.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/eventfd.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <gcip/gcip-memory.h>
#include <gcip/gcip-telemetry.h>

/**
 * gcip_telemetry_select() - Get the gcip_telemetry of the specified type.
 * @tel_ctx: The gcip_telemetry_ctx object to retrieve the desired gcip_telemetry.
 * @type: The type of the telemetry desired.
 *
 * Return: The pointer to the gcip_telemetry of the desired type, or the pointer to a negative errno
 *         otherwise.
 */
static struct gcip_telemetry *gcip_telemetry_select(struct gcip_telemetry_ctx *tel_ctx,
						    enum gcip_telemetry_type type)
{
	switch (type) {
	case GCIP_TELEMETRY_TYPE_LOG:
		return &tel_ctx->log;
	case GCIP_TELEMETRY_TYPE_TRACE:
		return &tel_ctx->trace;
	default:
		WARN_ONCE(true, "Unrecognized GCIP telemetry type: %d", type);
		/* return a valid object, don't crash the kernel */
		return &tel_ctx->log;
	}
}

/**
 * gcip_telemetry_select_mem() - Get the gcip_memory of the specified type.
 * @tel_ctx: The gcip_telemetry_ctx object to retrieve the desired gcip_memory.
 * @type: The type of the telemetry desired.
 *
 * Return: The pointer to the gcip_memory of the desired type, or the pointer to a
 *         negative errno otherwise.
 */
static struct gcip_memory *gcip_telemetry_select_mem(struct gcip_telemetry_ctx *tel_ctx,
						     enum gcip_telemetry_type type)
{
	switch (type) {
	case GCIP_TELEMETRY_TYPE_LOG:
		return &tel_ctx->log_mem;
	case GCIP_TELEMETRY_TYPE_TRACE:
		return &tel_ctx->trace_mem;
	default:
		WARN_ONCE(true, "Unrecognized GCIP telemetry type: %d", type);
		/* return a valid object, don't crash the kernel */
		return &tel_ctx->log_mem;
	}
}

int gcip_telemetry_kci(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
		       int (*send_kci)(const struct gcip_telemetry_kci_args *),
		       struct gcip_kci *kci)
{
	const struct gcip_telemetry *tel = gcip_telemetry_select(tel_ctx, type);
	const struct gcip_memory *mem = gcip_telemetry_select_mem(tel_ctx, type);
	const struct gcip_telemetry_kci_args args = {
		.kci = kci,
		.addr = mem->dma_addr,
		.size = mem->size,
	};
	int err;

	dev_dbg(tel->dev, "Sending KCI %s", tel->name);
	err = send_kci(&args);

	if (err < 0) {
		dev_err(tel->dev, "KCI %s failed - %d", tel->name, err);
		return err;
	}

	if (err > 0) {
		dev_err(tel->dev, "KCI %s returned %d", tel->name, err);
		return -EBADMSG;
	}

	dev_dbg(tel->dev, "KCI %s Succeeded", tel->name);

	return 0;
}

int gcip_telemetry_set_event(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
			     u32 eventfd)
{
	struct gcip_telemetry *tel = gcip_telemetry_select(tel_ctx, type);
	struct eventfd_ctx *ctx, *prev_ctx;
	ulong flags;

	ctx = eventfd_ctx_fdget(eventfd);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	write_lock_irqsave(&tel->ctx_lock, flags);
	prev_ctx = tel->ctx;
	tel->ctx = ctx;
	write_unlock_irqrestore(&tel->ctx_lock, flags);

	if (prev_ctx)
		eventfd_ctx_put(prev_ctx);

	return 0;
}

void gcip_telemetry_unset_event(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type)
{
	struct gcip_telemetry *tel = gcip_telemetry_select(tel_ctx, type);
	struct eventfd_ctx *prev_ctx;
	ulong flags;

	write_lock_irqsave(&tel->ctx_lock, flags);
	prev_ctx = tel->ctx;
	tel->ctx = NULL;
	write_unlock_irqrestore(&tel->ctx_lock, flags);

	if (prev_ctx)
		eventfd_ctx_put(prev_ctx);
}

/**
 * copy_with_wrap() - The helper function to copy data out of the log buffer with wrapping.
 * @header: The telemetry header to read and write the head value.
 * @dest: The buffer to copy the data to.
 * @length: The length of the data to be copied.
 * @size: The size of telemetry buffer.
 * @start: The start address of the telemetry buffer.
 */
static void copy_with_wrap(struct gcip_telemetry_header *header, void *dest, u32 length, u32 size,
			   void *start)
{
	const u32 wrap_bit = size + sizeof(*header);
	u32 remaining = 0;
	u32 head = header->head & (wrap_bit - 1);

	if (head + length < size) {
		memcpy(dest, start + head, length);
		header->head += length;
	} else {
		remaining = size - head;
		memcpy(dest, start + head, remaining);
		memcpy(dest + remaining, start, length - remaining);
		header->head = (header->head & wrap_bit) ^ wrap_bit;
		header->head |= length - remaining;
	}
}

/**
 * gcip_telemetry_fw_log() - The fallback function to consume the log buffer.
 * @log: The log telemetry object.
 *
 * This function will consume the log buffer and print it to dmesg from the host CPU. The logging
 * level depends on the code in the header entry.
 */
static void gcip_telemetry_fw_log(const struct gcip_telemetry *log)
{
	struct device *dev = log->dev;
	struct gcip_telemetry_header *header = log->header;
	struct gcip_log_entry_header entry;
	u8 *start;
	const size_t queue_size = header->size - sizeof(*header);
	const size_t max_length = queue_size - sizeof(entry);
	char *buffer = kmalloc(max_length + 1, GFP_ATOMIC);

	if (!buffer) {
		header->head = header->tail;
		return;
	}
	start = (u8 *)header + sizeof(*header);

	while (header->head != header->tail) {
		copy_with_wrap(header, &entry, sizeof(entry), queue_size, start);
		if (entry.length == 0 || entry.length > max_length) {
			header->head = header->tail;
			dev_err(dev, "log queue is corrupted");
			break;
		}
		copy_with_wrap(header, buffer, entry.length, queue_size, start);
		buffer[entry.length] = 0;

		if (entry.code > GCIP_FW_DMESG_LOG_LEVEL)
			continue;

		switch (entry.code) {
		case GCIP_FW_LOG_LEVEL_VERBOSE:
		case GCIP_FW_LOG_LEVEL_DEBUG:
			dev_dbg(dev, "%s", buffer);
			break;
		case GCIP_FW_LOG_LEVEL_WARN:
			dev_warn(dev, "%s", buffer);
			break;
		case GCIP_FW_LOG_LEVEL_FATAL:
		case GCIP_FW_LOG_LEVEL_ERROR:
			dev_err(dev, "%s", buffer);
			break;
		case GCIP_FW_LOG_LEVEL_INFO:
		default:
			dev_info(dev, "%s", buffer);
			break;
		}
	}
	kfree(buffer);
}

/**
 * gcip_telemetry_fw_trace() - The fallback function to consume the trace buffer.
 * @trace: The trace telemetry object.
 *
 * This function will do nothing but update the value of the head in the header.
 */
static void gcip_telemetry_fw_trace(const struct gcip_telemetry *trace)
{
	struct gcip_telemetry_header *header = trace->header;

	header->head = header->tail;
}

void gcip_telemetry_irq_handler(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type)
{
	struct gcip_telemetry *tel = gcip_telemetry_select(tel_ctx, type);
	unsigned long flags;

	/*
	 * If the lock is held by other threads - it means either
	 *   1. The worker gcip_telemetry_worker is working, or
	 *   2. The telemetry object is being released
	 * Either way we don't need to schedule another job.
	 */
	if (!spin_trylock_irqsave(&tel->state_lock, flags))
		return;

	if (tel->state == GCIP_TELEMETRY_ENABLED && tel->header->head != tel->header->tail)
		/*
		 * The telemetry work consumes the buffer until head equals tail, no need to check
		 * whether a pending work exists.
		 */
		schedule_work(&tel->work);

	spin_unlock_irqrestore(&tel->state_lock, flags);
}

/**
 * gcip_telemetry_inc_mmap_count() - Increases the telemetry mmap count.
 * @tel: The telemetry to add the mmapped_count.
 * @dif: The number to add the mmapped_count.
 */
static void gcip_telemetry_inc_mmap_count(struct gcip_telemetry *tel, int dif)
{
	mutex_lock(&tel->mmap_lock);
	tel->mmapped_count += dif;
	mutex_unlock(&tel->mmap_lock);
}

/**
 * gcip_telemetry_vma_ops_open() - The callback function to trigger when VMA is being mapped.
 * @vma: The VM area to be opened.
 *
 * Increses the mmap count of the retrieved telemetry.
 */
static void gcip_telemetry_vma_ops_open(struct vm_area_struct *vma)
{
	struct gcip_telemetry *tel = vma->vm_private_data;

	gcip_telemetry_inc_mmap_count(tel, 1);
}

/**
 * gcip_telemetry_vma_ops_close() - The callback function to trigger when VMA is being unmapped.
 * @vma: The VM area to be closed.
 *
 * Decreses the mmap count of the retrieved telemetry.
 */
static void gcip_telemetry_vma_ops_close(struct vm_area_struct *vma)
{
	struct gcip_telemetry *tel = vma->vm_private_data;

	gcip_telemetry_inc_mmap_count(tel, -1);
}

static const struct vm_operations_struct gcip_telemetry_vma_ops = {
	.open = gcip_telemetry_vma_ops_open,
	.close = gcip_telemetry_vma_ops_close,
};

int gcip_telemetry_mmap(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
			struct vm_area_struct *vma)
{
	struct gcip_telemetry *tel = gcip_telemetry_select(tel_ctx, type);
	struct gcip_memory *mem = gcip_telemetry_select_mem(tel_ctx, type);
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long orig_pgoff = vma->vm_pgoff;
	int ret;

	size = min(size, mem->size);
	if (!size) {
		dev_err(tel->dev, "The size of the telemetry buffer to be mapped cannot be 0");
		return -EINVAL;
	}

	dev_dbg(tel->dev, "%s: virt = %pK phys = %pap\n", __func__, mem->virt_addr,
		&mem->phys_addr);

	mutex_lock(&tel->mmap_lock);

	if (tel->mmapped_count) {
		ret = -EBUSY;
		dev_warn(tel->dev, "%s is already mmapped %ld times", tel->name,
			 tel->mmapped_count);
		goto err_unlock;
	}

	vm_flags_set(vma, VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_pgoff = 0;
	ret = remap_pfn_range(vma, vma->vm_start, mem->phys_addr >> PAGE_SHIFT, size,
			      vma->vm_page_prot);
	vma->vm_pgoff = orig_pgoff;
	if (ret)
		goto err_unlock;

	vma->vm_ops = &gcip_telemetry_vma_ops;
	vma->vm_private_data = tel;
	tel->mmapped_count = 1;
	mem->host_addr = vma->vm_start;

	mutex_unlock(&tel->mmap_lock);

	return 0;

err_unlock:
	mutex_unlock(&tel->mmap_lock);
	return ret;
}

/**
 * gcip_telemetry_worker() - The worker for processing the log/trace buffers.
 * @work: The work_struct of the telemetry.
 */
static void gcip_telemetry_worker(struct work_struct *work)
{
	struct gcip_telemetry *tel = container_of(work, struct gcip_telemetry, work);
	u32 prev_head;
	ulong state_lock_flags, ctx_lock_flags;

	/*
	 * Loops while telemetry enabled, there is data to be consumed, and the previous iteration
	 * made progress. If another IRQ arrives just after the last head != tail check we should
	 * get another worker schedule.
	 */
	do {
		spin_lock_irqsave(&tel->state_lock, state_lock_flags);
		if (tel->state != GCIP_TELEMETRY_ENABLED) {
			spin_unlock_irqrestore(&tel->state_lock, state_lock_flags);
			return;
		}

		prev_head = tel->header->head;
		if (tel->header->head != tel->header->tail) {
			read_lock_irqsave(&tel->ctx_lock, ctx_lock_flags);
			if (tel->ctx)
				eventfd_signal(tel->ctx, 1);
			else
				tel->fallback_fn(tel);
			read_unlock_irqrestore(&tel->ctx_lock, ctx_lock_flags);
		}

		spin_unlock_irqrestore(&tel->state_lock, state_lock_flags);
		msleep(GCIP_TELEMETRY_TYPE_LOG_RECHECK_DELAY);
	} while (tel->header->head != tel->header->tail && tel->header->head != prev_head);
}

int gcip_telemetry_init(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type,
			struct device *dev)
{
	struct gcip_telemetry *tel;
	const char *name;
	struct gcip_memory *mem;
	void (*fallback_fn)(const struct gcip_telemetry *tel);

	switch (type) {
	case GCIP_TELEMETRY_TYPE_LOG:
		tel = &tel_ctx->log;
		mem = &tel_ctx->log_mem;
		name = GCIP_TELEMETRY_NAME_LOG;
		fallback_fn = gcip_telemetry_fw_log;
		break;
	case GCIP_TELEMETRY_TYPE_TRACE:
		tel = &tel_ctx->trace;
		mem = &tel_ctx->trace_mem;
		name = GCIP_TELEMETRY_NAME_TRACE;
		fallback_fn = gcip_telemetry_fw_trace;
		break;
	default:
		dev_err(dev, "Unrecognized GCIP telemetry type: %d", type);
		return -EINVAL;
	}

	/* The log_mem and trace_mem have to be set before telemetry init. */
	if (!mem->virt_addr || !mem->size) {
		dev_err(dev, "The telemetry memory should be set before initializing: %s", name);
		return -EINVAL;
	}

	if (!is_power_of_2(mem->size) || mem->size <= sizeof(struct gcip_telemetry_header)) {
		dev_err(dev,
			"Size of GCIP telemetry buffer must be a power of 2 and greater than %zu.",
			sizeof(struct gcip_telemetry_header));
		return -EINVAL;
	}

	rwlock_init(&tel->ctx_lock);
	tel->name = name;
	tel->dev = dev;

	tel->header = mem->virt_addr;
	tel->header->head = 0;
	tel->header->tail = 0;
	tel->header->size = mem->size;
	tel->header->entries_dropped = 0;

	tel->ctx = NULL;

	spin_lock_init(&tel->state_lock);
	INIT_WORK(&tel->work, gcip_telemetry_worker);
	tel->fallback_fn = fallback_fn;
	tel->state = GCIP_TELEMETRY_ENABLED;
	mutex_init(&tel->mmap_lock);
	tel->mmapped_count = 0;

	return 0;
}

void gcip_telemetry_exit(struct gcip_telemetry_ctx *tel_ctx, enum gcip_telemetry_type type)
{
	struct gcip_telemetry *tel = gcip_telemetry_select(tel_ctx, type);
	ulong flags;

	spin_lock_irqsave(&tel->state_lock, flags);
	/* Prevents racing with the IRQ handler or worker. */
	tel->state = GCIP_TELEMETRY_INVALID;
	spin_unlock_irqrestore(&tel->state_lock, flags);
	cancel_work_sync(&tel->work);

	if (tel->ctx)
		eventfd_ctx_put(tel->ctx);
	tel->ctx = NULL;
}
