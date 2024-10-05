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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/smc.h>
#include <asm/cacheflush.h>
#include <linux/soc/samsung/exynos-smc.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <drm/display/drm_dp_helper.h>

#include "exynos-hdcp-interface.h"

#include "auth-state.h"
#include "auth22.h"
#include "auth22-internal.h"
#include "dpcd.h"
#include "hdcp-log.h"
#include "teeif.h"

#define RECVID_WAIT_RETRY_COUNT        (5)
#define DP_RXCAPS_HDCP_CAPABLE         (0x1 << 1)
#define DP_RXCAPS_REPEATER             (0x1 << 0)
#define DP_RXCAPS_HDCP_VERSION_2       (0x2)

static struct hdcp_link_data lkd;

char *hdcp_link_st_str[] = {
	"ST_INIT",
	"ST_H0_NO_RX_ATTACHED",
	"ST_H1_TX_LOW_VALUE_CONTENT",
	"ST_A0_DETERMINE_RX_HDCP_CAP",
	"ST_A1_EXCHANGE_MASTER_KEY",
	"ST_A2_LOCALITY_CHECK",
	"ST_A3_EXCHANGE_SESSION_KEY",
	"ST_A4_TEST_REPEATER",
	"ST_A5_AUTHENTICATED",
	"ST_A6_WAIT_RECEIVER_ID_LIST",
	"ST_A7_VERIFY_RECEIVER_ID_LIST",
	"ST_A8_SEND_RECEIVER_ID_LIST_ACK",
	"ST_A9_CONTENT_STREAM_MGT",
	"ST_END",
	NULL
};

static int auth22_determine_rx_hdcp_cap(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t rxcaps[HDCP_CAPS_BYTE_LEN];

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RX_CAPS_OFFSET, rxcaps,
			sizeof(rxcaps));
	if (ret) {
		hdcp_err("check rx caps recv fail: ret(%d)\n", ret);
		return -EIO;
	}

	if (!(rxcaps[2] & DP_RXCAPS_HDCP_CAPABLE)) {
		hdcp_err("RX is not HDCP capable. rxcaps(0x%02x%02x%02x)\n",
			rxcaps[0], rxcaps[1], rxcaps[2]);
		return -EIO;
	}

	if (rxcaps[0] != DP_RXCAPS_HDCP_VERSION_2) {
		hdcp_err("RX does not support V2. rxcaps(0x%02x%02x%02x)\n",
			rxcaps[0], rxcaps[1], rxcaps[2]);
		return -EIO;
	}

	lk->is_repeater = rxcaps[2] & DP_RXCAPS_REPEATER;
	return 0;
}

int hdcp22_dplink_authenticate(void)
{
	struct hdcp_link_data *lk_data = &lkd;
	bool rp_ready = lk_data->rp_ready;

	memset(&lkd, 0, sizeof(lkd));
	if (rp_ready)
		UPDATE_LINK_STATE(lk_data, LINK_ST_A7_VERIFY_RECEIVER_ID_LIST);
	else
		UPDATE_LINK_STATE(lk_data, LINK_ST_A0_DETERMINE_RX_HDCP_CAP);

	do {
		switch (lk_data->state) {
		case LINK_ST_H1_TX_LOW_VALUE_CONTENT:
			return -EIO;
		case LINK_ST_A0_DETERMINE_RX_HDCP_CAP:
			if (auth22_determine_rx_hdcp_cap(lk_data) == 0) {
				UPDATE_LINK_STATE(lk_data, LINK_ST_A1_EXCHANGE_MASTER_KEY);
			} else {
				UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
				return -EOPNOTSUPP;
			}
			break;
		case LINK_ST_A1_EXCHANGE_MASTER_KEY:
			if (auth22_exchange_master_key(lk_data) == 0) {
				UPDATE_LINK_STATE(lk_data, LINK_ST_A2_LOCALITY_CHECK);
			} else {
				UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
				return -EAGAIN;
			}
			break;
		case LINK_ST_A2_LOCALITY_CHECK:
			if (auth22_locality_check(lk_data) == 0) {
				UPDATE_LINK_STATE(lk_data, LINK_ST_A3_EXCHANGE_SESSION_KEY);
			} else {
				UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
				return -EAGAIN;
			}
			break;
		case LINK_ST_A3_EXCHANGE_SESSION_KEY:
			if (auth22_exchange_session_key(lk_data) == 0) {
				UPDATE_LINK_STATE(lk_data, LINK_ST_A4_TEST_REPEATER);
			} else {
				UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
			}
			break;
		case LINK_ST_A4_TEST_REPEATER:
			if (lk_data->is_repeater) {
				/* if it is a repeater, verify Rcv ID list */
				UPDATE_LINK_STATE(lk_data, LINK_ST_A6_WAIT_RECEIVER_ID_LIST);
				hdcp_info("It`s repeater link !\n");
			} else {
				/* if it is not a repeater, complete authentication */
				UPDATE_LINK_STATE(lk_data, LINK_ST_A5_AUTHENTICATED);
				hdcp_info("It`s Rx link !\n");
			}
			break;
		case LINK_ST_A5_AUTHENTICATED:
			return 0;
		case LINK_ST_A6_WAIT_RECEIVER_ID_LIST:
			if (auth22_wait_for_receiver_id_list(lk_data) == 0) {
				UPDATE_LINK_STATE(lk_data, LINK_ST_A7_VERIFY_RECEIVER_ID_LIST);
			} else {
				UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
				return -EAGAIN;
			}
			break;
		case LINK_ST_A7_VERIFY_RECEIVER_ID_LIST:
			if (auth22_verify_receiver_id_list(lk_data) == 0) {
				UPDATE_LINK_STATE(lk_data, LINK_ST_A9_CONTENT_STREAM_MGT);
			} else {
				UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
				return -EAGAIN;
			}
			break;
		case LINK_ST_A9_CONTENT_STREAM_MGT:
			if (auth22_stream_manage(lk_data) == 0) {
				UPDATE_LINK_STATE(lk_data, LINK_ST_A5_AUTHENTICATED);
			} else {
				UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
			}
			break;
		default:
			UPDATE_LINK_STATE(lk_data, LINK_ST_H1_TX_LOW_VALUE_CONTENT);
			break;
		}
	} while (1);
}

int hdcp22_dplink_handle_irq(void) {
	uint8_t rxstatus = 0;

	/* check as polling mode */
	int ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &rxstatus,
		sizeof(uint8_t));
	if (ret) {
		hdcp_err("RXStatus read fail (%d)\n", ret);
		return -EIO;
	}
	hdcp_info("RxStatus: %x\n", rxstatus);

	if (HDCP_2_2_DP_RXSTATUS_LINK_FAILED(rxstatus)) {
		hdcp_info("integrity check fail.\n");
		return -EFAULT;
	} else if (HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(rxstatus)) {
		hdcp_info("reauth requested.\n");
		return -EFAULT;
	} else if (HDCP_2_2_DP_RXSTATUS_PAIRING(rxstatus)) {
		hdcp_info("pairing avaible\n");
		lkd.pairing_ready = 1;
		return 0;
	} else if (HDCP_2_2_DP_RXSTATUS_H_PRIME(rxstatus)) {
		hdcp_info("h-prime avaible\n");
		lkd.hprime_ready = 1;
		return 0;
	} else if (HDCP_2_2_DP_RXSTATUS_READY(rxstatus)) {
		hdcp_info("ready avaible\n");
		lkd.rp_ready = 1;
		hdcp_set_auth_state(HDCP2_AUTH_RP);
		return -EAGAIN;
	}

	hdcp_err("undefined RxStatus(0x%x). ignore\n", rxstatus);
	return -EINVAL;
}
