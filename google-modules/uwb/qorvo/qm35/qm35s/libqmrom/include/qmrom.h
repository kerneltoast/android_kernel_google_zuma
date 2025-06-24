// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2021 Qorvo US, Inc.
 *
 */

#ifndef __QMROM_H__
#define __QMROM_H__

#ifndef __KERNEL__
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <byteswap.h>
#include <inttypes.h>
#else
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#define bswap_16 be16_to_cpu
#define PRIu32 "u"
#endif

#include <qmrom_error.h>

#undef CHECK_STCS

enum chip_revision_e {
	CHIP_REVISION_A0 = 0xA0,
	CHIP_REVISION_B0 = 0xB0,
	CHIP_REVISION_C0 = 0xC0,
	CHIP_REVISION_C2 = 0xC2,
	CHIP_REVISION_UNKNOWN = 0xFF
};

enum device_generation_e { DEVICE_GEN_QM357XX, DEVICE_GEN_UNKNOWN = 0xFF };

struct qmrom_handle;

#include <qm357xx_rom.h>

#define SSTC2UINT32(handle, offset)                                      \
	({                                                               \
		uint32_t tmp = 0xbeefdeed;                               \
		if ((handle)->sstc->len >= (offset) + sizeof(tmp))       \
			memcpy(&tmp, &(handle)->sstc->payload[(offset)], \
			       sizeof(tmp));                             \
		tmp;                                                     \
	})

#define SSTC2UINT16(handle, offset)                                      \
	({                                                               \
		uint16_t tmp = 0xbeed;                                   \
		if ((handle)->sstc->len >= (offset) + sizeof(tmp))       \
			memcpy(&tmp, &(handle)->sstc->payload[(offset)], \
			       sizeof(tmp));                             \
		tmp;                                                     \
	})

/* Those functions allow the libqmrom to call
 * device specific functions
 */
typedef int (*reset_device_fn)(void *handle);

struct device_ops {
	reset_device_fn reset;
};

struct qmrom_handle {
	void *spi_handle;
	void *reset_handle;
	void *ss_rdy_handle;
	void *ss_irq_handle;
	int comms_retries;
	enum device_generation_e dev_gen;
	enum chip_revision_e chip_rev;
	uint16_t device_version;
	struct device_ops dev_ops;
	int spi_speed;
	struct qm357xx_rom_code_ops qm357xx_rom_ops;
	struct stc *hstc;
	struct stc *sstc;
	struct qm357xx_soc_infos qm357xx_soc_info;
	bool is_be;
	bool skip_check_fw_boot;
	int nb_global_retry;
};

struct qmrom_handle *qmrom_init(void *spi_handle, void *reset_handle,
				void *ss_rdy_handle, void *ss_irq_handle,
				int spi_speed, int comms_retries,
				reset_device_fn reset,
				enum device_generation_e dev_gen_hint);
void qmrom_deinit(struct qmrom_handle *handle);
int qmrom_reboot_bootloader(struct qmrom_handle *handle);

int qmrom_pre_read(struct qmrom_handle *handle);
int qmrom_read(struct qmrom_handle *handle);

#ifdef CHECK_STCS
void check_stcs(const char *func, int line, struct qmrom_handle *h);
#else
#define check_stcs(f, l, h)
#endif
#endif /* __QMROM_H__ */
