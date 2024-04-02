// SPDX-License-Identifier: GPL-2.0-only
/* pgtable.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2023 Google LLC
 */

#include <linux/mm.h>

void vh_ptep_clear_flush_young(void *data, bool *skip)
{
	*skip = true;
}
