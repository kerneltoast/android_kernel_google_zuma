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
#ifndef __EXYNOS_HDCP_INTERFACE_H__
#define __EXYNOS_HDCP_INTERFACE_H__

/* Displayport */
enum dp_state {
	DP_DISCONNECT, /* HPD off */
	DP_CONNECT, /* HPD on */
	DP_PHYSICAL_DISCONNECT,
	DP_SHUTDOWN, /* Handle Reboot */
};

void hdcp_dplink_connect_state(enum dp_state state);
void hdcp_dplink_handle_irq(void);
void dp_register_func_for_hdcp22(void (*func0)(u32 en), int (*func1)(u32 address, u32 length, u8 *data), int (*func2)(u32 address, u32 length, u8 *data));

#endif
