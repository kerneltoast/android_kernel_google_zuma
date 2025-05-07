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
#ifndef __EXYNOS_HDCP1_AUTH_H__
#define __EXYNOS_HDCP1_AUTH_H__

int hdcp13_dplink_authenticate(bool* second_stage_required);
int hdcp13_dplink_repeater_auth(void);
int hdcp13_dplink_handle_irq(void);

#endif
