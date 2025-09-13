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

static int auth22_determine_rx_hdcp_cap(bool *is_repeater)
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

	*is_repeater = rxcaps[2] & DP_RXCAPS_REPEATER;
	return 0;
}

static int hdcp22_dplink_repeater_auth(void)
{
	if (auth22_wait_for_receiver_id_list())
		return -EAGAIN;

	if (auth22_verify_receiver_id_list())
		return -EAGAIN;

	if (auth22_stream_manage())
		return -EIO;

	return 0;
}

static int hdcp22_dplink_authenticate(bool* second_stage_required)
{
	if (auth22_determine_rx_hdcp_cap(second_stage_required))
		return -EOPNOTSUPP;

	if (auth22_exchange_master_key())
		return -EAGAIN;

	if (auth22_locality_check())
		return -EAGAIN;

	if (auth22_exchange_session_key(*second_stage_required))
		return -EIO;

	return 0;
}

int run_hdcp2_auth(void)
{
	int ret;
	int i;
	bool second_stage_required;

	if (hdcp_get_auth_state() == HDCP2_AUTH_RP) {
		SET_HDCP_STATE_OR_RETURN(HDCP2_AUTH_PROGRESS, -EBUSY);

		ret = hdcp22_dplink_repeater_auth();
		if (ret == 0) {
			SET_HDCP_STATE_OR_RETURN(HDCP2_AUTH_DONE, -EBUSY);
			return 0;
		}

		SET_HDCP_STATE_OR_RETURN(HDCP_AUTH_IDLE, -EBUSY);
		if (ret != -EAGAIN)
			return ret;
	}

	for (i = 0; i < 3; ++i) {
		hdcp_info("HDCP22 Try (%d)...\n", i);

		SET_HDCP_STATE_OR_RETURN(HDCP2_AUTH_PROGRESS, -EBUSY);

		ret = hdcp22_dplink_authenticate(&second_stage_required);
		if (ret) {
			SET_HDCP_STATE_OR_RETURN(HDCP_AUTH_IDLE, -EBUSY);
			if (ret == -EAGAIN)
				continue;
			else
				break;
		}

		if (!second_stage_required) {
			SET_HDCP_STATE_OR_RETURN(HDCP2_AUTH_DONE, -EBUSY);
			return 0;
		}

		ret = hdcp22_dplink_repeater_auth();
		if (ret) {
			SET_HDCP_STATE_OR_RETURN(HDCP_AUTH_IDLE, -EBUSY);
			if (ret == -EAGAIN)
				continue;
			else
				break;
		}

		SET_HDCP_STATE_OR_RETURN(HDCP2_AUTH_DONE, -EBUSY);
		return 0;
	}

	return ret;
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
	} else if (HDCP_2_2_DP_RXSTATUS_READY(rxstatus)) {
		hdcp_info("ready avaible\n");
		SET_HDCP_STATE_OR_RETURN(HDCP2_AUTH_RP, -EBUSY);
		return -EAGAIN;
	}

	hdcp_err("unexpected RxStatus(0x%x). ignore\n", rxstatus);
	return -EINVAL;
}
