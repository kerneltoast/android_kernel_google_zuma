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
#ifndef __EXYNOS_HDCP_AUTH_STATE_H__
#define __EXYNOS_HDCP_AUTH_STATE_H__

enum auth_state {
	HDCP_AUTH_RESET = (1 << 0),
	HDCP_AUTH_IDLE = (1 << 1),
	HDCP1_AUTH_PROGRESS = (1 << 2),
	HDCP1_AUTH_DONE = (1 << 3),
	HDCP2_AUTH_PROGRESS = (1 << 4),
	HDCP2_AUTH_DONE = (1 << 5),
	HDCP2_AUTH_RP = (1 << 6),
	HDCP_AUTH_ABORT = (1 << 7),
	HDCP_AUTH_SHUTDOWN = (1 << 8),
};

enum auth_state hdcp_get_auth_state(void);
const char* get_auth_state_str(uint32_t state);
int hdcp_set_auth_state(enum auth_state state);
bool is_hdcp_auth_aborted(void);

#endif
