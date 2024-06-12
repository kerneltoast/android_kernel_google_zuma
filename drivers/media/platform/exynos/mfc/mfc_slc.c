// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 */

#include "mfc_slc.h"

#include "mfc_core_reg_api.h"

#if IS_ENABLED(CONFIG_SLC_PARTITION_MANAGER)
void mfc_slc_enable(struct mfc_core *core)
{
	int i;

	mfc_core_debug_enter();

	if (slc_disable)
		goto done;

	/*
	 * SSMT ALLOCATE_OVERRIDE set to BYPASS
	 * Cache hint is applied on its own by 3 step below.
	 * 1) set AxCACHE(0x404) in SYSREG
	 * 2) set AXI_xxx_SLC in SFRs
	 * 3) Firmware control
	 */
	MFC_SYSREG_WRITEL(MFC_SLC_CMD_SYSREG_AX_CACHE, 0x404);
	/* Stream ID used from 0 to 15, and reserved up to max 63 */
	for (i = 0; i < 16; i++) {
		MFC_SSMT0_WRITEL(MFC_SLC_CMD_SSMT_AXI_XXX_SLC, 0x600 + 0x4 * i);
		MFC_SSMT0_WRITEL(MFC_SLC_CMD_SSMT_AXI_XXX_SLC, 0x800 + 0x4 * i);
		MFC_SSMT1_WRITEL(MFC_SLC_CMD_SSMT_AXI_XXX_SLC, 0x600 + 0x4 * i);
		MFC_SSMT1_WRITEL(MFC_SLC_CMD_SSMT_AXI_XXX_SLC, 0x800 + 0x4 * i);
	}

	/* default use 512KB for internal buffers */
	core->curr_slc_pt_idx = MFC_SLC_PARTITION_512KB;
	core->ptid = pt_client_enable(core->pt_handle, core->curr_slc_pt_idx);

	/*
	 * SSMT PID settings for internal buffers
	 * stream AXI ID: 4, 6, 7, 8, 9, 13
	 * READ : base + 0x000 + (0x4 * ID)
	 * WRITE: base + 0x200 + (0x4 * ID)
	 */
	MFC_SSMT0_WRITEL(core->ptid, 0x4 * 4);
	MFC_SSMT0_WRITEL(core->ptid, 0x4 * 6);
	MFC_SSMT0_WRITEL(core->ptid, 0x4 * 7);
	MFC_SSMT0_WRITEL(core->ptid, 0x4 * 8);
	MFC_SSMT0_WRITEL(core->ptid, 0x4 * 9);
	MFC_SSMT0_WRITEL(core->ptid, 0x4 * 13);
	MFC_SSMT0_WRITEL(core->ptid, 0x200 + 0x4 * 4);
	MFC_SSMT0_WRITEL(core->ptid, 0x200 + 0x4 * 6);
	MFC_SSMT0_WRITEL(core->ptid, 0x200 + 0x4 * 7);
	MFC_SSMT0_WRITEL(core->ptid, 0x200 + 0x4 * 8);
	MFC_SSMT0_WRITEL(core->ptid, 0x200 + 0x4 * 9);
	MFC_SSMT0_WRITEL(core->ptid, 0x200 + 0x4 * 13);

	core->slc_on_status = 1;
	mfc_core_info("[SLC] enabled ptid: %d\n", core->ptid);
	MFC_TRACE_CORE("[SLC] enabled\n");

done:
	mfc_core_debug_leave();
}

void mfc_slc_disable(struct mfc_core *core)
{
	mfc_core_debug_enter();

	pt_client_disable(core->pt_handle, core->curr_slc_pt_idx);
	core->slc_on_status = 0;
	core->curr_slc_pt_idx = MFC_SLC_PARTITION_INVALID;

	mfc_core_info("[SLC] disabled\n");
	MFC_TRACE_CORE("[SLC] disabled\n");

	mfc_core_debug_leave();
}

void mfc_slc_flush(struct mfc_core *core, struct mfc_ctx *ctx)
{
	mfc_core_debug_enter();

	if (slc_disable)
		goto done;

	mfc_slc_disable(core);
	mfc_slc_enable(core);

	mfc_slc_update_partition(core, ctx);

	mfc_core_debug(2, "[SLC] flushed\n");
	MFC_TRACE_CORE("[SLC] flushed\n");

done:
	mfc_core_debug_leave();
}

void mfc_pt_resize_callback(void *data, int id, size_t resize_allocated)
{
	struct mfc_core *core = (struct mfc_core *)data;

	if (resize_allocated < 512 * 1024)
		mfc_core_info("[SLC] available SLC size(%ld) is too small\n",
				resize_allocated);
}

void mfc_client_pt_register(struct mfc_core *core)
{

	mfc_core_debug_enter();

	core->pt_handle = pt_client_register(core->device->of_node, (void *)core,
		mfc_pt_resize_callback);
	if (!IS_ERR(core->pt_handle)) {
		core->has_slc = 1;
		core->num_slc_pt = of_property_count_strings(core->device->of_node, "pt_id");
		mfc_core_info("[SLC] PT Client Register success\n");
	} else {
		core->pt_handle = NULL;
		core->has_slc = 0;
		core->num_slc_pt = 0;
		mfc_core_info("[SLC] PT Client Register fail\n");
	}

	mfc_core_debug_leave();
}

void mfc_client_pt_unregister(struct mfc_core *core)
{
	mfc_core_debug_enter();

	if (core->pt_handle) {
		core->has_slc = 0;
		pt_client_unregister(core->pt_handle);

		mfc_core_info("[SLC] PT Client Unregister.\n");
	}

	mfc_core_debug_leave();
}

void mfc_slc_update_partition(struct mfc_core *core, struct mfc_ctx *ctx)
{
	mfc_core_debug_enter();

	if (slc_disable)
		goto done;

	if (core->num_slc_pt > 1) {
		/* When codec resolution >= 4k, resizing SLC partition to 1MB */
		if (OVER_UHD_RES(ctx) && (core->curr_slc_pt_idx == MFC_SLC_PARTITION_512KB)) {
			core->ptid = pt_client_mutate(core->pt_handle,
				core->curr_slc_pt_idx, MFC_SLC_PARTITION_1MB);

			if (core->ptid == PT_PTID_INVALID) {
				mfc_core_err("[SLC] Resizing SLC partition fail");
				mfc_slc_disable(core);
			} else {
				mfc_core_info("[SLC] Resizing SLC partition success\n");
				core->curr_slc_pt_idx = MFC_SLC_PARTITION_1MB;
			}
		}
	}

done:
	mfc_core_debug_leave();
}
#endif
