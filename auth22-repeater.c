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

#define DEV_COUNT_SHIFT        (4)
#define DEV_COUNT_MASK (0x1F)

static int cal_rcvid_list_size(uint8_t *rxinfo)
{
	uint8_t dev_count;
	uint16_t rxinfo_val;

	memcpy((uint8_t *)&rxinfo_val, rxinfo, sizeof(rxinfo_val));
	rxinfo_val = htons(rxinfo_val);
	dev_count = (rxinfo_val >> DEV_COUNT_SHIFT) & DEV_COUNT_MASK;
	return HDCP_RCV_ID_LEN * dev_count;
}

int auth22_wait_for_receiver_id_list(struct hdcp_link_data *lk)
{
	int i = 0;
	int ret;
	uint8_t status = 0;

	/* HDCP spec is 5 sec */
	while (i < 50) {
		/* check abort state firstly,
		 * if session is abored by Rx, Tx stops Authentication process
		 */
		if (is_hdcp_auth_aborted())
			return -ECANCELED;

		/* received from CP_IRQ */
		if (lk->rp_ready) {
			/* reset flag */
			lk->rp_ready = 0;
			return 0;
		}

		/* check as polling mode */
		ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &status,
			sizeof(uint8_t));
		hdcp_info("RxStatus: %x\n", status);

		if (ret == 0 && HDCP_2_2_DP_RXSTATUS_READY(status)) {
			/* reset flag */
			lk->rp_ready = 0;
			return 0;
		}

		msleep(110);
		i++;
	}

	hdcp_err("receiver ID list timeout(%dms)\n", (110 * i));
	return -ETIMEDOUT;
}

int auth22_verify_receiver_id_list(struct hdcp_link_data *lk) {
	int ret;
	uint8_t rx_info[HDCP_RP_RX_INFO_LEN];
	uint8_t seq_num_v[HDCP_RP_SEQ_NUM_V_LEN];
	uint8_t v_prime[HDCP_RP_HMAC_V_LEN / 2];
	uint8_t rcvid_list[HDCP_RP_RCVID_LIST_LEN];
	uint8_t v[HDCP_RP_HMAC_V_LEN / 2];
	uint8_t valid;

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RXINFO_OFFSET, rx_info,
		sizeof(rx_info));
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

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RECV_ID_LIST_OFFSET, rcvid_list,
		cal_rcvid_list_size(rx_info));
	if (ret) {
		hdcp_err("rcvid_list rcv fail: ret(%d)\n", ret);
		return -EIO;
	}

	/* set receiver id list */
	ret = teei_set_rcvlist_info(rx_info, seq_num_v, v_prime, rcvid_list,
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
