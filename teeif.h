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
#ifndef __EXYNOS_HDCP2_TEEIF_H__
#define __EXYNOS_HDCP2_TEEIF_H__

#include <linux/types.h>

/* SMC list for HDCP functions */
#define SMC_HDCP_INIT			((unsigned int)0x82004010)
#define SMC_HDCP_TERMINATE		((unsigned int)0x82004011)
#define SMC_HDCP_PROT_MSG		((unsigned int)0x82004012)
#define SMC_CHECK_STREAM_TYPE_FLAG	((unsigned int)0x82004022)
#define SMC_HDCP_NOTIFY_INTR_NUM	((unsigned int)0x82004023)

#define SMC_DRM_HDCP_AUTH_INFO		((unsigned int)0x82002140)
#define SMC_DRM_HDCP_FUNC_TEST		((unsigned int)0x82002141)
#define SMC_DRM_DP_CONNECT_INFO		((unsigned int)0x82002142)

/**
 * HDCP TEE service commands
 */
enum {
	HDCP_TEEI_GEN_RTX = 0x0,
	HDCP_TEEI_VERIFY_CERT,
	HDCP_TEEI_GEN_MKEY,
	HDCP_TEEI_COMPARE_AKE_HMAC,
	HDCP_TEEI_GEN_RN,
	HDCP_TEEI_COMPARE_LC_HMAC,
	HDCP_TEEI_GEN_RIV,
	HDCP_TEEI_GEN_SKEY,
	HDCP_TEEI_SET_PAIRING_INFO,
	HDCP_TEEI_GET_PAIRING_INFO,
	HDCP_TEEI_SET_RCV_ID_LIST,
	HDCP_TEEI_GEN_STREAM_MANAGE,
	HDCP_TEEI_VERIFY_M_PRIME,
	HDCP_TEEI_KSV_EXCHANGE,
	HDCP_TEEI_VERIFY_R_PRIME,
	HDCP_TEEI_VERIFY_V_PRIME,
	HDCP_TEEI_MSG_END
};

#define HCI_DISCONNECTED	0
#define HCI_CONNECTED		1

#define HDCP_WSM_SIZE	(1024)
#define AKE_INFO_SIZE	(128)

#define HDCP_BINFO_DEVS_COUNT_MAX       (0x7F)
#define HDCP_BINFO_SIZE                 (2)
#define HDCP_SHA1_SIZE                  (20)
#define HDCP_KSV_SIZE                   (5)
#define HDCP_KSV_MAX_LEN                (HDCP_KSV_SIZE * HDCP_BINFO_DEVS_COUNT_MAX)
#define HDCP_SHA1_MAX_INPUT_LEN         (HDCP_BINFO_SIZE + HDCP_KSV_MAX_LEN + HDCP_M0_SIZE)
#define HDCP_RX_MODULUS_LEN		(1024 / 8)
#define HDCP_RX_PUB_EXP_LEN		(24 / 8)
#define HDCP_AKE_ENCKEY_BYTE_LEN	(1024 / 8)
#define HDCP_AKE_MKEY_BYTE_LEN		(128 / 8)
#define HDCP_AKE_M_BYTE_LEN		HDCP_AKE_MKEY_BYTE_LEN
#define HDCP_AKE_EKH_MKEY_BYTE_LEN	HDCP_AKE_MKEY_BYTE_LEN
#define	HDCP_AKE_RTX_BYTE_LEN		(64 / 8)
#define HDCP_AKE_HMAC_APPEND		(6)
#define HDCP_STR_CTR_LEN		(4)
#define HDCP_INPUT_CTR_LEN		(8)
#define RECEIVER_ID_BYTE_LEN		(40 / 8)
#define HDCP_RRX_BYTE_LEN		(64 / 8)
#define HDCP_RTX_BYTE_LEN		HDCP_RRX_BYTE_LEN
#define HDCP_HMAC_SHA256_LEN         	(256 / 8)
#define HDCP_WRAP_APPEND_LEN		(92)
#define HDCP_RX_CERT_LEN		(522)
#define HDCP_AKE_WRAPPED_HMAC_LEN	(32)
#define HDCP_AKE_WKEY_BYTE_LEN		(32)
#define HDCP_SKE_SKEY_LEN		(128 / 8)
#define HDCP_SKE_WSKEY_BYTE_LEN		(32)
#define MKEY_LEN			(16)
#define CERT_LEN			(128)
#define HMAC_LEN			(32)
#define HDCP_RX_CERT_INFO_LEN		(138)
#define HDCP_CERT_SIGLEN		(384)
#define HDCP_RN_BYTE_LEN		(64 / 8)
#define HDCP_RIV_BYTE_LEN		(64 / 8)
#define HDCP_CAPS_BYTE_LEN		(3)
#define HDCP_VERSION_LEN		(1)
#define HDCP_CAPABILITY_MASK_LEN	(2)
#define HDCP_RP_HMAC_V_LEN		HDCP_HMAC_SHA256_LEN
#define HDCP_RP_HMAC_M_LEN		HDCP_HMAC_SHA256_LEN
#define HDCP_RP_RX_INFO_LEN		(2)
#define HDCP_RP_SEQ_NUM_V_LEN		(3)
#define HDCP_RCV_ID_LEN                 (5)
#define HDCP_RCV_DEVS_COUNT_MAX         (32)
#define HDCP_RP_RCVID_LIST_LEN		(HDCP_RCV_ID_LEN * HDCP_RCV_DEVS_COUNT_MAX)
#define HDCP_RP_SEQ_NUM_M_LEN		(3)
#define HDCP_RP_K_LEN			(2)
#define HDCP_RP_MAX_STREAMID_NUM	(63)
#define HDCP_RP_TYPE_LEN		(1)
#define HDCP_RP_STREAMID_LEN		(1)
#define HDCP_RP_STREAM_MSG_MAX_LEN	(32 * 8)
#define HDCP_RP_STREAMID_TYPE_LEN	(HDCP_RP_STREAMID_LEN + HDCP_RP_TYPE_LEN)
#define HDCP_RP_MAX_STREAMID_TYPE_LEN	(HDCP_RP_STREAMID_TYPE_LEN * HDCP_RP_MAX_STREAMID_NUM)
#define HDCP_STATIC_KEY			(16)
#define HDCP_WRAP_KEY			(16)
#define HDCP_WRAP_MAX_SIZE		(128)
#define HDCP_WRAP_AUTH_TAG		(16)
#define HDCP_RX_CAPS_LEN		(1)

/* TEEI error code */
enum hdcp_rv_t  {
	HDCP_OK				= 0x0000,
	E_HDCP_PRO_INVALID_RCV_ID	= 0x3015,
};

enum kw_mode {
        WRAP,
        UNWRAP,
};

typedef struct {
	uint32_t id;
	uint32_t len;
	uint32_t lk_type;
	uint8_t rtx[HDCP_RTX_BYTE_LEN];
	uint8_t tx_caps[HDCP_CAPS_BYTE_LEN];
} hci_genrtx_t;

typedef struct {
	uint32_t id;
	uint8_t version[HDCP_VERSION_LEN];
	uint8_t caps_mask[HDCP_CAPABILITY_MASK_LEN];
} hci_gettxinfo_t;

typedef struct {
	uint32_t id;
	uint32_t len;
	uint8_t cert[HDCP_RX_CERT_LEN];
	uint8_t rrx[HDCP_RRX_BYTE_LEN];
	uint8_t rx_caps[HDCP_CAPS_BYTE_LEN];
} hci_verifycert_t;

typedef struct {
	uint32_t id;
	uint8_t version[HDCP_VERSION_LEN];
	uint8_t caps_mask[HDCP_CAPABILITY_MASK_LEN];
} hci_setrxinfo_t;

typedef struct {
	uint32_t id;
	uint32_t lk_type;
	uint32_t emkey_len;
	uint8_t emkey[HDCP_AKE_ENCKEY_BYTE_LEN];
} hci_genmkey_t;

typedef struct {
	uint32_t id;
	uint32_t rx_hmac_len;
	uint8_t rx_hmac[HDCP_HMAC_SHA256_LEN];
} hci_comphmac_t;

typedef struct {
	uint32_t id;
	uint8_t ekh_mkey[HDCP_AKE_EKH_MKEY_BYTE_LEN];
} hci_setpairing_t;

typedef struct {
	uint32_t id;
	uint8_t ekh_mkey[HDCP_AKE_EKH_MKEY_BYTE_LEN];
	uint8_t m[HDCP_AKE_M_BYTE_LEN];
	uint8_t found;
} hci_getpairing_t;

typedef struct {
	uint32_t id;
	uint32_t len;
	uint8_t rn[HDCP_RN_BYTE_LEN];
} hci_genrn_t;

typedef struct {
	uint32_t id;
	uint32_t rx_hmac_len;
	uint8_t rx_hmac[HDCP_HMAC_SHA256_LEN];
} hci_complchmac_t;

typedef struct {
	uint32_t id;
	uint32_t len;
	uint8_t riv[HDCP_RIV_BYTE_LEN];
} hci_genriv_t;

typedef struct {
	uint32_t id;
	uint32_t lk_type;
	uint8_t eskey[HDCP_SKE_SKEY_LEN];
	uint32_t eskey_len;
	int share_skey;
} hci_genskey_t;

typedef struct {
	uint32_t id;
	uint8_t rx_info[HDCP_RP_RX_INFO_LEN];
	uint8_t seq_num_v[HDCP_RP_SEQ_NUM_V_LEN];
	uint8_t v_prime[HDCP_RP_HMAC_V_LEN / 2];
	uint8_t rcvid_lst[HDCP_RP_RCVID_LIST_LEN];
	uint8_t v[HDCP_RP_HMAC_V_LEN / 2];
} hci_setrcvlist_t;

typedef struct {
	uint32_t id;
	uint16_t stream_num;
	uint8_t streamid[HDCP_RP_MAX_STREAMID_NUM];
	uint8_t seq_num_m[HDCP_RP_SEQ_NUM_M_LEN];
	uint8_t k[HDCP_RP_K_LEN];
	uint8_t streamid_type[HDCP_RP_MAX_STREAMID_TYPE_LEN];
} hci_genstreaminfo_t;

typedef struct {
	uint32_t id;
	uint8_t m_prime[HDCP_RP_HMAC_M_LEN];
	uint8_t strmsg[HDCP_RP_STREAM_MSG_MAX_LEN];
	uint32_t str_len;
} hci_verifymprime_t;

typedef struct {
	uint32_t id;
	uint64_t bksv;
	uint64_t an;
	uint64_t aksv;
	uint32_t is_repeater;
} hci_ksvexchange_t;

typedef struct {
	uint32_t id;
	uint16_t r_prime;
} hci_verifyrprime_t;

typedef struct {
	uint32_t id;
	uint32_t ksv_len;
	uint16_t binfo;
	uint8_t ksv[HDCP_KSV_MAX_LEN];
	uint8_t v_prime[HDCP_SHA1_SIZE];
} hci_verifyvprime_t;

/* todo: define WSM message format for AKE */
struct hci_message {
	union {
		uint32_t cmd_id;
		hci_genrtx_t genrtx;
		hci_gettxinfo_t gettxinfo;
		hci_verifycert_t vfcert;
		hci_setrxinfo_t setrxinfo;
		hci_genmkey_t genmkey;
		hci_comphmac_t comphmac;
		hci_setpairing_t setpairing;
		hci_getpairing_t getpairing;
		hci_genrn_t genrn;
		hci_complchmac_t complchmac;
		hci_genriv_t genriv;
		hci_genskey_t genskey;
		hci_setrcvlist_t setrcvlist;
		hci_genstreaminfo_t genstrminfo;
		hci_verifymprime_t verifymprime;
		hci_ksvexchange_t ksvexchange;
		hci_verifyrprime_t verifyrprime;
		hci_verifyvprime_t verifyvprime;
		uint8_t data[HDCP_WSM_SIZE];
	};
};

struct hci_ctx {
	struct hci_message *msg;
	uint8_t state;
};

enum hdcp_auth_cmd {
	HDCP_CMD_AUTH_RESP = (1U << 31),
	HDCP_CMD_REINIT = 0,
	HDCP_CMD_PROTOCOL,
	HDCP_CMD_ENCRYPTION_SET,
	HDCP_CMD_ENCRYPTION_GET,
	HDCP_CMD_AUTH_MANUAL_START,
	HDCP_CMD_SESSION_SET,
	HDCP_CMD_SET_TEST_MODE,
	HDCP_CMD_CONNECT_INFO,
	HDCP_CMD_GET_CP_LVL,
};

void hdcp_tee_init(void);
int hdcp_tee_open(void);
int hdcp_tee_close(void);
int hdcp_tee_set_protection(uint32_t hdcp_lvl);
int hdcp_tee_send_cmd(uint32_t cmd);
int hdcp_tee_check_protection(int* version);
int hdcp_tee_set_test_mode(bool enable);
int hdcp_tee_connect_info(int connect_info);
int hdcp_tee_get_cp_level(uint32_t* requested_lvl);

/* HDCP TEE interfaces */
int teei_gen_rtx(uint32_t lk_type,
		uint8_t *rtx,
		size_t rtx_len,
		uint8_t *caps,
		uint32_t caps_len);
int teei_verify_cert(uint8_t *cert, size_t cert_len,
		uint8_t *rrx, size_t rrx_len,
		uint8_t *rx_caps, size_t rx_caps_len);
int teei_generate_master_key(uint32_t lk_type,
			uint8_t *emkey, size_t emkey_len);
int teei_compare_ake_hmac(uint8_t *hmac, size_t hamc_len);
int teei_set_pairing_info(uint8_t *ekh_mkey, size_t ekh_mkey_len);
int teei_get_pairing_info(uint8_t *ekh_mkey, size_t ekh_mkey_len,
			uint8_t *m, size_t m_len, int* found);

/* LC interface */
int teei_gen_rn(uint8_t *out, size_t len);
int teei_compare_lc_hmac(uint8_t *rx_hmac, size_t rx_hmac_len);
int teei_generate_riv(uint8_t *out, size_t len);
int teei_generate_skey(uint32_t lk_type,
		uint8_t *eskey, size_t eskey_len,
		int share_skey);

/* Repeater Auth interface */
int teei_set_rcvlist_info(uint8_t *rx_info,
		uint8_t *seq_num_v,
		uint8_t *v_prime,
		uint8_t *rcvid_list,
		uint8_t *v,
		uint8_t *valid);
int teei_gen_stream_manage(uint16_t stream_num,
		uint8_t *streamid,
		uint8_t *seq_num_m,
		uint8_t *k,
		uint8_t *streamid_type);
int teei_verify_m_prime(uint8_t *m_prime, uint8_t *input, size_t input_len);

int teei_verify_r_prime(uint16_t rprime);
int teei_ksv_exchange(uint64_t bksv, uint32_t is_repeater,
	uint64_t *aksv, uint64_t *an);
int teei_verify_v_prime(uint16_t binfo, uint8_t *ksv, uint32_t ksv_len,
	uint8_t *vprime);

#endif
