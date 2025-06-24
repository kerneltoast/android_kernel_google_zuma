// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU IOMMU interface.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <gcip/gcip-iommu.h>

#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"

#if !defined(EDGETPU_NUM_PREALLOCATED_DOMAINS)
#define EDGETPU_NUM_PREALLOCATED_DOMAINS 0
#endif

struct edgetpu_iommu {
	struct iommu_group *iommu_group;
	/*
	 * IOMMU domains currently attached.
	 * NULL for a slot that doesn't have an attached domain.
	 */
	struct edgetpu_iommu_domain *attached_etdomains[EDGETPU_NUM_PASIDS];
	/*
	 * Container for the default domain. `attached_etdomains[0]` will always point to
	 * `default_domain`, if initialization of this structure is successful.
	 */
	struct edgetpu_iommu_domain default_etdomain;
	bool context_0_default;		/* is context 0 domain the default? */
	/*
	 * Holds a pool of pre-allocated IOMMU domains if the chip config specifies this is
	 * required.
	 * The implementation will fall back to dynamically allocated domains otherwise.
	 */
	struct gcip_iommu_domain_pool domain_pool;
};

bool edgetpu_mmu_is_domain_default_domain(struct edgetpu_dev *etdev,
					  struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;

	return etdomain == &etiommu->default_etdomain;
}

#if EDGETPU_REPORT_PAGE_FAULT_ERRORS
static void report_page_fault(struct edgetpu_dev *etdev, u64 addr, u32 pasid, u32 flags, u32 perm)
{
	etdev_warn(etdev, "page fault addr=%#llx pasid=%u flags=%#x perm=%#x\n",
		   addr, pasid, flags, perm);
}
#else
static void report_page_fault(struct edgetpu_dev *etdev, u64 addr, u32 pasid, u32 flags, u32 perm)
{
	etdev_dbg(etdev, "page fault addr=%#llx pasid=%u flags=%#x perm=%#x\n",
		  addr, pasid, flags, perm);
}
#endif

static int edgetpu_check_fault(struct edgetpu_dev *etdev, struct iommu_fault *fault)
{
	u64 iova;
	uint pasid;

	if (fault->type == IOMMU_FAULT_PAGE_REQ) {
		iova = fault->prm.addr;
		pasid = fault->prm.pasid;
	} else if (fault->type == IOMMU_FAULT_DMA_UNRECOV) {
		iova = fault->event.addr;
		pasid = fault->event.pasid;
	} else {
		return -EAGAIN;
	}

	edgetpu_device_group_handle_fault(etdev, iova, pasid);
	return -EAGAIN;
}

static int edgetpu_iommu_dev_fault_handler(struct iommu_fault *fault, void *token)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)token;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL, DEFAULT_RATELIMIT_BURST);

	/* Optional debugging info / other fixup/handling for an IOMMU fault. */
	edgetpu_check_fault(etdev, fault);
	/* Ignore return, continue on with error reporting. */

	if (!__ratelimit(&rs))
		return -EAGAIN;

	if (fault->type == IOMMU_FAULT_DMA_UNRECOV) {
		if (fault->event.reason == IOMMU_FAULT_REASON_PTE_FETCH)
			report_page_fault(etdev, fault->event.addr, fault->event.pasid,
					  fault->event.flags, fault->event.perm);
		else
			etdev_warn(etdev, "iommu fault reason=%u addr=%#llx pasid=%u flags=%#x perm=%#x fetch_addr=%llx\n",
				   fault->event.reason, fault->event.addr, fault->event.pasid,
				   fault->event.flags, fault->event.perm, fault->event.fetch_addr);
	} else if (fault->type == IOMMU_FAULT_PAGE_REQ) {
		report_page_fault(etdev, fault->prm.addr, fault->prm.pasid, fault->prm.flags,
				  fault->prm.perm);
	}
	/* Tell the IOMMU driver to carry on */
	return -EAGAIN;
}

static int edgetpu_register_iommu_device_fault_handler(struct edgetpu_dev *etdev)
{
	etdev_dbg(etdev, "Registering IOMMU device fault handler\n");
	return iommu_register_device_fault_handler(etdev->dev, edgetpu_iommu_dev_fault_handler,
						   etdev);
}

static int edgetpu_unregister_iommu_device_fault_handler(struct edgetpu_dev *etdev)
{
	etdev_dbg(etdev, "Unregistering IOMMU device fault handler\n");
	return iommu_unregister_device_fault_handler(etdev->dev);
}

static void edgetpu_init_etdomain(struct edgetpu_iommu_domain *etdomain, struct edgetpu_dev *etdev,
				  struct gcip_iommu_domain *gdomain)
{
	etdomain->gdomain = gdomain;
	etdomain->pasid = IOMMU_PASID_INVALID;
}

/*
 * Expect a default domain was already allocated for the group. If not try to allocate and attach
 * one.
 */
static int check_default_domain(struct edgetpu_dev *etdev,
				struct edgetpu_iommu *etiommu)
{
	struct gcip_iommu_domain *gdomain;
	int ret;

	gdomain = gcip_iommu_get_domain_for_dev(etdev->dev);
	/* if default domain exists then we are done */
	if (!IS_ERR(gdomain)) {
		etiommu->context_0_default = true;
		goto out;
	}
	etdev_warn(etdev, "device group has no default iommu domain\n");

	gdomain = gcip_iommu_domain_pool_alloc_domain(&etiommu->domain_pool);
	if (IS_ERR(gdomain)) {
		etdev_warn(etdev, "iommu domain alloc failed");
		return PTR_ERR(gdomain);
	}

	ret = iommu_attach_device(gdomain->domain, etdev->dev);
	if (ret) {
		etdev_warn(etdev, "Attach default domain failed: %d", ret);
		gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, gdomain);
		return ret;
	}

out:
	etiommu->default_etdomain.pasid = 0;
	etiommu->default_etdomain.gdomain = gdomain;
	etiommu->attached_etdomains[0] = &etiommu->default_etdomain;
	return 0;
}

int edgetpu_mmu_attach(struct edgetpu_dev *etdev)
{
	struct edgetpu_iommu *etiommu;
	u32 num_bits, num_pasids;
	int ret;

	etiommu = kzalloc(sizeof(*etiommu), GFP_KERNEL);
	if (!etiommu)
		return -ENOMEM;
	/*
	 * Specify `base_addr` and `iova_space_size` as 0 so the gcip_iommu_domain_pool will obtain
	 * the values from the device tree.
	 */
	ret = gcip_iommu_domain_pool_init(&etiommu->domain_pool, etdev->dev, 0, 0, SZ_4K,
					  EDGETPU_NUM_PREALLOCATED_DOMAINS,
					  GCIP_IOMMU_DOMAIN_TYPE_IOVAD);
	if (ret) {
		etdev_err(etdev, "Unable create domain pool (%d)\n", ret);
		goto err_free_etiommu;
	}

	ret = of_property_read_u32(etdev->dev->of_node, "pasid-num-bits", &num_bits);
	if (ret || num_bits > 31) {
		/* TODO(b/285949227) remove fallback once device-trees are updated */
		etdev_warn(etdev, "Failed to fetch pasid-num-bits, defaulting to 8 PASIDs (%d)\n",
			   ret);
		num_pasids = 8;
	} else {
		num_pasids = BIT(num_bits);
	}

	/* PASID 0 is reserved for the default domain */
	gcip_iommu_domain_pool_set_pasid_range(&etiommu->domain_pool, 1, num_pasids - 1);

	etiommu->iommu_group = iommu_group_get(etdev->dev);
	if (etiommu->iommu_group)
		iommu_group_set_name(etiommu->iommu_group, "edgetpu");
	else
		dev_warn(etdev->dev, "device has no iommu group\n");

	ret = check_default_domain(etdev, etiommu);
	if (ret)
		goto err_destroy_pool;

	ret = edgetpu_register_iommu_device_fault_handler(etdev);
	if (ret)
		etdev_warn(etdev, "Failed to register fault handler! (%d)\n",
			   ret);

	/* etiommu initialization done */
	etdev->mmu_cookie = etiommu;
	return 0;

err_destroy_pool:
	gcip_iommu_domain_pool_destroy(&etiommu->domain_pool);
err_free_etiommu:
	kfree(etiommu);
	return ret;
}

void edgetpu_mmu_detach(struct edgetpu_dev *etdev)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	struct gcip_iommu_domain *gdomain;
	int i, ret;

	if (!etiommu)
		return;

	ret = edgetpu_unregister_iommu_device_fault_handler(etdev);
	if (ret)
		etdev_warn(etdev,
			   "Failed to unregister device fault handler (%d)\n",
			   ret);
	for (i = 1; i < EDGETPU_NUM_PASIDS; i++) {
		if (etiommu->attached_etdomains[i]) {
			gdomain = etiommu->attached_etdomains[i]->gdomain;
			gcip_iommu_domain_pool_detach_domain(&etiommu->domain_pool, gdomain);
		}
	}

	if (etiommu->iommu_group)
		iommu_group_put(etiommu->iommu_group);

	/* detach the domain if the context 0 domain is not default */
	if (!etiommu->context_0_default && etiommu->attached_etdomains[0]) {
		gdomain = etiommu->attached_etdomains[0]->gdomain;
		iommu_detach_device(gdomain->domain, etdev->dev);
	}

	/* domain_pool will free any remaining domains while being destroyed */
	gcip_iommu_domain_pool_destroy(&etiommu->domain_pool);
	kfree(etiommu);
	etdev->mmu_cookie = NULL;
}

int edgetpu_mmu_map_iova_sgt(struct edgetpu_dev *etdev, tpu_addr_t iova,
			     struct sg_table *sgt, enum dma_data_direction dir,
			     u32 mmu_flags,
			     struct edgetpu_iommu_domain *etdomain)
{
	const u64 gcip_map_flags = mmu_flag_to_gcip_flags(mmu_flags, dir);
	const tpu_addr_t orig_iova = iova;
	struct scatterlist *sg;
	int i;
	int ret;

	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
		ret = edgetpu_mmu_add_translation(etdev, iova, sg_phys(sg),
						  sg->length, gcip_map_flags, etdomain);
		if (ret)
			goto error;
		iova += sg->length;
	}
	sgt->nents = 1;
	sg = sgt->sgl;
	sg_dma_address(sg) = orig_iova;
	sg_dma_len(sg) = iova - orig_iova;
	etdev_dbg(etdev, "%s: pasid=%u iova=%pad size=%#x dir=%d\n", __func__, etdomain->pasid,
		  &sg_dma_address(sgt->sgl), sg_dma_len(sgt->sgl), dir);
	return 0;

error:
	edgetpu_mmu_remove_translation(etdev, orig_iova, iova - orig_iova, etdomain);
	return ret;
}

void edgetpu_mmu_unmap_iova_sgt_attrs(struct edgetpu_dev *etdev,
				      tpu_addr_t iova, struct sg_table *sgt,
				      enum dma_data_direction dir,
				      struct edgetpu_iommu_domain *etdomain,
				      unsigned long attrs)
{
	size_t size = 0;
	struct scatterlist *sg;
	int i;

	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
		size += sg->length;
	etdev_dbg(etdev, "%s: pasid=%u iova=%pad size=%#zx\n", __func__, etdomain->pasid, &iova,
		  size);
	edgetpu_mmu_remove_translation(etdev, iova, size, etdomain);
}

int edgetpu_mmu_add_translation(struct edgetpu_dev *etdev, unsigned long iova,
				phys_addr_t paddr, size_t size, u64 gcip_map_flags,
				struct edgetpu_iommu_domain *etdomain)
{
	if (!etdomain || !etdomain->gdomain)
		return -ENODEV;
	etdev_dbg(etdev, "%s: pasid=%u iova=%pad paddr=%pap size=%#zx flags=%#llx\n", __func__,
		  etdomain->pasid, &iova, &paddr, size, gcip_map_flags);
	return gcip_iommu_map(etdomain->gdomain, iova, paddr, size, gcip_map_flags);
}

void edgetpu_mmu_remove_translation(struct edgetpu_dev *etdev,
				    unsigned long iova, size_t size,
				    struct edgetpu_iommu_domain *etdomain)
{
	if (etdomain && etdomain->gdomain) {
		etdev_dbg(etdev, "%s: pasid=%u iova=%#lx size=%#zx\n", __func__, etdomain->pasid,
			  iova, size);
		gcip_iommu_unmap(etdomain->gdomain, iova, size);
	}
}

struct edgetpu_iommu_domain *edgetpu_mmu_alloc_domain(struct edgetpu_dev *etdev)
{
	struct edgetpu_iommu_domain *etdomain;
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	struct gcip_iommu_domain *gdomain;

	gdomain = gcip_iommu_domain_pool_alloc_domain(&etiommu->domain_pool);
	if (IS_ERR(gdomain)) {
		etdev_warn(etdev, "iommu domain allocation failed");
		return NULL;
	}

	etdomain = kzalloc(sizeof(*etdomain), GFP_KERNEL);
	if (!etdomain) {
		gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, gdomain);
		return NULL;
	}

	edgetpu_init_etdomain(etdomain, etdev, gdomain);
	return etdomain;
}

void edgetpu_mmu_free_domain(struct edgetpu_dev *etdev,
			     struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;

	if (!etdomain)
		return;
	if (etdomain->pasid != IOMMU_PASID_INVALID) {
		etdev_warn(etdev, "Domain should be detached before free");
		edgetpu_mmu_detach_domain(etdev, etdomain);
	}
	gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, etdomain->gdomain);
	kfree(etdomain);
}

int edgetpu_mmu_attach_domain(struct edgetpu_dev *etdev,
			      struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	int ret;
	ioasid_t pasid;

	if (etdomain->pasid != (ioasid_t)IOMMU_PASID_INVALID) {
		etdev_err(etdev, "Attempt to attach already-attached domain with PASID=%u",
			  etdomain->pasid);
		return -EINVAL;
	}

	ret = gcip_iommu_domain_pool_attach_domain(&etiommu->domain_pool, etdomain->gdomain);
	if (ret < 0) {
		etdev_warn(etdev, "Attach IOMMU domain failed: %d", ret);
		return ret;
	}

	pasid = etdomain->gdomain->pasid;
	etiommu->attached_etdomains[pasid] = etdomain;
	etdomain->pasid = pasid;
	/* Establish "shared to all contexts" mappings from the firmware image config. */
	edgetpu_firmware_shared_mappings_context_map(etdev, etdomain);
	return 0;
}

void edgetpu_mmu_detach_domain(struct edgetpu_dev *etdev,
			       struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	uint pasid = etdomain->pasid;

	if (pasid == IOMMU_PASID_INVALID || !pasid
	    || pasid >= EDGETPU_NUM_PASIDS)
		return;
	etiommu->attached_etdomains[pasid] = NULL;
	/* Unmap "shared to all contexts" mappings from the firmware image config. */
	edgetpu_firmware_shared_mappings_context_unmap(etdev, etdomain);
	etdomain->pasid = IOMMU_PASID_INVALID;
	gcip_iommu_domain_pool_detach_domain(&etiommu->domain_pool, etdomain->gdomain);
}

struct edgetpu_iommu_domain *edgetpu_mmu_domain_for_pasid(struct edgetpu_dev *etdev, uint pasid)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;

	if (pasid >= EDGETPU_NUM_PASIDS)
		return NULL;

	return etiommu->attached_etdomains[pasid];
}
