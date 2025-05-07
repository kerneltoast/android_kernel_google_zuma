/*
 * Broadcom Dongle Host Driver (DHD), DSCP Policy implementation file.
 *
 * This file implements the DSCP policy management, routines to parse and build the QoS Mgmt
 * vendor-specific action frames. The module wl_cfg80211.c calls into these routines whenever
 * the QoS Mgmt action frame is received.
 *
 * It also adds the QoS Mgmt vendor-sepcific IE to the assoc request in the firmware using the
 * IOVAR.

 * Currently, DSCP policy table is allocated at the attach time with max entries.
 *
 * This file is used only if the DHD is built with the feature string "dscp_policy".
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


#include <typedefs.h>
#include <linuxver.h>

#include <osl.h>
#include <bcmutils.h>
#include <bcmendian.h>

#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <dscp_policy.h>
#include <wlioctl.h>

#include <dhd_cfg_dscp_policy.h>
#include <dhd_cfg_dscp_policy_api.h>

#define DSCP_POLICY_MAX_ENTRIES		8u	/* Max supported DSCP policy entries */
#define DOMAIN_NAME_SIZE_MAX		256u	/* Domain name size */
#define DSCP_AF_DEFAULT_DWELL_TIME	20u	/* ms */

/* dialog token */
#define DSCP_POLICY_UPDATE_TOKEN(i) (++(i) ? (i) : ++(i))

/* DSCP policy lock, protect between IOCTL aka user process vs. Data path contexts */
#define DHD_DSCP_POLICY_LOCK(lock, flags)       (flags) = osl_spin_lock(lock)
#define DHD_DSCP_POLICY_UNLOCK(lock, flags)     osl_spin_unlock((lock), (flags))

/* Status codes */
typedef enum dscp_policy_sc dscp_policy_sc_e;
enum dscp_policy_sc {
	/* Everything looks good */
	DSCP_POLICY_SC_SUCCESS					= 0u,

	/* request denined for various reason(s) */
	DSCP_POLICY_SC_REQUEST_DECLINED				= 1u,

	/* One of the calssifiers/attributes is not supported */
	DSCP_POLICY_SC_REQUESTED_CLASSIFIER_NOT_SUPPORTED	= 2u,

	/* Unable to process, too many classifiers etc */
	DSCP_POLICY_SC_INSUFFICIENT_PROCESSING_RESOURCES	= 3u
};

/* Status tuple for the response */
typedef struct dscp_policy_status dscp_policy_status_t;
struct dscp_policy_status {
	uint8 policy_id;
	uint8 status;
};

static inline bool is_ap_enabled_dscp_policy(struct bcm_cfg80211 *cfg);
static int dhd_dscp_populate_policy_entry(dscp_policy_info_t *policy_info,
                                          dscp_policy_entry_t *entry,
                                          dscp_policy_port_range_attr_t *pr_attr,
                                          dscp_policy_attr_t *policy_attr,
                                          dscp_policy_tclas_attr_t *tclas_attr,
                                          dscp_policy_domain_name_attr_t *dn_attr);
static int dhd_dscp_policy_send_response(struct bcm_cfg80211 *cfg, struct net_device *dev,
                                         dscp_policy_sc_e status, bool unsolicited);
static int dhd_dscp_policy_send_af(struct bcm_cfg80211 *cfg, struct net_device *ndev,
                                   uint8 *buf, uint16 buf_len);
/*
 * Allocats the memory for the max DSCP policies.
 * TODO: Add/delete dynamically based on the number of policies received from the AP.
 *       Plan to revisit when the scope is fully known.
 */
int
dhd_dscp_policy_attach(struct bcm_cfg80211 *cfg)
{
	int ret_val;
	dscp_policy_info_t *policy_info = NULL;


	policy_info = (dscp_policy_info_t *) MALLOCZ(cfg->osh, sizeof(*policy_info));
	if (policy_info == NULL) {
		ret_val = BCME_NOMEM;
		goto done;
	}

	policy_info->policy_entries =
	        (dscp_policy_entry_t *) MALLOCZ(cfg->osh,
	                                        (DSCP_POLICY_MAX_ENTRIES *
	                                         sizeof(dscp_policy_entry_t)));
	if (policy_info->policy_entries == NULL) {
		ret_val = BCME_NOMEM;
		goto done;
	}

	policy_info->num_entries = DSCP_POLICY_MAX_ENTRIES;

	/* Setup policy spin lock */
	policy_info->dscp_policy_lock = osl_spin_lock_init(cfg->osh);
	if (policy_info->dscp_policy_lock == NULL) {
		ret_val = BCME_ERROR;
		goto done;
	}

	cfg->dscp_policy_info = (void *) policy_info;

	ret_val = BCME_OK;

done:
	if (ret_val != BCME_OK) {
		// Failure return path
		cfg->dscp_policy_info = (void *) policy_info;
		dhd_dscp_policy_detach(cfg);
	}

	return ret_val;
}

void
dhd_dscp_policy_detach(struct bcm_cfg80211 *cfg)
{
	dscp_policy_info_t *policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;

	if (policy_info != NULL) {
		if (policy_info->policy_entries != NULL) {
			osl_spin_lock_deinit(cfg->osh, policy_info->dscp_policy_lock);
			MFREE(cfg->osh, policy_info->policy_entries,
			      (DSCP_POLICY_MAX_ENTRIES * sizeof(dscp_policy_entry_t)));
		}

		MFREE(cfg->osh, policy_info, sizeof(*policy_info));
	}
}

/* Add/update the incoming DSCP policies from the AP.
 * Update the existing policy information for the existing policy or use a new entry from the table.
 * Returns BCME_OK on success, BCME_ERROR on failure.
 */
static int
dhd_dscp_policy_update(struct bcm_cfg80211 *cfg, dscp_policy_port_range_attr_t *pr_attr,
                       dscp_policy_attr_t *policy_attr,
                       dscp_policy_tclas_attr_t *tclas_attr,
                       dscp_policy_domain_name_attr_t *dn_attr)
{
	uint counter;
	int ret_val = BCME_ERROR;
	dscp_policy_info_t *policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;

	/* First pass, go through the list and update if exists */
	for (counter = 0; counter < policy_info->num_entries; counter++) {
		if (policy_info->policy_entries[counter].policy_id == policy_attr->policy_id) {
			ret_val = dhd_dscp_populate_policy_entry(
				policy_info,
				&policy_info->policy_entries[counter],
				pr_attr, policy_attr, tclas_attr, dn_attr);
			goto done;
		}
	}

	/* Second pass, find the free slot and update the policy */
	for (counter = 0; counter < policy_info->num_entries; counter++) {
		if (policy_info->policy_entries[counter].policy_id == 0) {
			/* Update here */
			ret_val = dhd_dscp_populate_policy_entry(
				policy_info,
				&policy_info->policy_entries[counter],
				pr_attr, policy_attr, tclas_attr, dn_attr);
			goto done;
		}
	}
done:
	return ret_val;
}

/* Popluate the DSCP policy entry from the received DSCP policy
 * attributes(policy attribute, tclas attribute and domain name attribute).
 */
static int
dhd_dscp_populate_policy_entry(dscp_policy_info_t *policy_info,
                               dscp_policy_entry_t *entry, dscp_policy_port_range_attr_t *pr_attr,
                               dscp_policy_attr_t *policy_attr,
                               dscp_policy_tclas_attr_t *tclas_attr,
                               dscp_policy_domain_name_attr_t *dn_attr)
{
	int ret_val = BCME_ERROR;

	/* policy attribute must always be present, return error otherwise */
	if (policy_attr == NULL) {
		goto done;
	}

	/* set the add/del flags for the policy */
	if (policy_attr->req_type == POLICY_REQ_TYPE_ADD) {
		/* For test purposes, reject all DSCP policy add requests */
		if (policy_info->policy_flags & DSCP_POLICY_FLAG_QUERY_REJECT_REQUEST) {
			entry->proc_flags |= DSCP_POLICY_PROC_FLAG_DEL_PENDING;
		} else {
			entry->proc_flags |= DSCP_POLICY_PROC_FLAG_ADD_PENDING;
		}
	} else if (policy_attr->req_type == POLICY_REQ_TYPE_REMOVE) {
		entry->proc_flags |= DSCP_POLICY_PROC_FLAG_DEL_PENDING;
		ret_val = BCME_OK;
	} else {
		goto done;
	}

	/* update policy attribute */
	entry->policy_id = policy_attr->policy_id;
	entry->req_type = policy_attr->req_type;
	entry->dscp = policy_attr->dscp;

	/* port range attribute */
	if (pr_attr) {
		entry->start_port = pr_attr->start_port;
		entry->end_port = pr_attr->end_port;
		ret_val = BCME_OK;
	}

	/* update tclas attribute */
	if (tclas_attr) {

		if (tclas_attr->len == DOT11_TCLAS_FC_4_IPV4_LEN) {

			dot11_tclas_fc_4_ipv4_t *fc4_ipv4 =
			        (dot11_tclas_fc_4_ipv4_t *) tclas_attr->data;

			if (fc4_ipv4->version == IP_VER_4) {

				entry->ct4.src_port = fc4_ipv4->src_port;
				entry->ct4.dst_port = fc4_ipv4->dst_port;
				entry->ct4.dscp = fc4_ipv4->dscp;
				entry->ct4.proto_or_nh = fc4_ipv4->protocol;
				entry->ct4.ip_version = fc4_ipv4->version;
				entry->ct4.classifier_mask = fc4_ipv4->mask;

				/* Both src_ip and dst_ip are 16 bytes long,
				 * copy only IPv4 length.
				 */
				ret_val = memcpy_s(entry->ct4.src_ip, sizeof(entry->ct4.src_ip),
				                   &fc4_ipv4->src_ip, IPV4_ADDR_LEN);
				if (ret_val != BCME_OK) {
					goto done;
				}

				ret_val = memcpy_s(entry->ct4.dst_ip, sizeof(entry->ct4.dst_ip),
				                   &fc4_ipv4->dst_ip, IPV4_ADDR_LEN);
				if (ret_val != BCME_OK) {
					goto done;
				}
			} else {
				goto done;
			}

		} else if (tclas_attr->len == DOT11_TCLAS_FC_4_IPV6_LEN) {

			dot11_tclas_fc_4_ipv6_t *fc4_ipv6 =
			        (dot11_tclas_fc_4_ipv6_t *) tclas_attr->data;

			if (fc4_ipv6->version == IP_VER_6) {
				entry->ct4.src_port = fc4_ipv6->src_port;
				entry->ct4.dst_port = fc4_ipv6->dst_port;
				entry->ct4.dscp = fc4_ipv6->dscp;
				entry->ct4.proto_or_nh = fc4_ipv6->nexthdr;
				entry->ct4.ip_version = fc4_ipv6->version;
				entry->ct4.classifier_mask = fc4_ipv6->mask;
				ret_val = BCME_OK;
			} else {
				goto done;
			}
		} else {
			goto done;
		}
	}

	/* update domain name attribute */
	if (dn_attr) {
		entry->domain_name_len = dn_attr->len;
		ret_val = memcpy_s(entry->domain_name, entry->domain_name_len,
		                   dn_attr->data, dn_attr->len);
		if (ret_val != BCME_OK) {
			entry->domain_name_len = 0x00;
			goto done;
		}
	}
done:
	return ret_val;
}

/*
 * Format the DSCP policy table into the incoming buffer
 */
int
dhd_dscp_policy_dump(struct net_device *ndev, char *buf, uint buflen)
{
	unsigned long flags;
	uint16 count;
	wl_qos_rav_scs_ct4_v1_t *ct4;
	struct bcmstrbuf b;
	struct bcmstrbuf *strbuf = &b;
	char ip_buf[128];
	uint8 dn_name[DOMAIN_NAME_SIZE_MAX + 1];
	dscp_policy_info_t *policy_info;
	int ret_val;
	struct bcm_cfg80211 *cfg;

	cfg = wl_get_cfg(ndev);
	if (cfg == NULL) {
		return (BCME_ERROR);
	}

	policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;

	if ((policy_info == NULL) || (policy_info->policy_entries == NULL)) {
		return BCME_ERROR;
	}

	if (!buf) {
		return BCME_ERROR;
	}

	bcm_binit(strbuf, buf, buflen);

	bcm_bprintf(strbuf, "==== START: DSCP Policy Table ====\n\n");

	DHD_DSCP_POLICY_LOCK(policy_info->dscp_policy_lock, flags);
	bcm_bprintf(strbuf, "sta_mac = " MACDBG "\n", MAC2STRDBG(policy_info->sta_mac.octet));
	bcm_bprintf(strbuf, "Num max policies = %d\n\n", policy_info->num_entries);
	for (count = 0; count < policy_info->num_entries; count++) {
		if (policy_info->policy_entries[count].policy_id == 0) {
			continue;
		}

		ct4 = &policy_info->policy_entries[count].ct4;

		bcm_bprintf(strbuf, "policy_id = %d, req_type = %d \n",
		            policy_info->policy_entries[count].policy_id,
		            policy_info->policy_entries[count].req_type);
		bcm_bprintf(strbuf, "dscp = %d \n", policy_info->policy_entries[count].dscp);

		/* IP info */
		bcm_bprintf(strbuf, "classifier_mask = %02X \n", ct4->classifier_mask);
		bcm_bprintf(strbuf, "IP version = %d \n", ct4->ip_version);

		if (ct4->ip_version == IP_VER_4) {

			uint32 addr = *((uint32 *) (&ct4->src_ip[0]));

			bcm_ip_ntoa((struct ipv4_addr *)&addr, ip_buf);
			bcm_bprintf(strbuf, "src_ip = %s \n", ip_buf);

			addr = *((uint32 *) (&ct4->dst_ip[0]));
			bcm_ip_ntoa((struct ipv4_addr *)&addr, ip_buf);
			bcm_bprintf(strbuf, "dst_ip = %s \n", ip_buf);

		} else {
			(void) bcm_ipv6_ntoa((void *)ct4->src_ip, ip_buf);
			bcm_bprintf(strbuf, "src_ip = %s \n", ip_buf);
			(void) bcm_ipv6_ntoa((void *)ct4->dst_ip, ip_buf);
			bcm_bprintf(strbuf, "dst_ip = %s \n", ip_buf);
		}

		bcm_bprintf(strbuf, "src_port = %d \n", ntoh16(ct4->src_port));
		bcm_bprintf(strbuf, "dst_port = %d \n", ntoh16(ct4->dst_port));
		bcm_bprintf(strbuf, "proto_or_nh = %d \n", ct4->proto_or_nh);

		bcm_bprintf(strbuf, "domain_name_len = %d \n",
		            policy_info->policy_entries[count].domain_name_len);
		if (policy_info->policy_entries[count].domain_name_len == 0) {
			bcm_bprintf(strbuf, "domain_name = INVALID \n");
		} else {
			ret_val = memcpy_s(dn_name, sizeof(dn_name) - 1u,
			                   policy_info->policy_entries[count].domain_name,
			                   policy_info->policy_entries[count].domain_name_len);
			if (ret_val != BCME_OK) {
				bcm_bprintf(strbuf, "domain_name = INVALID\n");
			} else {
				dn_name[policy_info->policy_entries[count].domain_name_len] = '\0';
				bcm_bprintf(strbuf, "domain_name = %s \n", dn_name);
			}
		}

		/* Add port range attribute info: start_port and end_port */
		bcm_bprintf(strbuf, "start_port = %d \n",
		            ntoh16(policy_info->policy_entries[count].start_port));
		bcm_bprintf(strbuf, "end_port = %d \n\n",
		            ntoh16(policy_info->policy_entries[count].end_port));
	}
	DHD_DSCP_POLICY_UNLOCK(policy_info->dscp_policy_lock, flags);

	bcm_bprintf(strbuf, "====  END: DSCP Policy Table ====\n");

	DHD_ERROR(("%s bufsize: %d free: %d\n", __FUNCTION__, buflen, strbuf->size));

	/* return remaining buffer length */
	return (!strbuf->size ? BCME_BUFTOOSHORT : strbuf->size);
}

/*
 * Reset (set policy_id to 0) the DSCP policy table.
 */
int
dhd_dscp_policy_flush(struct net_device *ndev)
{
	uint count;
	struct bcm_cfg80211 *cfg;
	int ret_val = BCME_ERROR;
	dscp_policy_info_t *policy_info;
	unsigned long flags;

	cfg = wl_get_cfg(ndev);
	if (cfg == NULL) {
		goto done;
	}

	policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;
	if ((policy_info == NULL) || (policy_info->policy_entries == NULL)) {
		goto done;
	}


	/* acquire the lock */
	DHD_DSCP_POLICY_LOCK(policy_info->dscp_policy_lock, flags);

	/* Reset all policy entries */
	for (count = 0; count < policy_info->num_entries; count++) {
		bzero(&policy_info->policy_entries[count],
		      sizeof(policy_info->policy_entries[count]));
	}

	/* release the lock */
	DHD_DSCP_POLICY_UNLOCK(policy_info->dscp_policy_lock, flags);

	ret_val = BCME_OK;
done:
	return ret_val;
}

/*
 * Process the incoming DSCP Policy vendor-specific action frame and send the response.
 * Parse all DSCP attributes and saves it in DSCP policy table.
 */
int
dhd_dscp_policy_process_vsaf(struct bcm_cfg80211 *cfg, struct net_device *dev,
                             uint8 *body, uint body_len)
{
	/* Generic vendor specifc action frame header */
	dot11_action_vs_frmhdr_t *vsafh;
	dscp_policy_req_action_vs_frmhdr_t *dp_vsafh;	/* DSCP Policy req frame header */
	qos_mgmt_ie_t *mgmt_ie = NULL;
	dscp_policy_port_range_attr_t *pr_attr;		/* Port range */
	dscp_policy_attr_t *policy_attr;		/* Policy */
	dscp_policy_tclas_attr_t *tclas_attr;		/* TCLAS */
	dscp_policy_domain_name_attr_t *dn_attr;	/* Domain name */
	uint8 mgmt_oui_type = QOS_MGMT_VSIE_OUI_TYPE;
	dscp_policy_info_t *policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;
	uint mgmt_oui_type_len = 1;
	uint attr_len;
	int ret_val = BCME_ERROR;
	uint8 *buf;
	uint buf_len;
	uint8 status = DSCP_POLICY_SC_SUCCESS;

	vsafh = (dot11_action_vs_frmhdr_t *) body;

	/* Check if the AP supports the DSCP policy */
	if  (is_ap_enabled_dscp_policy(cfg) == false) {
		DHD_ERROR(("** QoS Mgmt VSIE was missing from the AP \n"));
		ret_val = BCME_ERROR;
		goto done;
	}

	if (vsafh->subtype == DSCP_POLICY_REQ_FRAME) {

		/* handle DSCP Policy Request frame */
		if (body_len < sizeof(dscp_policy_req_action_vs_frmhdr_t)) {
			ret_val = BCME_BADLEN;
			goto done;
		}

		dp_vsafh = (dscp_policy_req_action_vs_frmhdr_t *) body;

		/* save the dialog token */
		policy_info->dialog_token = dp_vsafh->dialog_token;

		buf = (body + sizeof(dscp_policy_req_action_vs_frmhdr_t));
		buf_len = body_len - sizeof(dscp_policy_req_action_vs_frmhdr_t);

		/* parse one or more QoS Mgmt vendor specific IEs */
		while (buf_len && (mgmt_ie = (qos_mgmt_ie_t *)
		                   bcm_find_vendor_ie(buf, buf_len, WFA_OUI,
		                                      &mgmt_oui_type, mgmt_oui_type_len))) {

			attr_len = mgmt_ie->len - (WFA_OUI_LEN + sizeof(mgmt_ie->oui_type));

			/* Find the DSCP Port Range attribute */
			pr_attr = (dscp_policy_port_range_attr_t *)
			        bcm_parse_tlvs(mgmt_ie->data, attr_len,
			                       DSCP_POLICY_PORT_RANGE_ATTR);
			if (pr_attr) {
				if ((pr_attr->len !=
				     (DSCP_POLICY_PORT_RANGE_ATTR_SIZE - TLV_HDR_LEN)) ||
				    (pr_attr->end_port < pr_attr->start_port)) {
					DHD_ERROR(("*** DSCP wrong port range attr length \n"));
					pr_attr = NULL;
				}
			}

			/* Find the DSCP policy attribute */
			policy_attr = (dscp_policy_attr_t *)
			        bcm_parse_tlvs(mgmt_ie->data, attr_len, DSCP_POLICY_ATTR);
			if (policy_attr) {
				if (policy_attr->len != (DSCP_POLICY_ATTR_SIZE - TLV_HDR_LEN)) {
					DHD_ERROR(("*** DSCP policy wrong policy attr length \n"));
					policy_attr = NULL;
				}
			}

			/* Find the DSCP TCLAS attribute */
			tclas_attr = (dscp_policy_tclas_attr_t *)
			        bcm_parse_tlvs(mgmt_ie->data, attr_len, DSCP_POLICY_TCLAS_ATTR);
			if (tclas_attr) {
				if (!((tclas_attr->len == DOT11_TCLAS_FC_4_IPV4_LEN) ||
				      (tclas_attr->len == DOT11_TCLAS_FC_4_IPV6_LEN))) {
					DHD_ERROR(("*** DSCP policy wrong tclass attr length \n"));
					tclas_attr = NULL;
				}
			}

			/* Find the DSCP Domain Name attribute */
			dn_attr = (dscp_policy_domain_name_attr_t *)
			        bcm_parse_tlvs(mgmt_ie->data, attr_len,
			                       DSCP_POLICY_DOMAIN_NAME_ATTR);
			if (dn_attr) {
				if (dn_attr->len == 0) {
					DHD_ERROR(("DSCP Policy wrong domain attr length \n"));
					dn_attr = NULL;
				}
			}

			/* update the policy information */
			ret_val = dhd_dscp_policy_update(cfg, pr_attr, policy_attr, tclas_attr,
			                                 dn_attr);
			if (ret_val != BCME_OK) {
				DHD_ERROR(("*** dhd_dscP_policy_update has failed \n"));
				goto done;
			}

			if ((mgmt_ie = (qos_mgmt_ie_t *)
			     bcm_next_tlv((const bcm_tlv_t *) mgmt_ie, &buf_len)) == NULL) {
				break;
			}

			buf = (uint8 *) mgmt_ie;
		}
	}

	/* check if the application asked to reject all policies */
	if (policy_info->policy_flags & DSCP_POLICY_FLAG_QUERY_REJECT_REQUEST) {
		status = DSCP_POLICY_SC_REQUEST_DECLINED;
	}

	/* Send the DSCP Policy response frame */
	ret_val = dhd_dscp_policy_send_response(cfg, dev, status, false);

done:
	if (ret_val != BCME_OK) {
		DHD_ERROR(("*** dhd_dscp_policy_process_vsaf() has failed = %d\n", ret_val));
	}

	return ret_val;
}

/*
 * Build the DSCP Policy response action frame rame based on various flags in the policy table.
 * If the MFP is enabled, builds the protected vendor-specific action frame.
 */
static int
dhd_dscp_policy_send_response(struct bcm_cfg80211 *cfg, struct net_device *dev,
                              dscp_policy_sc_e status, bool unsolicited)
{
	dscp_policy_resp_action_vs_frmhdr_t *resp_h;
	dscp_policy_status_t *dscp_status;
	uint16 count;
	uint8 *buf = NULL;
	uint16 buf_len = sizeof(dscp_policy_resp_action_vs_frmhdr_t);
	dscp_policy_info_t *policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;
	int ret_val;
	uint16 valid_entries = 0;
	struct wl_security *sec;
	unsigned long flags;

	/* acquire the lock */
	DHD_DSCP_POLICY_LOCK(policy_info->dscp_policy_lock, flags);

	/* find valid policy entries */
	for (count = 0; count < policy_info->num_entries; count++) {
		if ((policy_info->policy_entries[count].policy_id != 0) &&
		    ((policy_info->policy_entries[count].proc_flags &
		      DSCP_POLICY_PROC_FLAG_ADD_PENDING) ||
		     (policy_info->policy_entries[count].proc_flags &
		      DSCP_POLICY_PROC_FLAG_DEL_PENDING))) {
			valid_entries++;
		}
	}

	/* release the lock */
	DHD_DSCP_POLICY_UNLOCK(policy_info->dscp_policy_lock, flags);

	/* Compute the total length of the Policy Response frame */
	buf_len += (valid_entries * sizeof(dscp_policy_status_t));

	if ((buf = (uint8 *) MALLOCZ(cfg->osh, buf_len)) == NULL) {
		ret_val = BCME_NOMEM;
		goto done;
	}

	/* populate vendor specific action frame header */
	resp_h = (dscp_policy_resp_action_vs_frmhdr_t *) buf;

	/* Read MFP settings */
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);

	/* sec has the information about the security parameters
	 * for the current ongoing association.
	 */
	if (sec == NULL) {
		ret_val = BCME_ERROR;
		goto done;
	}

	if (sec->fw_mfp > WL_MFP_CAPABLE) {
		resp_h->category = DOT11_ACTION_CAT_VSP; /* protected */
		DHD_INFO(("*** Sending protected action frame ...\n"));
	} else {
		resp_h->category = DOT11_ACTION_CAT_VS; /* non-protected */
		DHD_INFO(("*** Sending non-protected action frame ...\n"));
	}

	/* Copy WFA OUI */
	ret_val = memcpy_s(resp_h->oui, sizeof(resp_h->oui), WFA_OUI, WFA_OUI_LEN);
	if (ret_val != BCME_OK) {
		goto done;
	}

	resp_h->oui_type = DSCP_POLICY_AF_OUI_TYPE;
	resp_h->oui_subtype = DSCP_POLICY_RESP_FRAME;	/* DSCP_POLICY_RESPONSE */
	resp_h->dialog_token = ((unsolicited == true) ? 0x00 : policy_info->dialog_token);
	resp_h->control = 0x00;
	resp_h->count = valid_entries;

	/* populate the status list */
	valid_entries = 0;

		/* acquire the lock */
	DHD_DSCP_POLICY_LOCK(policy_info->dscp_policy_lock, flags);

	for (count = 0; count < policy_info->num_entries; count++) {
		if ((policy_info->policy_entries[count].policy_id != 0) &&
		    ((policy_info->policy_entries[count].proc_flags &
		      DSCP_POLICY_PROC_FLAG_ADD_PENDING) ||
		     (policy_info->policy_entries[count].proc_flags &
		      DSCP_POLICY_PROC_FLAG_DEL_PENDING))) {
			dscp_status = (dscp_policy_status_t *)
				(resp_h->data + (valid_entries * sizeof(dscp_policy_status_t)));
			dscp_status->policy_id = policy_info->policy_entries[count].policy_id;
			dscp_status->status = (uint8) status;

			valid_entries++;

			/* clear the entry if it was delete */
			if (policy_info->policy_entries[count].proc_flags &
			    DSCP_POLICY_PROC_FLAG_DEL_PENDING) {
				bzero(&policy_info->policy_entries[count],
				      sizeof(dscp_policy_entry_t));
			}

			/* reset the flags */
			policy_info->policy_entries[count].proc_flags &=
				~DSCP_POLICY_PROC_FLAG_ADD_PENDING;
			policy_info->policy_entries[count].proc_flags &=
				~DSCP_POLICY_PROC_FLAG_DEL_PENDING;
		}
	}

	/* Reset flags, go back to normal operation accepeting the requests */
	policy_info->policy_flags &= ~DSCP_POLICY_FLAG_QUERY_REJECT_REQUEST;

	/* release the lock */
	DHD_DSCP_POLICY_UNLOCK(policy_info->dscp_policy_lock, flags);

	if (wl_dbg_level & WL_DBG_DBG) {
		prhex("Display of DSCP response", buf, buf_len);
	}

	/* send the action frame */
	ret_val = dhd_dscp_policy_send_af(cfg, dev, buf, buf_len);
done:
	if (buf) {
		MFREE(cfg->osh, buf, buf_len);
	}

	return (ret_val);
}

/*
 * Adds the vendor-specific WFA Capabilities IE to the association request.
 * In the wpa_supplicant association path, wl_config_assoc_ies() calls this function to set
 * WFA Cap IE using the IOVAR.
 */
int
dhd_dscp_policy_set_vndr_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, int bssidx)
{
	uint8 *buf = NULL;
	uint16 buf_len = WLC_IOCTL_MAXLEN;
	int ret_val;
	wfa_cap_ie_t *wfa_cap_ie;
	uint8 *vndr_ie_buf = NULL;
	uint16 vndr_ie_buf_len;
	vndr_ie_setbuf_t *hdr;
	s32 iecount;
	s32 pktflag = VNDR_IE_ASSOCREQ_FLAG;
	uint8 cap_bits = 0;
	uint16 tot_cap_len = WFA_CAP_IE_HDR_SIZE + WFA_CAP_IE_DATA_LEN; /* WFA Capabilities IE */

	DHD_ERROR(("*** dhd_dscp_policy_set_vndr_ie has caled \n"));

	if (is_ap_enabled_dscp_policy(cfg) == false) {
		ret_val = BCME_ERROR;
		goto done;
	}

	vndr_ie_buf_len = sizeof(vndr_ie_setbuf_t) + tot_cap_len;

	vndr_ie_buf = (uint8 *) MALLOCZ(cfg->osh, vndr_ie_buf_len);
	if (vndr_ie_buf == NULL) {
		DHD_ERROR(("*** dhd_dscp_policy_set_vndr_ie: failed to allocate vendor buf "
		           "memory %d bytes\n", buf_len));
		ret_val = BCME_NOMEM;
		goto done;
	}

	hdr = (vndr_ie_setbuf_t *) vndr_ie_buf;

	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strlcpy(hdr->cmd, "add", sizeof(hdr->cmd));

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htol32(1);

	hdr->vndr_ie_buffer.iecount = iecount;

	/* Copy packet flags that indicate which packets will contain this IE */
	pktflag = htol32(pktflag);
	(void) memcpy((void *)&hdr->vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag, sizeof(u32));

	wfa_cap_ie = (wfa_cap_ie_t *) &hdr->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data;
	wfa_cap_ie->id = DOT11_MNG_VS_ID; /* 0xDD */
	wfa_cap_ie->len = tot_cap_len - TLV_HDR_LEN;

	wfa_cap_ie->capabilities_len = WFA_CAP_IE_DATA_LEN; /* WFA Capabilities length */

	/* Copy WFA OUI */
	ret_val = memcpy_s(wfa_cap_ie->oui, sizeof(wfa_cap_ie->oui), WFA_OUI, WFA_OUI_LEN);
	if (ret_val != BCME_OK) {
		goto done;
	}

	/* WFA capablities OUI type */
	wfa_cap_ie->oui_type = WFA_CAP_VSIE_OUI_TYPE;

	/* advertize the DSCP capability */
	cap_bits |= QOS_MGMT_CAP_DSCP_POLICY;
	ret_val = memcpy_s(wfa_cap_ie->capabilities, wfa_cap_ie->capabilities_len,
	                   &cap_bits, sizeof(cap_bits));
	if (ret_val != BCME_OK) {
		goto done;
	}

	buf = (uint8 *) MALLOCZ(cfg->osh, buf_len);
	if (buf == NULL) {
		DHD_ERROR(("*** dhd_dscp_policy_set_vndr_ie: failed to allocate "
		           "memory %d bytes\n", buf_len));
		ret_val = BCME_NOMEM;
		goto done;
	}

	if (wl_dbg_level & WL_DBG_DBG) {
		prhex("**** Sending vendor IE", vndr_ie_buf, vndr_ie_buf_len);
	}

	ret_val = wldev_iovar_setbuf_bsscfg(ndev, "vndr_ie", vndr_ie_buf, vndr_ie_buf_len,
	                                    buf, WLC_IOCTL_MAXLEN, bssidx, NULL);
	if (ret_val != BCME_OK) {
		DHD_ERROR(("*** failed to set the vndr_ie in the assoc request = %d\n", ret_val));
	}
done:
	if (buf) {
		MFREE(cfg->osh, buf, buf_len);
	}

	if (vndr_ie_buf) {
		MFREE(cfg->osh, vndr_ie_buf, vndr_ie_buf_len);
	}

	return ret_val;
}

/*
 * Sends the DSCP query to the AP. When STA sends the DSCP query, AP
 * responds with DSCP policy request frame with all the policies. By default all the policies
 * from the AP are accepted.
 *
 * The query_val parameter indicates whether to accpet or reject all policies. For test
 * purposes, value 0 is passed to the query_val.
 *
 * Returns BCME_OK on success and BCME_ERROR on all failures.
 */
int
dhd_dscp_policy_send_query(struct net_device *ndev, uint32 query_val, uint8 *dn, uint8 dn_len)
{
	uint16 buf_len = DSCP_POLICY_QUERY_ACTION_FRAME_HDR_SIZE;
	uint8 *buf = NULL;
	dscp_policy_query_action_vs_frmhdr_t *query_h;
	int ret_val;
	struct wl_security *sec;
	struct bcm_cfg80211 *cfg;
	dscp_policy_info_t *policy_info;
	unsigned long flags;
	uint8 dn_attr_len = 0;

	/* Allow only 0 or 1 for now.
	 * 0 means reject all policies from the DSCP request following DSCP query
	 * and 1 means accept all.
	 */
	if (query_val > 2) {
		ret_val = BCME_ERROR;
		goto done;
	}

	cfg = wl_get_cfg(ndev);
	if (cfg == NULL) {
		ret_val = BCME_ERROR;
		goto done;
	}

	policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;
	if (policy_info == NULL) {
		ret_val = BCME_ERROR;
		goto done;
	}

	/* check if the AP supports the DSCP policy */
	if  (is_ap_enabled_dscp_policy(cfg) == false) {
		ret_val = BCME_ERROR;
		goto done;
	}

	if ((query_val == 1) && (dn_len != 0) && (dn != NULL)) {
		dn_attr_len = DSCP_POLICY_DOMAIN_NAME_ATTR_SIZE + dn_len;
		buf_len += (QOS_MGMT_IE_HDR_SIZE + dn_attr_len);
	}

	if ((buf = (uint8 *) MALLOCZ(cfg->osh, buf_len)) == NULL) {
		ret_val = BCME_NOMEM;
		goto done;
	}

	/* populate vendor specific action frame header */
	query_h = (dscp_policy_query_action_vs_frmhdr_t *) buf;

	sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);

	/* sec has the information about the security parameters
	 * for the current ongoing association.
	 */
	if (sec == NULL) {
		ret_val = BCME_ERROR;
		goto done;
	}

	if (sec->fw_mfp > WL_MFP_CAPABLE) {
		query_h->category = DOT11_ACTION_CAT_VSP; /* protected */
		DHD_INFO(("*** Sending protected action frame ...\n"));
	} else {
		query_h->category = DOT11_ACTION_CAT_VS; /* non-protected */
		DHD_INFO(("*** Sending non-protected action frame ...\n"));
	}

	/* Copy WFA OUI */
	ret_val = memcpy_s(query_h->oui, sizeof(query_h->oui), WFA_OUI, WFA_OUI_LEN);
	if (ret_val != BCME_OK) {
		goto done;
	}

	query_h->oui_type = DSCP_POLICY_AF_OUI_TYPE;
	query_h->oui_subtype = DSCP_POLICY_QUERY_FRAME;	/* DSCP Policy Query frame */
	DSCP_POLICY_UPDATE_TOKEN(policy_info->dialog_token);
	query_h->dialog_token = policy_info->dialog_token;

	if ((query_val == 1) && (dn_len != 0) && (dn != NULL)) {
		dscp_policy_domain_name_attr_t *dn_attr;
		qos_mgmt_ie_t *qos_mgmt_ie;

		qos_mgmt_ie = (qos_mgmt_ie_t *) (buf + DSCP_POLICY_QUERY_ACTION_FRAME_HDR_SIZE);
		qos_mgmt_ie->id = DOT11_MNG_VS_ID; /* 0xDD */
		qos_mgmt_ie->len = QOS_MGMT_IE_HDR_SIZE - TLV_HDR_LEN + dn_attr_len;

		/* Copy WFA OUI */
		ret_val = memcpy_s(qos_mgmt_ie->oui, sizeof(qos_mgmt_ie->oui),
		                   WFA_OUI, WFA_OUI_LEN);
		if (ret_val != BCME_OK) {
			goto done;
		}
		qos_mgmt_ie->oui_type = QOS_MGMT_VSIE_OUI_TYPE;

		dn_attr = (dscp_policy_domain_name_attr_t *) qos_mgmt_ie->data;
		dn_attr->id = DSCP_POLICY_DOMAIN_NAME_ATTR;
		dn_attr->len = dn_len;

		ret_val = memcpy_s(dn_attr->data, dn_attr->len, dn, dn_len);
		if (ret_val != BCME_OK) {
			goto done;
		}
	}

	if (wl_dbg_level & WL_DBG_DBG) {
		prhex("Display of DSCP query frame", buf, buf_len);
	}

	ret_val = dhd_dscp_policy_send_af(cfg, ndev, buf, buf_len);
	if (ret_val == BCME_OK) {

		/* This is only for test purposes.
		 * Reject all the policies following the query frame.
		 */
		if (query_val == 0) {
			/* acquire the lock */
			DHD_DSCP_POLICY_LOCK(policy_info->dscp_policy_lock, flags);

			policy_info->policy_flags |= DSCP_POLICY_FLAG_QUERY_REJECT_REQUEST;

			/* release the lock */
			DHD_DSCP_POLICY_UNLOCK(policy_info->dscp_policy_lock, flags);
		}
	}
done:
	if (buf) {
		MFREE(cfg->osh, buf, buf_len);
	}

	return ret_val;
}

/*
 * Sends the action with the given buffer and its length.
 * Returns BCME_OK on success and BCME_ERROR on failures.
 */
static int
dhd_dscp_policy_send_af(struct bcm_cfg80211 *cfg, struct net_device *ndev,
                        uint8 *buf, uint16 buf_len)
{
	wl_af_params_v1_t *af_params = NULL;
	wl_action_frame_v1_t *action_frame;
	struct ether_addr tmp_bssid;
	struct channel_info ci;
	uint8 *smbuf  = NULL;
	int tmp_channel = 0;
	int ret_val;
	wl_af_params_v2_t *af_params_v2_p = NULL;
	u8 *af_params_iov_p = NULL;
	s32 af_params_iov_len = 0;
	uint16 wl_af_params_size = 0;

	/* fill up af_params */
	af_params = (wl_af_params_v1_t *)MALLOCZ(cfg->osh, WL_WIFI_AF_PARAMS_SIZE_V1);
	if (af_params == NULL) {
		ret_val = BCME_NOMEM;
		DHD_ERROR(("*** dhd_dscp_policy_send_response: unable to allocate frame\n"));
		goto done;
	}

	bzero(&tmp_bssid, ETHER_ADDR_LEN);
	ret_val = wldev_ioctl_get(ndev, WLC_GET_BSSID, &tmp_bssid, ETHER_ADDR_LEN);
	if (ret_val != BCME_OK) {
		DHD_ERROR(("dhd_dscp_policy_send_response: failed to get bssid,"
		           " ret_val = %d\n", ret_val));
		goto done;
	}

	bzero(&ci, sizeof(ci));
	ret_val = wldev_ioctl_get(ndev, WLC_GET_CHANNEL, &ci, sizeof(ci));
	if (ret_val != BCME_OK) {
		DHD_ERROR(("wldev_ioctl_get has failed to get channel,"
		           " ret_val = %d\n", ret_val));
		goto done;
	}

	tmp_channel = ci.hw_channel;

	af_params->channel = tmp_channel;
	af_params->dwell_time = DSCP_AF_DEFAULT_DWELL_TIME;
	eacopy(tmp_bssid.octet, af_params->BSSID.octet);
	action_frame = &af_params->action_frame;

	action_frame->packetId = 0;
	eacopy(tmp_bssid.octet, action_frame->da.octet);

	action_frame->len = buf_len;
	ret_val = memcpy_s(action_frame->data, action_frame->len, buf, buf_len);
	if (ret_val != BCME_OK) {
		goto done;
	}

	smbuf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (smbuf == NULL) {
		DHD_ERROR(("*** dhd_dscp_policy_send_response: failed to allocate "
		           "memory %d bytes\n", WLC_IOCTL_MAXLEN));
		ret_val = BCME_NOMEM;
		goto done;
	}

	if (cfg->actframe_params_ver == WL_ACTFRAME_VERSION_MAJOR_2) {
		wl_af_params_size = OFFSETOF(wl_af_params_v2_t, action_frame) +
			OFFSETOF(wl_action_frame_v2_t, data) +
			af_params->action_frame.len;
		af_params_v2_p = MALLOCZ(cfg->osh, wl_af_params_size);
		if (af_params_v2_p == NULL) {
			DHD_ERROR(("unable to allocate frame\n"));
			ret_val = -ENOMEM;
			goto done;
		}
		ret_val = wl_cfg80211_actframe_fillup_v2(cfg, ndev->ieee80211_ptr, ndev,
			af_params_v2_p, af_params, ndev->dev_addr, wl_af_params_size);
		if (ret_val != BCME_OK) {
			DHD_ERROR(("unable to fill actframe_params, ret %d\n", ret_val));
			goto done;
		}
		af_params_iov_p = (u8 *)af_params_v2_p;
		af_params_iov_len = wl_af_params_size;
	} else {
		af_params_iov_p = (u8 *)af_params;
		af_params_iov_len = sizeof(*af_params);
	}


	ret_val = wldev_iovar_setbuf(ndev, "actframe", af_params_iov_p,
	                             af_params_iov_len, smbuf, WLC_IOCTL_MAXLEN, NULL);
	if (ret_val != BCME_OK) {
		DHD_ERROR(("*** dhd_dscp_policy_send_response: failed to send action frame,"
		           " error = %d\n", ret_val));
	} else {
		DHD_INFO(("*** sending DSCP response action frame is a success \n"));
	}
done:

	if (af_params_v2_p) {
		MFREE(cfg->osh, af_params_v2_p, wl_af_params_size);
	}

	if (smbuf) {
		MFREE(cfg->osh, smbuf, WLC_IOCTL_MAXLEN);
	}

	if (af_params) {
		MFREE(cfg->osh, af_params, WL_WIFI_AF_PARAMS_SIZE_V1);
	}

	return ret_val;
}

/*
 * Sends the unsolicied DSCP response for a given policy id.
 */
int
dhd_dscp_policy_send_unsolicited_resp(struct net_device *ndev, uint8 policy_id)
{
	int ret_val = BCME_ERROR;
	dscp_policy_info_t *policy_info;
	uint16 counter;
	struct bcm_cfg80211 *cfg;
	bool policy_id_found = false;
	unsigned long flags;

	cfg = wl_get_cfg(ndev);
	if (cfg == NULL) {
		goto done;
	}

	policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;
	if (policy_info == NULL) {
		goto done;
	}

	/* check if the AP supports the DSCP policy */
	if  (is_ap_enabled_dscp_policy(cfg) == false) {
		goto done;
	}

	/* acquire the lock */
	DHD_DSCP_POLICY_LOCK(policy_info->dscp_policy_lock, flags);

	/* Go through the list and update if exists */
	for (counter = 0; counter < policy_info->num_entries; counter++) {
		if (policy_info->policy_entries[counter].policy_id == policy_id) {
			/* mark the entry to be deleted */
			policy_info->policy_entries[counter].proc_flags |=
			        DSCP_POLICY_PROC_FLAG_DEL_PENDING;
			policy_id_found = true;
			break;
		}
	}

	/* release the lock */
	DHD_DSCP_POLICY_UNLOCK(policy_info->dscp_policy_lock, flags);

	if (policy_id_found == true) {
		/* send unsolicited DSCP response frame */
		ret_val = dhd_dscp_policy_send_response(cfg, ndev,
		                                        DSCP_POLICY_SC_REQUEST_DECLINED, true);
	}
done:
	return ret_val;
}

/*
 * Finds the WFA Capabilities IE in the association response and copy into the policy info.
 * Returns BCME_OK on success and BCME_ERROR on failures.
 */
int
dhd_dscp_process_wfa_cap_ie(struct bcm_cfg80211 *cfg, const uint8 *ies, uint ies_len)
{
	wfa_cap_ie_t *wfa_cap_ie;
	uint8 wfa_cap_oui_type = WFA_CAP_VSIE_OUI_TYPE;
	uint wfa_cap_oui_type_len = 1;
	dscp_policy_info_t *policy_info = (dscp_policy_info_t *) cfg->dscp_policy_info;
	uint8 wfa_cap_data_len;
	int ret_val = BCME_OK;

	/* Look for vendor-specific WFA Capabilities IE in the association response ies */
	wfa_cap_ie = (wfa_cap_ie_t *) bcm_find_vendor_ie(ies, ies_len,
	                                                 WFA_OUI, &wfa_cap_oui_type,
	                                                 wfa_cap_oui_type_len);
	if (wfa_cap_ie == NULL) {
		DHD_INFO(("no wfa cap IE\n"));
		goto done;
	}

	if ((wfa_cap_ie->len < (WFA_CAP_IE_HDR_SIZE - TLV_HDR_LEN)) ||
	    (wfa_cap_ie->capabilities_len == 0) ||
	    (wfa_cap_ie->len < ((WFA_CAP_IE_HDR_SIZE - TLV_HDR_LEN) +
	                        (wfa_cap_ie->capabilities_len)))) {
		ret_val = BCME_BADLEN;
		goto done;
	}

	wfa_cap_data_len = MIN(wfa_cap_ie->capabilities_len, WFA_CAP_IE_MAX_DATA_LEN);
	ret_val = memcpy_s(policy_info->wfa_caps, sizeof(policy_info->wfa_caps),
	                   wfa_cap_ie->capabilities, wfa_cap_data_len);

	if (ret_val != BCME_OK) {
		DHD_ERROR(("*** Couldn't find the WFA Capabiliites IE \n"));
	}
done:
	return ret_val;
}

/* Returns true if the WFA Capabilities IE has the QOS_MGMT_CAP_DSCP_POLICY bit set.
 */
static inline bool
is_ap_enabled_dscp_policy(struct bcm_cfg80211 *cfg)
{
	BCM_REFERENCE(cfg);

	/* TODO: Firmware needs to include the WFA capabilities IE in the assoc request based
	 * on the beacon.
	 *
	 * For now, returns TRUE so that DHD sets the WFA capabilities IE using the IOVAR.
	 */
	return (TRUE);
}
