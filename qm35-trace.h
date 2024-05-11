/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file is part of the QM35 UCI stack for linux.
 *
 * Copyright (c) 2021 Qorvo US, Inc.
 *
 * This software is provided under the GNU General Public License, version 2
 * (GPLv2), as well as under a Qorvo commercial license.
 *
 * You may choose to use this software under the terms of the GPLv2 License,
 * version 2 ("GPLv2"), as published by the Free Software Foundation.
 * You should have received a copy of the GPLv2 along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * This program is distributed under the GPLv2 in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GPLv2 for more
 * details.
 *
 * If you cannot meet the requirements of the GPLv2, you may not use this
 * software for any purpose without first obtaining a commercial license from
 * Qorvo.
 * Please contact Qorvo to inquire about licensing terms.
 *
 * QM35 trace
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qm35

#if !defined(_QM35_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _QM35_TRACE_H

#include <linux/tracepoint.h>

#include "hsspi.h"

#define show_work_type(type)                            \
	__print_symbolic(type,                          \
			 {                              \
				 HSSPI_WORK_TX,         \
				 "TX",                  \
			 },                             \
			 {                              \
				 HSSPI_WORK_COMPLETION, \
				 "COMPLETION",          \
			 },                             \
			 {                              \
				 -1,                    \
				 "no",                  \
			 })

TRACE_DEFINE_ENUM(HSSPI_WORK_TX);
TRACE_DEFINE_ENUM(HSSPI_WORK_COMPLETION);

TRACE_EVENT(hsspi_get_work, TP_PROTO(const struct device *dev, int type),
	    TP_ARGS(dev, type),
	    TP_STRUCT__entry(__string(dev, dev_name(dev)) __field(int, type)),
	    TP_fast_assign(__assign_str(dev, dev_name(dev));
			   __entry->type = type;),
	    TP_printk("[%s]: %s work", __get_str(dev),
		      show_work_type(__entry->type)));

#define show_hsspi_state(state)                 \
	__print_symbolic(state,                 \
			 {                      \
				 HSSPI_RUNNING, \
				 "running",     \
			 },                     \
			 {                      \
				 HSSPI_ERROR,   \
				 "error",       \
			 },                     \
			 {                      \
				 HSSPI_STOPPED, \
				 "stopped",     \
			 })

TRACE_DEFINE_ENUM(HSSPI_RUNNING);
TRACE_DEFINE_ENUM(HSSPI_ERROR);
TRACE_DEFINE_ENUM(HSSPI_STOPPED);

TRACE_EVENT(hsspi_is_txrx_waiting,
	    TP_PROTO(const struct device *dev, bool is_empty,
		     enum hsspi_state state),
	    TP_ARGS(dev, is_empty, state),
	    TP_STRUCT__entry(__string(dev, dev_name(dev))
				     __field(bool, is_empty)
					     __field(enum hsspi_state, state)),
	    TP_fast_assign(__assign_str(dev, dev_name(dev));
			   __entry->is_empty = is_empty;
			   __entry->state = state;),
	    TP_printk("[%s]: is_empty: %d state: %s", __get_str(dev),
		      __entry->is_empty, show_hsspi_state(__entry->state)));

#define STC_ENTRY(header)                                  \
	__field(u8, header##flags) __field(u8, header##ul) \
		__field(u16, header##length)
#define STC_ASSIGN(header, var)              \
	__entry->header##flags = var->flags; \
	__entry->header##ul = var->ul;       \
	__entry->header##length = var->length;
#define STC_FMT "flags:0x%hhx ul:%hhd len:%hd"
#define STC_ARG(header) \
	__entry->header##flags, __entry->header##ul, __entry->header##length

TRACE_EVENT(hsspi_spi_xfer,
	    TP_PROTO(const struct device *dev, const struct stc_header *host,
		     struct stc_header *soc, int ret),
	    TP_ARGS(dev, host, soc, ret),
	    TP_STRUCT__entry(__string(dev, dev_name(dev)) STC_ENTRY(host)
				     STC_ENTRY(soc) __field(int, ret)),
	    TP_fast_assign(__assign_str(dev, dev_name(dev));
			   STC_ASSIGN(host, host); STC_ASSIGN(soc, soc);
			   __entry->ret = ret;),
	    TP_printk("[%s]: host " STC_FMT " | soc " STC_FMT " rc=%d",
		      __get_str(dev), STC_ARG(host), STC_ARG(soc),
		      __entry->ret));

#endif /* _QM35_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
// clang-format off
#define TRACE_INCLUDE_FILE qm35-trace
// clang-format on
#include <trace/define_trace.h>
