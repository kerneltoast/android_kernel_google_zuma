/*
 * Misc utility routines for accessing chip-specific features
 * of the SiliconBackplane-based Broadcom chips.
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
#include <bcmdefs.h>
#include <bcmutils.h>
#include <osl.h>
#include <siutils.h>
#include "siutils_priv.h"
#include <sbchipc.h>
#ifdef BCMSDIO
#include <bcmsdh.h>
#include <sbsdio.h>
#include <sdio.h>
#endif
#include <hndsoc.h>
#ifdef SOCI_NCI_BUS
#include <nci.h>
#endif

bool
si_buscore_prep(si_t *sih, uint bustype, uint devid, void *sdh)
{
	UNUSED_PARAMETER(sih);
	BCM_REFERENCE(bustype);
	BCM_REFERENCE(devid);
	BCM_REFERENCE(sdh);
#if defined(BCMSDIO) && !defined(BCMSDIOLITE)
	/* PR 39902, 43618, 44891, 41539 -- avoid backplane accesses that may
	 * cause SDIO clock requests before a stable ALP clock.  Originally had
	 * this later (just before srom_var_init() below) to guarantee ALP for
	 * CIS read, but due to these PRs moving it here before backplane use.
	 */
	/* As it precedes any backplane access, can't check chipid; but may
	 * be able to qualify with devid if underlying SDIO allows.  But should
	 * be ok for all our SDIO (4318 doesn't support clock and pullup regs,
	 * but the access attempts don't seem to hurt.)  Might elimiante the
	 * the need for ALP for CIS at all if underlying SDIO uses CMD53...
	 */
	if (BUSTYPE(bustype) == SDIO_BUS) {
		int err;
		uint8 clkset;

		/* Try forcing SDIO core to do ALPAvail request only */
		clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkset, &err);
		if (!err) {
			uint8 clkval;

			/* If register supported, wait for ALPAvail and then force ALP */
			clkval = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, NULL);
			if ((clkval & ~SBSDIO_AVBITS) == clkset) {
				SPINWAIT(((clkval = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
					SBSDIO_FUNC1_CHIPCLKCSR, NULL)), !SBSDIO_ALPAV(clkval)),
					PMU_MAX_TRANSITION_DLY);
				if (!SBSDIO_ALPAV(clkval)) {
					SI_ERROR(("timeout on ALPAV wait, clkval 0x%02x\n",
						clkval));
					return FALSE;
				}
				clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP;
				bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
					clkset, &err);
				/* PR 40613: account for possible ALP delay */
				OSL_DELAY(65);
			}
		}

		/* Also, disable the extra SDIO pull-ups */
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SDIOPULLUP, 0, NULL);
	}

#ifdef BCMSPI
	/* Avoid backplane accesses before wake-wlan (i.e. htavail) for spi.
	 * F1 read accesses may return correct data but with data-not-available dstatus bit set.
	 */
	if (BUSTYPE(bustype) == SPI_BUS) {

		int err;
		uint32 regdata;
		/* wake up wlan function :WAKE_UP goes as HT_AVAIL request in hardware */
		regdata = bcmsdh_cfg_read_word(sdh, SDIO_FUNC_0, SPID_CONFIG, NULL);
		SI_MSG(("F0 REG0 rd = 0x%x\n", regdata));
		regdata |= WAKE_UP;

		bcmsdh_cfg_write_word(sdh, SDIO_FUNC_0, SPID_CONFIG, regdata, &err);

		/* It takes time for wakeup to take effect. */
		OSL_DELAY(100000);
	}
#endif /* BCMSPI */
#endif /* BCMSDIO && !BCMSDIOLITE */

	return TRUE;
}

uint32
si_get_pmu_reg_addr(si_t *sih, uint32 offset)
{
	si_info_t *sii = SI_INFO(sih);
	uint32 pmuaddr = INVALID_ADDR;
	uint origidx = 0;

	SI_MSG(("si_get_pmu_reg_addr: pmu access, offset: %x\n", offset));
	if (!(sii->pub.cccaps & CC_CAP_PMU)) {
		goto done;
	}
	if (AOB_ENAB(&sii->pub)) {
		uint pmucoreidx;
		pmuregs_t *pmu;
		SI_MSG(("si_get_pmu_reg_addr: AOBENAB: %x\n", offset));
		origidx = sii->curidx;
		pmucoreidx = si_findcoreidx(&sii->pub, PMU_CORE_ID, 0);
		pmu = si_setcoreidx(&sii->pub, pmucoreidx);
		/* note: this function is used by dhd and possible 64 bit compilation needs
		 * a cast to (unsigned long) for avoiding a compilation error.
		 */
		pmuaddr = (uint32)(uintptr)((volatile uint8*)pmu + offset);
		si_setcoreidx(sih, origidx);
	} else
		pmuaddr = SI_ENUM_BASE(sih) + offset;

done:
	printf("si_get_pmu_reg_addr: addrRET: %x\n", pmuaddr);
	return pmuaddr;
}

static void
BCMATTACHFN(si_oob_war_BT_F1)(si_t *sih)
{
	uint origidx = si_coreidx(sih);
	volatile void *regs;

	regs = si_setcore(sih, AXI2AHB_BRIDGE_ID, 0);
	ASSERT(regs);
	BCM_REFERENCE(regs);

	si_wrapperreg(sih, AI_OOBSELINA30, 0xF00, 0x300);

	si_setcoreidx(sih, origidx);
}

/**
 * Allocate an si handle. This function may be called multiple times. This function is called by
 * both si_attach() and si_kattach().
 *
 * vars - pointer to a to-be created pointer area for "environment" variables. Some callers of this
 *        function set 'vars' to NULL.
 */
si_info_t *
BCMATTACHFN(si_doattach)(si_info_t *sii, uint devid, osl_t *osh, volatile void *regs,
                       uint bustype, void *sdh, char **vars, uint *varsz)
{
	struct si_pub *sih = &sii->pub;
	chipcregs_t *cc;
	uint origidx = SI_CC_IDX;

	ASSERT(GOODREGS(regs));

#ifdef SI_SPROM_PROBE
	si_sprom_init(sih);
#endif /* SI_SPROM_PROBE */

	cc = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);
	ASSERT(cc != NULL);

	W_REG(osh, CC_REG_ADDR(cc, GPIOPullup), 0);
	W_REG(osh, CC_REG_ADDR(cc, GPIOPulldown), 0);
	si_setcoreidx(sih, origidx);

	/* Skip PMU initialization from the Dongle Host.
	 * Firmware will take care of it when it comes up.
	 */

	/* clear any previous epidiag-induced target abort */
	ASSERT(!si_taclear(sih, FALSE));

	if (((PCIECOREREV(sih->buscorerev) == 66) || (PCIECOREREV(sih->buscorerev) == 68)) &&
		CST4378_CHIPMODE_BTOP(sih->chipst)) {
		/*
		 * HW4378-413 :
		 * BT oob connections for pcie function 1 seen at oob_ain[5] instead of oob_ain[1]
		 */
		si_oob_war_BT_F1(sih);
	}

	/* Enable AXI Error Immediate Enable Scheme */
	if ((GCIREV(sih->gcirev) == 26) ||
		((GCIREV(sih->gcirev) > 27) && GCIREV(sih->gcirev) != 29)) {
		SI_GCI_CC_WRITE(sih, nci_error_immediate_en, 1u);
	}

	return (sii);
}

/** Return the TCM-RAM size of the ARMCR4 core. */
uint32
si_tcm_size(si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	uint origidx;
	bcm_int_bitmask_t intr_val;
	volatile uint8 *regs;
	bool wasup;
	uint32 corecap;
	uint memsize = 0;
	uint banku_size = 0;
	uint32 nab = 0;
	uint32 nbb = 0;
	uint32 totb = 0;
	uint32 bxinfo = 0;
	uint32 idx = 0;
	volatile uint32 *arm_cap_reg;
	volatile uint32 *arm_bidx;
	volatile uint32 *arm_binfo;

	SI_MSG_DBG_REG(("%s: Enter\n", __FUNCTION__));
	/* Block ints and save current core */
	INTR_OFF(sii, &intr_val);
	origidx = si_coreidx(sih);

	/* Switch to CR4 core */
	if (!(regs = si_setcore(sih, ARMCR4_CORE_ID, 0)))
		goto done;

	/* Get info for determining size. If in reset, come out of reset,
	 * but remain in halt
	 */
	if (!(wasup = si_iscoreup(sih)))
		si_core_reset(sih, SICF_CPUHALT, SICF_CPUHALT);

	arm_cap_reg = (volatile uint32 *)(regs + SI_CR4_CAP);
	corecap = R_REG(sii->osh, arm_cap_reg);

	nab = (corecap & ARMCR4_TCBANB_MASK) >> ARMCR4_TCBANB_SHIFT;
	nbb = (corecap & ARMCR4_TCBBNB_MASK) >> ARMCR4_TCBBNB_SHIFT;
	totb = nab + nbb;

	arm_bidx = (volatile uint32 *)(regs + SI_CR4_BANKIDX);
	arm_binfo = (volatile uint32 *)(regs + SI_CR4_BANKINFO);
	for (idx = 0; idx < totb; idx++) {
		W_REG(sii->osh, arm_bidx, idx);

		bxinfo = R_REG(sii->osh, arm_binfo);
		if (bxinfo & ARMCR4_BUNITSZ_MASK) {
			banku_size = ARMCR4_BSZ_1K;
		} else {
			banku_size = ARMCR4_BSZ_8K;
		}
		memsize += ((bxinfo & ARMCR4_BSZ_MASK) + 1) * banku_size;
	}

	/* Return to previous state and core */
	if (!wasup)
		si_core_disable(sih, 0);
	si_setcoreidx(sih, origidx);

done:
	INTR_RESTORE(sii, &intr_val);
	SI_MSG_DBG_REG(("%s: Exit memsize=%d\n", __FUNCTION__, memsize));
	return memsize;
}

bool
si_has_flops(si_t *sih)
{
	uint origidx, cr4_rev;

	/* Find out CR4 core revision */
	origidx = si_coreidx(sih);
	if (si_setcore(sih, ARMCR4_CORE_ID, 0)) {
		cr4_rev = si_corerev(sih);
		si_setcoreidx(sih, origidx);

		if (cr4_rev == 1 || cr4_rev >= 3)
			return TRUE;
	}
	return FALSE;
}

uint32
si_get_coreaddr(si_t *sih, uint coreidx)
{
	if (CHIPTYPE(sih->socitype) == SOCI_NCI) {
		return nci_get_coreaddr(sih, coreidx);
	}

	return 0;
}

uint32
si_ccreg(si_t *sih, uint32 offset, uint32 mask, uint32 val)
{
	si_info_t *sii;
	uint32 reg_val = 0;

	sii = SI_INFO(sih);

	/* abort for invalid offset */
	if (offset > SI_CORE_SIZE)
		return 0;

	reg_val = si_corereg(&sii->pub, SI_CC_IDX, offset, mask, val);

	return reg_val;
}

/* read from pcie space using back plane  indirect access */
/* set below mask for reading 1, 2, 4 bytes in single read */
/* #define	SI_BPIND_1BYTE		0x1 */
/* #define	SI_BPIND_2BYTE		0x3 */
/* #define	SI_BPIND_4BYTE		0xF */
int
si_bpind_access(si_t *sih, uint32 addr_high, uint32 addr_low,
		int32 * data, bool read, uint32 us_timeout)
{

	uint32 status = 0;
	uint8 mask = SI_BPIND_4BYTE;
	int ret_val = BCME_OK;

	/* program address low and high fields */
	si_ccreg(sih, CC_REG_OFF(BackplaneAddrLow), ~0, addr_low);
	si_ccreg(sih, CC_REG_OFF(BackplaneAddrHi), ~0, addr_high);

	if (read) {
		/* start the read */
		si_ccreg(sih, CC_REG_OFF(BackplaneIndAccess), ~0,
			CC_BP_IND_ACCESS_START_MASK | mask);
	} else {
		/* write the data and force the trigger */
		si_ccreg(sih, CC_REG_OFF(BackplaneData), ~0, *data);
		si_ccreg(sih, CC_REG_OFF(BackplaneIndAccess), ~0,
			CC_BP_IND_ACCESS_START_MASK |
			CC_BP_IND_ACCESS_RDWR_MASK | mask);

	}

	/* Wait for status to be cleared */
	SPINWAIT(((status = si_ccreg(sih, CC_REG_OFF(BackplaneIndAccess), 0, 0)) &
		CC_BP_IND_ACCESS_START_MASK), us_timeout);

	if (status & (CC_BP_IND_ACCESS_START_MASK | CC_BP_IND_ACCESS_ERROR_MASK)) {
		ret_val = BCME_ERROR;
		SI_ERROR(("Action Failed for address 0x%08x:0x%08x \t status: 0x%x\n",
			addr_high, addr_low, status));
	} else if (read) { /* read data */
		*data = si_ccreg(sih, CC_REG_OFF(BackplaneData), 0, 0);
	}

	return ret_val;
}
