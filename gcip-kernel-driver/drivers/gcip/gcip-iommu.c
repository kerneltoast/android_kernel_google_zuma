// SPDX-License-Identifier: GPL-2.0-only
/*
 * Manages GCIP IOMMU domains and allocates/maps IOVAs.
 *
 * Copyright (C) 2023 Google LLC
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/genalloc.h>
#include <linux/iova.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <gcip/gcip-domain-pool.h>
#include <gcip/gcip-iommu.h>
#include <gcip/gcip-mem-pool.h>

#if HAS_IOVAD_BEST_FIT_ALGO
#include <linux/dma-iommu.h>
#endif

/* Macros for manipulating @gcip_map_flags parameter. */
#define GCIP_MAP_FLAGS_GET_VALUE(ATTR, flags)                                                      \
	(((flags) >> (GCIP_MAP_FLAGS_##ATTR##_OFFSET)) &                                           \
	 (BIT_ULL(GCIP_MAP_FLAGS_##ATTR##_BIT_SIZE) - 1))
#define GCIP_MAP_FLAGS_GET_DMA_DIRECTION(flags) GCIP_MAP_FLAGS_GET_VALUE(DMA_DIRECTION, flags)
#define GCIP_MAP_FLAGS_GET_DMA_COHERENT(flags) GCIP_MAP_FLAGS_GET_VALUE(DMA_COHERENT, flags)
#define GCIP_MAP_FLAGS_GET_DMA_ATTR(flags) GCIP_MAP_FLAGS_GET_VALUE(DMA_ATTR, flags)

/**
 * dma_info_to_prot - Translate DMA API directions and attributes to IOMMU API
 *                    page flags.
 * @dir: Direction of DMA transfer
 * @coherent: If true, create coherent mappings of the scatterlist.
 * @attrs: DMA attributes for the mapping
 *
 * See v5.15.94/source/drivers/iommu/dma-iommu.c#L418
 *
 * Return: corresponding IOMMU API page protection flags
 */
static int dma_info_to_prot(enum dma_data_direction dir, bool coherent, unsigned long attrs)
{
	int prot = coherent ? IOMMU_CACHE : 0;

	if (attrs & DMA_ATTR_PRIVILEGED)
		prot |= IOMMU_PRIV;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return prot | IOMMU_READ | IOMMU_WRITE;
	case DMA_TO_DEVICE:
		return prot | IOMMU_READ;
	case DMA_FROM_DEVICE:
		return prot | IOMMU_WRITE;
	default:
		return 0;
	}
}

static inline unsigned long gcip_iommu_domain_shift(struct gcip_iommu_domain *domain)
{
	return __ffs(domain->domain_pool->granule);
}

static inline unsigned long gcip_iommu_domain_pfn(struct gcip_iommu_domain *domain, dma_addr_t iova)
{
	return iova >> gcip_iommu_domain_shift(domain);
}

static inline size_t gcip_iommu_domain_align(struct gcip_iommu_domain *domain, size_t size)
{
	return ALIGN(size, domain->domain_pool->granule);
}

static int iovad_initialize_domain(struct gcip_iommu_domain *domain)
{
	struct gcip_iommu_domain_pool *dpool = domain->domain_pool;

	init_iova_domain(&domain->iova_space.iovad, dpool->granule,
			 max_t(unsigned long, 1, dpool->base_daddr >> ilog2(dpool->granule)));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
	return iova_domain_init_rcaches(&domain->iova_space.iovad);
#else
	return 0;
#endif
}

static void iovad_finalize_domain(struct gcip_iommu_domain *domain)
{
	put_iova_domain(&domain->iova_space.iovad);
}

static void iovad_enable_best_fit_algo(struct gcip_iommu_domain *domain)
{
#if HAS_IOVAD_BEST_FIT_ALGO
	domain->iova_space.iovad.best_fit = true;
#endif /* HAS_IOVAD_BEST_FIT_ALGO */
}

static dma_addr_t iovad_alloc_iova_space(struct gcip_iommu_domain *domain, size_t size)
{
	unsigned long iova, shift = gcip_iommu_domain_shift(domain);

	size = size >> shift;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
	/*
	 * alloc_iova_fast() makes use of a cache of recently freed IOVA pages which does not
	 * behave correctly for non-power-of-two amounts of pages. Round up the number of
	 * pages being allocated to ensure it's a safe number of pages.
	 *
	 * This rounding is done automatically as of 5.17
	 */
	if (size < (1 << (IOVA_RANGE_CACHE_MAX_SIZE - 1)))
		size = roundup_pow_of_two(size);
#endif

	iova = alloc_iova_fast(&domain->iova_space.iovad, size,
			       domain->domain_pool->last_daddr >> shift, true);

	return (dma_addr_t)iova << shift;
}

static void iovad_free_iova_space(struct gcip_iommu_domain *domain, dma_addr_t iova, size_t size)
{
	free_iova_fast(&domain->iova_space.iovad, gcip_iommu_domain_pfn(domain, iova),
		       size >> gcip_iommu_domain_shift(domain));
}

static const struct gcip_iommu_domain_ops iovad_ops = {
	.initialize_domain = iovad_initialize_domain,
	.finalize_domain = iovad_finalize_domain,
	.enable_best_fit_algo = iovad_enable_best_fit_algo,
	.alloc_iova_space = iovad_alloc_iova_space,
	.free_iova_space = iovad_free_iova_space,
};

static int mem_pool_initialize_domain(struct gcip_iommu_domain *domain)
{
	struct gcip_iommu_domain_pool *dpool = domain->domain_pool;
	int ret;

	ret = gcip_mem_pool_init(&domain->iova_space.mem_pool, dpool->dev, dpool->base_daddr,
				 dpool->size, dpool->granule);

	return ret;
}

static void mem_pool_finalize_domain(struct gcip_iommu_domain *domain)
{
	gcip_mem_pool_exit(&domain->iova_space.mem_pool);
}

static void mem_pool_enable_best_fit_algo(struct gcip_iommu_domain *domain)
{
	gen_pool_set_algo(domain->iova_space.mem_pool.gen_pool, gen_pool_best_fit, NULL);
}

static dma_addr_t mem_pool_alloc_iova_space(struct gcip_iommu_domain *domain, size_t size)
{
	return (dma_addr_t)gcip_mem_pool_alloc(&domain->iova_space.mem_pool, size);
}

static void mem_pool_free_iova_space(struct gcip_iommu_domain *domain, dma_addr_t iova, size_t size)
{
	gcip_mem_pool_free(&domain->iova_space.mem_pool, iova, size);
}

static const struct gcip_iommu_domain_ops mem_pool_ops = {
	.initialize_domain = mem_pool_initialize_domain,
	.finalize_domain = mem_pool_finalize_domain,
	.enable_best_fit_algo = mem_pool_enable_best_fit_algo,
	.alloc_iova_space = mem_pool_alloc_iova_space,
	.free_iova_space = mem_pool_free_iova_space,
};

static bool enable_best_fit_algo_legacy(struct gcip_iommu_domain_pool *pool)
{
	__maybe_unused int ret;

#if HAS_IOVAD_BEST_FIT_ALGO
	ret = iommu_dma_enable_best_fit_algo(pool->dev);
	if (!ret)
		return true;
	dev_warn(pool->dev, "Failed to enable best-fit IOMMU domain pool (%d)\n", ret);
#else
	dev_warn(pool->dev, "This env doesn't support best-fit algorithm in the legacy mode");
#endif
	return false;
}

static ssize_t dma_iommu_map_sg(struct gcip_iommu_domain *domain, struct scatterlist *sgl,
				int nents, enum dma_data_direction dir, unsigned long attrs,
				int prot)
{
	int nents_mapped;
	dma_addr_t iova;
	ssize_t ret;

	nents_mapped = dma_map_sg_attrs(domain->dev, sgl, nents, dir, attrs);
	if (!nents_mapped)
		return 0;

	if (!domain->default_domain) {
		iova = sg_dma_address(sgl);

		ret = (ssize_t)iommu_map_sg(domain->domain, iova, sgl, nents, prot);
		if (ret <= 0) {
			dma_unmap_sg_attrs(domain->dev, sgl, nents, dir, attrs);
			return 0;
		}
	}

	return nents_mapped;
}

static void dma_iommu_unmap_sg(struct gcip_iommu_domain *domain, struct scatterlist *sgl, int nents,
			       enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	size_t size = 0;
	int i;

	if (!domain->default_domain) {
		for_each_sg (sgl, sg, nents, i)
			size += sg_dma_len(sg);

		if (!iommu_unmap(domain->domain, sg_dma_address(sgl), size))
			dev_warn(domain->dev, "Failed to unmap sg");
	}
	dma_unmap_sg_attrs(domain->dev, sgl, nents, dir, attrs);
}

int gcip_iommu_domain_pool_init(struct gcip_iommu_domain_pool *pool, struct device *dev,
				dma_addr_t base_daddr, size_t iova_space_size, size_t granule,
				unsigned int num_domains, enum gcip_iommu_domain_type domain_type)
{
	const __be32 *user_window;
	int ret;

	ret = gcip_domain_pool_init(dev, &pool->domain_pool, num_domains);
	if (ret)
		return ret;

	pool->dev = dev;
	pool->base_daddr = base_daddr;
	pool->size = iova_space_size;
	pool->granule = granule;
	pool->best_fit = false;
	pool->domain_type = domain_type;

	if (!base_daddr || !iova_space_size) {
		user_window = of_get_property(dev->of_node, "gcip-dma-window", NULL);
		if (!user_window) {
			dev_warn(dev, "Failed to find gcip-dma-window property");
		} else {
			pool->base_daddr = of_read_number(user_window, 1);
			pool->size = of_read_number(user_window + 1, 1);
		}
	}

	if (!pool->base_daddr || !pool->size) {
		dev_warn(dev, "GCIP IOMMU domain pool is initialized as the legacy mode");
		pool->size = 0;
	} else {
		pool->last_daddr = pool->base_daddr + pool->size - 1;
	}

	dev_dbg(dev, "Init GCIP IOMMU domain pool, base_daddr=%#llx, size=%#zx", pool->base_daddr,
		pool->size);

	return 0;
}

void gcip_iommu_domain_pool_destroy(struct gcip_iommu_domain_pool *pool)
{
	gcip_domain_pool_destroy(&pool->domain_pool);
}

void gcip_iommu_domain_pool_enable_best_fit_algo(struct gcip_iommu_domain_pool *pool)
{
	if (gcip_iommu_domain_pool_is_legacy_mode(pool)) {
		pool->best_fit = enable_best_fit_algo_legacy(pool);
	} else if (pool->domain_type == GCIP_IOMMU_DOMAIN_TYPE_IOVAD && !HAS_IOVAD_BEST_FIT_ALGO) {
		dev_warn(pool->dev, "This env doesn't support best-fit algorithm with IOVAD");
		pool->best_fit = false;
	} else {
		pool->best_fit = true;
	}
}

void gcip_iommu_domain_pool_enable_legacy_mode(struct gcip_iommu_domain_pool *pool)
{
	pool->size = 0;
	pool->base_daddr = 0;

	if (pool->best_fit)
		pool->best_fit = enable_best_fit_algo_legacy(pool);
}

struct gcip_iommu_domain *gcip_iommu_domain_pool_alloc_domain(struct gcip_iommu_domain_pool *pool)
{
	struct gcip_iommu_domain *gdomain;
	int ret;

	gdomain = devm_kzalloc(pool->dev, sizeof(*gdomain), GFP_KERNEL);
	if (!gdomain)
		return ERR_PTR(-ENOMEM);

	gdomain->dev = pool->dev;
	gdomain->domain_pool = pool;
	gdomain->domain = gcip_domain_pool_alloc(&pool->domain_pool);
	if (IS_ERR_OR_NULL(gdomain->domain)) {
		ret = -ENOMEM;
		goto err_free_gdomain;
	}

	if (gcip_iommu_domain_pool_is_legacy_mode(pool)) {
		gdomain->legacy_mode = true;
		return gdomain;
	}

	switch (pool->domain_type) {
	case GCIP_IOMMU_DOMAIN_TYPE_IOVAD:
		gdomain->ops = &iovad_ops;
		break;
	case GCIP_IOMMU_DOMAIN_TYPE_MEM_POOL:
		gdomain->ops = &mem_pool_ops;
		break;
	default:
		ret = -EINVAL;
		goto err_free_domain_pool;
	}

	ret = gdomain->ops->initialize_domain(gdomain);
	if (ret)
		goto err_free_domain_pool;

	if (pool->best_fit)
		gdomain->ops->enable_best_fit_algo(gdomain);

	return gdomain;

err_free_domain_pool:
	gcip_domain_pool_free(&pool->domain_pool, gdomain->domain);
err_free_gdomain:
	devm_kfree(pool->dev, gdomain);
	return ERR_PTR(ret);
}

void gcip_iommu_domain_pool_free_domain(struct gcip_iommu_domain_pool *pool,
					struct gcip_iommu_domain *domain)
{
	if (!gcip_iommu_domain_is_legacy_mode(domain))
		domain->ops->finalize_domain(domain);
	gcip_domain_pool_free(&pool->domain_pool, domain->domain);
	devm_kfree(pool->dev, domain);
}

unsigned int gcip_iommu_domain_map_sg(struct gcip_iommu_domain *domain, struct scatterlist *sgl,
				      int nents, u64 gcip_map_flags)
{
	enum dma_data_direction dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);
	bool coherent = GCIP_MAP_FLAGS_GET_DMA_COHERENT(gcip_map_flags);
	unsigned long attrs = GCIP_MAP_FLAGS_GET_DMA_ATTR(gcip_map_flags);
	int i, prot = dma_info_to_prot(dir, coherent, attrs);
	struct scatterlist *sg;
	dma_addr_t iova;
	size_t iova_len = 0;
	ssize_t ret;

	if (gcip_iommu_domain_is_legacy_mode(domain))
		return dma_iommu_map_sg(domain, sgl, nents, dir, attrs, prot);

	/* Calculates how much IOVA space we need. */
	for_each_sg (sgl, sg, nents, i)
		iova_len += sg->length;

	/* Allocates one continuous IOVA. */
	iova = domain->ops->alloc_iova_space(domain, gcip_iommu_domain_align(domain, iova_len));
	if (!iova)
		return 0;

	/*
	 * Maps scatterlist to the allocated IOVA.
	 *
	 * It will iterate each scatter list segment in order and map them to the IOMMU domain
	 * as amount of the size of each segment successively.
	 * Returns an error on failure or the total length of mapped segments on success.
	 *
	 * Note: Before Linux 5.15, its return type was `size_t` and it returned 0 on failure.
	 *       To make it compatible with those old versions, we should cast the return value.
	 */
	ret = (ssize_t)iommu_map_sg(domain->domain, iova, sgl, nents, prot);
	if (ret < 0 || ret < iova_len)
		goto err_free_iova;

	/* Fills out the mapping information. */
	sg_dma_address(sgl) = iova;
	sg_dma_len(sgl) = iova_len;

	/* As it put the whole mapping information to the first segment, it should return 1. */
	return 1;

err_free_iova:
	domain->ops->free_iova_space(domain, iova, gcip_iommu_domain_align(domain, iova_len));
	return 0;
}

void gcip_iommu_domain_unmap_sg(struct gcip_iommu_domain *domain, struct scatterlist *sgl,
				int nents, u64 gcip_map_flags)
{
	dma_addr_t iova;
	size_t iova_len;

	if (gcip_iommu_domain_is_legacy_mode(domain)) {
		enum dma_data_direction dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);
		unsigned long attrs = GCIP_MAP_FLAGS_GET_DMA_ATTR(gcip_map_flags);

		dma_iommu_unmap_sg(domain, sgl, nents, dir, attrs);
		return;
	}

	iova = sg_dma_address(sgl);
	iova_len = sg_dma_len(sgl);

	iommu_unmap(domain->domain, iova, iova_len);
	domain->ops->free_iova_space(domain, iova, gcip_iommu_domain_align(domain, iova_len));
}

struct gcip_iommu_domain *gcip_iommu_get_domain_for_dev(struct device *dev)
{
	struct gcip_iommu_domain *gdomain;

	gdomain = devm_kzalloc(dev, sizeof(*gdomain), GFP_KERNEL);
	if (!gdomain)
		return ERR_PTR(-ENOMEM);

	gdomain->domain = iommu_get_domain_for_dev(dev);
	if (!gdomain->domain) {
		devm_kfree(dev, gdomain);
		return ERR_PTR(-ENODEV);
	}

	gdomain->dev = dev;
	gdomain->legacy_mode = true;
	gdomain->default_domain = true;

	return gdomain;
}
