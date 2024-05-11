/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fallback header for systems without GSA support.
 *
 * Copyright (C) 2022 Google, Inc.
 */

#ifndef __LINUX_GSA_IMAGE_AUTH_H
#define __LINUX_GSA_IMAGE_AUTH_H

#include <linux/device.h>
#include <linux/types.h>

static inline int gsa_authenticate_image(struct device *gsa, dma_addr_t img_meta,
					 phys_addr_t img_body)
{
	return 0;
}

#endif /* __LINUX_GSA_IMAGE_AUTH_H */
