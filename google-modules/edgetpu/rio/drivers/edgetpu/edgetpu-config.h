/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Defines chipset dependent configuration.
 *
 * Copyright (C) 2019-2025 Google LLC
 */

#ifndef __EDGETPU_CONFIG_H__
#define __EDGETPU_CONFIG_H__

#if IS_ENABLED(CONFIG_RIO)

#include "rio/config.h"

#else /* unknown */

#error "Unknown EdgeTPU config"

#endif /* unknown */

#define EDGETPU_DEFAULT_FIRMWARE_NAME "google/edgetpu-" DRIVER_NAME ".fw"
#define EDGETPU_TEST_FIRMWARE_NAME "google/edgetpu-" DRIVER_NAME "-test.fw"

#ifndef EDGETPU_NUM_CORES
#define EDGETPU_NUM_CORES 1
#endif

#ifndef EDGETPU_MAX_TELEMETRY_BUFFERS
#define EDGETPU_MAX_TELEMETRY_BUFFERS EDGETPU_NUM_CORES
#endif

/* By default IOMMU domains can be modified while detached from a mailbox.*/
#ifndef HAS_DETACHABLE_IOMMU_DOMAINS
#define HAS_DETACHABLE_IOMMU_DOMAINS	1
#endif

#ifndef EDGETPU_HAS_GSA
#define EDGETPU_HAS_GSA 1
#endif

#ifndef EDGETPU_ALLOW_NONSECURE_FW
#define EDGETPU_ALLOW_NONSECURE_FW 0
#endif

#ifndef EDGETPU_FEATURE_ALWAYS_ON
#define EDGETPU_FEATURE_ALWAYS_ON 0
#endif

#ifndef EDGETPU_USE_LITEBUF_VII
#define EDGETPU_USE_LITEBUF_VII 0
#endif

#ifndef EDGETPU_HAS_FW_DEBUG
#define EDGETPU_HAS_FW_DEBUG 0
#endif

#ifndef EDGETPU_REPORT_PAGE_FAULT_ERRORS
#define EDGETPU_REPORT_PAGE_FAULT_ERRORS 0
#endif

#ifndef EDGETPU_USE_IIF_MAILBOX
#define EDGETPU_USE_IIF_MAILBOX 0
#endif

#ifndef EDGETPU_NUM_VII_CREDITS_PER_CLIENT
#define EDGETPU_NUM_VII_CREDITS_PER_CLIENT 8
#endif

#endif /* __EDGETPU_CONFIG_H__ */
