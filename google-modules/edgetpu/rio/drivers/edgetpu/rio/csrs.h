/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip-dependent CSRs.
 *
 * Copyright (C) 2021 Google, Inc.
 */
enum edgetpu_csrs {
	EDGETPU_REG_CPUNS_TIMESTAMP = 0x1a01c0,
};

/* SYSREG TPU */
#define EDGETPU_SYSREG_TPU0_SHAREABILITY	0x700
#define EDGETPU_SYSREG_TPU1_SHAREABILITY	0x704
#define SHAREABLE_WRITE	(1 << 13)
#define SHAREABLE_READ	(1 << 12)
#define INNER_SHAREABLE	1
