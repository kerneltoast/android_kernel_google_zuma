// SPDX-License-Identifier: GPL-2.0
/*
 * GCMA(Guaranteed Contiguous Memory Allocator) vendor hooks
 *
 */

#include <trace/hooks/mm.h>

static atomic64_t total_gcma_pages = ATOMIC64_INIT(0);

static void vh_gcma_si_meminfo_fixup(void *data, struct sysinfo *val)
{
	val->totalram += (u64)atomic64_read(&total_gcma_pages);
}

void inc_gcma_total_pages(unsigned long page_count)
{
	atomic64_add(page_count, &total_gcma_pages);
}

int __init gcma_vh_init(void)
{

	return register_trace_android_vh_si_meminfo(vh_gcma_si_meminfo_fixup, NULL);
}
