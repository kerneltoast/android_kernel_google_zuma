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
#ifndef __EXYNOS_HDCP2_AUTH_INTERNAL_H__
#define __EXYNOS_HDCP2_AUTH_INTERNAL_H__

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/module.h>

#define HDCP_LINK_TYPE_DP      1

int auth22_exchange_master_key(void);
int auth22_locality_check(void);
int auth22_exchange_session_key(bool is_repeater);
int auth22_wait_for_receiver_id_list(void);
int auth22_verify_receiver_id_list(void);
int auth22_stream_manage(void);

#endif
