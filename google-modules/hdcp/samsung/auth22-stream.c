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
#include <linux/kernel.h>

#include "auth22-internal.h"
#include "auth-state.h"
#include "dpcd.h"
#include "teeif.h"
#include "hdcp-log.h"

#define HDCP_DP_STREAM_NUM 1

static int do_send_rp_stream_manage(struct hdcp_link_data *lk)
{
	int ret;
	uint16_t stream_num = HDCP_DP_STREAM_NUM;
	uint8_t strm_id[HDCP_RP_MAX_STREAMID_NUM] = {0x0};
	uint8_t seq_num_m[HDCP_RP_SEQ_NUM_M_LEN];
	uint8_t k[HDCP_RP_K_LEN];
	uint8_t streamid_type[HDCP_RP_MAX_STREAMID_TYPE_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	/* set receiver id list */
	ret = teei_gen_stream_manage(stream_num, strm_id,
		seq_num_m, k, streamid_type);
	if (ret) {
		hdcp_err("teei_gen_stream_manage() failed %d\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_SEQ_NUM_M_OFFSET, seq_num_m,
			sizeof(seq_num_m));
	if (ret) {
		hdcp_err("seq_num_M send fail. ret(%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_K_OFFSET, k, sizeof(k));
	if (ret) {
		hdcp_err("k send fail. ret(%x)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_STREAM_ID_TYPE_OFFSET,
		streamid_type, stream_num * HDCP_RP_STREAMID_TYPE_LEN);
	if (ret) {
		hdcp_err("Streamid_Type send fail. ret(%x)\n", ret);
		return -EIO;
	}

	return 0;
}

static int do_recv_rp_stream_ready(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t m_prime[HDCP_RP_HMAC_M_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_MPRIME_OFFSET, m_prime,
		sizeof(m_prime));
	if (ret) {
		hdcp_err("M' recv fail. ret(%d)\n", ret);
		return -EIO;
	}

	ret = teei_verify_m_prime(m_prime, NULL, 0);
	if (ret) {
		hdcp_err("teei_verify_m_prime failed %d\n", ret);
		return -EIO;
	}

	return 0;
}

int auth22_stream_manage(struct hdcp_link_data *lk)
{
	/* Send Tx -> Rx: RepeaterAuth_Stream_Manage */
	if (do_send_rp_stream_manage(lk) < 0) {
		hdcp_err("send_rp_stream_manage fail\n");
		return -EIO;
	}

	/* HDCP spec define 110ms as min delay. But we give 110ms margin */
	msleep(220);

	/* recv Rx->Tx: RepeaterAuth_Stream_Ready message */
	if (do_recv_rp_stream_ready(lk) < 0) {
		hdcp_err("recv_rp_stream_ready fail\n");
		return -EIO;
	}

	return 0;
}
