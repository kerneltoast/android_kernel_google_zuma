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

static int do_send_ake_init(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t rtx[HDCP_AKE_RTX_BYTE_LEN];
	uint8_t txcaps[HDCP_CAPS_BYTE_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	/* Generate rtx */
	ret = teei_gen_rtx(HDCP_LINK_TYPE_DP, rtx, sizeof(rtx),
		txcaps, sizeof(txcaps));
	if (ret) {
		hdcp_err("teei_gen_rtx failed (%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_RTX_OFFSET, rtx, sizeof(rtx));
	if (ret) {
		hdcp_err("rtx send fail: ret(%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_TXCAPS_OFFSET, txcaps,
		sizeof(txcaps));
	if (ret) {
		hdcp_err("txcaps send fail: ret(%d)\n", ret);
		return -EIO;
	}

	return 0;
}

static int do_recv_ake_send_cert(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t cert[HDCP_RX_CERT_LEN + HDCP_RRX_BYTE_LEN + HDCP_CAPS_BYTE_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_CERT_RX_OFFSET, cert,
		sizeof(cert));
	if (ret) {
		hdcp_err("read cert fail. ret(%d)\n", ret);
		return -EIO;
	}

	ret = teei_verify_cert(cert, HDCP_RX_CERT_LEN,
		&cert[HDCP_RX_CERT_LEN], HDCP_RRX_BYTE_LEN,
		&cert[HDCP_RX_CERT_LEN + HDCP_RRX_BYTE_LEN], HDCP_CAPS_BYTE_LEN);
	if (ret) {
		hdcp_err("teei_verify_cert failed (%d)\n", ret);
		return -EIO;
	}

	return 0;
}

static int do_send_ake_nostored_km(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t ekpub_km[HDCP_AKE_ENCKEY_BYTE_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = teei_generate_master_key(HDCP_LINK_TYPE_DP, ekpub_km, sizeof(ekpub_km));
	if (ret) {
		hdcp_err("teei_generate_master_key failed (%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_EKPUB_KM_OFFSET, ekpub_km,
		sizeof(ekpub_km));
	if (ret) {
		hdcp_err("ekpub_km send fail: ret(%d)\n", ret);
		return -EIO;
	}

	lk->is_stored_km = false;
	return 0;
}

static int do_send_ake_restore_km(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t ekh_mkey[HDCP_AKE_EKH_MKEY_BYTE_LEN];
	uint8_t m[HDCP_AKE_M_BYTE_LEN];
	int found_km;

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = teei_get_pairing_info(ekh_mkey, HDCP_AKE_EKH_MKEY_BYTE_LEN,
		m, HDCP_AKE_M_BYTE_LEN, &found_km);
	if (ret) {
		hdcp_err("teei_get_pairing_info failed (%d)\n", ret);
		return -EIO;
	}
	if (!found_km) {
		hdcp_info("master key is not stored\n");
		return do_send_ake_nostored_km(lk);
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_EKH_KM_WR_OFFSET, ekh_mkey,
		HDCP_AKE_EKH_MKEY_BYTE_LEN);
	if (ret) {
		hdcp_err("ekh_km send fail: ret(%d)\n", ret);
		return -EIO;
	}

	ret = hdcp_dplink_send(DP_HDCP_2_2_REG_M_OFFSET, m,
		HDCP_AKE_M_BYTE_LEN);
	if (ret) {
		hdcp_err("msg_m send fail: ret(%d)\n", ret);
		return -EIO;
	}

	lk->is_stored_km = true;
	return 0;
}

static int check_h_prime_ready(struct hdcp_link_data *lk)
{
	int i = 0;
	int ret;
	uint8_t status = 0;

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	msleep(110);

	/* HDCP spec is 1 sec. but we give margin 110ms */
	while (i < 10) {
		/* check abort state firstly,
		 * if session is abored by Rx, Tx stops Authentication process
		 */
		if (is_hdcp_auth_aborted())
			return -ECANCELED;

		/* received from CP_IRQ */
		if (lk->hprime_ready) {
			/* reset flag */
			lk->hprime_ready = 0;
			return 0;
		}

		/* check as polling mode */
		ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &status,
			sizeof(uint8_t));
		hdcp_info("RxStatus: %x\n", status);

		if (ret == 0 && HDCP_2_2_DP_RXSTATUS_H_PRIME(status)) {
			/* reset flag */
			lk->hprime_ready = 0;
			return 0;
		}

		msleep(110);
		i++;
	}

	hdcp_err("hprime timeout(%dms)\n", (110 * i));
	return -EIO;
}

static int do_recv_ake_send_h_prime(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t hprime[HDCP_HMAC_SHA256_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_HPRIME_OFFSET, hprime,
		sizeof(hprime));
	if (ret) {
		hdcp_err("send_h_prime recv fail: ret(%d)\n", ret);
		return -EIO;
	}

	ret = teei_compare_ake_hmac(hprime, HDCP_HMAC_SHA256_LEN);
	if (ret) {
		hdcp_err("teei_compare_ake_hmac failed (%d)\n", ret);
		return -EIO;
	}

	return 0;
}

static int check_pairing_ready(struct hdcp_link_data *lk)
{
	int i = 0;
	int ret;
	uint8_t status = 0;

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	msleep(220);
	/* HDCP spec is 200ms. but we give margin 110ms */
	while (i < 2) {
		/* check abort state firstly,
		 * if session is abored by Rx, Tx stops Authentication process
		 */
		if (is_hdcp_auth_aborted())
			return -ECANCELED;

		/* received from CP_IRQ */
		if (lk->pairing_ready) {
			/* reset flag */
			lk->pairing_ready = 0;
			return 0;
		}

		/* check as polling mode */
		ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &status,
			sizeof(uint8_t));
		hdcp_info("RxStatus: %x\n", status);

		if (ret == 0 && HDCP_2_2_DP_RXSTATUS_PAIRING(status)) {
			/* reset flag */
			lk->pairing_ready = 0;
			return 0;
		}

		msleep(110);
		i++;
	}

	hdcp_err("pairing timeout(%dms)\n", (110 * i));
	return -EIO;
}

static int do_recv_ake_send_pairing_info(struct hdcp_link_data *lk)
{
	int ret;
	uint8_t ekh_km[HDCP_AKE_EKH_MKEY_BYTE_LEN];

	if (is_hdcp_auth_aborted())
		return -ECANCELED;

	ret = hdcp_dplink_recv(DP_HDCP_2_2_REG_EKH_KM_RD_OFFSET,
		ekh_km, sizeof(ekh_km));
	if (ret) {
		hdcp_err("ake_send_pairing_info recv fail: ret(%d)\n", ret);
		return -EIO;
	}

	ret = teei_set_pairing_info(ekh_km, sizeof(ekh_km));
	if (ret) {
		hdcp_err("teei_set_pairing_info failed (%d)\n", ret);
		return -EIO;
	}

	return 0;
}

int auth22_exchange_master_key(struct hdcp_link_data *lk)
{
	/* send Tx -> Rx: AKE_init */
	if (do_send_ake_init(lk) < 0) {
		hdcp_err("send_ake_int fail\n");
		return -EIO;
	}

	/* HDCP spec defined 110ms as min delay after write AKE_Init */
	msleep(110);

	/* recv Rx->Tx: AKE_Send_Cert message */
	if (do_recv_ake_send_cert(lk) < 0) {
		hdcp_err("recv_ake_send_cert fail\n");
		return -EIO;
	}

	if (do_send_ake_restore_km(lk) < 0) {
		hdcp_err("send_ake_restore_km fail\n");
		return -EIO;
	}

	if (check_h_prime_ready(lk) < 0) {
		hdcp_err("Cannot read H prime\n");
		return -EIO;
	}

	/* recv Rx->Tx: AKE_Send_H_Prime message */
	if (do_recv_ake_send_h_prime(lk) < 0) {
		hdcp_err("recv_ake_send_h_prime fail\n");
		return -EIO;
	}

	if (lk->is_stored_km) {
		return 0;
	}

	if (check_pairing_ready(lk) < 0) {
		hdcp_err("Cannot read pairing info\n");
		return -EIO;
	}

	/* recv Rx->Tx: AKE_Send_Pairing_Info message */
	if (do_recv_ake_send_pairing_info(lk) < 0) {
		hdcp_err("recv_ake_send_h_prime fail\n");
		return -EIO;
	}

	return 0;
}

