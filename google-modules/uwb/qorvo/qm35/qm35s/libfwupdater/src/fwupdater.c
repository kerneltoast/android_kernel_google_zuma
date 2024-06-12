// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2023 Qorvo US, Inc.
 *
 */

#ifndef __KERNEL__
#include <stddef.h>
#endif

#include <qmrom_spi.h>
#include <qmrom_log.h>
#include <qmrom_utils.h>
#include <spi_rom_protocol.h>

#include <fwupdater.h>

/* Extract from C0 rom code */
#define MAX_CERTIFICATE_SIZE 0x400
#define MAX_CHUNK_SIZE 3072
#define WAIT_REBOOT_DELAY_MS 250
#define WAIT_SS_IRQ_AFTER_RESET_TIMEOUT_MS 450
#define WAIT_SS_RDY_CHUNK_TIMEOUT 100
#define WAIT_SS_RDY_STATUS_TIMEOUT 10
#define RESULT_RETRIES 3
#define RESULT_CMD_INTERVAL_MS 50
#define CKSUM_TYPE uint32_t
#define CKSUM_SIZE (sizeof(CKSUM_TYPE))
#define TRANPORT_HEADER_SIZE (sizeof(struct stc) + CKSUM_SIZE)
#define EMERGENCY_SPI_FREQ 1000000 /* 1MHz */

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#ifndef __KERNEL__
_Static_assert(MAX_CHUNK_SIZE >= CRYPTO_IMAGES_CERT_PKG_SIZE);
_Static_assert(TRANPORT_HEADER_SIZE + MAX_CERTIFICATE_SIZE < MAX_CHUNK_SIZE);
#endif

#ifndef CONFIG_FWUPDATER_GLOBAL_CHUNK_FLASHING_RETRIES
#define CONFIG_FWUPDATER_GLOBAL_CHUNK_FLASHING_RETRIES 50
#endif

/* local stats */
static int gstats_spi_errors;
static int gstats_ss_rdy_timeouts;

static int send_data_chunks(struct qmrom_handle *handle, const char *data,
			    size_t size);

int run_fwupdater(struct qmrom_handle *handle, const char *fwpkg_bin,
		  size_t size)
{
	int rc;

	gstats_spi_errors = 0;
	gstats_ss_rdy_timeouts = 0;
	handle->nb_global_retry =
		CONFIG_FWUPDATER_GLOBAL_CHUNK_FLASHING_RETRIES;

	if (size < sizeof(struct fw_pkg_hdr_t) +
			   sizeof(struct fw_pkg_img_hdr_t) +
			   CRYPTO_IMAGES_CERT_PKG_SIZE +
			   CRYPTO_FIRMWARE_CHUNK_MIN_SIZE) {
		LOG_ERR("Cannot extract enough data from fw package binary\n");
		return -EINVAL;
	}

	rc = send_data_chunks(handle, fwpkg_bin, size);
	if (rc) {
		LOG_ERR("Sending image failed with %d\n", rc);
		return rc;
	}
	return 0;
}

static int run_fwupdater_get_status(struct qmrom_handle *handle,
				    struct stc *hstc, struct stc *sstc,
				    struct fw_updater_status_t *status)
{
	uint32_t i = 0;
	CKSUM_TYPE *cksum = (CKSUM_TYPE *)(hstc + 1);
	bool owa;
	memset(hstc, 0, TRANPORT_HEADER_SIZE + sizeof(*status));

	while (i++ < RESULT_RETRIES) {
		// Poll the QM
		sstc->all = 0;
		hstc->all = 0;
		*cksum = 0;
		qmrom_spi_transfer(handle->spi_handle, (char *)sstc,
				   (const char *)hstc, TRANPORT_HEADER_SIZE);
		qmrom_spi_wait_for_ready_line(handle->ss_rdy_handle,
					      WAIT_SS_RDY_STATUS_TIMEOUT);
		sstc->all = 0;
		hstc->all = 0;
		hstc->host_flags.pre_read = 1;
		*cksum = 0;
		qmrom_spi_transfer(handle->spi_handle, (char *)sstc,
				   (const char *)hstc, TRANPORT_HEADER_SIZE);
		// LOG_INFO("Pre-Read received:\n");
		// hexdump(LOG_INFO, sstc, sizeof(sstc));

		/* Stops the loop when QM has a result to share */
		owa = sstc->soc_flags.out_waiting;
		qmrom_spi_wait_for_ready_line(handle->ss_rdy_handle,
					      WAIT_SS_RDY_STATUS_TIMEOUT);
		sstc->all = 0;
		hstc->all = 0;
		hstc->host_flags.read = 1;
		hstc->len = sizeof(*status);
		*cksum = 0;
		qmrom_spi_transfer(handle->spi_handle, (char *)sstc,
				   (const char *)hstc,
				   TRANPORT_HEADER_SIZE + sizeof(*status));
		// LOG_INFO("Read received:\n");
		// hexdump(LOG_INFO, sstc, sizeof(*hstc) + sizeof(uint32_t));
		if (owa) {
			memcpy(status, sstc->payload, sizeof(*status));
			if (status->magic == FWUPDATER_STATUS_MAGIC)
				break;
		}
		qmrom_spi_wait_for_ready_line(handle->ss_rdy_handle,
					      WAIT_SS_RDY_STATUS_TIMEOUT);
		// Failed to get the status, reduces the spi speed to
		// an emergency speed to maximize the chance to get the
		// final status
		qmrom_spi_set_freq(EMERGENCY_SPI_FREQ);
		gstats_spi_errors++;
	}
	if (status->magic != FWUPDATER_STATUS_MAGIC) {
		LOG_ERR("Timedout waiting for result\n");
		return -1;
	}
	return 0;
}

static CKSUM_TYPE checksum(const void *data, const size_t size)
{
	CKSUM_TYPE cksum = 0;
	CKSUM_TYPE *ptr = (CKSUM_TYPE *)data;
	CKSUM_TYPE remainder = size & (CKSUM_SIZE - 1);
	size_t idx;

	for (idx = 0; idx < size; idx += CKSUM_SIZE, ptr++)
		cksum += *ptr;

	if (!remainder)
		return cksum;

	while (remainder) {
		cksum += ((uint8_t *)data)[size - remainder];
		remainder--;
	}

	return cksum;
}

static void prepare_hstc(struct stc *hstc, const char *data, size_t len)
{
	CKSUM_TYPE *cksum = (CKSUM_TYPE *)(hstc + 1);
	void *payload = cksum + 1;

	hstc->all = 0;
	hstc->host_flags.write = 1;
	hstc->len = len + CKSUM_SIZE;
	*cksum = checksum(data, len);
#if IS_ENABLED(CONFIG_INJECT_ERROR)
	*cksum += 2;
#endif
	memcpy(payload, data, len);
}

static int xfer_payload_prep_next(struct qmrom_handle *handle,
				  const char *step_name, struct stc *hstc,
				  struct stc *sstc, struct stc *hstc_next,
				  const char **data, size_t *size)
{
	int rc = 0, nb_retry = CONFIG_FWUPDATER_CHUNK_FLASHING_RETRIES;
	CKSUM_TYPE *cksum = (CKSUM_TYPE *)(hstc + 1);

	do {
		int ss_rdy_rc, irq_up;
		sstc->all = 0;
		rc = qmrom_spi_transfer(handle->spi_handle, (char *)sstc,
					(const char *)hstc,
					hstc->len + sizeof(struct stc));
		if (hstc_next) {
			/* Don't wait idle, prepare the next hstc to be sent */
			size_t to_send = MIN(MAX_CHUNK_SIZE, *size);
			prepare_hstc(hstc_next, *data, to_send);
			*size -= to_send;
			*data += to_send;
			hstc_next = NULL;
		}
		ss_rdy_rc = qmrom_spi_wait_for_ready_line(
			handle->ss_rdy_handle, WAIT_SS_RDY_CHUNK_TIMEOUT);
		if (ss_rdy_rc) {
			LOG_ERR("%s Waiting for ss-rdy failed with %d (nb_retry %d , cksum 0x%x)\n",
				step_name, ss_rdy_rc, nb_retry, *cksum);
			gstats_ss_rdy_timeouts++;
			rc = -EAGAIN;
		}
		irq_up = qmrom_spi_read_irq_line(handle->ss_irq_handle);
		if ((!rc && !sstc->soc_flags.ready) || irq_up) {
			LOG_ERR("%s Retry rc %d, sstc 0x%08x, irq %d, cksum %08x\n",
				step_name, rc, sstc->all, irq_up, *cksum);
			rc = -EAGAIN;
			gstats_spi_errors++;
		}
#if IS_ENABLED(CONFIG_INJECT_ERROR)
		(*cksum)--;
#endif
	} while (rc && --nb_retry > 0 && --handle->nb_global_retry > 0);
	if (rc) {
		LOG_ERR("%s transfer failed with %d - (sstc 0x%08x)\n",
			step_name, rc, sstc->all);
	}
	return rc;
}

static int xfer_payload(struct qmrom_handle *handle, const char *step_name,
			struct stc *hstc, struct stc *sstc)
{
	return xfer_payload_prep_next(handle, step_name, hstc, sstc, NULL, NULL,
				      NULL);
}

static int send_data_chunks(struct qmrom_handle *handle, const char *data,
			    size_t size)
{
	struct fw_updater_status_t status;
	uint32_t chunk_nr = 0;
	struct stc *hstc, *sstc, *hstc_current, *hstc_next;
	char *rx, *tx;
	CKSUM_TYPE *cksum;
	int rc = 0;

	qmrom_alloc(rx, MAX_CHUNK_SIZE + TRANPORT_HEADER_SIZE);
	qmrom_alloc(tx, 2 * (MAX_CHUNK_SIZE + TRANPORT_HEADER_SIZE));
	if (!rx || !tx) {
		LOG_ERR("Rx/Tx buffers allocation failure\n");
		rc = -ENOMEM;
		goto exit_nomem;
	}

	sstc = (struct stc *)rx;
	hstc = (struct stc *)tx;
	hstc_current = hstc;
	hstc_next = (struct stc *)&tx[MAX_CHUNK_SIZE + TRANPORT_HEADER_SIZE];
	cksum = (CKSUM_TYPE *)(hstc + 1);

	/* wait for the QM to be ready */
	rc = qmrom_spi_wait_for_ready_line(handle->ss_rdy_handle,
					   WAIT_SS_RDY_CHUNK_TIMEOUT);
	if (rc)
		LOG_ERR("Waiting for ss-rdy failed with %d\n", rc);

	/* Sending the fw package header */
	prepare_hstc(hstc, data, sizeof(struct fw_pkg_hdr_t));
	LOG_INFO("Sending the fw package header (%zu bytes, cksum is 0x%08x)\n",
		 sizeof(struct fw_pkg_hdr_t), *cksum);
	// hexdump(LOG_INFO, hstc->payload + 4, sizeof(struct fw_pkg_hdr_t));
	rc = xfer_payload(handle, "fw package header", hstc, sstc);
	if (rc)
		goto exit;
	/* Move the data to the next offset minus the header footprint */
	size -= sizeof(struct fw_pkg_hdr_t);
	data += sizeof(struct fw_pkg_hdr_t);

	/* Sending the image header */
	prepare_hstc(hstc, data, sizeof(struct fw_pkg_img_hdr_t));
	LOG_INFO("Sending the image header (%zu bytes cksum 0x%08x)\n",
		 sizeof(struct fw_pkg_img_hdr_t), *cksum);
	// hexdump(LOG_INFO, hstc->payload + 4, sizeof(struct fw_pkg_img_hdr_t));
	rc = xfer_payload(handle, "image header", hstc, sstc);
	if (rc)
		goto exit;
	size -= sizeof(struct fw_pkg_img_hdr_t);
	data += sizeof(struct fw_pkg_img_hdr_t);

	/* Sending the cert chain */
	prepare_hstc(hstc, data, CRYPTO_IMAGES_CERT_PKG_SIZE);
	LOG_INFO("Sending the cert chain (%d bytes cksum 0x%08x)\n",
		 CRYPTO_IMAGES_CERT_PKG_SIZE, *cksum);
	rc = xfer_payload(handle, "cert chain", hstc, sstc);
	if (rc)
		goto exit;
	size -= CRYPTO_IMAGES_CERT_PKG_SIZE;
	data += CRYPTO_IMAGES_CERT_PKG_SIZE;

	/* Sending the fw image */
	LOG_INFO("Sending the image (%zu bytes)\n", size);
	LOG_DBG("Sending a chunk (%zu bytes cksum 0x%08x)\n",
		MIN(MAX_CHUNK_SIZE, size), *cksum);
	prepare_hstc(hstc_current, data, MIN(MAX_CHUNK_SIZE, size));
	size -= hstc_current->len - CKSUM_SIZE;
	data += hstc_current->len - CKSUM_SIZE;
	do {
		rc = xfer_payload_prep_next(handle, "data chunk", hstc_current,
					    sstc, hstc_next, &data, &size);
		if (rc)
			goto exit;
		chunk_nr++;
		/* swap hstcs */
		hstc = hstc_current;
		hstc_current = hstc_next;
		hstc_next = hstc;
	} while (size);

	/* Sends the last now */
	rc = xfer_payload_prep_next(handle, "data chunk", hstc_current, sstc,
				    NULL, NULL, NULL);

exit:
	// tries to get the flashing status anyway...
	rc = run_fwupdater_get_status(handle, hstc, sstc, &status);
	if (!rc) {
		if (status.status) {
			LOG_ERR("Flashing failed, fw updater status %#x (errors: sub %#x, cksum %u, rram %u, crypto %d)\n",
				status.status, status.suberror,
				status.cksum_errors, status.rram_errors,
				status.crypto_errors);
			rc = status.status;
		} else {
			if (gstats_ss_rdy_timeouts + gstats_spi_errors +
			    status.cksum_errors + status.rram_errors +
			    status.crypto_errors) {
				LOG_WARN(
					"Flashing succeeded with errors (host %u, ss_rdy_timeout %u, QM %u, cksum %u, rram %u, crypto %d)\n",
					gstats_spi_errors,
					gstats_ss_rdy_timeouts,
					status.spi_errors, status.cksum_errors,
					status.rram_errors,
					status.crypto_errors);
			} else {
				LOG_INFO(
					"Flashing succeeded without any errors\n");
			}

			if (!handle->skip_check_fw_boot) {
				handle->dev_ops.reset(handle->reset_handle);
				qmrom_msleep(WAIT_REBOOT_DELAY_MS);
				rc = qmrom_check_fw_boot_state(
					handle,
					WAIT_SS_IRQ_AFTER_RESET_TIMEOUT_MS);
			}
		}
	} else {
		LOG_ERR("run_fwupdater_get_status returned %d\n", rc);
	}
exit_nomem:
	if (rx)
		qmrom_free(rx);
	if (tx)
		qmrom_free(tx);
	return rc;
}
