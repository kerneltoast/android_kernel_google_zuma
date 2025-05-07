/*
 * WFA specific types and constants relating to 802.11
 * Also, see WFA QoS Management spec:
 * https://drive.google.com/file/d/1dj4D92kUhLKrImWkOJZ0__Meg9fZm9fL/view?usp=share_link
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

#ifndef _802_11wfa_h_
#define _802_11wfa_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

#ifndef _NET_ETHERNET_H_
#include <ethernet.h>
#endif

#include <802.11.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* WME Elements */
#define WME_OUI			"\x00\x50\xf2"	/* WME OUI */
#define WME_OUI_LEN		3
#define WME_OUI_TYPE		2	/* WME type */
#define WME_TYPE		2	/* WME type, deprecated */
#define WME_SUBTYPE_IE		0	/* Information Element */
#define WME_SUBTYPE_PARAM_IE	1	/* Parameter Element */
#define WME_SUBTYPE_TSPEC	2	/* Traffic Specification */
#define WME_VER			1	/* WME version */

/** WME Information Element (IE) */
BWL_PRE_PACKED_STRUCT struct wme_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
} BWL_POST_PACKED_STRUCT;
typedef struct wme_ie wme_ie_t;
#define WME_IE_LEN 7	/* WME IE length */

/** WME Parameter Element (PE) */
BWL_PRE_PACKED_STRUCT struct wme_param_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
	uint8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct wme_param_ie wme_param_ie_t;
#define WME_PARAM_IE_LEN            24          /* WME Parameter IE length */

/* QoS Info field for IE as sent from AP */
#define WME_QI_AP_APSD_MASK         0x80        /* U-APSD Supported mask */
#define WME_QI_AP_APSD_SHIFT        7           /* U-APSD Supported shift */
#define WME_QI_AP_COUNT_MASK        0x0f        /* Parameter set count mask */
#define WME_QI_AP_COUNT_SHIFT       0           /* Parameter set count shift */

/* QoS Info field for IE as sent from STA */
#define WME_QI_STA_MAXSPLEN_MASK    0x60        /* Max Service Period Length mask */
#define WME_QI_STA_MAXSPLEN_SHIFT   5           /* Max Service Period Length shift */
#define WME_QI_STA_APSD_ALL_MASK    0xf         /* APSD all AC bits mask */
#define WME_QI_STA_APSD_ALL_SHIFT   0           /* APSD all AC bits shift */
#define WME_QI_STA_APSD_BE_MASK     0x8         /* APSD AC_BE mask */
#define WME_QI_STA_APSD_BE_SHIFT    3           /* APSD AC_BE shift */
#define WME_QI_STA_APSD_BK_MASK     0x4         /* APSD AC_BK mask */
#define WME_QI_STA_APSD_BK_SHIFT    2           /* APSD AC_BK shift */
#define WME_QI_STA_APSD_VI_MASK     0x2         /* APSD AC_VI mask */
#define WME_QI_STA_APSD_VI_SHIFT    1           /* APSD AC_VI shift */
#define WME_QI_STA_APSD_VO_MASK     0x1         /* APSD AC_VO mask */
#define WME_QI_STA_APSD_VO_SHIFT    0           /* APSD AC_VO shift */

/* Default BE ACI value for non-WME connection STA */
#define NON_EDCF_AC_BE_ACI_STA          0x02

/* WME Action Codes */
#define WME_ADDTS_REQUEST	0	/* WME ADDTS request */
#define WME_ADDTS_RESPONSE	1	/* WME ADDTS response */
#define WME_DELTS_REQUEST	2	/* WME DELTS request */

/* WME Setup Response Status Codes */
#define WME_ADMISSION_ACCEPTED		0	/* WME admission accepted */
#define WME_INVALID_PARAMETERS		1	/* WME invalide parameters */
#define WME_ADMISSION_REFUSED		3	/* WME admission refused */

/* ************* WPA definitions. ************* */
#define WPA_OUI			"\x00\x50\xF2"	/* WPA OUI */
#define WPA_OUI_LEN		3		/* WPA OUI length */
#define WPA_OUI_TYPE		1
#define WPA_VERSION		1		/* WPA version */
#define WPA_VERSION_LEN 2 /* WPA version length */

/* ************* WPA2 definitions. ************* */
#define WPA2_OUI		"\x00\x0F\xAC"	/* WPA2 OUI */
#define WPA2_OUI_LEN		3		/* WPA2 OUI length */
#define WPA2_VERSION		1		/* WPA2 version */
#define WPA2_VERSION_LEN	2		/* WAP2 version length */
#define MAX_RSNE_SUPPORTED_VERSION  WPA2_VERSION /* Max supported version */

/* ************* WPS definitions. ************* */
#define WPS_OUI			"\x00\x50\xF2"	/* WPS OUI */
#define WPS_OUI_LEN		3		/* WPS OUI length */
#define WPS_OUI_TYPE		4

/* ************* TPC definitions. ************* */
#define TPC_OUI			"\x00\x50\xF2"	/* TPC OUI */
#define TPC_OUI_LEN		3		/* TPC OUI length */
#define TPC_OUI_TYPE		8
#define WFA_OUI_TYPE_TPC	8		/* deprecated */

/* ************* WFA definitions. ************* */
#define WFA_OUI			"\x50\x6F\x9A"	/* WFA OUI */
#define WFA_OUI_LEN		3		/* WFA OUI length */
#define WFA_OUI_TYPE_P2P	9

#ifndef WL_LEGACY_P2P
#define P2P_OUI         WFA_OUI
#define P2P_OUI_LEN     WFA_OUI_LEN
#define P2P_OUI_TYPE    WFA_OUI_TYPE_P2P
#else
#define P2P_OUI         APPLE_OUI
#define P2P_OUI_LEN     APPLE_OUI_LEN
#define P2P_OUI_TYPE    APPLE_OUI_TYPE_P2P
#endif /* !WL_LEGACY_P2P */

#ifdef WLTDLS
#define WFA_OUI_TYPE_TPQ	4	/* WFD Tunneled Probe ReQuest */
#define WFA_OUI_TYPE_TPS	5	/* WFD Tunneled Probe ReSponse */
#define WFA_OUI_TYPE_WFD	10
#endif /* WTDLS */
#define WFA_OUI_TYPE_HS20		0x10
#define WFA_OUI_TYPE_OSEN		0x12
#define WFA_OUI_TYPE_NAN		0x13
#define WFA_OUI_TYPE_MBO		0x16
#define WFA_OUI_TYPE_MBO_OCE		0x16
#define WFA_OUI_TYPE_OWE		0x1C
#define WFA_OUI_TYPE_SAE_PK		0x1F
#define WFA_OUI_TYPE_TD_INDICATION	0x20

/* WCN */
#define WCN_OUI			"\x00\x50\xf2"	/* WCN OUI */
#define WCN_TYPE		4	/* WCN type */

/* ************* WMM Parameter definitions. ************* */
#define WMM_OUI			"\x00\x50\xF2"	/* WNN OUI */
#define WMM_OUI_LEN		3		/* WMM OUI length */
#define WMM_OUI_TYPE		2		/* WMM OUT type */
#define WMM_VERSION		1
#define WMM_VERSION_LEN		1

/* WMM OUI subtype */
#define WMM_OUI_SUBTYPE_PARAMETER	1
#define WMM_PARAMETER_IE_LEN		24


#define SAE_PK_MOD_LEN		32u
BWL_PRE_PACKED_STRUCT struct dot11_sae_pk_element {
	uint8 id;			/* IE ID, 221, DOT11_MNG_PROPR_ID */
	uint8 len;			/* IE length */
	uint8 oui[WFA_OUI_LEN];		/* WFA_OUI */
	uint8 type;			/* SAE-PK */
	uint8 data[SAE_PK_MOD_LEN];	/* Modifier. 32Byte fixed */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_sae_pk_element dot11_sae_pk_element_t;

/* WPA3 Transition Mode bits */
#define TRANSISION_MODE_WPA3_PSK		BCM_BIT(0)
#define TRANSITION_MODE_WPA3_PSK		BCM_BIT(0)
#define TRANSITION_MODE_SAE_PK			BCM_BIT(1)
#define TRANSITION_MODE_WPA3_ENTERPRISE		BCM_BIT(2)
#define TRANSITION_MODE_ENHANCED_OPEN		BCM_BIT(3)

#define TRANSITION_MODE_SUPPORTED_MASK (\
	TRANSITION_MODE_WPA3_PSK | \
	TRANSITION_MODE_SAE_PK | \
	TRANSITION_MODE_WPA3_ENTERPRISE | \
	TRANSITION_MODE_ENHANCED_OPEN)

/** Transition Disable Indication element */
BWL_PRE_PACKED_STRUCT struct dot11_tdi_element {
	uint8 id;	/* DOT11_MNG_VS_ID */
	uint8 len;	/* IE length */
	uint8 oui[3];	/* WFA_OUI */
	uint8 type;	/* WFA_OUI_TYPE_TD_INDICATION */
	uint8 tdi;	/* Transition Disable Indication bitmap */
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tdi_element dot11_tdi_element_t;

#define DOT11_TDI_ELEM_LENGTH	sizeof(dot11_tdi_element_t)

/* WiFi OWE transition OUI values */
#define OWE_TRANS_OUI       WFA_OUI         /* WiFi OUI 50:6F:9A */
/* oui_type field identifying the type and version of the OWE transition mode IE. */
#define OWE_OUI_TYPE        WFA_OUI_TYPE_OWE /* OUI Type/Version */
/* IEEE 802.11 vendor specific information element. */
#define OWE_IE_ID           0xdd

/*  2.3.1 OWE transition mode IE (WFA OWE spec 1.1) */
typedef BWL_PRE_PACKED_STRUCT struct wifi_owe_ie_s {
	uint8 id;               /* IE ID: OWE_IE_ID 0xDD */
	uint8 len;              /* IE length */
	uint8 oui[WFA_OUI_LEN]; /* OWE OUI 50:6F:9A */
	uint8 oui_type;         /* WFA_OUI_TYPE_OWE 0x1A */
	uint8 attr[];           /* var len attributes */
} wifi_owe_ie_t;

/* owe transition mode ie */
typedef BWL_PRE_PACKED_STRUCT struct owe_transition_mode_ie_s {
	uint8 bssid[ETHER_ADDR_LEN];  /* Contains the BSSID of the other virtual AP */
	uint8 ssid_len;
	uint8 *ssid;     /* SSID of the other virtual AP */
	uint8 band_info; /* operating band info of the other virtual AP */
	uint8 chan;      /* operating channel number of the other virtual AP */
} BWL_POST_PACKED_STRUCT owe_transition_mode_ie_t;
#define OWE_IE_HDR_SIZE (OFFSETOF(wifi_owe_ie_t, attr))
/* oui:3 bytes + oui type:1 byte */
#define OWE_IE_NO_ATTR_LEN  4

/** hotspot2.0 indication element (vendor specific) */
BWL_PRE_PACKED_STRUCT struct hs20_ie {
	uint8 oui[3];
	uint8 type;
	uint8 config;
} BWL_POST_PACKED_STRUCT;
typedef struct hs20_ie hs20_ie_t;
#define HS20_IE_LEN 5	/* HS20 IE length */

/* QoS Mgmt vendor specific IE OUI type */
#define QOS_MGMT_VSIE_OUI_TYPE		0x22u

/* WFA Capabilities vendor specific IE OUI type */
#define WFA_CAP_VSIE_OUI_TYPE		0x23u

/* DSCP Policy Action Frame OUI type */
#define DSCP_POLICY_AF_OUI_TYPE		0x1Au

/* DSCP Policy Action Frame OUI subtypes */
#define DSCP_POLICY_QUERY_FRAME		0u
#define DSCP_POLICY_REQ_FRAME		1u
#define DSCP_POLICY_RESP_FRAME		2u

/* DSCP Policy request types */
#define POLICY_REQ_TYPE_ADD		0u
#define POLICY_REQ_TYPE_REMOVE		1u

/* DSCP Policy attribute field offsets */
#define DSCP_POLICY_ATTR_ID_OFF		0u
#define DSCP_POLICY_ATTR_LEN_OFF	1u
#define DSCP_POLICY_ATTR_DATA_OFF	2u

/* DSCP Policy attribute various length fields */
#define DSCP_POLICY_ATTR_ID_LEN		1u	/* attr id field length */
#define DSCP_POLICY_ATTR_LEN_LEN	1u	/* attr field length */
#define DSCP_POLICY_ATTR_HDR_LEN	2u	/* id + 1-byte length field */

/* DSCP Policy attributes as defined in the QoS Mgmt spec */
typedef enum qos_mgmt_attrs {

	/* DSCP Port Range attribute */
	DSCP_POLICY_PORT_RANGE_ATTR	= 1,

	/* DSCP Policy attribute */
	DSCP_POLICY_ATTR		= 2,

	/* DSCP Policy TCLAS attribute for classifier type 4 */
	DSCP_POLICY_TCLAS_ATTR		= 3,

	/* DSCP Domain Name attribute */
	DSCP_POLICY_DOMAIN_NAME_ATTR	= 4
} qos_mgmt_attrs_e;

/* DSCP Policy Query frame header */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_query_action_vs_frmhdr {
	uint8 category;		/* category VS/VSP */
	uint8 oui[WFA_OUI_LEN];	/* WFA OUI */
	uint8 oui_type;		/* DSCP_POLICY_AF_OUI_TYPE */
	uint8 oui_subtype;	/* DSCP Policy Query frame */
	uint8 dialog_token;	/* to match req/resp */
	uint8 data[];		/* zero or more QoS Mgmt elements */
} BWL_POST_PACKED_STRUCT dscp_policy_query_action_vs_frmhdr_t;
#define DSCP_POLICY_QUERY_ACTION_FRAME_HDR_SIZE (sizeof(dscp_policy_query_action_vs_frmhdr_t))

/* DSCP Policy request frame header */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_req_action_vs_frmhdr {
	uint8 category;		/* category VS/VSP */
	uint8 oui[WFA_OUI_LEN];	/* WFA OUI */
	uint8 oui_type;		/* DSCP_POLICY_AF_OUI_TYPE */
	uint8 oui_subtype;	/* DSCP Policy Request frame */
	uint8 dialog_token;	/* to match req/resp */
	uint8 control;		/* request control */
	uint8 data[];		/* zero or more QoS Mgmt elements */
} BWL_POST_PACKED_STRUCT dscp_policy_req_action_vs_frmhdr_t;
#define DSCP_POLICY_REQ_ACTION_FRAME_HDR_SIZE (sizeof(dscp_policy_req_action_vs_frmhdr_t))

/* DSCP Policy Response frame header */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_resp_action_vs_frmhdr {
	uint8 category;		/* category VS/VSP */
	uint8 oui[WFA_OUI_LEN];	/* WFA OUI */
	uint8 oui_type;		/* DSCP_POLICY_AF_OUI_TYPE */
	uint8 oui_subtype;	/* DSCP Policy Response frame type */
	uint8 dialog_token;	/* to validate req/resp frames */
	uint8 control;		/* response control */
	uint8 count;		/* Number of items in the data (Stauts List) */
	uint8 data[];		/* Status List */
} BWL_POST_PACKED_STRUCT dscp_policy_resp_action_vs_frmhdr_t;
#define DSCP_POLICY_RESP_ACTION_FRAME_HDR_SIZE (sizeof(dscp_policy_resp_action_vs_frmhdr_t))

/* WFA Capabilities IE */
typedef BWL_PRE_PACKED_STRUCT struct wfa_cap_ie {
	uint8 id;		/* 0xDD, IEEE 802.11 vendor specific information element */
	uint8 len;		/* length of data following */
	uint8 oui[WFA_OUI_LEN];	/* WFA OUI */
	uint8 oui_type;		/* WFA_CAP_VSIE_OUI_TYPE */
	uint8 capabilities_len;	/* WFA capabilities length */
	uint8 capabilities[];	/* WFA capability data + optional attributes */
} BWL_POST_PACKED_STRUCT wfa_cap_ie_t;
#define WFA_CAP_IE_HDR_SIZE (sizeof(wfa_cap_ie_t))

/* QoS Mgmt IE */
typedef BWL_PRE_PACKED_STRUCT struct qos_mgmt_ie {
	uint8 id;		/* 0xDD, IEEE 802.11 vendor specific information element */
	uint8 len;		/* length of data following */
	uint8 oui[WFA_OUI_LEN];	/* WFA OUI */
	uint8 oui_type;		/* QOS_MGMT_VSIE_OUI_TYPE */
	uint8 data[];		/* one or more DSCP policy attributes */
} BWL_POST_PACKED_STRUCT qos_mgmt_ie_t;
#define QOS_MGMT_IE_HDR_SIZE (sizeof(qos_mgmt_ie_t))

/* DSCP Policy capability attribute */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_capability_attr {
	uint8 id;		/* attribute id */
	uint8 len;		/* length of data */
	uint8 capabilities;	/* capabilities, 1 indicates DSCP Policy enabled */
} BWL_POST_PACKED_STRUCT dscp_policy_capability_attr_t;
#define DSCP_POLICY_CAPABILITY_ATTR_SIZE (sizeof(dscp_policy_capability_attr_t))

/* QoS Mgmt capability bits */
typedef enum qos_mgmt_cap_bits {

	/* When bit 0 is set, indicates the DSCP Policy support */
	QOS_MGMT_CAP_DSCP_POLICY			= (1u << 0u),

	/* When bit 1 is set, means that AP intends to send an unsolicited
	 * DSCP Policy Request frame to the STA imminently once association
	 * (and any security exchanges) is complete, or set to 0 to indicate it
	 * does not intend to do so.
	 */
	QOS_MGMT_CAP_UNSOLICIT_DSCP_POLICY_AT_ASSOC	= (1u << 1u),

	/* Indicates the support for SCS traffic description when bit2 is set to 1, otherwise
	 * not supported.
	 */
	QOS_MGMT_CAP_SCS_TRAFFIC_DESCRIPTION		= (1u << 2u)
} qos_mgmt_cap_bits_e;

/* DSCP Port Range attribute */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_port_range_attr {
	uint8 id;		/* attribute id */
	uint8 len;		/* length of data */
	uint16 start_port;	/* port range (both are in network byte order): start port */
	uint16 end_port;	/* end port */
} BWL_POST_PACKED_STRUCT dscp_policy_port_range_attr_t;
#define DSCP_POLICY_PORT_RANGE_ATTR_SIZE (sizeof(dscp_policy_port_range_attr_t))

/* DSCP Policy attribute */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_attr {
	uint8 id;		/* attribute id */
	uint8 len;		/* length of data */
	uint8 policy_id;	/* policy id: 1..255, 0 is reserved */
	uint8 req_type;		/* 0(Add), 1(Remove), 2..255 (Reserved) */
	uint8 dscp;		/* DSCP value associated with the policy */
} BWL_POST_PACKED_STRUCT dscp_policy_attr_t;
#define DSCP_POLICY_ATTR_SIZE (sizeof(dscp_policy_attr_t))

/* DSCP Policy TCLAS attribute */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_tclas_attr {
	uint8 id;		/* attribute id */
	uint8 len;		/* length of data */
	uint8 data[];		/* frame classifier type 4 data */
} BWL_POST_PACKED_STRUCT dscp_policy_tclas_attr_t;
#define DSCP_POLICY_TCLAS_ATTR_SIZE (sizeof(dscp_policy_tclas_attr_t))

/* DSCP Policy Domain Name attribute */
typedef BWL_PRE_PACKED_STRUCT struct dscp_policy_domain_name_attr {
	uint8 id;		/* attribute id */
	uint8 len;		/* length of data */
	uint8 data[];		/* domain name */
} BWL_POST_PACKED_STRUCT dscp_policy_domain_name_attr_t;
#define DSCP_POLICY_DOMAIN_NAME_ATTR_SIZE (sizeof(dscp_policy_domain_name_attr_t))

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _802_11wfa_h_ */
