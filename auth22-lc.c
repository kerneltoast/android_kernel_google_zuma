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

#define MAX_LC_RETRY 10

static int do_send_lc_init(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t rn[HDCP_RTX_BYTE_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = teei_gen_rn(rn, sizeof(rn));
	if (ret) {
		hdcp_err("teei_gen_rn failed (%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_RN_OFFSET, rn, sizeof(rn));
	if (ret) {
		hdcp_err("rn send fail: ret(%d)\n", ret);
		return -EIO;
	}

	return 0;
}

static int do_recv_lc_send_l_prime(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t lprime[HDCP_HMAC_SHA256_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_LPRIME_OFFSET, lprime,
		sizeof(lprime));
	if (ret) {
		hdcp_err("l_prime recv fail. ret(%d)\n", ret);
		return -EIO;
	}

	ret = teei_compare_lc_hmac(lprime, sizeof(lprime));
	if (ret) {
		hdcp_err("teei_compare_lc_hmac failed (%d).\n", ret);
		return -EIO;
	}

	return 0;
}

int auth22_locality_check(struct hdcp_link_data *lk)
{
	int i;

	for (i = 0; i < MAX_LC_RETRY; i++) {
		/* send Tx -> Rx: LC_init */
		if (do_send_lc_init(lk) < 0) {
			hdcp_err("send_lc_init fail\n");
			return -EIO;
		}

		/* wait until max dealy */
		msleep(16);

		/* recv Rx -> Tx: LC_Send_L_Prime */
		if (do_recv_lc_send_l_prime(lk) < 0) {
			hdcp_err("recv_lc_send_l_prime fail\n");
			/* retry */
			continue;
		} else {
			hdcp_debug("LC success. retryed(%d)\n", i);
			break;
		}
	}

	if (i == MAX_LC_RETRY) {
		hdcp_err("LC check fail. exceed retry count(%d)\n", i);
		return -EFAULT;
	}

	return 0;
}
