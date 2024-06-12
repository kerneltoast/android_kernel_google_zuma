// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2022 Qorvo US, Inc.
 *
 */

#include <qmrom.h>
#include <qmrom_spi.h>
#include <qmrom_log.h>
#include <qmrom_utils.h>
#include <spi_rom_protocol.h>

#define DEFAULT_SPI_CLOCKRATE 750000
#define CHIP_VERSION_CHIP_REV_PAYLOAD_OFFSET 1
#define CHUNK_SIZE_A0 1016

enum A0_CMD {
	ROM_CMD_A0_GET_CHIP_VER = 0x0,
	ROM_CMD_A0_DOWNLOAD_RRAM_CMD = 0x40,
};

enum A0_RESP {
	/* Waiting for download command */
	SPI_RSP_WAIT_DOWNLOAD_MODE = 1,
	SPI_RSP_WAIT_FOR_KEY1_CERT,
	SPI_RSP_WAIT_FOR_KEY2_CERT,
	SPI_RSP_WAIT_FOR_IMAGE_CERT,
	SPI_RSP_WAIT_IMAGE_SIZE,
	SPI_RSP_WAIT_FOR_IMAGE,
	SPI_RSP_DOWNLOAD_OK,
	SPI_RSP_BOOT_OK,
	/* Checksum/CRC error */
	SPI_RSP_ERROR_CS,
	/* Got error certificate RSA/FW ver. Didn't get
	 * all the data before switching to image...
	 */
	SPI_RSP_ERROR_CERTIFICATE,
	/* Got command smaller than SPI_HEADER_SIZE.
	 * Each command must be at least this size.
	 */
	SPI_RSP_CMD_TOO_SHORT,
	/* Error checking certificates or image, going
	 * to download mode.
	 */
	SPI_RSP_ERROR_LOADING_IN_DOWNLOAD,
};

static int qmrom_a0_flash_fw(struct qmrom_handle *handle,
			     const struct firmware *fw);

void qmrom_a0_poll_soc(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;
	handle->hstc->all = 0;
	qmrom_msleep(SPI_READY_TIMEOUT_MS);
	do {
		qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				   (const char *)handle->hstc, 1);
	} while (retries-- && handle->sstc->raw_flags == 0);
}

int qmrom_a0_wait_ready(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;
	qmrom_a0_poll_soc(handle);

	while (retries-- && !handle->sstc->soc_flags.out_waiting) {
		qmrom_a0_poll_soc(handle);
	}
	return handle->sstc->soc_flags.out_waiting ? 0 :
						     SPI_ERR_WAIT_READY_TIMEOUT;
}

int qmrom_a0_probe_device(struct qmrom_handle *handle)
{
	int rc;
	LOG_DBG("%s: enters...\n", __func__);
	handle->is_be = true;

	qmrom_spi_set_freq(DEFAULT_SPI_CLOCKRATE);

	rc = qmrom_reboot_bootloader(handle);
	if (rc) {
		LOG_ERR("%s: cannot reset the device...\n", __func__);
		return rc;
	}

	rc = qmrom_a0_wait_ready(handle);
	if (rc) {
		LOG_INFO("%s: maybe not a A0 device\n", __func__);
		return rc;
	}
	qmrom_pre_read(handle);
	handle->sstc->len = bswap_16(handle->sstc->len);
	if (handle->sstc->len > 0xff) {
		/* likely the wrong endianness, B0 or C0? */
		return -1;
	}
	qmrom_read(handle);

	LOG_DBG("%s: Set the chip_rev/device_version\n", __func__);
	handle->chip_rev =
		bswap_16(SSTC2UINT16(handle,
				     CHIP_VERSION_CHIP_REV_PAYLOAD_OFFSET)) &
		0xFF;

	if (handle->chip_rev != CHIP_REVISION_A0) {
		LOG_ERR("%s: wrong chip revision %#x\n", __func__,
			handle->chip_rev);
		handle->chip_rev = -1;
		return -1;
	}

	/* Set rom ops */
	handle->rom_ops.flash_fw = qmrom_a0_flash_fw;
	handle->rom_ops.flash_debug_cert = NULL;
	handle->rom_ops.erase_debug_cert = NULL;
	return 0;
}

int qmrom_a0_write_data(struct qmrom_handle *handle, uint16_t data_size,
			const char *data)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = data_size;
	memcpy(handle->hstc->payload, data, data_size);

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + data_size);
}

static int qmrom_a0_write_chunks(struct qmrom_handle *handle,
				 const struct firmware *fw)
{
	int rc, sent = 0;
	const char *bin_data = (const char *)fw->data;

	check_stcs(__func__, __LINE__, handle);
	while (sent < fw->size) {
		uint32_t tx_bytes = fw->size - sent;
		if (tx_bytes > CHUNK_SIZE_A0)
			tx_bytes = CHUNK_SIZE_A0;

		LOG_DBG("%s: poll soc...\n", __func__);
		check_stcs(__func__, __LINE__, handle);
		qmrom_a0_poll_soc(handle);
		qmrom_pre_read(handle);
		handle->sstc->len = bswap_16(handle->sstc->len);
		qmrom_read(handle);
		if (handle->sstc->payload[0] != SPI_RSP_WAIT_FOR_IMAGE) {
			LOG_ERR("%s: wrong data result (%#x vs %#x)!!!\n",
				__func__, handle->sstc->payload[0] & 0xff,
				SPI_RSP_WAIT_FOR_IMAGE);
			return SPI_PROTO_WRONG_RESP;
		}

		LOG_DBG("%s: sending %" PRIu32 " bytes of data\n", __func__,
			tx_bytes);
		rc = qmrom_a0_write_data(handle, tx_bytes, bin_data);
		if (rc)
			return rc;
		sent += tx_bytes;
		bin_data += tx_bytes;
		check_stcs(__func__, __LINE__, handle);
	}
	return 0;
}

static int qmrom_a0_flash_fw(struct qmrom_handle *handle,
			     const struct firmware *fw)
{
	int rc = 0, resp;

	LOG_DBG("%s: starting...\n", __func__);

	/* Reboot since the rom code on A0 seems
	 * to have issues when starting flashing
	 * after some prior interaction (like GET_CHIP_VERSION)
	 */
	rc = qmrom_reboot_bootloader(handle);
	if (rc) {
		LOG_ERR("%s: cannot reset the device...\n", __func__);
		return rc;
	}

	rc = qmrom_a0_wait_ready(handle);
	if (rc) {
		LOG_ERR("%s: timedout waiting for the device to be ready\n",
			__func__);
		return rc;
	}
	qmrom_pre_read(handle);
	handle->sstc->len = bswap_16(handle->sstc->len);
	qmrom_read(handle);
	if (handle->sstc->payload[0] != SPI_RSP_WAIT_DOWNLOAD_MODE) {
		LOG_ERR("%s: wrong data result (%#x vs %#x)!!!\n", __func__,
			handle->sstc->payload[0] & 0xff,
			SPI_RSP_WAIT_DOWNLOAD_MODE);
		return SPI_PROTO_WRONG_RESP;
	}

	check_stcs(__func__, __LINE__, handle);
	LOG_DBG("%s: sending ROM_CMD_A0_DOWNLOAD_RRAM_CMD command\n", __func__);
	rc = qmrom_write_cmd(handle, ROM_CMD_A0_DOWNLOAD_RRAM_CMD);
	if (rc)
		return rc;

	for (resp = SPI_RSP_WAIT_FOR_KEY1_CERT; resp < SPI_RSP_WAIT_FOR_IMAGE;
	     resp++) {
		qmrom_a0_poll_soc(handle);
		qmrom_pre_read(handle);
		handle->sstc->len = bswap_16(handle->sstc->len);
		qmrom_read(handle);
		if (handle->sstc->payload[0] != resp) {
			LOG_ERR("%s: wrong data result (%#x vs %#x)!!!\n",
				__func__, handle->sstc->payload[0] & 0xff,
				resp);
			return SPI_PROTO_WRONG_RESP;
		}
		if (resp < SPI_RSP_WAIT_IMAGE_SIZE) {
			rc = qmrom_write_cmd(handle, 0);
			if (rc)
				return rc;
		}
	}

	LOG_DBG("%s: sending fw size\n", __func__);
	rc = qmrom_a0_write_data(handle, sizeof(uint32_t),
				 (const char *)&fw->size);
	if (rc)
		return rc;

	check_stcs(__func__, __LINE__, handle);
	rc = qmrom_a0_write_chunks(handle, fw);
	check_stcs(__func__, __LINE__, handle);
	return rc;
}
