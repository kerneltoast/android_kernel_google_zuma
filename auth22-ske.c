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

static int do_send_ske_send_eks(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t type = 0x00;
	uint8_t edkey_ks[HDCP_AKE_MKEY_BYTE_LEN];
	uint8_t riv[HDCP_RTX_BYTE_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = teei_generate_riv(riv, sizeof(riv));
	if (ret) {
		hdcp_err("teei_generate_riv failed (%d)\n", ret);
		return -EIO;
	}

	ret = teei_generate_skey(HDCP_LINK_TYPE_DP, edkey_ks,
		HDCP_SKE_SKEY_LEN, 0);
	if (ret) {
		hdcp_err("teei_generate_skey failed (%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_EDKEY_KS_OFFSET, edkey_ks,
		HDCP_AKE_MKEY_BYTE_LEN);
	if (ret) {
		hdcp_err("edkey_ks send fail. ret(%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_RIV_OFFSET, riv, sizeof(riv));
	if (ret) {
		hdcp_err("riv send fail. ret(%x)\n", ret);
		return -EIO;
	}

	if (lk->is_repeater)
		return 0;

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_STREAM_TYPE_OFFSET, &type,
		sizeof(type));
	if (ret) {
		hdcp_err("type send fail: %x\n", ret);
		return -EIO;
	}

	return 0;
}

int auth22_exchange_session_key(struct hdcp_link_data *lk)
{
	/* Send Tx -> Rx: SKE_Send_Eks */
	if (do_send_ske_send_eks(lk) < 0) {
		hdcp_err("send_ske_send_eks fail\n");
		return -EIO;
	}

	return 0;
}
