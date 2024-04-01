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

extern char *hdcp_link_st_str[];

typedef enum hdcp_tx_hdcp_link_state {
	LINK_ST_INIT = 0,
	LINK_ST_H0_NO_RX_ATTACHED,
	LINK_ST_H1_TX_LOW_VALUE_CONTENT,
	LINK_ST_A0_DETERMINE_RX_HDCP_CAP,
	LINK_ST_A1_EXCHANGE_MASTER_KEY,
	LINK_ST_A2_LOCALITY_CHECK,
	LINK_ST_A3_EXCHANGE_SESSION_KEY,
	LINK_ST_A4_TEST_REPEATER,
	LINK_ST_A5_AUTHENTICATED,
	LINK_ST_A6_WAIT_RECEIVER_ID_LIST,
	LINK_ST_A7_VERIFY_RECEIVER_ID_LIST,
	LINK_ST_A8_SEND_RECEIVER_ID_LIST_ACK,
	LINK_ST_A9_CONTENT_STREAM_MGT,
	LINK_ST_END
} hdcp_tx_hdcp_link_state;

#define UPDATE_LINK_STATE(link, st)             do { \
        hdcp_info("HDCP Link: %s -> %s\n", hdcp_link_st_str[link->state], hdcp_link_st_str[st]); \
        link->state = st; \
        } while (0)

struct hdcp_link_data {
	uint32_t state;
	bool is_repeater;
	bool is_aborted;
	bool is_stored_km;
	bool pairing_ready;
	bool hprime_ready;
	bool rp_ready;
};

int auth22_exchange_master_key(struct hdcp_link_data *lk);
int auth22_locality_check(struct hdcp_link_data *lk);
int auth22_exchange_session_key(struct hdcp_link_data *lk);
int auth22_wait_for_receiver_id_list(struct hdcp_link_data *lk);
int auth22_verify_receiver_id_list(struct hdcp_link_data *lk);
int auth22_stream_manage(struct hdcp_link_data *lk);

#endif
