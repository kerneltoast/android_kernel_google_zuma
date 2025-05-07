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

int qm357xx_rom_probe_device(struct qmrom_handle *handle);

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

/*
 * Unfortunately, A0, B0 and C0 have different
 * APIs to get the chip version...
 *
 */
int qmrom_probe_device(struct qmrom_handle *handle,
		       enum device_generation_e dev_gen_hint)
{
	int rc = -1;

	if (dev_gen_hint == DEVICE_GEN_QM357XX ||
	    dev_gen_hint == DEVICE_GEN_UNKNOWN)
		rc = qm357xx_rom_probe_device(handle);

	return rc;
}

struct qmrom_handle *qmrom_init(void *spi_handle, void *reset_handle,
				void *ss_rdy_handle, void *ss_irq_handle,
				int spi_speed, int comms_retries,
				reset_device_fn reset,
				enum device_generation_e dev_gen_hint)
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
	handle->ss_irq_handle = ss_irq_handle;
	handle->comms_retries = comms_retries;
	handle->chip_rev = CHIP_REVISION_UNKNOWN;
	handle->device_version = -1;
	handle->spi_speed = spi_speed;
	handle->skip_check_fw_boot = false;

	handle->dev_ops.reset = reset;

	rc = qmrom_probe_device(handle, dev_gen_hint);
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
