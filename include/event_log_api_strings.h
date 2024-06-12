/*
 * EVENT_LOG API strings definitions
 * The strings defined in this file are "API" strings and therefore can't be changed without
 * consulting the consumers of these strings
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
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _EVENT_LOG_API_STRINGS_H_
#define _EVENT_LOG_API_STRINGS_H_

#define WLC_RRM_RX_NEIGHBOR_REPORT_TOKEN_STR	"received neighbor report (token = %d)\n"

#define WLC_ASSOC_TGT_SEL_JOIN_PREF_FIELD_STR	\
	"wl%d: RSSI is %d; %d roaming target[s]; Join preference fields are 0x%x\n"

#define WLC_ASSOC_TGT_SEL_TGTS_AFTER_PRUNE_STR	"result: %d target(s) after prune\n"
#define WLC_ASSOC_SCAN_START_ALL_CHNLS_STR	"scan start: all channels\n"
#define WLC_ASSOC_ROAM_SCAN_RSN_STR		"roam scan: reason=%d rssi=%d\n"
#define WLC_ASSOC_FULL_SCAN_STR			"starting full scan\n"
#define WLC_ASSOC_FULL_SCAN_LP_STR		"starting full scan (LP:%d)\n"

#endif /* _EVENT_LOG_API_STRINGS_H_ */
