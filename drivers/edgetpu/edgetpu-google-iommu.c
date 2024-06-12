// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU IOMMU interface.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/iommu.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <gcip/gcip-iommu.h>

#include "edgetpu-config.h"
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
	struct gcip_iommu_domain *gdomains[EDGETPU_NCONTEXTS];
	/*
	 * Pointer to the default domain. `domains[0]` will always point to `default_domain`, if
	 * initialization of this structure is successful.
	 */
	struct gcip_iommu_domain *default_gdomain;
	/*
	 * Records IDs for all domains currently allocated, to support IOMMU (un)mapping
	 * when the domain is not attached.
	 */
	struct idr domain_id_pool;
	struct mutex pool_lock;		/* protects access of @domain_id_pool */
	bool context_0_default;		/* is context 0 domain the default? */
	bool aux_enabled;
	/*
	 * Holds a pool of pre-allocated IOMMU domains if the chip config specifies this is
	 * required.
	 * The implementation will fall back to dynamically allocated domains otherwise.
	 */
	struct gcip_iommu_domain_pool domain_pool;
};

/*
 * Return context ID enumeration value as a Process Address Space ID.
 * Caller ensures context_id is valid, i.e. does not equal to
 * EDGETPU_CONTEXT_INVALID or OR'ed with EDGETPU_CONTEXT_DOMAIN_TOKEN.
 */
static uint context_id_to_pasid(enum edgetpu_context_id context_id)
{
	return (uint)context_id;
}

static struct gcip_iommu_domain *get_domain_by_token(struct edgetpu_iommu *etiommu, int token)
{
	struct gcip_iommu_domain *gdomain;

	mutex_lock(&etiommu->pool_lock);
	gdomain = idr_find(&etiommu->domain_id_pool, token);
	mutex_unlock(&etiommu->pool_lock);
	return gdomain;
}

static struct gcip_iommu_domain *get_domain_by_context_id(struct edgetpu_dev *etdev,
							  enum edgetpu_context_id ctx_id)
{
	struct gcip_iommu_domain *gdomain = NULL;
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	uint pasid;

	/* always return the default domain when AUX is not supported */
	if (!etiommu->aux_enabled)
		return etiommu->default_gdomain;
	if (ctx_id == EDGETPU_CONTEXT_INVALID)
		return NULL;
	if (ctx_id & EDGETPU_CONTEXT_DOMAIN_TOKEN)
		return get_domain_by_token(
			etiommu, ctx_id ^ EDGETPU_CONTEXT_DOMAIN_TOKEN);
	pasid = context_id_to_pasid(ctx_id);
	if (pasid < EDGETPU_NCONTEXTS)
		gdomain = etiommu->gdomains[pasid];

	/* Fall back to default domain. */
	if (!gdomain)
		gdomain = etiommu->default_gdomain;
	return gdomain;
}

bool edgetpu_mmu_is_context_using_default_domain(struct edgetpu_dev *etdev,
						 enum edgetpu_context_id ctx_id)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;

	return get_domain_by_context_id(etdev, ctx_id) == etiommu->default_gdomain;
}

static int edgetpu_iommu_dev_fault_handler(struct iommu_fault *fault,
					   void *token)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)token;

	if (fault->type == IOMMU_FAULT_DMA_UNRECOV) {
		etdev_warn(etdev, "Unrecoverable IOMMU fault!\n");
		etdev_warn(etdev, "Reason = %08X\n", fault->event.reason);
		etdev_warn(etdev, "flags = %08X\n", fault->event.flags);
		etdev_warn(etdev, "pasid = %08X\n", fault->event.pasid);
		etdev_warn(etdev, "perms = %08X\n", fault->event.perm);
		etdev_warn(etdev, "addr = %llX\n", fault->event.addr);
		etdev_warn(etdev, "fetch_addr = %llX\n", fault->event.fetch_addr);
	} else if (fault->type == IOMMU_FAULT_PAGE_REQ) {
		etdev_dbg(etdev, "IOMMU page request fault!\n");
		etdev_dbg(etdev, "flags = %08X\n", fault->prm.flags);
		etdev_dbg(etdev, "pasid = %08X\n", fault->prm.pasid);
		etdev_dbg(etdev, "grpid = %08X\n", fault->prm.grpid);
		etdev_dbg(etdev, "perms = %08X\n", fault->prm.perm);
		etdev_dbg(etdev, "addr = %llX\n", fault->prm.addr);
	}
	// Tell the IOMMU driver to carry on
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

/* A callback for idr_for_each to release the domains */
static int edgetpu_idr_free_domain_callback(int id, void *p, void *data)
{
	struct gcip_iommu_domain *gdomain = p;
	struct edgetpu_iommu *etiommu = data;

	gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, gdomain);
	return 0;
}

static int edgetpu_iommu_fault_handler(struct iommu_domain *domain,
				       struct device *dev, unsigned long iova,
				       int flags, void *token)
{
	struct edgetpu_iommu_domain *etdomain = (struct edgetpu_iommu_domain *)token;

	dev_dbg(dev, "IOMMU fault on address %08lX. PASID = %u flags = %08X",
		iova, etdomain->pasid, flags);
	// Tell the IOMMU driver we are OK with this fault
	return 0;
}

static void edgetpu_init_etdomain(struct edgetpu_iommu_domain *etdomain,
				  struct gcip_iommu_domain *gdomain, int token)
{
	etdomain->gdomain = gdomain;
	etdomain->pasid = IOMMU_PASID_INVALID;
	etdomain->token = token;
	iommu_set_fault_handler(gdomain->domain, edgetpu_iommu_fault_handler, etdomain);
}

/*
 * Expect a default domain was already allocated for the group. If not try to
 * use the domain AUX feature to allocate one.
 */
static int check_default_domain(struct edgetpu_dev *etdev,
				struct edgetpu_iommu *etiommu)
{
	struct gcip_iommu_domain *gdomain;
	int ret;
	uint pasid;

	gdomain = gcip_iommu_get_domain_for_dev(etdev->dev);
	/* if default domain exists then we are done */
	if (gdomain) {
		etiommu->context_0_default = true;
		goto out;
	}
	etdev_warn(etdev, "device group has no default iommu domain\n");
	/* no default domain and no AUX - we can't have any domain */
	if (!etiommu->aux_enabled)
		return -EINVAL;

	gdomain = gcip_iommu_domain_pool_alloc_domain(&etiommu->domain_pool);
	if (!gdomain) {
		etdev_warn(etdev, "iommu domain alloc failed");
		return -EINVAL;
	}
	ret = iommu_aux_attach_device(gdomain->domain, etdev->dev);
	if (ret) {
		etdev_warn(etdev, "Attach IOMMU aux failed: %d", ret);
		gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, gdomain);
		return ret;
	}
	pasid = iommu_aux_get_pasid(gdomain->domain, etdev->dev);
	/* the default domain must have pasid = 0 */
	if (pasid != 0) {
		etdev_warn(etdev, "Invalid PASID %d returned from iommu\n",
			   pasid);
		iommu_aux_detach_device(gdomain->domain, etdev->dev);
		gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, gdomain);
		return -EINVAL;
	}
out:
	etiommu->default_gdomain = gdomain;
	etiommu->gdomains[0] = gdomain;
	return 0;
}

/* mmu_info is unused and NULL for IOMMU version, let IOMMU API supply info */
int edgetpu_mmu_attach(struct edgetpu_dev *etdev, void *mmu_info)
{
	struct edgetpu_iommu *etiommu;
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

	idr_init(&etiommu->domain_id_pool);
	mutex_init(&etiommu->pool_lock);
	etiommu->iommu_group = iommu_group_get(etdev->dev);
	if (etiommu->iommu_group)
		iommu_group_set_name(etiommu->iommu_group, "edgetpu");
	else
		dev_warn(etdev->dev, "device has no iommu group\n");

	iommu_dev_enable_feature(etdev->dev, IOMMU_DEV_FEAT_AUX);
	if (!iommu_dev_feature_enabled(etdev->dev, IOMMU_DEV_FEAT_AUX))
		etdev_warn(etdev, "AUX domains not supported\n");
	else
		etiommu->aux_enabled = true;
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
	int i, ret;

	if (!etiommu)
		return;

	ret = edgetpu_unregister_iommu_device_fault_handler(etdev);
	if (ret)
		etdev_warn(etdev,
			   "Failed to unregister device fault handler (%d)\n",
			   ret);
	for (i = etiommu->context_0_default ? 1 : 0; i < EDGETPU_NCONTEXTS;
	     i++) {
		if (etiommu->gdomains[i])
			iommu_aux_detach_device(etiommu->gdomains[i]->domain, etdev->dev);
	}

	if (etiommu->iommu_group)
		iommu_group_put(etiommu->iommu_group);

	/* free the domain if the context 0 domain is not default */
	if (!etiommu->context_0_default && etiommu->gdomains[0])
		gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, etiommu->gdomains[0]);

	idr_for_each(&etiommu->domain_id_pool, edgetpu_idr_free_domain_callback,
		     etiommu);
	idr_destroy(&etiommu->domain_id_pool);
	gcip_iommu_domain_pool_destroy(&etiommu->domain_pool);
	kfree(etiommu);
	etdev->mmu_cookie = NULL;
}

int edgetpu_mmu_map(struct edgetpu_dev *etdev, struct edgetpu_mapping *map,
		    enum edgetpu_context_id context_id, u32 mmu_flags)
{
	int ret;

	ret = edgetpu_mmu_map_sgt(etdev, &map->sgt, context_id, map->dir, map->dma_attrs,
				  mmu_flags);
	if (!ret)
		return -EINVAL;

	map->sgt.nents = ret;
	map->device_address = sg_dma_address(map->sgt.sgl);
	etdev_dbg(etdev, "%s: ctx=%x iova=%#llx dma=%#llx size=%#x flags=%#x\n",
		  __func__, context_id, map->device_address, (u64)sg_dma_address(map->sgt.sgl),
		  sg_dma_len(map->sgt.sgl), mmu_flags);
	return 0;
}

void edgetpu_mmu_unmap(struct edgetpu_dev *etdev, struct edgetpu_mapping *map,
		       enum edgetpu_context_id context_id)
{
	edgetpu_mmu_unmap_sgt(etdev, &map->sgt, context_id, map->dir, map->dma_attrs, 0);
}

int edgetpu_mmu_map_sgt(struct edgetpu_dev *etdev, struct sg_table *sgt,
			enum edgetpu_context_id context_id, enum dma_data_direction dir,
			unsigned long dma_attrs, u32 mmu_flags)

{
	struct gcip_iommu_domain *gdomain;
	u64 gcip_map_flags =
		GCIP_MAP_FLAGS_DMA_DIRECTION_TO_FLAGS(dir) |
		GCIP_MAP_FLAGS_DMA_COHERENT_TO_FLAGS((mmu_flags & EDGETPU_MMU_COHERENT) != 0) |
		GCIP_MAP_FLAGS_DMA_ATTR_TO_FLAGS(dma_attrs) |
		GCIP_MAP_FLAGS_RESTRICT_IOVA_TO_FLAGS(!(mmu_flags & EDGETPU_MMU_CC_NO_ACCESS));
	int ret;

	gdomain = get_domain_by_context_id(etdev, context_id);
	if (!gdomain) {
		etdev_err(etdev, "Unable to find an iommu_domain for context_id %#x\n", context_id);
		return 0;
	}

	ret = gcip_iommu_domain_map_sg(gdomain, sgt->sgl, sgt->nents, gcip_map_flags);
	if (!ret)
		return 0;

	/* TODO(b/281157263): Remove once gcip-iommu checks DMA_ATTR_SKIP_CPU_SYNC */
	if (!(dma_attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_sync_sg_for_device(etdev->dev, sgt->sgl, sgt->orig_nents, dir);

	etdev_dbg(etdev, "%s: ctx=%x iova=%#llx size=%#x flags=%#llx\n",
		  __func__, context_id, (u64)sg_dma_address(sgt->sgl), sg_dma_len(sgt->sgl),
		  gcip_map_flags);
	return ret;
}

void edgetpu_mmu_unmap_sgt(struct edgetpu_dev *etdev, struct sg_table *sgt,
			   enum edgetpu_context_id context_id, enum dma_data_direction dir,
			   unsigned long dma_attrs, u32 mmu_flags)
{
	struct gcip_iommu_domain *gdomain;
	u64 gcip_map_flags =
		GCIP_MAP_FLAGS_DMA_DIRECTION_TO_FLAGS(dir) |
		GCIP_MAP_FLAGS_DMA_COHERENT_TO_FLAGS((mmu_flags & EDGETPU_MMU_COHERENT) != 0) |
		GCIP_MAP_FLAGS_DMA_ATTR_TO_FLAGS(dma_attrs);

	gdomain = get_domain_by_context_id(etdev, context_id);
	if (!gdomain) {
		etdev_err(etdev, "Unable to find an iommu_domain\n");
		return;
	}

	/* TODO(b/281157263): Remove once gcip-iommu checks DMA_ATTR_SKIP_CPU_SYNC */
	if (!(dma_attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_sync_sg_for_cpu(etdev->dev, sgt->sgl, sgt->orig_nents, dir);

	gcip_iommu_domain_unmap_sg(gdomain, sgt->sgl, sgt->orig_nents, gcip_map_flags);
	etdev_dbg(etdev, "%s: ctx=%x iova=%#llx size=%#x\n",
		  __func__, context_id, (u64)sg_dma_address(sgt->sgl), sg_dma_len(sgt->sgl));
}

int edgetpu_mmu_map_iova_sgt(struct edgetpu_dev *etdev, tpu_addr_t iova,
			     struct sg_table *sgt, enum dma_data_direction dir,
			     u32 mmu_flags,
			     enum edgetpu_context_id context_id)
{
	const int prot = mmu_flag_to_iommu_prot(mmu_flags, etdev->dev, dir);
	const tpu_addr_t orig_iova = iova;
	struct scatterlist *sg;
	int i;
	int ret;

	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
		ret = edgetpu_mmu_add_translation(etdev, iova, sg_phys(sg),
						  sg->length, prot, context_id);
		if (ret)
			goto error;
		iova += sg->length;
	}
	etdev_dbg(etdev, "%s: ctx=%x iova=%#llx dma=%#llx size=%#llx dir=%d\n",
		  __func__, context_id, iova, (u64)sg_dma_address(sgt->sgl), orig_iova - iova, dir);
	return 0;

error:
	edgetpu_mmu_remove_translation(etdev, orig_iova, iova - orig_iova,
				       context_id);
	return ret;
}

void edgetpu_mmu_unmap_iova_sgt_attrs(struct edgetpu_dev *etdev,
				      tpu_addr_t iova, struct sg_table *sgt,
				      enum dma_data_direction dir,
				      enum edgetpu_context_id context_id,
				      unsigned long attrs)
{
	size_t size = 0;
	struct scatterlist *sg;
	int i;

	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
		size += sg->length;
	etdev_dbg(etdev, "%s: ctx=%x iova=%#llx size=%#zx\n", __func__, context_id, iova, size);
	edgetpu_mmu_remove_translation(etdev, iova, size, context_id);
}

int edgetpu_mmu_add_translation(struct edgetpu_dev *etdev, unsigned long iova,
				phys_addr_t paddr, size_t size, int prot,
				enum edgetpu_context_id context_id)
{
	struct gcip_iommu_domain *gdomain;

	etdev_dbg(etdev, "%s: ctx=%x iova=%#lx paddr=%pap size=%#zx prot=%#x\n",
		  __func__, context_id, iova, &paddr, size, prot);
	gdomain = get_domain_by_context_id(etdev, context_id);
	if (!gdomain)
		return -ENODEV;
	return iommu_map(gdomain->domain, iova, paddr, size, prot);
}

void edgetpu_mmu_remove_translation(struct edgetpu_dev *etdev,
				    unsigned long iova, size_t size,
				    enum edgetpu_context_id context_id)
{
	struct gcip_iommu_domain *gdomain;

	etdev_dbg(etdev, "%s: ctx=%x iova=%#lx size=%#zx\n", __func__, context_id, iova, size);
	gdomain = get_domain_by_context_id(etdev, context_id);
	if (gdomain)
		iommu_unmap(gdomain->domain, iova, size);
}

/* to be returned when domain aux is not supported */
static struct edgetpu_iommu_domain invalid_etdomain = {
	.pasid = IOMMU_PASID_INVALID,
	.token = EDGETPU_DOMAIN_TOKEN_END,
};

struct edgetpu_iommu_domain *edgetpu_mmu_alloc_domain(struct edgetpu_dev *etdev)
{
	struct edgetpu_iommu_domain *etdomain;
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	struct gcip_iommu_domain *gdomain;
	int token;

	if (!etiommu->aux_enabled)
		return &invalid_etdomain;
	gdomain = gcip_iommu_domain_pool_alloc_domain(&etiommu->domain_pool);
	if (!gdomain) {
		etdev_warn(etdev, "iommu domain allocation failed");
		return NULL;
	}

	etdomain = kzalloc(sizeof(*etdomain), GFP_KERNEL);
	if (!etdomain) {
		gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, gdomain);
		return NULL;
	}

	mutex_lock(&etiommu->pool_lock);
	token = idr_alloc(&etiommu->domain_id_pool, gdomain, 0, EDGETPU_DOMAIN_TOKEN_END,
			  GFP_KERNEL);
	mutex_unlock(&etiommu->pool_lock);
	if (token < 0) {
		etdev_warn(etdev, "alloc iommu domain token failed: %d", token);
		kfree(etdomain);
		gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, gdomain);
		return NULL;
	}

	edgetpu_init_etdomain(etdomain, gdomain, token);
	return etdomain;
}

void edgetpu_mmu_free_domain(struct edgetpu_dev *etdev,
			     struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;

	if (!etdomain || etdomain == &invalid_etdomain)
		return;
	if (etdomain->pasid != IOMMU_PASID_INVALID) {
		etdev_warn(etdev, "Domain should be detached before free");
		edgetpu_mmu_detach_domain(etdev, etdomain);
	}
	mutex_lock(&etiommu->pool_lock);
	idr_remove(&etiommu->domain_id_pool, etdomain->token);
	mutex_unlock(&etiommu->pool_lock);
	gcip_iommu_domain_pool_free_domain(&etiommu->domain_pool, etdomain->gdomain);
	kfree(etdomain);
}

int edgetpu_mmu_attach_domain(struct edgetpu_dev *etdev,
			      struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	struct gcip_iommu_domain *gdomain;
	int ret;
	uint pasid;

	/* Changes nothing if domain AUX is not supported. */
	if (!etiommu->aux_enabled)
		return 0;
	if (etdomain->pasid != IOMMU_PASID_INVALID)
		return -EINVAL;
	gdomain = etdomain->gdomain;
	ret = iommu_aux_attach_device(gdomain->domain, etdev->dev);
	if (ret) {
		etdev_warn(etdev, "Attach IOMMU aux failed: %d", ret);
		return ret;
	}
	pasid = iommu_aux_get_pasid(gdomain->domain, etdev->dev);
	if (pasid <= 0 || pasid >= EDGETPU_NCONTEXTS) {
		etdev_warn(etdev, "Invalid PASID %d returned from iommu",
			   pasid);
		ret = -EINVAL;
		goto err_detach;
	}
	/* the IOMMU driver returned a duplicate PASID */
	if (etiommu->gdomains[pasid]) {
		ret = -EBUSY;
		goto err_detach;
	}

	etiommu->gdomains[pasid] = gdomain;
	etdomain->pasid = pasid;
	return 0;
err_detach:
	iommu_aux_detach_device(gdomain->domain, etdev->dev);
	return ret;
}

void edgetpu_mmu_detach_domain(struct edgetpu_dev *etdev,
			       struct edgetpu_iommu_domain *etdomain)
{
	struct edgetpu_iommu *etiommu = etdev->mmu_cookie;
	uint pasid = etdomain->pasid;

	if (!etiommu->aux_enabled)
		return;
	if (pasid <= 0 || pasid >= EDGETPU_NCONTEXTS)
		return;

	etiommu->gdomains[pasid] = NULL;
	etdomain->pasid = IOMMU_PASID_INVALID;
	iommu_aux_detach_device(etdomain->gdomain->domain, etdev->dev);
}
