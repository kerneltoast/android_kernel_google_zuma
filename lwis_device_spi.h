/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Google LWIS SPI Device Driver
 *
 * Copyright (c) 2023 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LWIS_DEVICE_SPI_H_
#define LWIS_DEVICE_SPI_H_

#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include "lwis_device.h"

/*
 *  struct lwis_spi_device
 *  "Derived" lwis_device struct, with added SPI related elements.
 */
struct lwis_spi_device {
	struct lwis_device base_dev;
	struct spi_device *spi;
	struct mutex spi_lock;
};

int lwis_spi_device_init(void);
int lwis_spi_device_deinit(void);

#endif /* LWIS_DEVICE_SPI_H_ */
