/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of the direct IIF.
 *
 * The direct IIF can be used if there is no underlying sync-unit (e.g., SSU) and IP firmwares are
 * communicating with each other directly to signal fences. Therefore, to support direct fences, we
 * also need support of the firmware side.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __IIF_IIF_DIRECT_H__
#define __IIF_IIF_DIRECT_H__

#include <iif/iif-manager.h>

extern const struct iif_manager_fence_ops iif_direct_fence_ops;

#endif /* __IIF_IIF_DIRECT_H__ */
