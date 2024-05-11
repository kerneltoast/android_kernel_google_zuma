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

#define PEG_ERR_TIMEOUT PEG_ERR_BASE - 1
#define PEG_ERR_ROM_NOT_READY PEG_ERR_BASE - 2
#define PEG_ERR_SEND_CERT_WRITE PEG_ERR_BASE - 3
#define PEG_ERR_WRONG_REVISION PEG_ERR_BASE - 4
#define PEG_ERR_FIRST_KEY_CERT_OR_FW_VER PEG_ERR_BASE - 5

enum chip_revision_e {
	CHIP_REVISION_A0 = 0xA0,
	CHIP_REVISION_B0 = 0xB0,
	CHIP_REVISION_C0 = 0xC0,
	CHIP_REVISION_UNKNOWN = 0xFF
};

#define HBK_LOC 12
typedef enum {
	HBK_2E_ICV = 0,
	HBK_2E_OEM = 1,
	HBK_1E_ICV_OEM = 2,
} hbk_t;

#define ROM_VERSION_A0 0x01a0
#define ROM_VERSION_B0 0xb000

#define ROM_SOC_ID_LEN 0x20
#define ROM_UUID_LEN 0x10

/* Life cycle state definitions. */

/*! Defines the CM life-cycle state value. */
#define CC_BSV_CHIP_MANUFACTURE_LCS 0x0
/*! Defines the DM life-cycle state value. */
#define CC_BSV_DEVICE_MANUFACTURE_LCS 0x1
/*! Defines the Secure life-cycle state value. */
#define CC_BSV_SECURE_LCS 0x5
/*! Defines the RMA life-cycle state value. */
#define CC_BSV_RMA_LCS 0x7

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

struct qmrom_handle;

struct unstitched_firmware {
	struct firmware *fw_img;
	struct firmware *fw_crt;
	struct firmware *key1_crt;
	struct firmware *key2_crt;
};

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
 * revision specific functions
 */
typedef int (*flash_fw_fn)(struct qmrom_handle *handle,
			   const struct firmware *fw);
typedef int (*flash_unstitched_fw_fn)(struct qmrom_handle *handle,
				      const struct unstitched_firmware *fw);
typedef int (*flash_debug_cert_fn)(struct qmrom_handle *handle,
				   struct firmware *dbg_cert);
typedef int (*erase_debug_cert_fn)(struct qmrom_handle *handle);

struct rom_code_ops {
	flash_fw_fn flash_fw;
	flash_unstitched_fw_fn flash_unstitched_fw;
	flash_debug_cert_fn flash_debug_cert;
	erase_debug_cert_fn erase_debug_cert;
};

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
	int comms_retries;
	enum chip_revision_e chip_rev;
	uint16_t device_version;
	struct device_ops dev_ops;
	struct rom_code_ops rom_ops;
	uint32_t lcs_state;
	struct stc *hstc;
	struct stc *sstc;
	uint8_t soc_id[ROM_SOC_ID_LEN];
	uint8_t uuid[ROM_UUID_LEN];
	bool is_be;
};

int qmrom_unstitch_fw(const struct firmware *fw,
		      struct unstitched_firmware *unstitched_fw,
		      enum chip_revision_e revision);
struct qmrom_handle *qmrom_init(void *spi_handle, void *reset_handle,
				void *ss_rdy_handle, int comms_retries,
				reset_device_fn reset);
void qmrom_deinit(struct qmrom_handle *handle);
int qmrom_reboot_bootloader(struct qmrom_handle *handle);
int qmrom_flash_dbg_cert(struct qmrom_handle *handle,
			 struct firmware *dbg_cert);
int qmrom_erase_dbg_cert(struct qmrom_handle *handle);
int qmrom_flash_fw(struct qmrom_handle *handle, const struct firmware *fw);
int qmrom_flash_unstitched_fw(struct qmrom_handle *handle,
			      const struct unstitched_firmware *fw);

int qmrom_pre_read(struct qmrom_handle *handle);
int qmrom_read(struct qmrom_handle *handle);
int qmrom_write_cmd(struct qmrom_handle *handle, uint8_t cmd);
int qmrom_write_cmd32(struct qmrom_handle *handle, uint32_t cmd);
int qmrom_write_size_cmd(struct qmrom_handle *handle, uint8_t cmd,
			 uint16_t data_size, const char *data);
int qmrom_write_size_cmd32(struct qmrom_handle *handle, uint32_t cmd,
			   uint16_t data_size, const char *data);

#ifdef CHECK_STCS
void check_stcs(const char *func, int line, struct qmrom_handle *h);
#else
#define check_stcs(f, l, h)
#endif
#endif /* __QMROM_H__ */
