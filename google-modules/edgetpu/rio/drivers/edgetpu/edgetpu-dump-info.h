/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Structures used for debug dump segments.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __EDGETPU_DUMP_INFO_H__
#define __EDGETPU_DUMP_INFO_H__

/*
 * Note: A copy of this file is maintained in the debug dump parser project, do not include other
 * headers.
 */

/*
 * +------------+------------------+
 * | type ETDEV | edgetpu_dev_info |
 * +------------+------------------+
 */

struct edgetpu_dev_info {
	uint32_t state;
	uint32_t vcid_pool;
	uint32_t job_count;
	uint32_t firmware_crash_count;
	uint32_t watchdog_timeout_count;
	uint32_t reserved[11];
};

#endif /* __EDGETPU_DUMP_INFO_H__ */
