/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VH_MM_H
#define _VH_MM_H
#include <trace/hooks/mm.h>

void vh_ptep_clear_flush_young(void *data, bool *skip);

#endif