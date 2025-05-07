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

#include <linux/kernel.h>
#include <linux/errno.h>

#include <drm/display/drm_dp_helper.h>

#include "exynos-hdcp-interface.h"

#include "auth-state.h"
#include "auth13.h"
#include "dpcd.h"
#include "hdcp-log.h"
#include "teeif.h"

#define HDCP_R0_SIZE 2
#define HDCP_BKSV_SIZE 5
#define HDCP_AN_SIZE 8
#define HDCP_AKSV_SIZE 5

#define V_READ_RETRY_CNT 3
#define RI_READ_RETRY_CNT 3
#define RI_AVAILABLE_WAITING 2
#define RI_DELAY 100
#define REPEATER_READY_MAX_WAIT_DELAY 5000

#define MAX_CASCADE_EXCEEDED (0x00000800)
#define MAX_DEVS_EXCEEDED (0x00000080)
#define BKSV_LIST_FIFO_SIZE (15)

static int compare_rprime(void)
{
	uint8_t bstatus, ri_retry_cnt = 0;
	uint16_t rprime;
	int ret;

	usleep_range(RI_DELAY * 1000, RI_DELAY * 1000 + 1);

	ret = hdcp_dplink_recv(DP_AUX_HDCP_BSTATUS, &bstatus,
		sizeof(bstatus));
	if (ret || !(bstatus & DP_BSTATUS_R0_PRIME_READY)) {
		hdcp_err("BSTATUS read err ret(%d) bstatus(%d)\n",
			ret, bstatus);
		return -EIO;
	}

	hdcp_info("R0-Prime is ready in HDCP Receiver\n");
	do {
		ret = hdcp_dplink_recv(DP_AUX_HDCP_RI_PRIME, (uint8_t*)&rprime,
			HDCP_R0_SIZE);
		if (!ret) {
			ret = teei_verify_r_prime(rprime);
			if (!ret)
				return 0;

			hdcp_err("RPrime verification fails (%d)\n", ret);
		} else {
			hdcp_err("RPrime read fails (%d)\n", ret);
		}

		ri_retry_cnt++;
		usleep_range(RI_DELAY * 1000, RI_DELAY * 1000 + 1);
	}
	while (ri_retry_cnt < RI_READ_RETRY_CNT && !is_hdcp_auth_aborted());

	return -EFAULT;
}

static int read_ksv_list(u8* hdcp_ksv, u32 len)
{
	uint32_t read_len = len < BKSV_LIST_FIFO_SIZE ?
		len : BKSV_LIST_FIFO_SIZE;

	return hdcp_dplink_recv(DP_AUX_HDCP_KSV_FIFO, hdcp_ksv, read_len) ?
		-EIO : read_len;
}

int hdcp13_dplink_repeater_auth(void)
{
	uint8_t ksv_list[HDCP_KSV_MAX_LEN];
	uint8_t *ksv_list_ptr = ksv_list;
	uint16_t binfo;
	uint8_t v_read_retry_cnt = 0;
	uint8_t vprime[HDCP_SHA1_SIZE];
	ktime_t start_time_ns;
	int64_t waiting_time_ms;
	uint8_t bstatus;
	uint32_t ksv_len, bytes_read;
	int ret;

	hdcp_info("Start HDCP Repeater Authentication!!!\n");

	// Step0-1. Poll BStatus Ready
	start_time_ns = ktime_get();
	do {
		usleep_range(RI_AVAILABLE_WAITING * 1000, RI_AVAILABLE_WAITING * 1000 + 1);
		waiting_time_ms = (s64)((ktime_get() - start_time_ns) / 1000000);
		if ((waiting_time_ms >= REPEATER_READY_MAX_WAIT_DELAY) ||
		     is_hdcp_auth_aborted()) {
			hdcp_err("Not repeater ready in RX part %lld\n",
				waiting_time_ms);
			return -EINVAL;
		}

		ret = hdcp_dplink_recv(DP_AUX_HDCP_BSTATUS, &bstatus,
				sizeof(bstatus));
		if (ret) {
			hdcp_err("Read BSTATUS failed (%d)\n", ret);
			return -EIO;
		}
	} while (!(bstatus & DP_BSTATUS_READY));
	hdcp_info("Ready HDCP RX Repeater!!!\n");

	if (is_hdcp_auth_aborted())
		return -EINVAL;

	ret = hdcp_dplink_recv(DP_AUX_HDCP_BINFO, (uint8_t*)&binfo, HDCP_BINFO_SIZE);
	if (ret) {
		hdcp_err("Read BINFO failed (%d)\n", ret);
		return -EIO;
	}

	if (binfo & MAX_DEVS_EXCEEDED) {
		hdcp_err("Max Devs Exceeded\n");
		return -EIO;
	}

	if (binfo & MAX_CASCADE_EXCEEDED) {
		hdcp_err("Max Cascade Exceeded\n");
		return -EIO;
	}

	ksv_len = (binfo & HDCP_BINFO_DEVS_COUNT_MAX) * HDCP_KSV_SIZE;
	while (ksv_len != 0) {
		bytes_read = read_ksv_list(ksv_list_ptr, ksv_len);
		if (bytes_read < 0) {
			hdcp_err("Read KSV failed (%d)\n", bytes_read);
			return -EIO;
		}
		ksv_len -= bytes_read;
		ksv_list_ptr += bytes_read;
	}

	do {
		ret = hdcp_dplink_recv(DP_AUX_HDCP_V_PRIME(0), vprime,
			HDCP_SHA1_SIZE);
		if (!ret) {
			ret = teei_verify_v_prime(binfo, ksv_list,
				ksv_list_ptr - ksv_list, vprime);
			if (!ret) {
				hdcp_info("Done 2nd Authentication!!!\n");
				return 0;
			}

			hdcp_err("Vprime verify failed (%d)\n", ret);
		} else
			hdcp_err("Vprime read failed (%d)\n", ret);

		v_read_retry_cnt++;
	} while(v_read_retry_cnt < V_READ_RETRY_CNT && !is_hdcp_auth_aborted());

	hdcp_err("2nd Auth fail!!!\n");
	return -EIO;
}

int hdcp13_dplink_authenticate(bool* second_stage_required)
{
	uint64_t aksv, bksv, an;
	uint8_t bcaps;
	int ret;

	hdcp_info("Start SW Authentication\n");

	aksv = bksv = an = 0;

	ret = hdcp_dplink_recv(DP_AUX_HDCP_BCAPS, &bcaps, sizeof(bcaps));
	if (ret) {
		hdcp_err("BCaps Read failure (%d)\n", ret);
		return -EOPNOTSUPP;
	}

	if (!(bcaps & DP_BCAPS_HDCP_CAPABLE)) {
		hdcp_err("HDCP13 is not supported\n");
		return -EOPNOTSUPP;
	}

	ret = hdcp_dplink_recv(DP_AUX_HDCP_BKSV, (uint8_t*)&bksv, HDCP_BKSV_SIZE);
	if (ret) {
		hdcp_err("Read Bksv failed (%d)\n", ret);
		return -EIO;
	}

	ret = teei_ksv_exchange(bksv, bcaps & DP_BCAPS_REPEATER_PRESENT,
		&aksv, &an);
	if (ret) {
		hdcp_err("Ksv exchange failed (%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_AUX_HDCP_AN, (uint8_t*)&an, HDCP_AN_SIZE);
	if (ret) {
		hdcp_err("Write AN failed (%d)\n", ret);
		return -EIO;
	}
	ret = hdcp_dplink_send(DP_AUX_HDCP_AKSV, (uint8_t*)&aksv, HDCP_AKSV_SIZE);
	if (ret) {
		hdcp_err("Write AKSV failed (%d)\n", ret);
		return -EIO;
	}

	if (compare_rprime() != 0) {
		hdcp_err("R0 is not same\n");
		return -EIO;
	}

	hdcp_info("Done 1st Authentication\n");
	*second_stage_required = bcaps & DP_BCAPS_REPEATER_PRESENT;
	return 0;
}

int hdcp13_dplink_handle_irq(void)
{
	uint8_t bstatus;

	if (hdcp_get_auth_state() != HDCP1_AUTH_DONE) {
		hdcp_err("Ignoring IRQ during auth\n");
		return 0;
	}

	hdcp_dplink_recv(DP_AUX_HDCP_BSTATUS, &bstatus, sizeof(bstatus));

	if (bstatus & DP_BSTATUS_LINK_FAILURE ||
	    bstatus & DP_BSTATUS_REAUTH_REQ) {
		hdcp_err("Resetting link and encryption\n");
		return -EFAULT;
	}

	hdcp_err("unexpected BStatus(0x%x). ignore\n", bstatus);
	return 0;
}
