/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Platform device driver for the Google Edge TPU ML accelerator.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __RIO_PLATFORM_H__
#define __RIO_PLATFORM_H__

#include "edgetpu-internal.h"
#include "edgetpu-mobile-platform.h"

#define to_rio_dev(etdev) container_of(to_mobile_dev(etdev), struct rio_platform_dev, mobile_dev)

struct rio_platform_dev {
	struct edgetpu_mobile_platform_dev mobile_dev;
};

#endif /* __RIO_PLATFORM_H__ */
