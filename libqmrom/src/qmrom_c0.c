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

#define DEFAULT_SPI_CLOCKRATE 5000000
#define CHIP_VERSION_CHIP_REV_PAYLOAD_OFFSET 4
#define CHIP_VERSION_DEV_REV_PAYLOAD_OFFSET 6
#define CHUNK_SIZE_C0 2040
#define SPI_READY_TIMEOUT_MS_C0 200

#ifdef C0_WRITE_STATS
#include <linux/ktime.h>
#endif

#define SPI_SH_READY_CMD_BIT_MASK_C0 \
	(SPI_SH_READY_CMD_BIT_MASK >> 4 | SPI_SH_READY_CMD_BIT_MASK)

enum C0_CMD {
	ROM_CMD_C0_SEC_LOAD_ICV_IMG_TO_RRAM = 0x0,
	ROM_CMD_C0_SEC_LOAD_OEM_IMG_TO_RRAM = 0x1,
	ROM_CMD_C0_GET_CHIP_VER = 0x2,
	ROM_CMD_C0_GET_SOC_INFO = 0x3,
	ROM_CMD_C0_ERASE_DBG_CERT = 0x4,
	ROM_CMD_C0_USE_DIRECT_RRAM_WR = 0X5,
	ROM_CMD_C0_USE_INDIRECT_RRAM_WR = 0x6,
	ROM_CMD_C0_WRITE_DBG_CERT = 0x7,
	ROM_CMD_C0_SEC_IMAGE_DATA = 0x12,
	ROM_CMD_C0_CERT_DATA = 0x13,
	ROM_CMD_C0_DEBUG_CERT_SIZE = 0x14,
};

enum C0_RESP {
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

static int qmrom_c0_flash_fw(struct qmrom_handle *handle,
			     const struct firmware *fw);
static int qmrom_c0_flash_debug_cert(struct qmrom_handle *handle,
				     struct firmware *dbg_cert);
static int qmrom_c0_erase_debug_cert(struct qmrom_handle *handle);

#define qmrom_pre_read_c0(h)                                            \
	({                                                              \
		int rc;                                                 \
		qmrom_spi_wait_for_ready_line((h)->ss_rdy_handle,       \
					      SPI_READY_TIMEOUT_MS_C0); \
		rc = qmrom_pre_read((h));                               \
		rc;                                                     \
	})
#define qmrom_read_c0(h)                                                \
	({                                                              \
		int rc;                                                 \
		qmrom_spi_wait_for_ready_line((h)->ss_rdy_handle,       \
					      SPI_READY_TIMEOUT_MS_C0); \
		rc = qmrom_read((h));                                   \
		rc;                                                     \
	})
#define qmrom_write_cmd_c0(h, cmd)                                      \
	({                                                              \
		int rc;                                                 \
		qmrom_spi_wait_for_ready_line((h)->ss_rdy_handle,       \
					      SPI_READY_TIMEOUT_MS_C0); \
		rc = qmrom_write_cmd((h), (cmd));                       \
		rc;                                                     \
	})
#define qmrom_write_cmd32_c0(h, cmd)                                    \
	({                                                              \
		int rc;                                                 \
		qmrom_spi_wait_for_ready_line((h)->ss_rdy_handle,       \
					      SPI_READY_TIMEOUT_MS_C0); \
		rc = qmrom_write_cmd32((h), (cmd));                     \
		rc;                                                     \
	})
#define qmrom_write_size_cmd_c0(h, cmd, ds, d)                          \
	({                                                              \
		int rc;                                                 \
		qmrom_spi_wait_for_ready_line((h)->ss_rdy_handle,       \
					      SPI_READY_TIMEOUT_MS_C0); \
		rc = qmrom_write_size_cmd((h), (cmd), (ds), (d));       \
		rc;                                                     \
	})
#define qmrom_write_size_cmd32_c0(h, cmd, ds, d)                        \
	({                                                              \
		int rc;                                                 \
		qmrom_spi_wait_for_ready_line((h)->ss_rdy_handle,       \
					      SPI_READY_TIMEOUT_MS_C0); \
		rc = qmrom_write_size_cmd32((h), (cmd), (ds), (d));     \
		rc;                                                     \
	})

static void qmrom_c0_poll_soc(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;
	memset(handle->hstc, 0, sizeof(struct stc));
	handle->sstc->raw_flags = 0;
	do {
		int rc = qmrom_spi_wait_for_ready_line(handle->ss_rdy_handle,
						       SPI_READY_TIMEOUT_MS_C0);
		if (rc) {
			LOG_ERR("%s qmrom_spi_wait_for_ready_line failed\n",
				__func__);
			continue;
		}
		qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				   (const char *)handle->hstc,
				   sizeof(struct stc) + handle->hstc->len);
	} while (retries-- && handle->sstc->raw_flags == 0);
}

static int qmrom_c0_wait_ready(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;

	qmrom_c0_poll_soc(handle);

	/* handle->sstc has been updated */
	while (retries-- &&
	       handle->sstc->raw_flags != SPI_SH_READY_CMD_BIT_MASK_C0) {
		if (handle->sstc->soc_flags.out_waiting) {
			qmrom_pre_read_c0(handle);
			qmrom_read_c0(handle);
		} else if (handle->sstc->soc_flags.out_active) {
			return qmrom_read_c0(handle);
		} else
			qmrom_c0_poll_soc(handle);
	}

	return handle->sstc->raw_flags == SPI_SH_READY_CMD_BIT_MASK_C0 ?
		       0 :
		       SPI_ERR_WAIT_READY_TIMEOUT;
}

static int qmrom_c0_poll_cmd_resp(struct qmrom_handle *handle)
{
	int retries = handle->comms_retries;

	qmrom_c0_poll_soc(handle);
	do {
		if (handle->sstc->soc_flags.out_waiting) {
			qmrom_pre_read_c0(handle);
			return qmrom_read_c0(handle);
		} else
			qmrom_c0_poll_soc(handle);
	} while (retries--);
	if (retries <= 0)
		LOG_ERR("%s failed after %d replies\n", __func__,
			handle->comms_retries);

	return retries > 0 ? 0 : -1;
}

int qmrom_c0_probe_device(struct qmrom_handle *handle)
{
	int rc, i;
	uint8_t *soc_lcs_uuid;

	handle->is_be = false;

	qmrom_spi_set_freq(DEFAULT_SPI_CLOCKRATE);

	rc = qmrom_reboot_bootloader(handle);
	if (rc) {
		LOG_ERR("%s: cannot reset the device...\n", __func__);
		return rc;
	}

	rc = qmrom_c0_wait_ready(handle);
	if (rc) {
		LOG_INFO("%s: maybe not a C0 device\n", __func__);
		return rc;
	}

	rc = qmrom_write_cmd32_c0(handle, ROM_CMD_C0_GET_CHIP_VER);
	if (rc)
		return rc;

	rc = qmrom_c0_poll_cmd_resp(handle);
	if (rc)
		return rc;

	handle->chip_rev =
		SSTC2UINT16(handle, CHIP_VERSION_CHIP_REV_PAYLOAD_OFFSET) &
		0xFF;
	handle->device_version = bswap_16(
		SSTC2UINT16(handle, CHIP_VERSION_DEV_REV_PAYLOAD_OFFSET));
	if (handle->chip_rev != CHIP_REVISION_C0) {
		LOG_ERR("%s: wrong chip revision %#x\n", __func__,
			handle->chip_rev);
		handle->chip_rev = -1;
		return -1;
	}

	rc = qmrom_c0_wait_ready(handle);
	if (rc) {
		LOG_ERR("%s: hmm something went wrong!!!\n", __func__);
		return rc;
	}

	rc = qmrom_write_cmd32_c0(handle, ROM_CMD_C0_GET_SOC_INFO);
	if (rc)
		return rc;

	rc = qmrom_c0_poll_cmd_resp(handle);
	if (rc)
		return rc;

	/* skip the first 4 bytes */
	soc_lcs_uuid = &(handle->sstc->payload[4]);
	for (i = 0; i < ROM_SOC_ID_LEN; i++)
		handle->soc_id[i] = soc_lcs_uuid[ROM_SOC_ID_LEN - i - 1];
	soc_lcs_uuid += ROM_SOC_ID_LEN;
	memcpy(&handle->lcs_state, soc_lcs_uuid, sizeof(uint32_t));
	soc_lcs_uuid += 4;
	for (i = 0; i < ROM_UUID_LEN; i++)
		handle->uuid[i] = soc_lcs_uuid[ROM_UUID_LEN - i - 1];

	/* Set rom ops */
	handle->rom_ops.flash_fw = qmrom_c0_flash_fw;
	handle->rom_ops.flash_debug_cert = qmrom_c0_flash_debug_cert;
	handle->rom_ops.erase_debug_cert = qmrom_c0_erase_debug_cert;

	return 0;
}

#ifdef C0_WRITE_STATS
static uint64_t total_bytes, total_time_ns;
static uint32_t max_write_time_ns, min_write_time_ns = ~0;

static void update_write_max_chunk_stats(ktime_t start_time)
{
	uint64_t elapsed_time_ns;

	total_bytes += CHUNK_SIZE_C0;
	elapsed_time_ns = ktime_to_ns(ktime_sub(ktime_get(), start_time));
	total_time_ns += elapsed_time_ns;
	if (elapsed_time_ns > max_write_time_ns)
		max_write_time_ns = elapsed_time_ns;
	if (elapsed_time_ns < min_write_time_ns)
		min_write_time_ns = elapsed_time_ns;
}

static void dump_stats(void)
{
	uint32_t nb_chunks = total_bytes / CHUNK_SIZE_C0;
	LOG_WARN(
		"C0 flashing time stats: %llu bytes over %llu us (chunk size %u, write timings: mean %u us, min %u us, max %u us)\n",
		total_bytes, total_time_ns / 1000, CHUNK_SIZE_C0,
		(uint32_t)((total_time_ns / nb_chunks) / 1000),
		min_write_time_ns / 1000, max_write_time_ns / 1000);
}
#endif

static int qmrom_c0_flash_data(struct qmrom_handle *handle, struct firmware *fw,
			       uint8_t cmd, uint8_t resp, bool skip_last_check)
{
	int rc, sent = 0;
	const char *bin_data = (const char *)fw->data;
#ifdef C0_WRITE_STATS
	ktime_t start_time;
#endif

	while (sent < fw->size) {
		uint32_t tx_bytes = fw->size - sent;
		if (tx_bytes > CHUNK_SIZE_C0)
			tx_bytes = CHUNK_SIZE_C0;

		LOG_DBG("%s: sending command %#x with %" PRIu32 " bytes\n",
			__func__, cmd, tx_bytes);
#ifdef C0_WRITE_STATS
		start_time = ktime_get();
#endif
		rc = qmrom_write_size_cmd32_c0(handle, cmd, tx_bytes, bin_data);
		if (rc)
			return rc;
		sent += tx_bytes;
		bin_data += tx_bytes;
		if (skip_last_check && sent == fw->size) {
			LOG_INFO("%s: flashing done, quitting now\n", __func__);
			break;
		}
		qmrom_c0_poll_soc(handle);
#ifdef C0_WRITE_STATS
		if (tx_bytes == CHUNK_SIZE_C0)
			update_write_max_chunk_stats(start_time);
#endif
		qmrom_pre_read_c0(handle);
		qmrom_read_c0(handle);
		if (handle->sstc->payload[0] != resp) {
			LOG_ERR("%s: wrong data result (%#x vs %#x)!!!\n",
				__func__, handle->sstc->payload[0] & 0xff,
				resp);
			if (handle->sstc->payload[0] ==
			    ERR_FIRST_KEY_CERT_OR_FW_VER)
				return PEG_ERR_FIRST_KEY_CERT_OR_FW_VER;
			else
				return SPI_PROTO_WRONG_RESP;
		}
	}
	qmrom_msleep(SPI_READY_TIMEOUT_MS_C0);
	return 0;
}

static int qmrom_c0_flash_fw(struct qmrom_handle *handle,
			     const struct firmware *fw)
{
	int rc = 0;
	struct unstitched_firmware all_fws = { 0 };
	uint8_t flash_cmd = handle->lcs_state == CC_BSV_SECURE_LCS ?
				    ROM_CMD_C0_SEC_LOAD_OEM_IMG_TO_RRAM :
				    ROM_CMD_C0_SEC_LOAD_ICV_IMG_TO_RRAM;

	LOG_INFO("Unstitching the fw %p->data %p\n", (void *)fw,
		 (void *)fw->data);
	rc = qmrom_unstitch_fw(fw, &all_fws, handle->chip_rev);
	if (rc) {
		LOG_ERR("%s: Unstitched fw flashing not supported yet\n",
			__func__);
		return rc;
	}

	if (all_fws.key1_crt->data[HBK_LOC] == HBK_2E_ICV &&
	    handle->lcs_state != CC_BSV_CHIP_MANUFACTURE_LCS) {
		LOG_ERR("%s: Trying to flash an ICV fw on a non ICV platform\n",
			__func__);
		rc = -EINVAL;
		goto end;
	}

	if (all_fws.key1_crt->data[HBK_LOC] == HBK_2E_OEM &&
	    handle->lcs_state != CC_BSV_SECURE_LCS) {
		LOG_ERR("%s: Trying to flash an OEM fw on a non OEM platform\n",
			__func__);
		rc = -EINVAL;
		goto end;
	}

	LOG_DBG("%s: starting...\n", __func__);

	/* Set RRAM write mode */
	rc = qmrom_c0_wait_ready(handle);
	if (rc)
		goto end;

	rc = qmrom_write_cmd32_c0(handle, ROM_CMD_C0_USE_INDIRECT_RRAM_WR);
	if (rc)
		goto end;

	qmrom_c0_poll_soc(handle);
	qmrom_pre_read_c0(handle);
	qmrom_read_c0(handle);
	qmrom_c0_poll_soc(handle);

	LOG_DBG("%s: sending flash_cmd %u command\n", __func__, flash_cmd);
	rc = qmrom_write_cmd32_c0(handle, flash_cmd);
	if (rc)
		goto end;

	qmrom_c0_poll_cmd_resp(handle);
	if (handle->sstc->payload[0] != WAITING_FOR_FIRST_KEY_CERT) {
		LOG_ERR("%s: Waiting for WAITING_FOR_FIRST_KEY_CERT(%#x) but got %#x\n",
			__func__, WAITING_FOR_FIRST_KEY_CERT,
			handle->sstc->payload[0]);
		goto end;
	}

	qmrom_c0_poll_soc(handle);

	rc = qmrom_c0_flash_data(handle, all_fws.key1_crt, ROM_CMD_C0_CERT_DATA,
				 WAITING_FOR_SECOND_KEY_CERT, false);
	if (rc)
		goto end;

	rc = qmrom_c0_flash_data(handle, all_fws.key2_crt, ROM_CMD_C0_CERT_DATA,
				 WAITING_FOR_CONTENT_CERT, false);
	if (rc)
		goto end;

	rc = qmrom_c0_flash_data(handle, all_fws.fw_crt, ROM_CMD_C0_CERT_DATA,
				 WAITING_FOR_SEC_FILE_DATA, false);
	if (rc)
		goto end;

	rc = qmrom_c0_flash_data(handle, all_fws.fw_img,
				 ROM_CMD_C0_SEC_IMAGE_DATA,
				 WAITING_FOR_SEC_FILE_DATA, true);

#ifdef C0_WRITE_STATS
	dump_stats();
#endif

end:
	qmrom_free(all_fws.fw_img);
	qmrom_free(all_fws.fw_crt);
	qmrom_free(all_fws.key1_crt);
	qmrom_free(all_fws.key2_crt);
	return rc;
}

static int qmrom_c0_flash_debug_cert(struct qmrom_handle *handle,
				     struct firmware *dbg_cert)
{
	int rc;

	LOG_DBG("%s: starting...\n", __func__);
	rc = qmrom_c0_wait_ready(handle);
	if (rc)
		return rc;

	LOG_DBG("%s: sending ROM_CMD_C0_USE_DIRECT_RRAM_WR command\n",
		__func__);
	rc = qmrom_write_cmd32_c0(handle, ROM_CMD_C0_USE_INDIRECT_RRAM_WR);
	if (rc)
		return rc;

	qmrom_c0_poll_soc(handle);
	qmrom_pre_read_c0(handle);
	qmrom_read_c0(handle);
	qmrom_c0_poll_soc(handle);

	LOG_DBG("%s: sending ROM_CMD_C0_WRITE_DBG_CERT command\n", __func__);
	rc = qmrom_write_cmd32_c0(handle, ROM_CMD_C0_WRITE_DBG_CERT);
	if (rc)
		return rc;
	qmrom_c0_poll_cmd_resp(handle);
	if (handle->sstc->payload[0] != WAITING_TO_DEBUG_CERTIFICATE_SIZE) {
		LOG_ERR("%s: Waiting for WAITING_TO_DEBUG_CERTIFICATE_SIZE(%#x) but got %#x\n",
			__func__, WAITING_TO_DEBUG_CERTIFICATE_SIZE,
			handle->sstc->payload[0]);
		return rc;
	}

	LOG_DBG("%s: sending ROM_CMD_C0_DEBUG_CERT_SIZE command\n", __func__);
	rc = qmrom_write_size_cmd32_c0(handle, ROM_CMD_C0_DEBUG_CERT_SIZE,
				       sizeof(uint32_t),
				       (const char *)&dbg_cert->size);
	qmrom_c0_poll_cmd_resp(handle);
	if (handle->sstc->payload[0] != WAITING_FOR_DEBUG_CERT_DATA) {
		LOG_ERR("%s: Waiting for WAITING_FOR_DEBUG_CERT_DATA(%#x) but got %#x\n",
			__func__, WAITING_FOR_DEBUG_CERT_DATA,
			handle->sstc->payload[0]);
		return rc;
	}

	rc = qmrom_c0_flash_data(handle, dbg_cert, ROM_CMD_C0_CERT_DATA,
				 WAITING_FOR_DEBUG_CERT_DATA, true);
	return 0;
}

static int qmrom_c0_erase_debug_cert(struct qmrom_handle *handle)
{
	int rc;

	LOG_DBG("%s: starting...\n", __func__);

	rc = qmrom_c0_wait_ready(handle);
	if (rc)
		return rc;

	LOG_DBG("%s: sending ROM_CMD_C0_ERASE_DBG_CERT command\n", __func__);
	rc = qmrom_write_cmd32_c0(handle, ROM_CMD_C0_ERASE_DBG_CERT);
	if (rc)
		return rc;

	qmrom_msleep(SPI_READY_TIMEOUT_MS_C0);
	return 0;
}
