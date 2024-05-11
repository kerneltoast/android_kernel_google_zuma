// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *
 * Samsung DisplayPort driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __EXYNOS_HDCP_LOG_H__
#define __EXYNOS_HDCP_LOG_H__

#undef HDCP_DEBUG

#ifdef HDCP_DEBUG
#define hdcp_debug(fmt, args...)				\
	do {							\
		printk(KERN_ERR "exynos-drm-hdcp: %s:%d: " fmt,	\
				__func__, __LINE__, ##args);	\
	} while (0)
#else
#define hdcp_debug(fmt, args...)
#endif

#define hdcp_err(fmt, args...)				\
	do {						\
		printk(KERN_ERR "exynos-drm-hdcp: %s:%d: " fmt,	\
		       __func__, __LINE__, ##args);	\
	} while (0)

#define hdcp_info(fmt, args...)				\
	do {						\
		printk(KERN_INFO "exynos-drm-hdcp: %s:%d: " fmt,	\
			__func__, __LINE__, ##args);	\
	} while (0)
#endif
