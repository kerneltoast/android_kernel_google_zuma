/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Wrapper for GSA related APIs.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#ifndef __GXP_GSA_H__
#define __GXP_GSA_H__

#include "gxp-config.h"

#if GXP_HAS_GSA

#include <linux/gsa/gsa_dsp.h>

#else

#include <linux/device.h>
#include <linux/types.h>

static inline int gsa_load_dsp_fw_image(struct device *gsa, dma_addr_t img_meta,
					phys_addr_t img_body)
{
	return 0;
}

static inline int gsa_unload_dsp_fw_image(struct device *gsa)
{
	return 0;
}

/**
 * enum gsa_dsp_state - DSP state
 * @GSA_DSP_STATE_INACTIVE:  All DSP firmware images are not loaded
 * @GSA_DSP_STATE_LOADING:   DSP firmware images are loading
 * @GSA_DSP_STATE_LOADED:    All DSP firmware images are loaded
 * @GSA_DSP_STATE_RUNNING:   DSP is running
 * @GSA_DSP_STATE_SUSPENDED: DSP is suspended
 */
enum gsa_dsp_state {
	GSA_DSP_STATE_INACTIVE,
	GSA_DSP_STATE_LOADING,
	GSA_DSP_STATE_LOADED,
	GSA_DSP_STATE_RUNNING,
	GSA_DSP_STATE_SUSPENDED,
};

/**
 * enum gsa_dsp_cmd - DSP management commands
 * @GSA_DSP_GET_STATE: return current DSP state
 * @GSA_DSP_START:     take DSP out of reset and start executing loaded
 *                     firmware
 * @GSA_DSP_SUSPEND:   put DSP into suspended state
 * @GSA_DSP_RESUME:    take DSP out of suspended state and resume executing
 * @GSA_DSP_SHUTDOWN:  reset DSP
 */
enum gsa_dsp_cmd {
	GSA_DSP_GET_STATE,
	GSA_DSP_START,
	GSA_DSP_SUSPEND,
	GSA_DSP_RESUME,
	GSA_DSP_SHUTDOWN,
};

/**
 * gsa_send_dsp_cmd() - execute specified DSP management command
 * @gsa: pointer to GSA &struct device
 * @cmd: &enum gsa_dsp_cmd to execute
 *
 * Return: new DSP state (&enum gsa_dsp_state) on success, negative error code
 *         otherwise.
 */
static inline int gsa_send_dsp_cmd(struct device *gsa, enum gsa_dsp_cmd cmd)
{
	if (cmd == GSA_DSP_START)
		return GSA_DSP_STATE_RUNNING;
	return GSA_DSP_STATE_INACTIVE;
}

#endif /* GXP_HAS_GSA */

#endif /* __GXP_GSA_H__ */
