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
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "exynos-hdcp-interface.h"

#include "dpcd.h"
#include "hdcp-log.h"

static void (*pdp_hdcp_update_cp)(u32 drm_cp_status);
static int (*pdp_dpcd_read_for_hdcp22)(u32 address, u32 length, u8 *data);
static int (*pdp_dpcd_write_for_hdcp22)(u32 address, u32 length, u8 *data);

void hdcp_dplink_update_cp(uint32_t drm_cp_status) {
	pdp_hdcp_update_cp(drm_cp_status);
}

int hdcp_dplink_recv(uint32_t addr, uint8_t *data, uint32_t size)
{
	return pdp_dpcd_read_for_hdcp22(addr, size, data);
}

int hdcp_dplink_send(uint32_t addr, uint8_t *data, uint32_t size)
{
	return pdp_dpcd_write_for_hdcp22(addr, size, data);
}

void dp_register_func_for_hdcp22(void (*func0)(u32 en), int (*func1)(u32 address, u32 length, u8 *data), int (*func2)(u32 address, u32 length, u8 *data))
{
	pdp_hdcp_update_cp = func0;
	pdp_dpcd_read_for_hdcp22 = func1;
	pdp_dpcd_write_for_hdcp22 = func2;
}
EXPORT_SYMBOL_GPL(dp_register_func_for_hdcp22);
