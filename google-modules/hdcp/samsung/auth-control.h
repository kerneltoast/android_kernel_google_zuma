// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *
 * Samsung DisplayPort driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __EXYNOS_HDCP_AUTH_CONTROL_H__
#define __EXYNOS_HDCP_AUTH_CONTROL_H__

#define HDCP_SCHEDULE_DELAY_MSEC (5000)

struct hdcp_device;

int hdcp_auth_worker_init(struct hdcp_device *dev);
int hdcp_auth_worker_deinit(struct hdcp_device *dev);
void hdcp_auth_worker_schedule(struct hdcp_device *dev);

#endif
