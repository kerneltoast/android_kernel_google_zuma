/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EdgeTPU usage stats header
 *
 * Copyright (C) 2020-2023 Google, Inc.
 */

#ifndef __EDGETPU_USAGE_STATS_H__
#define __EDGETPU_USAGE_STATS_H__

#include <gcip/gcip-usage-stats.h>

#include "edgetpu-internal.h"

/* The version of metric TPU KD is using currently. */
#define EDGETPU_USAGE_METRIC_VERSION GCIP_USAGE_STATS_V2

struct edgetpu_usage_stats {
	struct edgetpu_dev *etdev;
	struct gcip_usage_stats ustats;
};

void edgetpu_usage_stats_process_buffer(struct edgetpu_dev *etdev, void *buf);
void edgetpu_usage_stats_init(struct edgetpu_dev *etdev);
void edgetpu_usage_stats_exit(struct edgetpu_dev *etdev);

#endif /* __EDGETPU_USAGE_STATS_H__ */
