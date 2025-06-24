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
#include <drm/display/drm_dp_helper.h>
#include <linux/in.h>
#include <linux/kernel.h>

#include "auth22-internal.h"
#include "auth-state.h"
#include "dpcd.h"
#include "teeif.h"
#include "hdcp-log.h"

#define DEPTH_SHIFT     (9)
#define DEPTH_MASK      (0x7)
#define DEV_COUNT_SHIFT (4)
#define DEV_COUNT_MASK  (0x1F)
#define DEV_EXD_SHIFT   (3)
#define DEV_EXD_MASK    (0x1)
#define CASCADE_EXD_SHIFT       (2)
#define CASCADE_EXD_MASK        (0x1)
#define HDCP20_DOWN_SHIFT       (1)
#define HDCP20_DOWN_MASK        (0x1)
#define HDCP1X_DOWN_SHIFT       (0)
#define HDCP1X_DOWN_MASK        (0x1)

typedef struct rxinfo {
	uint8_t depth;
	uint8_t dev_count;
	uint8_t max_dev_exd;
	uint8_t max_cascade_exd;
	uint8_t hdcp20_downstream;
	uint8_t hdcp1x_downstream;
} rxinfo_t;

static void rxinfo_convert_arr2st(uint8_t *arr, struct rxinfo *st)
{
	uint16_t rxinfo_val;

	memcpy((uint8_t *)&rxinfo_val, arr, sizeof(rxinfo_val));
	rxinfo_val = htons(rxinfo_val);

	st->depth = (rxinfo_val >> DEPTH_SHIFT) & DEPTH_MASK;
	st->dev_count = (rxinfo_val >> DEV_COUNT_SHIFT) & DEV_COUNT_MASK;
	st->max_dev_exd = (rxinfo_val >> DEV_EXD_SHIFT) & DEV_EXD_MASK;
	st->max_cascade_exd = (rxinfo_val >> CASCADE_EXD_SHIFT) & CASCADE_EXD_MASK;
	st->hdcp20_downstream = (rxinfo_val >> HDCP20_DOWN_SHIFT) & HDCP20_DOWN_MASK;
	st->hdcp1x_downstream = (rxinfo_val >> HDCP1X_DOWN_SHIFT) & HDCP1X_DOWN_MASK;
}

static int cal_rcvid_list_size(struct rxinfo *st)
{
	return HDCP_RCV_ID_LEN * st->dev_count;
}

int auth22_wait_for_receiver_id_list(void)
{
	int i = 0;
	int ret;
	uint8_t status = 0;

	/* HDCP spec is 3 sec. but we give margin 110ms */
	while (i < 30) {
		/* check abort state firstly,
		 * if session is abored by Rx, Tx stops Authentication process
		 */
		if (is_hdcp_auth_aborted())
			return -ECANCELED;

		/* check as polling mode */
		ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &status,
			sizeof(uint8_t));

		if (ret) {
			hdcp_err("RXStatus read fail (%d)\n", ret);
			return ret;
		}

		if (HDCP_2_2_DP_RXSTATUS_READY(status))
			return 0;

		if (status) {
			hdcp_err("Unexpected RxStatus (%x)\n", status);
			return -EIO;
		}

		msleep(110);
		i++;
	}

	hdcp_err("receiver ID list timeout(%dms)\n", (110 * i));
	return -ETIMEDOUT;
}

int auth22_verify_receiver_id_list(void) {
	int ret;
	uint8_t rx_info_arr[HDCP_RP_RX_INFO_LEN];
	rxinfo_t rx_info;
	uint8_t seq_num_v[HDCP_RP_SEQ_NUM_V_LEN];
	uint8_t v_prime[HDCP_RP_HMAC_V_LEN / 2];
	uint8_t rcvid_list[HDCP_RP_RCVID_LIST_LEN];
	uint8_t v[HDCP_RP_HMAC_V_LEN / 2];
	uint8_t valid;

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RXINFO_OFFSET, rx_info_arr,
		sizeof(rx_info_arr));
	if (ret) {
		hdcp_err("rx_info rcv fail: ret(%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_SEQ_NUM_V_OFFSET, seq_num_v,
		sizeof(seq_num_v));
	if (ret) {
		hdcp_err("seq_num_v rcv fail: ret(%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_VPRIME_OFFSET, v_prime,
		sizeof(v_prime));
	if (ret) {
		hdcp_err("v_prime rcv fail: ret(%d)\n", ret);
		return -EIO;
	}

	rxinfo_convert_arr2st(rx_info_arr, &rx_info);

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RECV_ID_LIST_OFFSET, rcvid_list,
		cal_rcvid_list_size(&rx_info));
	if (ret) {
		hdcp_err("rcvid_list rcv fail: ret(%d)\n", ret);
		return -EIO;
	}

	if (rx_info.dev_count >= HDCP_RCV_DEVS_COUNT_MAX ||
	    rx_info.max_dev_exd) {
		hdcp_err("rcvid_list rcv dev count exceeded\n");
		return -EIO;
	}

	if (rx_info.max_cascade_exd) {
		hdcp_err("rcvid_list rcv cascade count exceeded\n");
		return -EIO;
	}

	if (rx_info.hdcp20_downstream || rx_info.hdcp1x_downstream) {
		hdcp_err("rcvid_list rcv legacy device detected (%d, %d)\n",
			rx_info.hdcp20_downstream, rx_info.hdcp1x_downstream);
		return -EIO;
	}

	/* set receiver id list */
	ret = teei_set_rcvlist_info(rx_info_arr, seq_num_v, v_prime, rcvid_list,
		v, &valid);
	if (ret) {
		hdcp_err("teei_set_rcvid_list() failed %d\n", ret);
		return -EIO;
	}

	if (valid == 0) {
		hdcp_err("vprime verification failed\n");
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_V_OFFSET, v,
		HDCP_RP_HMAC_V_LEN / 2);
	if (ret) {
		hdcp_err("V send fail: ret(%d)\n", ret);
		return -EIO;
	}

	return 0;
}
