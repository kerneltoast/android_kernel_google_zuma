// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2023 Qorvo US, Inc.
 *
 */

#ifndef __FWUPDATER_H__
#define __FWUPDATER_H__

#ifndef __KERNEL__
#include <stdint.h>
#include <stddef.h>
#else
#include <linux/types.h>
#endif

#include <qmrom.h>
#include <qm357xx_fwpkg.h>

#ifndef CONFIG_FWUPDATER_CHUNK_FLASHING_RETRIES
#define CONFIG_FWUPDATER_CHUNK_FLASHING_RETRIES 10
#endif

// #define CONFIG_INJECT_ERROR 1

enum fw_pkg_error_e {
	FW_PKG_SUCCESS,
	FW_PKG_DOWNLOAD_ERROR,
	FW_PKG_MAGIC_NUM_INVALID,
	FW_PKG_VERSION_INVALID,
	FW_PKG_ENCRYPTION_MODE_INVALID,
	FW_PKG_IMG_HDR_MAGIC_NUM_INVALID,
	FW_PKG_IMG_HDR_VERSION_INVALID,
	FW_PKG_IMG_HDR_CERT_SIZE_INVALID,
	FW_PKG_IMG_HDR_IMG_NUM_INVALID,
	FW_PKG_PLD_CHK_MAGIC_NUM_INVALID,
	FW_PKG_PLD_CHK_VERSION_INVALID,
	FW_PKG_PLD_CHK_LENGTH_INVALID,
	FW_PKG_HDR_CRYPTO_ERROR,
	FW_PKG_HDR_CRYPTO_INVALID,
	FW_PKG_HDR_PAYLOAD_SIZE_INVALID,
	FW_PKG_IMG_HDR_SIZE_INVALID,
	FW_PKG_IMG_HDR_CRYPTO_ERROR,
	FW_PKG_IMG_HDR_NUMDESCS_INVALID,
	FW_PKG_CERT_CHAIN_SIZE_INVALID,
	FW_PKG_CERT_CHAIN_INVALID,
	FW_PKG_CERT_INSTALLED_FW_VERSION_INVALID,
	FW_PKG_CERT_KEY1_CHECK_INVALID,
	FW_PKG_CERT_KEY2_CHECK_INVALID,
	FW_PKG_CERT_CONTENT_CHECK_INVALID,
	FW_PKG_CERT_CHAIN_CRYPTO_ERROR,
	FW_PKG_CERT_CHAIN_IMG_RANGE_INVALID,
	FW_PKG_CERT_CHAIN_WRITE_ERROR,
	FW_PKG_IMG_CHUNK_CRYPTO_ERROR,
	FW_PKG_IMG_CHUNK_WRITE_ERROR,
	FW_PKG_IMG_FIXUP_FAILED
};

/*! Firmware Update Status fields */
struct fw_updater_status_t {
	uint32_t magic;
	uint32_t status;
	uint32_t suberror;
	uint32_t spi_errors;
	uint32_t cksum_errors;
	uint32_t rram_errors;
	uint32_t crypto_errors;
} __attribute__((packed));

#define FWUPDATER_STATUS_MAGIC 0xCAFECAFE

void run_fwupdater_unit_tests(void *spi_handle);
int run_fwupdater(struct qmrom_handle *handle, const char *fwpkg_bin,
		  size_t size);

#endif /* __FWUPDATER_H__ */
