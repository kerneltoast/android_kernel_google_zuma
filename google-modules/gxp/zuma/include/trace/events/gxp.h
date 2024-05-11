/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace events for gxp
 *
 * Copyright (c) 2023 Google LLC
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gxp

#if !defined(_TRACE_GXP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GXP_H

#include <linux/stringify.h>
#include <linux/tracepoint.h>

#define GXP_TRACE_SYSTEM __stringify(TRACE_SYSTEM)

TRACE_EVENT(gxp_dma_map_sg_start,

	    TP_PROTO(int nents),

	    TP_ARGS(nents),

	    TP_STRUCT__entry(__field(int, nents)),

	    TP_fast_assign(__entry->nents = nents;),

	    TP_printk("nents = %d", __entry->nents));

TRACE_EVENT(gxp_dma_map_sg_end,

	    TP_PROTO(int nents_mapped, ssize_t size_mapped),

	    TP_ARGS(nents_mapped, size_mapped),

	    TP_STRUCT__entry(__field(int, nents_mapped)
				     __field(ssize_t, size_mapped)),

	    TP_fast_assign(__entry->nents_mapped = nents_mapped;
			   __entry->size_mapped = size_mapped;),

	    TP_printk("nents_mapped = %d, size_mapped = %ld",
		      __entry->nents_mapped, __entry->size_mapped));

TRACE_EVENT(gxp_dma_unmap_sg_start,

	    TP_PROTO(int nents),

	    TP_ARGS(nents),

	    TP_STRUCT__entry(__field(int, nents)),

	    TP_fast_assign(__entry->nents = nents;),

	    TP_printk("nents = %d", __entry->nents));

TRACE_EVENT(gxp_dma_unmap_sg_end,

	    TP_PROTO(size_t size),

	    TP_ARGS(size),

	    TP_STRUCT__entry(__field(size_t, size)),

	    TP_fast_assign(__entry->size = size;),

	    TP_printk("size = %ld", __entry->size));

TRACE_EVENT(gxp_vd_block_ready_start,

	    TP_PROTO(int vdid),

	    TP_ARGS(vdid),

	    TP_STRUCT__entry(__field(int, vdid)),

	    TP_fast_assign(__entry->vdid = vdid;),

	    TP_printk("vdid = %d", __entry->vdid));

TRACE_EVENT(gxp_vd_block_ready_end,

	    TP_PROTO(int vdid),

	    TP_ARGS(vdid),

	    TP_STRUCT__entry(__field(int, vdid)),

	    TP_fast_assign(__entry->vdid = vdid;),

	    TP_printk("vdid = %d", __entry->vdid));

TRACE_EVENT(gxp_vd_block_unready_start,

	    TP_PROTO(int vdid),

	    TP_ARGS(vdid),

	    TP_STRUCT__entry(__field(int, vdid)),

	    TP_fast_assign(__entry->vdid = vdid;),

	    TP_printk("vdid = %d", __entry->vdid));

TRACE_EVENT(gxp_vd_block_unready_end,

	    TP_PROTO(int vdid),

	    TP_ARGS(vdid),

	    TP_STRUCT__entry(__field(int, vdid)),

	    TP_fast_assign(__entry->vdid = vdid;),

	    TP_printk("vdid = %d", __entry->vdid));

TRACE_EVENT(gxp_vd_allocate_start,

	    TP_PROTO(u16 requested_cores),

	    TP_ARGS(requested_cores),

	    TP_STRUCT__entry(__field(u16, requested_cores)),

	    TP_fast_assign(__entry->requested_cores = requested_cores;),

	    TP_printk("requested_cores = %d", __entry->requested_cores));

TRACE_EVENT(gxp_vd_allocate_end,

	    TP_PROTO(int vdid),

	    TP_ARGS(vdid),

	    TP_STRUCT__entry(__field(int, vdid)),

	    TP_fast_assign(__entry->vdid = vdid;),

	    TP_printk("vdid = %d", __entry->vdid));

TRACE_EVENT(gxp_vd_release_start,

	    TP_PROTO(int vdid),

	    TP_ARGS(vdid),

	    TP_STRUCT__entry(__field(int, vdid)),

	    TP_fast_assign(__entry->vdid = vdid;),

	    TP_printk("vdid = %d", __entry->vdid));

TRACE_EVENT(gxp_vd_release_end,

	    TP_PROTO(int vdid),

	    TP_ARGS(vdid),

	    TP_STRUCT__entry(__field(int, vdid)),

	    TP_fast_assign(__entry->vdid = vdid;),

	    TP_printk("vdid = %d", __entry->vdid));

#endif /* _TRACE_GXP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
