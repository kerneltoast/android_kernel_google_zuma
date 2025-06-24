/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QM35_SSCD__
#define __QM35_SSCD__

#include <linux/platform_data/sscoredump.h>
#include "qm35.h"
#include "hsspi_coredump.h"

#define QM35_COREDUMP_SEGMENTS 1

struct sscd_info {
	const char *name;
	u16 seg_count;
	struct sscd_segment segs[QM35_COREDUMP_SEGMENTS];
};

struct sscd_desc {
	struct sscd_info sscd_info;
	struct sscd_platform_data sscd_pdata;
	struct platform_device sscd_dev;
};

int qm35_report_coredump(struct qm35_ctx *qm35_ctx);
int qm35_register_coredump(struct qm35_ctx *qm35_ctx);
void qm35_unregister_coredump(struct qm35_ctx *qm35_ctx);

#endif /* __QM35_SSCD__ */
