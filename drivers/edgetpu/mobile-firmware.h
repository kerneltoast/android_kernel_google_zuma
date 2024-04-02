/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Implements utilities for firmware management of mobile chipsets.
 *
 * Copyright (C) 2021-2022 Google LLC
 */

#ifndef __MOBILE_FIRMWARE_H__
#define __MOBILE_FIRMWARE_H__

#include <linux/sizes.h>

#include <gcip/gcip-image-config.h>

#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"
#include "edgetpu.h"

#define MAX_IOMMU_MAPPINGS 23
#define MAX_NS_IOMMU_MAPPINGS 5

/* mobile FW header size */
#define MOBILE_FW_HEADER_SIZE SZ_4K

/*
 * Mobile firmware header.
 */

struct mobile_image_sub_header_common {
	int Magic;
	int Generation;
	int RollbackInfo;
	int Length;
	char Flags[16];
};

struct mobile_image_sub_header_gen1 {
	char BodyHash[32];
	char ChipId[32];
	char AuthConfig[256];
	struct gcip_image_config ImageConfig;
};

struct mobile_image_sub_header_gen2 {
	char BodyHash[64];
	char ChipId[32];
	char AuthConfig[256];
	struct gcip_image_config ImageConfig;
};

struct mobile_image_header {
	char sig[512];
	char pub[512];
	struct {
		struct mobile_image_sub_header_common common;
		union {
			struct mobile_image_sub_header_gen1 gen1;
			struct mobile_image_sub_header_gen2 gen2;
		};
	};
};

/* Value of Magic field above: 'TPUF' as a 32-bit LE int */
#define EDGETPU_MOBILE_FW_MAGIC	0x46555054

int edgetpu_mobile_firmware_create(struct edgetpu_dev *etdev);
void edgetpu_mobile_firmware_destroy(struct edgetpu_dev *etdev);

/*
 * Assert or release the reset signal of the TPU's CPU
 * Depending on privilege level, this may be by a direct register write
 * or a call into GSA.
 */
int edgetpu_mobile_firmware_reset_cpu(struct edgetpu_dev *etdev, bool assert_reset);

#endif /* __MOBILE_FIRMWARE_H__ */
