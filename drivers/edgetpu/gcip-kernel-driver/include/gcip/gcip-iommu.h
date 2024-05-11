/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Manages GCIP IOMMU domains and allocates/maps IOVAs.
 *
 * One can replace allocating IOVAs via Linux DMA interface which will allocate and map them to
 * the default IOMMU domain with this framework. This framework will allocate and map IOVAs to the
 * specific IOMMU domain directly. This has following two advantages:
 *
 * - Can remove the mapping time by once as it maps to the target IOMMU domain directly.
 * - IOMMU domains don't have to share the total capacity.
 *
 * GCIP IOMMU domain is implemented by utilizing multiple kinds of IOVA space pool:
 * - struct iova_domain
 * - struct gcip_mem_pool
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __GCIP_IOMMU_H__
#define __GCIP_IOMMU_H__

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/scatterlist.h>
#include <linux/version.h>

#include <gcip/gcip-domain-pool.h>
#include <gcip/gcip-mem-pool.h>

/*
 * TODO(b/277649169) Best fit IOVA allocator was removed in 6.1 GKI
 * The API needs to either be upstreamed, integrated into this driver, or disabled for 6.1
 * compatibility. For now, disable best-fit on all non-Android kernels and any GKI > 5.15.
 */
#define HAS_IOVAD_BEST_FIT_ALGO                                                                    \
	(LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) &&                                          \
	 (IS_ENABLED(CONFIG_GCIP_TEST) || IS_ENABLED(CONFIG_ANDROID)))

/*
 * Helpers for manipulating @gcip_map_flags parameter of the `gcip_iommu_domain_{map,unmap}_sg`
 * functions.
 */
#define GCIP_MAP_FLAGS_DMA_DIRECTION_OFFSET 0
#define GCIP_MAP_FLAGS_DMA_DIRECTION_BIT_SIZE 2
#define GCIP_MAP_FLAGS_DMA_DIRECTION_TO_FLAGS(dir)                                                 \
	((u64)(dir) << GCIP_MAP_FLAGS_DMA_DIRECTION_OFFSET)

#define GCIP_MAP_FLAGS_DMA_COHERENT_OFFSET                                                         \
	(GCIP_MAP_FLAGS_DMA_DIRECTION_OFFSET + GCIP_MAP_FLAGS_DMA_DIRECTION_BIT_SIZE)
#define GCIP_MAP_FLAGS_DMA_COHERENT_BIT_SIZE 1
#define GCIP_MAP_FLAGS_DMA_COHERENT_TO_FLAGS(coherent)                                             \
	((u64)(coherent) << GCIP_MAP_FLAGS_DMA_COHERENT_OFFSET)

#define GCIP_MAP_FLAGS_DMA_ATTR_OFFSET                                                             \
	(GCIP_MAP_FLAGS_DMA_COHERENT_OFFSET + GCIP_MAP_FLAGS_DMA_COHERENT_BIT_SIZE)
#define GCIP_MAP_FLAGS_DMA_ATTR_BIT_SIZE 10
#define GCIP_MAP_FLAGS_DMA_ATTR_TO_FLAGS(attr) ((u64)(attr) << GCIP_MAP_FLAGS_DMA_ATTR_OFFSET)

#define GCIP_MAP_FLAGS_RESTRICT_IOVA_OFFSET                                                        \
	(GCIP_MAP_FLAGS_DMA_ATTR_OFFSET + GCIP_MAP_FLAGS_DMA_ATTR_BIT_SIZE)
#define GCIP_MAP_FLAGS_RESTRICT_IOVA_BIT_SIZE 1
#define GCIP_MAP_FLAGS_RESTRICT_IOVA_TO_FLAGS(restrict)                                            \
	((u64)(restrict) << GCIP_MAP_FLAGS_RESTRICT_IOVA_OFFSET)

struct gcip_iommu_domain_ops;

/*
 * Type of IOVA space pool that IOMMU domain will utilize.
 * Regardless of the type, its functionality will be the same. However, its implementation might be
 * different. For example, iova_domain uses red-black tree for the memory management, but gen_pool
 * uses bitmap. Therefore, their performance might be different and the kernel drivers can choose
 * which one to use according to its real use cases and the performance.
 *
 * Note: in legacy mode, only iova_domain is available as the Linux implementation utilizes that.
 */
enum gcip_iommu_domain_type {
	/* Uses iova_domain. */
	GCIP_IOMMU_DOMAIN_TYPE_IOVAD,
	/* Uses gcip_mem_pool which is based on gen_pool. */
	GCIP_IOMMU_DOMAIN_TYPE_MEM_POOL,
};

/*
 * IOMMU domain pool.
 *
 * It manages the pool of IOMMU domains. Also, it specifies the base address and the size of IOMMU
 * domains. Also, one can choose the data structure and algorithm of IOVA space management.
 */
struct gcip_iommu_domain_pool {
	struct device *dev;
	struct gcip_domain_pool domain_pool;
	dma_addr_t base_daddr;
	/* Will hold (base_daddr + size - 1) to prevent calculating it every IOVAD mappings. */
	dma_addr_t last_daddr;
	size_t size;
	size_t granule;
	bool best_fit;
	enum gcip_iommu_domain_type domain_type;
};

/*
 * Wrapper of iommu_domain.
 * It has its own IOVA space pool based on iova_domain or gcip_mem_pool. One can choose one of them
 * when calling the `gcip_iommu_domain_pool_init` function. See `enum gcip_iommu_domain_type`
 * for details.
 */
struct gcip_iommu_domain {
	struct device *dev;
	struct gcip_iommu_domain_pool *domain_pool;
	struct iommu_domain *domain;
	bool legacy_mode;
	bool default_domain;
	union {
		struct iova_domain iovad;
		struct gcip_mem_pool mem_pool;
	} iova_space;
	const struct gcip_iommu_domain_ops *ops;
};

/*
 * Holds operators which will be set according to the @domain_type.
 * These callbacks will be filled automatically when a `struct gcip_iommu_domain` is allocated.
 */
struct gcip_iommu_domain_ops {
	/* Initializes pool of @domain. */
	int (*initialize_domain)(struct gcip_iommu_domain *domain);
	/* Destroyes pool of @domain */
	void (*finalize_domain)(struct gcip_iommu_domain *domain);
	/*
	 * Enables best-fit algorithm for the memory management.
	 * Only domains which are allocated after calling this callback will be affected.
	 */
	void (*enable_best_fit_algo)(struct gcip_iommu_domain *domain);
	/* Allocates @size of IOVA space, optionally restricted to 32 bits, returns start IOVA. */
	dma_addr_t (*alloc_iova_space)(struct gcip_iommu_domain *domain, size_t size,
				       bool restrict_iova);
	/* Releases @size of buffer which was allocated to @iova. */
	void (*free_iova_space)(struct gcip_iommu_domain *domain, dma_addr_t iova, size_t size);
};

/*
 * Initializes an IOMMU domain pool.
 *
 * One can specify the base DMA address and IOVA space size via @base_daddr and @iova_space_size
 * parameters. If any of them is 0, it will try to parse "gcip-dma-window" property from the device
 * tree of @dev.
 *
 * If the base DMA address and IOVA space size are set successfully (i.e., larger than 0), IOMMU
 * domains allocated by this domain pool will have their own IOVA space pool and will map buffers
 * to their own IOMMU domain directly.
 *
 * Otherwise, it will fall into the legacy mode which will utilize the native DMA-IOMMU APIs.
 * In this mode, it will map the buffer to the default IOMMU domain first and then remap it to the
 * target domain.
 *
 * @pool: IOMMU domain pool to be initialized.
 * @dev: Device where to parse "gcip-dma-window" property.
 * @base_addr: The base address of IOVA space. Must be greater than 0 and a multiple of @granule.
 * @iova_space_size: The size of the IOVA space. @size must be a multiple of @granule.
 * @granule: The granule when invoking the IOMMU domain pool. Must be a power of 2.
 * @num_domains: The number of IOMMU domains.
 * @domain_type: Type of the IOMMU domain.
 *
 * Returns 0 on success or negative error value.
 */
int gcip_iommu_domain_pool_init(struct gcip_iommu_domain_pool *pool, struct device *dev,
				dma_addr_t base_daddr, size_t iova_space_size, size_t granule,
				unsigned int num_domains, enum gcip_iommu_domain_type domain_type);

/*
 * Destroys an IOMMU domain pool.
 *
 * @pool: IOMMU domain pool to be destroyed.
 */
void gcip_iommu_domain_pool_destroy(struct gcip_iommu_domain_pool *pool);

/*
 * Enables the best fit algorithm for allocating an IOVA space.
 * It affects domains which are allocated after calling this function only.
 *
 * @pool: IOMMU domain pool to be enabled.
 */
void gcip_iommu_domain_pool_enable_best_fit_algo(struct gcip_iommu_domain_pool *pool);

/*
 * Enables the legacy mode of allocating and mapping IOVA logic which utilizes native DMA-IOMMU
 * APIs of the Linux kernel.
 * It affects domains which are allocated after calling this function only.
 *
 * @pool: IOMMU domain pool to be enabled.
 */
void gcip_iommu_domain_pool_enable_legacy_mode(struct gcip_iommu_domain_pool *pool);

/*
 * Returns whether @pool is using legacy mode or not.
 *
 * @pool: IOMMU domain pool to be checked.
 */
static inline bool gcip_iommu_domain_pool_is_legacy_mode(struct gcip_iommu_domain_pool *pool)
{
	return !(pool && pool->size);
}

/*
 * Allocates a GCIP IOMMU domain.
 *
 * @pool: IOMMU domain pool.
 *
 * Returns a pointer of allocated domain on success or an error pointer on failure.
 */
struct gcip_iommu_domain *gcip_iommu_domain_pool_alloc_domain(struct gcip_iommu_domain_pool *pool);

/*
 * Releases a GCIP IOMMU domain.
 *
 * Before calling this function, you must unmap all IOVAs by calling `gcip_iommu_domain_unmap{_sg}`
 * functions.
 *
 * @pool: IOMMU domain pool.
 * @domain: GCIP IOMMU domain to be released.
 */
void gcip_iommu_domain_pool_free_domain(struct gcip_iommu_domain_pool *pool,
					struct gcip_iommu_domain *domain);

/*
 * Returns whether @domain is using legacy mode or not.
 *
 * @domain: GCIP IOMMU domain to be checked.
 */
static inline bool gcip_iommu_domain_is_legacy_mode(struct gcip_iommu_domain *domain)
{
	return domain->legacy_mode;
}

/*
 * Allocates an IOVA for the scatterlist and maps it to @domain.
 *
 * @domain: GCIP IOMMU domain which manages IOVA addresses.
 * @sgl: Scatterlist to be mapped.
 * @nents: The number of entries in @sgl.
 * @gcip_map_flags: Flags indicating mapping attributes.
 *
 * Bitfields:
 *   [1:0]   - DMA_DIRECTION:
 *               00 = DMA_BIDIRECTIONAL (host/device can write buffer)
 *               01 = DMA_TO_DEVICE     (host can write buffer)
 *               10 = DMA_FROM_DEVICE   (device can write buffer)
 *               (See [REDACTED]
 *   [2:2]   - Coherent Mapping:
 *               0 = Create non-coherent mappings of the buffer.
 *               1 = Create coherent mappings of the buffer.
 *   [12:3]  - DMA_ATTR:
 *               Not used in the non-legacy mode.
 *               (See [REDACTED]
 *   [13:13] - RESTRICT_IOVA:
 *               Restrict the IOVA assignment to 32 bit address window.
 *   [63:14] - RESERVED
 *               Set RESERVED bits to 0 to ensure backwards compatibility.
 *
 * One can use `GCIP_MAP_FLAGS_DMA_*_TO_FLAGS` macros to generate a flag.
 *
 * Returns the number of entries which are mapped to @domain. Returns 0 if it fails.
 */
unsigned int gcip_iommu_domain_map_sg(struct gcip_iommu_domain *domain, struct scatterlist *sgl,
				      int nents, u64 gcip_map_flags);

/*
 * Unmaps an IOVA which was mapped for the scatterlist.
 *
 * @domain: GCIP IOMMU domain which manages IOVA addresses.
 * @sgl: Scatterlist to be unmapped.
 * @gcip_map_flags: The same as the `gcip_iommu_domain_map_sg` function.
 *                  It will be ignored in the non-legacy mode.
 */
void gcip_iommu_domain_unmap_sg(struct gcip_iommu_domain *domain, struct scatterlist *sgl,
				int nents, u64 gcip_map_flags);

/*
 * Returns a default GCIP IOMMU domain.
 * This domain works with the legacy mode only.
 *
 * @dev: Device where to fetch the default IOMMU domain.
 */
struct gcip_iommu_domain *gcip_iommu_get_domain_for_dev(struct device *dev);

#endif /* __GCIP_IOMMU_H__ */
