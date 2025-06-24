/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wrapper to abstract #includes for systems regardless of whether they have GSA support
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __EDGETPU_GSA_H__
#define __EDGETPU_GSA_H__

#include "edgetpu-config.h"

#if EDGETPU_HAS_GSA

#include <linux/gsa/gsa_tpu.h>

#else

#include <linux/device.h>
#include <linux/types.h>

static inline int gsa_load_tpu_fw_image(struct device *gsa, dma_addr_t img_meta,
					phys_addr_t img_body)
{
	return -ENODEV;
}

static inline int gsa_unload_tpu_fw_image(struct device *gsa)
{
	return -ENODEV;
}

enum gsa_tpu_state {
	GSA_TPU_STATE_INACTIVE = 0,
	GSA_TPU_STATE_LOADED,
	GSA_TPU_STATE_RUNNING,
	GSA_TPU_STATE_SUSPENDED,
};

enum gsa_tpu_cmd {
	GSA_TPU_GET_STATE = 0,
	GSA_TPU_START,
	GSA_TPU_SUSPEND,
	GSA_TPU_RESUME,
	GSA_TPU_SHUTDOWN,
};

static inline int gsa_send_tpu_cmd(struct device *gsa, enum gsa_tpu_cmd cmd)
{
	return -ENODEV;
}

#endif /* EDGETPU_HAS_GSA */

#endif /* __EDGETPU_GSA_H__ */
