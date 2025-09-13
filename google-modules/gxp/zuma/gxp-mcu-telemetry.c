// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP MCU telemetry support
 *
 * Copyright (C) 2022 Google LLC
 */

#include <gcip/gcip-memory.h>
#include <gcip/gcip-telemetry.h>

#include "gxp-kci.h"
#include "gxp-mcu-telemetry.h"
#include "gxp-mcu.h"

int gxp_mcu_telemetry_init(struct gxp_mcu *mcu)
{
	struct gcip_telemetry_ctx *tel = &mcu->telemetry;
	int ret;

	ret = gxp_mcu_mem_alloc_data(mcu, &tel->log_mem, GXP_MCU_TELEMETRY_LOG_BUFFER_SIZE);
	if (ret)
		return ret;

	ret = gcip_telemetry_init(tel, GCIP_TELEMETRY_TYPE_LOG, mcu->gxp->dev);
	if (ret)
		goto free_log_mem;

	ret = gxp_mcu_mem_alloc_data(mcu, &tel->trace_mem, GXP_MCU_TELEMETRY_TRACE_BUFFER_SIZE);
	if (ret)
		goto uninit_log;

	ret = gcip_telemetry_init(tel, GCIP_TELEMETRY_TYPE_TRACE, mcu->gxp->dev);
	if (ret)
		goto free_trace_mem;

	return 0;

free_trace_mem:
	gxp_mcu_mem_free_data(mcu, &tel->trace_mem);

uninit_log:
	gcip_telemetry_exit(&mcu->telemetry, GCIP_TELEMETRY_TYPE_LOG);

free_log_mem:
	gxp_mcu_mem_free_data(mcu, &tel->log_mem);

	return ret;
}

void gxp_mcu_telemetry_exit(struct gxp_mcu *mcu)
{
	struct gcip_telemetry_ctx *tel = &mcu->telemetry;

	gcip_telemetry_exit(&mcu->telemetry, GCIP_TELEMETRY_TYPE_TRACE);
	gxp_mcu_mem_free_data(mcu, &tel->trace_mem);
	gcip_telemetry_exit(&mcu->telemetry, GCIP_TELEMETRY_TYPE_LOG);
	gxp_mcu_mem_free_data(mcu, &tel->log_mem);
}

void gxp_mcu_telemetry_irq_handler(struct gxp_mcu *mcu)
{
	gcip_telemetry_irq_handler(&mcu->telemetry, GCIP_TELEMETRY_TYPE_LOG);
	gcip_telemetry_irq_handler(&mcu->telemetry, GCIP_TELEMETRY_TYPE_TRACE);
}

int gxp_mcu_telemetry_kci(struct gxp_mcu *mcu)
{
	int ret;

	ret = gcip_telemetry_kci(&mcu->telemetry, GCIP_TELEMETRY_TYPE_LOG,
				 gxp_kci_map_mcu_log_buffer, mcu->kci.mbx->mbx_impl.gcip_kci);
	if (ret)
		return ret;

	ret = gcip_telemetry_kci(&mcu->telemetry, GCIP_TELEMETRY_TYPE_TRACE,
				 gxp_kci_map_mcu_trace_buffer, mcu->kci.mbx->mbx_impl.gcip_kci);
	if (ret)
		return ret;

	return ret;
}
