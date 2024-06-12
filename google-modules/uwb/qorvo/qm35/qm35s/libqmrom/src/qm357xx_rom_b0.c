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

#define DEFAULT_SPI_CLOCKRATE 3000000
#define CHIP_VERSION_CHIP_REV_PAYLOAD_OFFSET 1
#define CHIP_VERSION_DEV_REV_PAYLOAD_OFFSET 3
#define CHUNK_SIZE_B0 1008

enum B0_CMD {
	ROM_CMD_B0_SEC_LOAD_ICV_IMG_TO_RRAM = 0x0,
	ROM_CMD_B0_SEC_LOAD_OEM_IMG_TO_RRAM = 0x1,
	ROM_CMD_B0_GET_CHIP_VER = 0x2,
	ROM_CMD_B0_GET_SOC_INFO = 0x3,
	ROM_CMD_B0_ERASE_DBG_CERT = 0x4,
	ROM_CMD_B0_WRITE_DBG_CERT = 0x5,
	ROM_CMD_B0_SEC_IMAGE_DATA = 0xf,
	ROM_CMD_B0_CERT_DATA = 0x10,
	ROM_CMD_B0_DEBUG_CERT_SIZE = 0x11,
};

enum B0_RESP {
	READY_FOR_CS_LOW_CMD = 0x00,
	WRONG_CS_LOW_CMD = 0x01,
	WAITING_FOR_NS_RRAM_FILE_SIZE = 0x02,
	WAITING_FOR_NS_SRAM_FILE_SIZE = 0x03,
	WAITING_FOR_NS_RRAM_FILE_DATA = 0x04,
	WAITING_FOR_NS_SRAM_FILE_DATA = 0x05,
	WAITING_FOR_SEC_FILE_DATA = 0x06,
	ERR_NS_SRAM_OR_RRAM_SIZE_CMD = 0x07,
	ERR_SEC_RRAM_SIZE_CMD = 0x08,
	ERR_WAITING_FOR_NS_IMAGE_DATA_CMD = 0x09,
	ERR_WAITING_FOR_SEC_IMAGE_DATA_CMD = 0x0A,
	ERR_IMAGE_SIZE_IS_ZERO = 0x0B,
	/* Got more data than expected size */
	ERR_IMAGE_SIZE_TOO_BIG = 0x0C,
	/* Image must divide in 16 without remainder */
	ERR_IMAGE_IS_NOT_16BYTES_MUL = 0x0D,
	ERR_GOT_DATA_MORE_THAN_ALLOWED = 0x0E,
	/* Remainder is allowed only for last packet */
	ERR_RRAM_DATA_REMAINDER_NOT_ALLOWED = 0x0F,
	ERR_WAITING_FOR_CERT_DATA_CMD = 0x10,
	WAITING_FOR_FIRST_KEY_CERT = 0x11,
	WAITING_FOR_SECOND_KEY_CERT = 0x12,
	WAITING_FOR_CONTENT_CERT = 0x13,
	WAITING_FOR_DEBUG_CERT_DATA = 0x14,
	ERR_FIRST_KEY_CERT_OR_FW_VER = 0x15,
	ERR_SECOND_KEY_CERT = 0x16,
	ERR_CONTENT_CERT_DOWNLOAD_ADDR = 0x17,
	/* If the content certificate contains to much images */
	ERR_TOO_MANY_IMAGES_IN_CONTENT_CERT = 0x18,
	ERR_ADDRESS_NOT_DIVIDED_BY_8 = 0x19,
	ERR_IMAGE_BOUNDARIES = 0x1A,
	/* Expected ICV type and got OEM */
	ERR_CERT_TYPE = 0x1B,
	ERR_PRODUCT_ID = 0x1C,
	ERR_RRAM_RANGE_OR_WRITE = 0x1D,
	WAITING_TO_DEBUG_CERTIFICATE_SIZE = 0x1E,
	ERR_DEBUG_CERT_SIZE = 0x1F,
};

static int qm357xx_rom_b0_flash_debug_cert(struct qmrom_handle *handle,
					   struct firmware *dbg_cert);
static int qm357xx_rom_b0_erase_debug_cert(struct qmrom_handle *handle);
static int
qm357xx_rom_b0_flash_unstitched_fw(struct qmrom_handle *handle,
				   const struct unstitched_firmware *all_fws);

static void qm357xx_rom_b0_poll_soc(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;
	memset(handle->hstc, 0, sizeof(struct stc));
	qmrom_msleep(SPI_READY_TIMEOUT_MS);
	do {
		qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				   (const char *)handle->hstc,
				   sizeof(struct stc) + handle->hstc->len);
	} while (retries-- && handle->sstc->raw_flags == 0);
}

static int qm357xx_rom_b0_wait_ready(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;
	int rc;

	qm357xx_rom_b0_poll_soc(handle);

	/* handle->sstc has been updated */
	while (retries-- &&
	       handle->sstc->raw_flags != SPI_SH_READY_CMD_BIT_MASK) {
		if (handle->sstc->soc_flags.out_waiting) {
			qmrom_pre_read(handle);
		} else if (handle->sstc->soc_flags.out_active) {
			rc = qmrom_read(handle);
			if (rc)
				return rc;
		} else {
			/* error? */
			qm357xx_rom_b0_poll_soc(handle);
		}
	}

	return handle->sstc->raw_flags == SPI_SH_READY_CMD_BIT_MASK ?
		       0 :
		       SPI_ERR_WAIT_READY_TIMEOUT;
}

static int qm357xx_rom_b0_poll_cmd_resp(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;

	qm357xx_rom_b0_poll_soc(handle);
	do {
		if (handle->sstc->soc_flags.out_waiting) {
			qmrom_pre_read(handle);
			if (handle->sstc->len > 0xff) {
				/* likely the wrong endianness, A0? */
				return -1;
			}
			qmrom_read(handle);
			break;
		} else
			qm357xx_rom_b0_poll_soc(handle);
	} while (retries--);

	return retries > 0 ? 0 : -1;
}

int qm357xx_rom_b0_probe_device(struct qmrom_handle *handle)
{
	int rc, i;
	uint8_t *soc_lcs_uuid;
	handle->is_be = false;
	check_stcs(__func__, __LINE__, handle);

	if (handle->spi_speed == 0)
		qmrom_spi_set_freq(DEFAULT_SPI_CLOCKRATE);
	else
		qmrom_spi_set_freq(handle->spi_speed);

	rc = qmrom_reboot_bootloader(handle);
	if (rc) {
		LOG_ERR("%s: cannot reset the device...\n", __func__);
		return rc;
	}

	rc = qm357xx_rom_b0_wait_ready(handle);
	if (rc) {
		LOG_INFO("%s: maybe not a B0 device\n", __func__);
		return rc;
	}

	rc = qm357xx_rom_write_cmd(handle, ROM_CMD_B0_GET_CHIP_VER);
	if (rc)
		return rc;

	rc = qm357xx_rom_b0_poll_cmd_resp(handle);
	if (rc)
		return rc;

	handle->chip_rev =
		SSTC2UINT16(handle, CHIP_VERSION_CHIP_REV_PAYLOAD_OFFSET) &
		0xFF;
	handle->device_version = bswap_16(
		SSTC2UINT16(handle, CHIP_VERSION_DEV_REV_PAYLOAD_OFFSET));
	if (handle->chip_rev != CHIP_REVISION_B0) {
		LOG_ERR("%s: wrong chip revision 0x%x\n", __func__,
			handle->chip_rev);
		handle->chip_rev = CHIP_REVISION_UNKNOWN;
		return -1;
	}

	rc = qm357xx_rom_b0_wait_ready(handle);
	if (rc) {
		LOG_ERR("%s: hmm something went wrong!!!\n", __func__);
		return rc;
	}

	rc = qm357xx_rom_write_cmd(handle, ROM_CMD_B0_GET_SOC_INFO);
	if (rc)
		return rc;

	rc = qm357xx_rom_b0_poll_cmd_resp(handle);
	if (rc)
		return rc;

	/* skip the first byte */
	soc_lcs_uuid = &(handle->sstc->payload[1]);
	for (i = 0; i < QM357XX_ROM_SOC_ID_LEN; i++)
		handle->qm357xx_soc_info.soc_id[i] =
			soc_lcs_uuid[QM357XX_ROM_SOC_ID_LEN - i - 1];
	soc_lcs_uuid += QM357XX_ROM_SOC_ID_LEN;
	handle->qm357xx_soc_info.lcs_state = soc_lcs_uuid[0];
	soc_lcs_uuid += 1;
	for (i = 0; i < QM357XX_ROM_UUID_LEN; i++)
		handle->qm357xx_soc_info.uuid[i] =
			soc_lcs_uuid[QM357XX_ROM_UUID_LEN - i - 1];

	/* Set device type */
	handle->dev_gen = DEVICE_GEN_QM357XX;
	/* Set rom ops */
	handle->qm357xx_rom_ops.flash_unstitched_fw =
		qm357xx_rom_b0_flash_unstitched_fw;
	handle->qm357xx_rom_ops.flash_debug_cert =
		qm357xx_rom_b0_flash_debug_cert;
	handle->qm357xx_rom_ops.erase_debug_cert =
		qm357xx_rom_b0_erase_debug_cert;

	check_stcs(__func__, __LINE__, handle);
	return 0;
}

static int qm357xx_rom_b0_flash_data(struct qmrom_handle *handle,
				     struct firmware *fw, uint8_t cmd,
				     uint8_t exp)
{
	int rc, sent = 0;
	const char *bin_data = (const char *)fw->data;

	check_stcs(__func__, __LINE__, handle);
	while (sent < fw->size) {
		uint32_t tx_bytes = fw->size - sent;
		if (tx_bytes > CHUNK_SIZE_B0)
			tx_bytes = CHUNK_SIZE_B0;

		LOG_DBG("%s: poll soc...\n", __func__);
		check_stcs(__func__, __LINE__, handle);
		qm357xx_rom_b0_poll_soc(handle);
		qmrom_pre_read(handle);
		qmrom_read(handle);
		if (handle->sstc->payload[0] != exp) {
			LOG_ERR("%s: wrong data expected (%#x vs %#x)!!!\n",
				__func__, handle->sstc->payload[0] & 0xff, exp);
			if (handle->sstc->payload[0] ==
			    ERR_FIRST_KEY_CERT_OR_FW_VER)
				return PEG_ERR_FIRST_KEY_CERT_OR_FW_VER;
			else
				return SPI_PROTO_WRONG_RESP;
		}

		LOG_DBG("%s: sending %d command with %" PRIu32 " bytes\n",
			__func__, cmd, tx_bytes);
		rc = qm357xx_rom_write_size_cmd(handle, cmd, tx_bytes,
						bin_data);
		if (rc)
			return rc;
		sent += tx_bytes;
		bin_data += tx_bytes;
		check_stcs(__func__, __LINE__, handle);
	}
	return 0;
}

static int
qm357xx_rom_b0_flash_unstitched_fw(struct qmrom_handle *handle,
				   const struct unstitched_firmware *all_fws)
{
	int rc = 0;
	uint8_t flash_cmd = handle->qm357xx_soc_info.lcs_state ==
					    CC_BSV_SECURE_LCS ?
				    ROM_CMD_B0_SEC_LOAD_OEM_IMG_TO_RRAM :
				    ROM_CMD_B0_SEC_LOAD_ICV_IMG_TO_RRAM;

	if (all_fws->key1_crt->data[HBK_LOC] == HBK_2E_ICV &&
	    handle->qm357xx_soc_info.lcs_state != CC_BSV_CHIP_MANUFACTURE_LCS) {
		LOG_ERR("%s: Trying to flash an ICV fw on a non ICV platform\n",
			__func__);
		rc = -EINVAL;
		goto end;
	}

	if (all_fws->key1_crt->data[HBK_LOC] == HBK_2E_OEM &&
	    handle->qm357xx_soc_info.lcs_state != CC_BSV_SECURE_LCS) {
		LOG_ERR("%s: Trying to flash an OEM fw on a non OEM platform\n",
			__func__);
		rc = -EINVAL;
		goto end;
	}

	LOG_DBG("%s: starting...\n", __func__);
	check_stcs(__func__, __LINE__, handle);

	rc = qm357xx_rom_b0_wait_ready(handle);
	if (rc)
		goto end;

	check_stcs(__func__, __LINE__, handle);
	LOG_DBG("%s: sending flash_cmd %u command\n", __func__, flash_cmd);
	rc = qm357xx_rom_write_cmd(handle, flash_cmd);
	if (rc)
		goto end;

	check_stcs(__func__, __LINE__, handle);
	rc = qm357xx_rom_b0_flash_data(handle, all_fws->key1_crt,
				       ROM_CMD_B0_CERT_DATA,
				       WAITING_FOR_FIRST_KEY_CERT);
	if (rc)
		goto end;

	check_stcs(__func__, __LINE__, handle);
	rc = qm357xx_rom_b0_flash_data(handle, all_fws->key2_crt,
				       ROM_CMD_B0_CERT_DATA,
				       WAITING_FOR_SECOND_KEY_CERT);
	if (rc)
		goto end;

	check_stcs(__func__, __LINE__, handle);
	rc = qm357xx_rom_b0_flash_data(handle, all_fws->fw_crt,
				       ROM_CMD_B0_CERT_DATA,
				       WAITING_FOR_CONTENT_CERT);
	if (rc)
		goto end;

	check_stcs(__func__, __LINE__, handle);
	rc = qm357xx_rom_b0_flash_data(handle, all_fws->fw_img,
				       ROM_CMD_B0_SEC_IMAGE_DATA,
				       WAITING_FOR_SEC_FILE_DATA);

	if (!rc)
		qmrom_msleep(SPI_READY_TIMEOUT_MS);

end:
	check_stcs(__func__, __LINE__, handle);
	return rc;
}

static int qm357xx_rom_b0_flash_debug_cert(struct qmrom_handle *handle,
					   struct firmware *dbg_cert)
{
	int rc;

	LOG_DBG("%s: starting...\n", __func__);
	check_stcs(__func__, __LINE__, handle);

	rc = qm357xx_rom_b0_wait_ready(handle);
	if (rc)
		return rc;

	check_stcs(__func__, __LINE__, handle);
	LOG_DBG("%s: sending ROM_CMD_B0_WRITE_DBG_CERT command\n", __func__);
	rc = qm357xx_rom_write_cmd(handle, ROM_CMD_B0_WRITE_DBG_CERT);
	if (rc)
		return rc;

	check_stcs(__func__, __LINE__, handle);
	LOG_DBG("%s: poll soc...\n", __func__);
	qm357xx_rom_b0_poll_soc(handle);
	qmrom_pre_read(handle);
	qmrom_read(handle);

	check_stcs(__func__, __LINE__, handle);
	LOG_DBG("%s: sending ROM_CMD_B0_DEBUG_CERT_SIZE command\n", __func__);
	rc = qm357xx_rom_write_size_cmd(handle, ROM_CMD_B0_DEBUG_CERT_SIZE,
					sizeof(uint32_t),
					(const char *)&dbg_cert->size);
	if (handle->sstc->payload[0] != WAITING_TO_DEBUG_CERTIFICATE_SIZE) {
		LOG_ERR("%s: wrong debug cert size result (0x%x vs 0x%x)!!!\n",
			__func__, handle->sstc->payload[0] & 0xff,
			WAITING_TO_DEBUG_CERTIFICATE_SIZE);
		return SPI_PROTO_WRONG_RESP;
	}
	if (rc)
		return rc;

	rc = qm357xx_rom_b0_flash_data(handle, dbg_cert, ROM_CMD_B0_CERT_DATA,
				       WAITING_FOR_DEBUG_CERT_DATA);
	check_stcs(__func__, __LINE__, handle);
	qmrom_msleep(SPI_READY_TIMEOUT_MS);
	return rc;
}

static int qm357xx_rom_b0_erase_debug_cert(struct qmrom_handle *handle)
{
	int rc;

	LOG_INFO("%s: starting...\n", __func__);
	check_stcs(__func__, __LINE__, handle);

	rc = qm357xx_rom_b0_wait_ready(handle);
	if (!rc)
		return rc;

	LOG_DBG("%s: sending ROM_CMD_B0_ERASE_DBG_CERT command\n", __func__);
	rc = qm357xx_rom_write_cmd(handle, ROM_CMD_B0_ERASE_DBG_CERT);
	if (rc)
		return rc;

	qmrom_msleep(SPI_READY_TIMEOUT_MS);
	check_stcs(__func__, __LINE__, handle);
	return 0;
}
