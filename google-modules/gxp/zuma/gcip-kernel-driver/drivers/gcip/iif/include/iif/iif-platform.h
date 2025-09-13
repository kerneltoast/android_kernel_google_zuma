/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform device driver for inter-IP fence (IIF).
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __IIF_IIF_PLATFORM_H__
#define __IIF_IIF_PLATFORM_H__

#define IIF_DRIVER_NAME "iif"

const char *iif_platform_get_driver_commit(void);

#endif /* __IIF_IIF_PLATFORM_H__ */
