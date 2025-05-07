/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EdgeTPU thermal management header.
 *
 * Copyright (C) 2020-2023 Google LLC
 */
#ifndef __EDGETPU_THERMAL_H__
#define __EDGETPU_THERMAL_H__

#include "edgetpu-internal.h"

#define EDGETPU_COOLING_NAME "tpu-cooling"
#define EDGETPU_COOLING_TYPE "tpu_cooling"

int edgetpu_thermal_create(struct edgetpu_dev *etdev);
void edgetpu_thermal_destroy(struct edgetpu_dev *etdev);
int edgetpu_thermal_set_rate(struct edgetpu_dev *etdev, unsigned long rate);

#endif /* __EDGETPU_THERMAL_H__ */
