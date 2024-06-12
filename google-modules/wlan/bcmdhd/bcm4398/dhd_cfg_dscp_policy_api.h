/*
 * DSCP Policy public header file, exports the APIs for other modules.
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

#ifndef dhd_cfg_dscp_policy_api_h
#define dhd_cfg_dscp_policy_api_h

#include <dhd.h>

int dhd_dscp_policy_set_vndr_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, int bssidx);
int dhd_dscp_policy_process_vsaf(struct bcm_cfg80211 *cfg, struct net_device *dev,
                                 uint8 *body, uint body_len);
int dhd_dscp_policy_attach(struct bcm_cfg80211 *cfg);
void dhd_dscp_policy_detach(struct bcm_cfg80211 *cfg);
int dhd_dscp_policy_dump(struct net_device *ndev, char *msg, uint msglen);
int dhd_dscp_policy_flush(struct net_device *ndev);
int dhd_dscp_policy_send_query(struct net_device *ndev, uint32 query_val, uint8 *dn, uint8 dn_len);
int dhd_dscp_policy_send_unsolicited_resp(struct net_device *ndev, uint8 policy_id);
int dhd_dscp_process_wfa_cap_ie(struct bcm_cfg80211 *cfg, const uint8 *ies, uint ies_len);

#endif /* dhd_cfg_dscp_policy_api_h */
