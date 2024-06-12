// SPDX-License-Identifier: GPL-2.0
/*
 * GXP IOMMU domain allocator.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/iommu.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

#include <gcip/gcip-iommu.h>

#include "gxp-dma.h"
#include "gxp-domain-pool.h"

/* If true, enable GCIP custom IOVA allocator. */
static bool gxp_gcip_dma_window_enable = true;
module_param_named(gcip_dma_window_enable, gxp_gcip_dma_window_enable, bool,
		   0660);

/*
 * See enum gcip_iommu_domain_type.
 * Default(0) = utilizing iova_domain
 */
static int gxp_gcip_iommu_domain_type;
module_param_named(gcip_iommu_domain_type, gxp_gcip_iommu_domain_type, int,
		   0660);

int gxp_domain_pool_init(struct gxp_dev *gxp,
			 struct gcip_iommu_domain_pool *pool, unsigned int size)
{
	int ret = gcip_iommu_domain_pool_init(pool, gxp->dev, 0, 0, SZ_4K, size,
					      gxp_gcip_iommu_domain_type);
	__maybe_unused int i;

	if (ret)
		return ret;

	if (!gxp_gcip_dma_window_enable)
		gcip_iommu_domain_pool_enable_legacy_mode(pool);
	gcip_iommu_domain_pool_enable_best_fit_algo(pool);

#if IS_ENABLED(CONFIG_GXP_GEM5)
	for (i = 0; i < size; i++) {
		struct iommu_domain *domain = pool->domain_pool.array[i];

		/*
		 * Gem5 uses arm-smmu-v3 which requires domain finalization to do iommu map. Calling
		 * iommu_aux_attach_device to finalize the allocated domain and detach the device
		 * right after that.
		 */
		ret = iommu_aux_attach_device(domain, gxp->dev);
		if (ret) {
			dev_err(gxp->dev,
				"Failed to attach device to iommu domain %d of %u, ret=%d\n",
				i + 1, size, ret);
			gcip_iommu_domain_pool_destroy(pool);
			return ret;
		}

		iommu_aux_detach_device(domain, gxp->dev);
	}
#endif /* CONFIG_GXP_GEM5 */

	return 0;
}

struct gcip_iommu_domain *
gxp_domain_pool_alloc(struct gcip_iommu_domain_pool *pool)
{
	struct gcip_iommu_domain *gdomain =
		gcip_iommu_domain_pool_alloc_domain(pool);

	if (IS_ERR_OR_NULL(gdomain))
		return NULL;

	return gdomain;
}

void gxp_domain_pool_free(struct gcip_iommu_domain_pool *pool,
			  struct gcip_iommu_domain *gdomain)
{
	gcip_iommu_domain_pool_free_domain(pool, gdomain);
}

void gxp_domain_pool_destroy(struct gcip_iommu_domain_pool *pool)
{
	gcip_iommu_domain_pool_destroy(pool);
}
