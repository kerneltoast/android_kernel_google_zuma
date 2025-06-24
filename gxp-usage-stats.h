/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DSP usage stats header
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_USAGE_STATS_H__
#define __GXP_USAGE_STATS_H__

#include <gcip/gcip-usage-stats.h>

#include "gxp-internal.h"

#define GXP_USAGE_METRIC_VERSION GCIP_USAGE_STATS_V2

/* Stores the usage of DSP which is collected from the GET_USAGE KCI metrics. */
struct gxp_usage_stats {
	struct gxp_dev *gxp;
	struct gcip_usage_stats ustats;
};

/* Parses the buffer from the get_usage KCI and updates the usage_stats of @gxp. */
void gxp_usage_stats_process_buffer(struct gxp_dev *gxp, void *buf);

/* Initializes the usage_stats of gxp to process the get_usage KCI. */
void gxp_usage_stats_init(struct gxp_dev *gxp);

/* Cleans up the usage_stats of gxp which is initialized by `gxp_usage_stats_init`. */
void gxp_usage_stats_exit(struct gxp_dev *gxp);

#endif /* __GXP_USAGE_STATS_H__ */
