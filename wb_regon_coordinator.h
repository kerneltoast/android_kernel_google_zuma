/*
 * DHD BT WiFi Coex RegON Coordinator - Interface
 *
 * Copyright (C) 2024, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 */
#ifndef __WB_REGON_COORDINATOR__
#define __WB_REGON_COORDINATOR__

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define WBRC_OK 0
#define WBRC_NOP 1
#define WBRC_ERR -1
#define WBRC_WL_FATAL_ERR -2
#define WBRC_WL_NONFATAL_ERR -3

/*
 * format of msg exchanged with BT stack.
 */
typedef struct wbrc_msg {
	unsigned char hdr;
	unsigned char len;
	unsigned char type;
	unsigned char val;
} wbrc_msg_t;

/*
 * format of extended length msg exchanged with BT stack.
 */
typedef struct wbrc_ext_msg {
	unsigned char hdr;
	unsigned char ext_len; /* MSB bit is set, 10000000b */
	unsigned char type;
	unsigned char cmd;
	unsigned int len;
	unsigned char val[];
} wbrc_ext_msg_t;

#define WBRC_MSG_DEFAULT_LEN 2u
#define WBRC_MSG_LEN (sizeof(wbrc_msg_t))
#define WBRC_EXT_MSG_LEN 0x80u
/* worst case msg size is size of ext msg + BT FW blob */
#define WBRC_BT_FW_MAXSIZE (1024u * 1024u)
#define WBRC_MSG_BUF_MAXLEN (sizeof(wbrc_ext_msg_t) + (WBRC_BT_FW_MAXSIZE))

#define WBRC_WAIT_TIMEOUT 6000u /* 6s */
#define WBRC_WAIT_BT_RESET_ACK_TIMEOUT 12000u /* 12s */

#define WBRC_HEADER_DIR_WBRC2BT 0x01u
#define WBRC_HEADER_DIR_BT2WBRC 0x02u

#define WBRC_TYPE_WBRC2BT_CMD 0x01u
#define WBRC_TYPE_WBRC2BT_ACK 0x02u
#define WBRC_TYPE_BT2WBRC_CMD 0x03u
#define WBRC_TYPE_BT2WBRC_ACK 0x04u

#define WBRC_CMD_BT_RESET 0x43u
#define WBRC_CMD_BT_FW_DWNLD 0x44u
#ifdef WBRC_HW_QUIRKS
#define WBRC_CMD_BT_BEFORE_REG_OFF 0x45u
#define WBRC_CMD_BT_AFTER_REG_OFF 0x46u
#define WBRC_CMD_BT_BEFORE_REG_ON 0x47u
#define WBRC_CMD_BT_AFTER_REG_ON 0x48u

#define MIN_REGOFF_TO_REGON_DELAY 20u /* 20ms */
#define WBRC_ONOFF_WAIT_TIMEOUT 2000u /* 2s */
#endif /* WBRC_HW_QUIRKS */

#define WBRC_ACK_BT_RESET_COMPLETE 0x81u

int wl2wbrc_wlan_on(void);		/* WL2WBRC - wlan ON start */
void wl2wbrc_wlan_on_finished(void);	/* WL2WBRC - wlan FW downloaded */
int wl2wbrc_wlan_off(void);		/* WL2WBRC - wlan OFF start */
int wl2wbrc_wlan_off_finished(void);	/* WL2WBRC - wlan OFF complete */
#ifdef WBRC_HW_QUIRKS
void wl2wbrc_wlan_before_regoff(void);
void wl2wbrc_wlan_after_regoff(void);
void wl2wbrc_wlan_before_regon(void);
void wl2wbrc_wlan_after_regon(void);
#endif /* WBRC_HW_QUIRKS */
/* WL2WBRC - request reset of BT stack and wait for ack, for fatal errors */
int wl2wbrc_req_bt_reset(void);
#ifdef WBRC_HW_QUIRKS
void wl2wbrc_wlan_init(void *wl_hdl, uint chipid); /* WL2WBRC - wlan host driver init */
#else
void wl2wbrc_wlan_init(void *wl_hdl);
#endif /* WBRC_HW_QUIRKS */

/* WBRC2WL - request wlan PCIE link to be up for BT FW download */
int wbrc2wl_wlan_pcie_link_request(void *dhd_pub);
/* WBRC2WL - request wlan to be turned ON */
int wbrc2wl_wlan_on_request(void *dhd_pub);
/* WBRC2WL - download BT FW over PCIE BAR2 */
int wbrc2wl_wlan_dwnld_bt_fw(void *dhd_pub, void *fw_blob, uint len);

#if defined(BCMDHD_MODULAR)
int wbrc_init(void);
void wbrc_exit(void);
#endif /* BCMDHD_MODULAR */

#ifdef WBRC_TEST
typedef struct wbrc_test_iovar {
	char name[128];
	int val;
} wbrc_test_iovar_t;

typedef enum wbrc_test_induce_error_code {
	WBRC_NO_ERROR = 0,
	WBRC_BT_FW_DWNLD_FAIL = 1,
	WBRC_WLAN_FW_DWNLD_FAIL = 2,
	WBRC_WLAN_PCIE_LINK_REQ_FAIL = 3,
	WBRC_DELAY_WLAN_OFF = 4,
	WBRC_DELAY_BT_FW_DWNLD = 5,
	WBRC_WLAN_TRAP_ON_LOAD = 6,
	WBRC_BT_FW_DWNLD_SIM = 7,
	WBRC_SLOWDOWN_BT_FW_DWNLD = 8
} wbrc_test_induce_error_code_t;

void *wbrc_get_fops(void);
int wl2wbrc_iovar(void *wl_hdl, wbrc_test_iovar_t *iov);
int wbrc_test_get_error(void);
int wbrc_test_is_verify_bt_dwnld(void);
#endif /* WBRC_TEST */

#endif /* __WB_REGON_COORDINATOR__ */
