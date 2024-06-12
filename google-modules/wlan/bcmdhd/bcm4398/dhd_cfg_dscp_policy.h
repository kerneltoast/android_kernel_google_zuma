/*
 * DSCP Policy header file.
 *
 * Provides type definitions and function prototypes to manage the DSCP policy
 * entries for the network-centric QoS Management in the DHD.
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

#ifndef dhd_cfg_dscp_policy_h
#define dhd_cfg_dscp_policy_h

#include <dhd.h>
#include <ethernet.h>

#include <802.11.h>
#include <wlioctl.h>

typedef enum dscp_policy_proc_flags dscp_policy_proc_flags_e;
enum dscp_policy_proc_flags {
	DSCP_POLICY_PROC_FLAG_ADD_PENDING = (1u << 0u), /* bit 0, add pending */
	DSCP_POLICY_PROC_FLAG_DEL_PENDING = (1u << 1u)	/* bit 1, delete pending */
};

#define DSCP_POLICY_DOMAIN_NAME_LEN	256u

/* This strucutre holds the DSCP policy information */
typedef struct dscp_policy_entry dscp_policy_entry_t;
struct dscp_policy_entry {
	uint8 oui[WFA_OUI_LEN];		/* WFA OUI */
	uint8 oui_type;
	uint8 policy_id;		/* uniquely identifies the policy */
	uint8 req_type;			/* req/resp */
	uint8 dscp;			/* DSCP value associated with the policy */
	uint8 proc_flags;		/* processing flags */
	uint8 pad[3];			/* reserved */
	uint8 domain_name_len;		/* specifies the domain name length */
	uint8 domain_name[DSCP_POLICY_DOMAIN_NAME_LEN];
	wl_qos_rav_scs_ct4_v1_t ct4;	/* Classifier type 4 information IPv4/IPv6 */
	uint16 start_port;		/* port range (in network byte order): start port */
	uint16 end_port;		/* end port */
};

typedef enum dscp_policy_flags dscp_policy_flags_e;
enum dscp_policy_flags {
	/* bit 0, indicates DSCP query has started */
	DSCP_POLICY_FLAG_QUERY_PENDING		= (1u << 0u),

	/* bit 1, reject DSCP policy request from AP (only for test purposes) */
	DSCP_POLICY_FLAG_QUERY_REJECT_REQUEST	= (1u << 1u)
};

#define WFA_CAP_IE_DATA_LEN		1u	/* Current length (1 byte, 8 bits) for the QoS Mgmt
						 * capabilities field
						 */
#define WFA_CAP_IE_MAX_DATA_LEN		4u	/* Max length (4 bytes) for the QoS Mgmt
						 * capabilities field
						 */

typedef struct dscp_policy_info dscp_policy_info_t;
struct dscp_policy_info {
	void *dscp_policy_lock;			/* spin lock to protect readers and writers */
	struct ether_addr sta_mac;		/* station mac address */
	uint8 dialog_token;			/* to match req/response */
	uint8 policy_flags;
	uint8 wfa_caps[WFA_CAP_IE_MAX_DATA_LEN];
	uint8 pad[3];				/* reserved */
	uint8 num_entries;			/* number of DSCP profile entries */
	dscp_policy_entry_t *policy_entries;	/* number (num_entires) of policy entries" */
};

#endif /* dhd_cfg_dscp_policy_h */
