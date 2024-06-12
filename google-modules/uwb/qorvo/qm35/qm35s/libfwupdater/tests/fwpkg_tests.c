// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2023 Qorvo US, Inc.
 *
 */

#include <stddef.h>
#include <qmrom_spi.h>
#include <qmrom_log.h>
#include <qmrom_utils.h>

#include <fwupdater.h>

#include "unit_tests.h"

#define PLD_CHK_PLD_SIZE 16
#define PLD_CHK_TOTAL_SIZE \
	(sizeof(struct fw_pkg_payload_chunk_t) + PLD_CHK_PLD_SIZE)

void run_fwupdater_download_fwpkg_test(void *spi_handle)
{
	uint8_t rx[STC_LEN + sizeof(struct fw_pkg_hdr_t)] = { 0 };
	uint8_t tx[STC_LEN + sizeof(struct fw_pkg_hdr_t)] = {
		0x80, 0x00, sizeof(struct fw_pkg_hdr_t), 0x00
	};
	struct fw_pkg_hdr_t *fwpkg = (struct fw_pkg_hdr_t *)&tx[STC_LEN];

	/* Setup the expected fw package */
	fwpkg->magic = CRYPTO_FIRMWARE_PACK_MAGIC_VALUE;
	fwpkg->version = CRYPTO_FIRMWARE_PACK_VERSION;
	fwpkg->enc_mode = CRYPTO_FIRMWARE_PACK_ENC_MODE_ENCRYPTED;
	fwpkg->package_type = CRYPTO_FIRMWARE_PACK_PACKAGE_TYPE_ICV;
	fwpkg->enc_mode = CRYPTO_FIRMWARE_PACK_ENC_MODE_ENCRYPTED;
	fwpkg->enc_algo = CRYPTO_FIRMWARE_PACK_ENC_ALGO_128BIT_AES_CTR;
	strcpy((char *)fwpkg->enc_data, "encrypted data");
	strcpy((char *)fwpkg->fw_version, "firmware version");
	fwpkg->payload_len = 0xDEADBEEF;
	strcpy((char *)fwpkg->tag, "AES-CMAC Tag");

	qmrom_spi_wait_for_ready_line(spi_handle, 100);
	qmrom_spi_transfer(spi_handle, (char *)rx, (char *)tx, sizeof(tx));
	LOG_INFO("received:\n");
	hexdump(LOG_INFO, rx, sizeof(tx));

	LOG_INFO("%s done\n", __func__);
}

void run_fwupdater_download_img_hdr_test(void *spi_handle)
{
	char rx[STC_LEN + CRYPTO_FIRMWARE_IMAGE_HDR_TOTAL_SIZE] = { 0 };
	char tx[STC_LEN + CRYPTO_FIRMWARE_IMAGE_HDR_TOTAL_SIZE] = {
		(char)0x80, 0x00, CRYPTO_FIRMWARE_IMAGE_HDR_TOTAL_SIZE, 0x00
	};
	struct fw_pkg_img_hdr_t *imghdr =
		(struct fw_pkg_img_hdr_t *)&tx[STC_LEN];

	/* Setup the expected fw package */
	imghdr->magic = CRYPTO_FIRMWARE_IMAGE_MAGIC_VALUE;
	imghdr->version = CRYPTO_FIRMWARE_IMAGE_VERSION;
	imghdr->cert_chain_length = CRYPTO_IMAGES_CERT_PKG_SIZE;
	imghdr->cert_chain_offset = 0xDEADBEEF;
	imghdr->num_descs = 1;
	imghdr->descs[0].offset = 0xDEADBEEF;
	imghdr->descs[0].length = 0xDEADBEEF;

	qmrom_spi_wait_for_ready_line(spi_handle, 100);
	qmrom_spi_transfer(spi_handle, rx, tx, sizeof(tx));
	LOG_INFO("received:\n");
	hexdump(LOG_INFO, rx, sizeof(tx));

	LOG_INFO("%s done\n", __func__);
}

void run_fwupdater_download_pld_chk_test(void *spi_handle)
{
	char rx[STC_LEN + PLD_CHK_TOTAL_SIZE] = { 0 };
	char tx[STC_LEN + PLD_CHK_TOTAL_SIZE] = { (char)0x80, 0x00,
						  PLD_CHK_TOTAL_SIZE, 0x00 };
	struct fw_pkg_payload_chunk_t *pldchk =
		(struct fw_pkg_payload_chunk_t *)&tx[STC_LEN];

	/* Setup the expected fw package */
	pldchk->magic = CRYPTO_FIRMWARE_CHUNK_MAGIC_VALUE;
	pldchk->version = CRYPTO_FIRMWARE_CHUNK_VERSION;
	pldchk->length = PLD_CHK_PLD_SIZE;
	strcpy((char *)pldchk->payload, "payload");

	qmrom_spi_wait_for_ready_line(spi_handle, 100);
	qmrom_spi_transfer(spi_handle, rx, tx, sizeof(tx));
	LOG_INFO("received:\n");
	hexdump(LOG_INFO, rx, sizeof(tx));

	LOG_INFO("%s done\n", __func__);
}
