// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2022 Qorvo US, Inc.
 *
 */

#include <qmrom_utils.h>
#include <qmrom_log.h>
#include <qmrom_spi.h>
#include <qmrom.h>
#include <spi_rom_protocol.h>

int qmrom_a0_probe_device(struct qmrom_handle *handle);
int qmrom_b0_probe_device(struct qmrom_handle *handle);
int qmrom_c0_probe_device(struct qmrom_handle *handle);

static void qmrom_free_stcs(struct qmrom_handle *h)
{
	if (h->hstc)
		qmrom_free(h->hstc);
	if (h->sstc)
		qmrom_free(h->sstc);
}

#ifdef CHECK_STCS
void check_stcs(const char *func, int line, struct qmrom_handle *h)
{
	uint32_t *buff = (uint32_t *)h->hstc;
	if (buff[MAX_STC_FRAME_LEN / sizeof(uint32_t)] != 0xfeeddeef) {
		LOG_ERR("%s:%d - hstc %pK corrupted\n", func, line,
			(void *)h->hstc);
	} else {
		LOG_ERR("%s:%d - hstc %pK safe\n", func, line, (void *)h->hstc);
	}
	buff = (uint32_t *)h->sstc;
	if (buff[MAX_STC_FRAME_LEN / sizeof(uint32_t)] != 0xfeeddeef) {
		LOG_ERR("%s:%d - sstc %pK corrupted\n", func, line,
			(void *)h->sstc);
	} else {
		LOG_ERR("%s:%d - sstc %pK safe\n", func, line, (void *)h->sstc);
	}
}
#endif

static int qmrom_allocate_stcs(struct qmrom_handle *h)
{
	int rc = 0;
	uint8_t *tx_buf = NULL, *rx_buf = NULL;

	qmrom_alloc(tx_buf, MAX_STC_FRAME_LEN + sizeof(uint32_t));
	if (tx_buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	qmrom_alloc(rx_buf, MAX_STC_FRAME_LEN + sizeof(uint32_t));
	if (rx_buf == NULL) {
		qmrom_free(tx_buf);
		rc = -ENOMEM;
		goto out;
	}

#ifdef CHECK_STCS
	((uint32_t *)tx_buf)[MAX_STC_FRAME_LEN / sizeof(uint32_t)] = 0xfeeddeef;
	((uint32_t *)rx_buf)[MAX_STC_FRAME_LEN / sizeof(uint32_t)] = 0xfeeddeef;
#endif
	h->hstc = (struct stc *)tx_buf;
	h->sstc = (struct stc *)rx_buf;
	return rc;
out:
	qmrom_free_stcs(h);
	return rc;
}

int qmrom_pre_read(struct qmrom_handle *handle)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.pre_read = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = 0;
	handle->hstc->payload[0] = 0;
	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

int qmrom_read(struct qmrom_handle *handle)
{
	size_t rd_size = handle->sstc->len;
	if (rd_size > MAX_STC_FRAME_LEN)
		return SPI_ERR_INVALID_STC_LEN;
	LOG_DBG("%s: reading %zu bytes...\n", __func__, rd_size);
	memset(handle->hstc, 0, sizeof(struct stc) + rd_size);
	handle->hstc->host_flags.read = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = handle->sstc->len;

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + rd_size);
}

int qmrom_write_cmd(struct qmrom_handle *handle, uint8_t cmd)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = 1;
	handle->hstc->payload[0] = cmd;

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

int qmrom_write_cmd32(struct qmrom_handle *handle, uint32_t cmd)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = sizeof(cmd);
	memcpy(handle->hstc->payload, &cmd, sizeof(cmd));

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

int qmrom_write_size_cmd(struct qmrom_handle *handle, uint8_t cmd,
			 uint16_t data_size, const char *data)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = data_size + 1;
	handle->hstc->payload[0] = cmd;
	memcpy(&handle->hstc->payload[1], data, data_size);

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

int qmrom_write_size_cmd32(struct qmrom_handle *handle, uint32_t cmd,
			   uint16_t data_size, const char *data)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = data_size + sizeof(cmd);
	memcpy(handle->hstc->payload, &cmd, sizeof(cmd));
	memcpy(&handle->hstc->payload[sizeof(cmd)], data, data_size);

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

/*
 * Unfortunately, A0, B0 and C0 have different
 * APIs to get the chip version...
 *
 */
int qmrom_probe_device(struct qmrom_handle *handle)
{
	int rc;

	/* Test B0 first */
	rc = qmrom_b0_probe_device(handle);
	if (!rc)
		return rc;

	/* Test C0 next */
	rc = qmrom_c0_probe_device(handle);
	if (!rc)
		return rc;

	/* Finally try A0 */
	rc = qmrom_a0_probe_device(handle);
	if (!rc)
		return rc;

	/* None matched!!! */
	return -1;
}

struct qmrom_handle *qmrom_init(void *spi_handle, void *reset_handle,
				void *ss_rdy_handle, int comms_retries,
				reset_device_fn reset)
{
	struct qmrom_handle *handle;
	int rc;

	qmrom_alloc(handle, sizeof(struct qmrom_handle));
	if (!handle) {
		LOG_ERR("%s: Couldn't allocate %zu bytes...\n", __func__,
			sizeof(struct qmrom_handle));
		return NULL;
	}
	rc = qmrom_allocate_stcs(handle);
	if (rc) {
		LOG_ERR("%s: Couldn't allocate stcs...\n", __func__);
		qmrom_free(handle);
		return NULL;
	}

	handle->spi_handle = spi_handle;
	handle->reset_handle = reset_handle;
	handle->ss_rdy_handle = ss_rdy_handle;
	handle->comms_retries = comms_retries;
	handle->chip_rev = CHIP_REVISION_UNKNOWN;
	handle->device_version = -1;
	handle->lcs_state = -1;

	handle->dev_ops.reset = reset;

	rc = qmrom_probe_device(handle);
	if (rc) {
		LOG_ERR("%s: qmrom_probe_device returned %d!\n", __func__, rc);
		qmrom_free_stcs(handle);
		qmrom_free(handle);
		return NULL;
	}

	check_stcs(__func__, __LINE__, handle);
	return handle;
}

void qmrom_deinit(struct qmrom_handle *handle)
{
	LOG_DBG("Deinitializing %pK\n", (void *)handle);
	qmrom_free_stcs(handle);
	qmrom_free(handle);
}

int qmrom_flash_dbg_cert(struct qmrom_handle *handle, struct firmware *dbg_cert)
{
	if (!handle->rom_ops.flash_debug_cert) {
		LOG_ERR("%s: flash debug certificate not support on this device\n",
			__func__);
		return -EINVAL;
	}
	return handle->rom_ops.flash_debug_cert(handle, dbg_cert);
}

int qmrom_erase_dbg_cert(struct qmrom_handle *handle)
{
	if (!handle->rom_ops.erase_debug_cert) {
		LOG_ERR("%s: erase debug certificate not support on this device\n",
			__func__);
		return -EINVAL;
	}
	return handle->rom_ops.erase_debug_cert(handle);
}

int qmrom_flash_fw(struct qmrom_handle *handle, const struct firmware *fw)
{
	return handle->rom_ops.flash_fw(handle, fw);
}

int qmrom_flash_unstitched_fw(struct qmrom_handle *handle,
			      const struct unstitched_firmware *fw)
{
	return handle->rom_ops.flash_unstitched_fw(handle, fw);
}

int qmrom_unstitch_fw(const struct firmware *fw,
		      struct unstitched_firmware *unstitched_fw,
		      enum chip_revision_e revision)
{
	uint32_t tot_len = 0;
	uint32_t fw_img_sz = 0;
	uint32_t fw_crt_sz = 0;
	uint32_t key1_crt_sz = 0;
	uint32_t key2_crt_sz = 0;
	uint8_t *p_key1;
	uint8_t *p_key2;
	uint8_t *p_crt;
	uint8_t *p_fw;
	int ret = 0;

	if (revision == CHIP_REVISION_A0) {
		LOG_ERR("%s: A0, no unstitching!!!\n", __func__);
		return -EINVAL;
	}
	if (fw->size < 2 * sizeof(key1_crt_sz)) {
		LOG_ERR("%s: Not enough data (%zu) to unstitch\n", __func__,
			fw->size);
		return -EINVAL;
	}
	LOG_INFO("%s: Unstitching %zu bytes\n", __func__, fw->size);

	/* key1 */
	key1_crt_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + key1_crt_sz + sizeof(key1_crt_sz) > fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (key1)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(key1_crt_sz);
	p_key1 = (uint8_t *)&fw->data[tot_len];
	tot_len += key1_crt_sz;

	/* key2 */
	key2_crt_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + key2_crt_sz + sizeof(key2_crt_sz) > fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (key2)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(key2_crt_sz);
	p_key2 = (uint8_t *)&fw->data[tot_len];
	tot_len += key2_crt_sz;

	/* cert */
	fw_crt_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + fw_crt_sz + sizeof(fw_crt_sz) > fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (content cert)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(fw_crt_sz);
	p_crt = (uint8_t *)&fw->data[tot_len];
	tot_len += fw_crt_sz;

	/* fw */
	fw_img_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + fw_img_sz + sizeof(fw_img_sz) != fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (firmnware)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(fw_img_sz);
	p_fw = (uint8_t *)&fw->data[tot_len];

	qmrom_alloc(unstitched_fw->fw_img, fw_img_sz + sizeof(struct firmware));
	if (unstitched_fw->fw_img == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	qmrom_alloc(unstitched_fw->fw_crt, fw_crt_sz + sizeof(struct firmware));
	if (unstitched_fw->fw_crt == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	qmrom_alloc(unstitched_fw->key1_crt,
		    key1_crt_sz + sizeof(struct firmware));
	if (unstitched_fw->key1_crt == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	qmrom_alloc(unstitched_fw->key2_crt,
		    key2_crt_sz + sizeof(struct firmware));
	if (unstitched_fw->key2_crt == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	unstitched_fw->key1_crt->data =
		(const uint8_t *)(unstitched_fw->key1_crt + 1);
	unstitched_fw->key2_crt->data =
		(const uint8_t *)(unstitched_fw->key2_crt + 1);
	unstitched_fw->fw_crt->data =
		(const uint8_t *)(unstitched_fw->fw_crt + 1);
	unstitched_fw->fw_img->data =
		(const uint8_t *)(unstitched_fw->fw_img + 1);
	unstitched_fw->key1_crt->size = key1_crt_sz;
	unstitched_fw->key2_crt->size = key2_crt_sz;
	unstitched_fw->fw_crt->size = fw_crt_sz;
	unstitched_fw->fw_img->size = fw_img_sz;

	memcpy((void *)unstitched_fw->key1_crt->data, p_key1, key1_crt_sz);
	memcpy((void *)unstitched_fw->key2_crt->data, p_key2, key2_crt_sz);
	memcpy((void *)unstitched_fw->fw_crt->data, p_crt, fw_crt_sz);
	memcpy((void *)unstitched_fw->fw_img->data, p_fw, fw_img_sz);
	return 0;

err:
	if (unstitched_fw->fw_img)
		qmrom_free(unstitched_fw->fw_img);
	if (unstitched_fw->fw_crt)
		qmrom_free(unstitched_fw->fw_crt);
	if (unstitched_fw->key1_crt)
		qmrom_free(unstitched_fw->key1_crt);
	if (unstitched_fw->key2_crt)
		qmrom_free(unstitched_fw->key2_crt);

out:
	return ret;
}

int qmrom_reboot_bootloader(struct qmrom_handle *handle)
{
	int rc;

	rc = qmrom_spi_set_cs_level(handle->spi_handle, 0);
	if (rc) {
		LOG_ERR("%s: spi_set_cs_level(0) failed with %d\n", __func__,
			rc);
		return rc;
	}
	qmrom_msleep(SPI_RST_LOW_DELAY_MS);

	handle->dev_ops.reset(handle->reset_handle);

	qmrom_msleep(SPI_RST_LOW_DELAY_MS);

	rc = qmrom_spi_set_cs_level(handle->spi_handle, 1);
	if (rc) {
		LOG_ERR("%s: spi_set_cs_level(1) failed with %d\n", __func__,
			rc);
		return rc;
	}

	qmrom_msleep(SPI_RST_LOW_DELAY_MS);

	return 0;
}
