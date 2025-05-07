/*
 * Misc utility routines for accessing chip-specific features
 * of the BOOKER NCI (non coherent interconnect) based Broadcom chips.
 * For DHD only.
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
#include <siutils.h>
#include "siutils_priv.h"
#include <nci.h>

uint32
nci_get_coreaddr(const si_t *sih, uint coreidx)
{
	const si_info_t *sii = SI_INFO(sih);
	struct nci_info *nci = sii->nci_info;

	return nci_get_curmap(nci, coreidx);
}
