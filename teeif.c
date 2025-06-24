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
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/soc/samsung/exynos-smc.h>
#include <linux/slab.h>
#include <linux/trusty/trusty_ipc.h>
#include <linux/module.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>

#include "teeif.h"
#include "hdcp-log.h"

#define TZ_CON_TIMEOUT  5000
#define TZ_BUF_TIMEOUT 10000
#define TZ_MSG_TIMEOUT 10000
#define HDCP_TA_PORT "com.android.trusty.hdcp.auth"

struct hdcp_auth_req {
	uint32_t cmd;
	int32_t arg;
};

struct hdcp_auth_rsp {
	uint32_t cmd;
	int32_t err;
	int32_t arg;
};

struct hdcp_tz_chan_ctx {
	struct tipc_chan *chan;
	struct mutex rsp_lock;
	struct completion reply_comp;
	struct hdcp_auth_rsp rsp;
	struct hci_message **msg_ptr;
};
static struct hdcp_tz_chan_ctx hdcp_ta_ctx;

static size_t deduce_num_payload(size_t msg_len)
{
	switch (msg_len) {
	case (sizeof(struct hdcp_auth_rsp)):
		return 1;
	case (sizeof(struct hdcp_auth_rsp) + sizeof(struct hci_message)):
		return 2;
	default:
		return 0;
	}
}
static struct tipc_msg_buf *tz_srv_handle_msg(void *data,
					      struct tipc_msg_buf* rxbuf)
{
	struct hdcp_tz_chan_ctx *ctx = data;
	size_t len;
	size_t payload_num;

	len = mb_avail_data(rxbuf);
	payload_num = deduce_num_payload(len);

	if (payload_num == 0) {
		hdcp_err("TZ: invalid RSP buffer size (%zd)\n", len);
	}

	if (payload_num > 0) {
		memcpy(&ctx->rsp,
		       mb_get_data(rxbuf, sizeof(struct hdcp_auth_rsp)),
		       sizeof(struct hdcp_auth_rsp));
	}
	if (payload_num > 1 && ctx->msg_ptr) {
		memcpy(*ctx->msg_ptr,
		       mb_get_data(rxbuf, sizeof(struct hci_message)),
		       sizeof(struct hci_message));
	}

	complete(&ctx->reply_comp);

	return rxbuf;
}

static void tz_srv_handle_event(void *data, int event)
{
	struct hdcp_tz_chan_ctx *ctx = data;
	complete(&ctx->reply_comp);
}

static const struct tipc_chan_ops tz_srv_ops = {
	.handle_msg = tz_srv_handle_msg,
	.handle_event = tz_srv_handle_event,
};

void hdcp_tee_init(void)
{
	init_completion(&hdcp_ta_ctx.reply_comp);
	mutex_init(&hdcp_ta_ctx.rsp_lock);
	hdcp_ta_ctx.msg_ptr = NULL;
}

int hdcp_tee_open(void)
{
	int ret = 0;
	struct tipc_chan *chan;

	if (hdcp_ta_ctx.chan) {
		hdcp_debug("HCI is already connected\n");
		return 0;
	}

	chan = tipc_create_channel(NULL, &tz_srv_ops, &hdcp_ta_ctx);
	if (IS_ERR(chan)) {
		hdcp_err("TZ: failed (%ld) to create chan\n", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	reinit_completion(&hdcp_ta_ctx.reply_comp);

	ret = tipc_chan_connect(chan, HDCP_TA_PORT);
	if (ret < 0) {
		hdcp_err("TZ: failed (%d) to connect\n", ret);
		tipc_chan_destroy(chan);
		return ret;
	}

	hdcp_ta_ctx.chan = chan;
	ret = wait_for_completion_timeout(&hdcp_ta_ctx.reply_comp,
					  msecs_to_jiffies(TZ_CON_TIMEOUT));
	if (ret <= 0) {
		ret = (!ret) ? -ETIMEDOUT : ret;
		hdcp_err("TZ: failed (%d) to wait for connect\n", ret);
		hdcp_tee_close();
		return ret;
	}

	return 0;
}

int hdcp_tee_close(void)
{
	if (!hdcp_ta_ctx.chan) {
		hdcp_info("HCI is already disconnected\n");
		return 0;
	}

	tipc_chan_shutdown(hdcp_ta_ctx.chan);
	tipc_chan_destroy(hdcp_ta_ctx.chan);
	hdcp_ta_ctx.chan = NULL;
	return 0;
}

static int hdcp_tee_comm_xchg_internal(uint32_t cmd, int32_t arg,
				       int32_t *rsp, struct hci_message *hci)
{
	int ret;
	struct tipc_msg_buf *txbuf;
	struct hdcp_auth_req auth_req;

	ret = hdcp_tee_open();
	if (ret)
		return ret;

	txbuf = tipc_chan_get_txbuf_timeout(hdcp_ta_ctx.chan, TZ_BUF_TIMEOUT);
	if (IS_ERR(txbuf)) {
		hdcp_err("TZ: failed (%ld) to get txbuf\n", PTR_ERR(txbuf));
		return PTR_ERR(txbuf);
	}

	auth_req.cmd = cmd;
	auth_req.arg = arg;
	memcpy(mb_put_data(txbuf, sizeof(struct hdcp_auth_req)),
	       &auth_req, sizeof(struct hdcp_auth_req));

	if (hci) {
		memcpy(mb_put_data(txbuf, sizeof(struct hci_message)),
		       hci, sizeof(struct hci_message));
		hdcp_ta_ctx.msg_ptr = &hci;
	} else {
		hdcp_ta_ctx.msg_ptr = NULL;
	}

	reinit_completion(&hdcp_ta_ctx.reply_comp);

	ret = tipc_chan_queue_msg(hdcp_ta_ctx.chan, txbuf);
	if (ret < 0) {
		hdcp_err("TZ: failed(%d) to queue msg\n", ret);
		tipc_chan_put_txbuf(hdcp_ta_ctx.chan, txbuf);
		hdcp_tee_close();
		return ret;
	}

	ret = wait_for_completion_timeout(&hdcp_ta_ctx.reply_comp,
					  msecs_to_jiffies(TZ_MSG_TIMEOUT));
	if (ret <= 0) {
		ret = (!ret) ? -ETIMEDOUT : ret;
		hdcp_err("TZ: failed (%d) to wait for reply\n", ret);
		hdcp_tee_close();
		return ret;
	}

	if (hdcp_ta_ctx.rsp.cmd != (cmd | HDCP_CMD_AUTH_RESP)) {
		hdcp_err("TZ: hdcp had an unexpected rsp cmd (%x vs %x)",
			 hdcp_ta_ctx.rsp.cmd, cmd | HDCP_CMD_AUTH_RESP);
		return -EIO;
	}

	if (hdcp_ta_ctx.rsp.err) {
		hdcp_err("TZ: hdcp had an unexpected rsp err (%d)",
			 hdcp_ta_ctx.rsp.err);
		return -EIO;
	}

	if (rsp)
		*rsp = hdcp_ta_ctx.rsp.arg;
	return 0;
}

static int hdcp_tee_comm_xchg(uint32_t cmd, int32_t arg, int32_t *rsp,
			      struct hci_message *hci) {
	int ret;
	int retries = 2;

	mutex_lock(&hdcp_ta_ctx.rsp_lock);
	while (retries) {
		retries--;
		ret = hdcp_tee_comm_xchg_internal(cmd, arg, rsp, hci);
		if (!ret) {
			mutex_unlock(&hdcp_ta_ctx.rsp_lock);
			return 0;
		}
	}
	mutex_unlock(&hdcp_ta_ctx.rsp_lock);

	return ret;
}

int hdcp_tee_send_cmd(uint32_t cmd) {
	return hdcp_tee_comm_xchg(cmd, 0, NULL, NULL);
}

int hdcp_tee_set_protection(uint32_t hdcp_lvl) {
	return hdcp_tee_comm_xchg(HDCP_CMD_ENCRYPTION_SET, hdcp_lvl, NULL,
		NULL);
}

int hdcp_tee_check_protection(int* version) {
	return hdcp_tee_comm_xchg(HDCP_CMD_ENCRYPTION_GET, 0, version, NULL);
}

int hdcp_tee_set_test_mode(bool enable) {
	return hdcp_tee_comm_xchg(HDCP_CMD_SET_TEST_MODE, enable, NULL, NULL);
}

int hdcp_tee_connect_info(int connect_info) {
	return hdcp_tee_comm_xchg(HDCP_CMD_CONNECT_INFO, connect_info, NULL, NULL);
}

int hdcp_tee_get_cp_level(uint32_t* requested_lvl) {
	return hdcp_tee_comm_xchg(HDCP_CMD_GET_CP_LVL, 0, requested_lvl, NULL);
}

static int hdcp_tee_comm(struct hci_message *hci) {
	return hdcp_tee_comm_xchg(HDCP_CMD_PROTOCOL, 0, NULL, hci);
}

int teei_gen_rtx(uint32_t lk_type,
		uint8_t *rtx, size_t rtx_len,
		uint8_t *caps, uint32_t caps_len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;

	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_GEN_RTX;
	hci->genrtx.lk_type = lk_type;
	hci->genrtx.len = rtx_len;

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	/* check returned message from SWD */
	if (rtx && rtx_len)
		memcpy(rtx, hci->genrtx.rtx, rtx_len);
	if (caps && caps_len)
		memcpy(caps, hci->genrtx.tx_caps, caps_len);

	return ret;
}

int teei_verify_cert(uint8_t *cert, size_t cert_len,
		uint8_t *rrx, size_t rrx_len,
		uint8_t *rx_caps, size_t rx_caps_len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;
	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_VERIFY_CERT;
	hci->vfcert.len = cert_len;
	memcpy(hci->vfcert.cert, cert, cert_len);
	if (rrx && rrx_len)
		memcpy(hci->vfcert.rrx, rrx, rrx_len);
	if (rx_caps && rx_caps_len)
		memcpy(hci->vfcert.rx_caps, rx_caps, rx_caps_len);

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	/* return verification result */
	return ret;
}

int teei_generate_master_key(uint32_t lk_type, uint8_t *emkey, size_t emkey_len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;
	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_GEN_MKEY;
	hci->genmkey.lk_type = lk_type;
	hci->genmkey.emkey_len = emkey_len;

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	/* copy encrypted mkey & wrapped mkey to hdcp ctx */
	memcpy(emkey, hci->genmkey.emkey, hci->genmkey.emkey_len);

	/* check returned message from SWD */

	return 0;
}

int teei_compare_ake_hmac(uint8_t *rx_hmac, size_t rx_hmac_len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;
	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_COMPARE_AKE_HMAC;
	hci->comphmac.rx_hmac_len = rx_hmac_len;
	memcpy(hci->comphmac.rx_hmac, rx_hmac, rx_hmac_len);

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	return ret;
}

int teei_set_pairing_info(uint8_t *ekh_mkey, size_t ekh_mkey_len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;

	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_SET_PAIRING_INFO;
	memcpy(hci->setpairing.ekh_mkey, ekh_mkey, ekh_mkey_len);

	/* send command to swd */
	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	return ret;
}

int teei_get_pairing_info(uint8_t *ekh_mkey, size_t ekh_mkey_len,
			  uint8_t *m, size_t m_len,
			  int *found)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;

	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_GET_PAIRING_INFO;

	/* send command to swd */
	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	memcpy(ekh_mkey, hci->getpairing.ekh_mkey, ekh_mkey_len);
	memcpy(m, hci->getpairing.m, m_len);
	*found = hci->getpairing.found ? 1 : 0;

	return ret;
}

int teei_gen_rn(uint8_t *out, size_t len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;

	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_GEN_RN;
	hci->genrn.len = len;

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	/* check returned message from SWD */

	memcpy(out, hci->genrn.rn, len);

	return ret;
}

int teei_compare_lc_hmac(uint8_t *rx_hmac, size_t rx_hmac_len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;
	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_COMPARE_LC_HMAC;
	hci->complchmac.rx_hmac_len = rx_hmac_len;
	memcpy(hci->complchmac.rx_hmac, rx_hmac, rx_hmac_len);

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	return ret;
}

int teei_generate_riv(uint8_t *out, size_t len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;

	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_GEN_RIV;
	hci->genriv.len = len;

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	memcpy(out, hci->genriv.riv, len);

	/* todo:  check returned message from SWD */

	return ret;
}

int teei_generate_skey(uint32_t lk_type,
		uint8_t *eskey, size_t eskey_len,
		int share_skey)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;
	/* todo: input check */

	/* Update TCI buffer */
	hci->cmd_id = HDCP_TEEI_GEN_SKEY;
	hci->genskey.lk_type = lk_type;
	hci->genskey.eskey_len = eskey_len;
	hci->genskey.share_skey = share_skey;

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	/* copy encrypted mkey & wrapped mkey to hdcp ctx */
	memcpy(eskey, hci->genskey.eskey, hci->genskey.eskey_len);

	/* todo: check returned message from SWD */

	return 0;
}

int teei_set_rcvlist_info(uint8_t *rx_info,
		uint8_t *seq_num_v,
		uint8_t *v_prime,
		uint8_t *rcvid_list,
		uint8_t *v,
		uint8_t *valid)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;
	/* todo: input check */

	hci->cmd_id = HDCP_TEEI_SET_RCV_ID_LIST;

	memcpy(hci->setrcvlist.rcvid_lst, rcvid_list, HDCP_RP_RCVID_LIST_LEN);
	memcpy(hci->setrcvlist.v_prime, v_prime, HDCP_RP_HMAC_V_LEN / 2);
	/* Only used DP */
	if (rx_info != NULL && seq_num_v != NULL) {
		memcpy(hci->setrcvlist.rx_info, rx_info, HDCP_RP_RX_INFO_LEN);
		memcpy(hci->setrcvlist.seq_num_v, seq_num_v, HDCP_RP_SEQ_NUM_V_LEN);
	}


	ret = hdcp_tee_comm(hci);
	if (ret != 0) {
		*valid = 0;
		return ret;
	}
	memcpy(v, hci->setrcvlist.v, HDCP_RP_HMAC_V_LEN / 2);
	*valid = 1;

	return 0;
}

int teei_gen_stream_manage(uint16_t stream_num,
		uint8_t *streamid,
		uint8_t *seq_num_m,
		uint8_t *k,
		uint8_t *streamid_type)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;
	/* todo: input check */

	/* Update TCI buffer */

	hci->cmd_id = HDCP_TEEI_GEN_STREAM_MANAGE;
	hci->genstrminfo.stream_num = stream_num;
	memcpy(hci->genstrminfo.streamid, streamid, sizeof(uint8_t) * stream_num);

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	memcpy(seq_num_m, hci->genstrminfo.seq_num_m, HDCP_RP_SEQ_NUM_M_LEN);
	memcpy(k, hci->genstrminfo.k, HDCP_RP_K_LEN);
	memcpy(streamid_type, hci->genstrminfo.streamid_type, HDCP_RP_MAX_STREAMID_TYPE_LEN);

	/* check returned message from SWD */

	/* return verification result */
	return ret;
}

int teei_verify_m_prime(uint8_t *m_prime, uint8_t *input, size_t input_len)
{
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;

	hci->cmd_id = HDCP_TEEI_VERIFY_M_PRIME;
	memcpy(hci->verifymprime.m_prime, m_prime, HDCP_RP_HMAC_M_LEN);
	if (input && input_len < sizeof(hci->verifymprime.strmsg)) {
		memcpy(hci->verifymprime.strmsg, input, input_len);
		hci->verifymprime.str_len = input_len;
	}

	ret = hdcp_tee_comm(hci);

	return ret;
}

int teei_ksv_exchange(uint64_t bksv, uint32_t is_repeater, uint64_t *aksv,
	uint64_t *an) {
	int ret = 0;
	struct hci_message msg;
	struct hci_message *hci = &msg;

	hci->cmd_id = HDCP_TEEI_KSV_EXCHANGE;
	hci->ksvexchange.bksv = bksv;
	hci->ksvexchange.is_repeater = is_repeater;

	if ((ret = hdcp_tee_comm(hci)) < 0)
		return ret;

	*aksv = hci->ksvexchange.aksv;
	*an = hci->ksvexchange.an;
	return 0;
}

int teei_verify_r_prime(uint16_t rprime) {
	struct hci_message msg;
	struct hci_message *hci = &msg;

	hci->cmd_id = HDCP_TEEI_VERIFY_R_PRIME;
	hci->verifyrprime.r_prime = rprime;

	return hdcp_tee_comm(hci);
}

int teei_verify_v_prime(uint16_t binfo, uint8_t *ksv, uint32_t ksv_len,
	uint8_t *vprime) {
	struct hci_message msg;
	struct hci_message *hci = &msg;

	hci->cmd_id = HDCP_TEEI_VERIFY_V_PRIME;
	hci->verifyvprime.ksv_len = ksv_len;
	hci->verifyvprime.binfo = binfo;
	memcpy(hci->verifyvprime.ksv, ksv, ksv_len);
	memcpy(hci->verifyvprime.v_prime, vprime, HDCP_SHA1_SIZE);

	return hdcp_tee_comm(hci);
}
