/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Devfreq interface for the TPU device.
 *
 * Copyright (C) 2024 Google LLC
 */
#ifndef __EDGETPU_DEVFREQ_H__
#define __EDGETPU_DEVFREQ_H__

#include <gcip/gcip-devfreq.h>

#include "edgetpu-internal.h"

/**
 * edgetpu_devfreq_create() - API to initialize devfreq for the device. Should be called on probe.
 *
 * Return:
 * * 0       - Initialization finished successfully.
 * * Error codes propagated by gcip_devfreq_create() on failure.
 */
int edgetpu_devfreq_create(struct edgetpu_dev *etdev);

/**
 * edgetpu_devfreq_destroy() - API for removing devfreq for the device.
 */
void edgetpu_devfreq_destroy(struct edgetpu_dev *etdev);

#endif /* __EDGETPU_DEVFREQ_H__*/
