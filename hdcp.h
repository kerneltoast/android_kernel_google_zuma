/* SPDX-License-Identifier: GPL-2.0-only
 *
 * HDCP platform device headerpanel common utility header
 *
 * Copyright (c) 2022 Google LLC
 */
#ifndef __EXYNOS_HDCP_H__
#define __EXYNOS_HDCP_H__

#include <linux/types.h>

struct hdcp_device {
	struct device *dev;
	struct delayed_work hdcp_work;

	ktime_t connect_time;

	/* HDCP Telemetry */
	uint32_t hdcp2_success_count;
	uint32_t hdcp2_fallback_count;
	uint32_t hdcp2_fail_count;
	uint32_t hdcp1_success_count;
	uint32_t hdcp1_fail_count;
	uint32_t hdcp0_count;
};

#endif
