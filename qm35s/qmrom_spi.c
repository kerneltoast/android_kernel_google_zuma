// SPDX-License-Identifier: GPL-2.0

/*
 * This file is part of the QM35 UCI stack for linux.
 *
 * Copyright (c) 2021 Qorvo US, Inc.
 *
 * This software is provided under the GNU General Public License, version 2
 * (GPLv2), as well as under a Qorvo commercial license.
 *
 * You may choose to use this software under the terms of the GPLv2 License,
 * version 2 ("GPLv2"), as published by the Free Software Foundation.
 * You should have received a copy of the GPLv2 along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * This program is distributed under the GPLv2 in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GPLv2 for more
 * details.
 *
 * If you cannot meet the requirements of the GPLv2, you may not use this
 * software for any purpose without first obtaining a commercial license from
 * Qorvo.
 * Please contact Qorvo to inquire about licensing terms.
 *
 * QM35 FW ROM protocol SPI ops
 */

#include <linux/interrupt.h>
#include <linux/spi/spi.h>

#include <qmrom_spi.h>
#include <spi_rom_protocol.h>

#include "qm35.h"

/* TODO Compile QM358XX code */
int qm358xx_rom_probe_device(struct qmrom_handle *handle)
{
	return -1;
}

static const char *fwname = NULL;
static unsigned int speed_hz;
extern int trace_spi_xfers;

void qmrom_set_fwname(const char *name)
{
	fwname = name;
}

int qmrom_spi_transfer(void *handle, char *rbuf, const char *wbuf, size_t size)
{
	struct spi_device *spi = (struct spi_device *)handle;
	struct qm35_ctx *qm35_ctx = spi_get_drvdata(spi);
	int rc;

	struct spi_transfer xfer[] = {
		{
			.tx_buf = wbuf,
			.rx_buf = rbuf,
			.len = size,
			.speed_hz = qmrom_spi_get_freq(),
		},
	};

	qm35_ctx->qmrom_qm_ready = false;
	rc = spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));

	if (trace_spi_xfers) {
		print_hex_dump(KERN_DEBUG, "qm35 tx:", DUMP_PREFIX_NONE, 16, 1,
			       wbuf, size, false);
		print_hex_dump(KERN_DEBUG, "qm35 rx:", DUMP_PREFIX_NONE, 16, 1,
			       rbuf, size, false);
	}

	return rc;
}

int qmrom_spi_set_cs_level(void *handle, int level)
{
	struct spi_device *spi = (struct spi_device *)handle;
	uint8_t dummy = 0;

	struct spi_transfer xfer[] = {
		{
			.tx_buf = &dummy,
			.len = 1,
			.cs_change = !level,
			.speed_hz = qmrom_spi_get_freq(),
		},
	};

	return spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));
}

int qmrom_spi_reset_device(void *reset_handle)
{
	struct qm35_ctx *qm35_hdl = (struct qm35_ctx *)reset_handle;

	return qm35_reset(qm35_hdl, SPI_RST_LOW_DELAY_MS, true);
}

const struct firmware *qmrom_spi_get_firmware(void *handle,
					      struct qmrom_handle *qmrom_h,
					      bool *is_macro_pkg,
					      bool use_prod_fw)
{
	const struct firmware *fw;
	struct spi_device *spi = handle;
	const char *fw_name;
	int ret;
	uint32_t lcs_state = qmrom_h->qm357xx_soc_info.lcs_state;
	*is_macro_pkg = true;

	if (!fwname) {
		if (lcs_state != CC_BSV_SECURE_LCS) {
			dev_warn(&spi->dev, "LCS state is not secure.");
		}

		if (use_prod_fw)
			fw_name = "qm35_fw_pkg_prod.bin";
		else
			fw_name = "qm35_fw_pkg.bin";
	} else {
		fw_name = fwname;
	}
	dev_info(&spi->dev, "Requesting fw %s!\n", fw_name);

	ret = request_firmware(&fw, fw_name, &spi->dev);
	if (ret) {
		if (lcs_state != CC_BSV_SECURE_LCS) {
			dev_warn(&spi->dev, "LCS state is not secure.");
		}

		/* Didn't get the macro package, try the stitched image */
		*is_macro_pkg = false;
		if (use_prod_fw)
			fw_name = "qm35_oem_prod.bin";
		else
			fw_name = "qm35_oem.bin";
		dev_info(&spi->dev, "Requesting fw %s!\n", fw_name);
		ret = request_firmware(&fw, fw_name, &spi->dev);
		if (!ret) {
			dev_info(&spi->dev, "Firmware size is %zu!\n",
				 fw->size);
			return fw;
		}

		release_firmware(fw);
		dev_err(&spi->dev,
			"request_firmware failed (ret=%d) for '%s'\n", ret,
			fw_name);
		return NULL;
	}

	dev_info(&spi->dev, "Firmware size is %zu!\n", fw->size);

	return fw;
}

void qmrom_spi_release_firmware(const struct firmware *fw)
{
	release_firmware(fw);
}

int qmrom_spi_wait_for_ready_line(void *handle, unsigned int timeout_ms)
{
	struct qm35_ctx *qm35_ctx = (struct qm35_ctx *)handle;
	unsigned long flags;
	spin_lock_irqsave(&qm35_ctx->lock, flags);
	if (gpiod_get_value(qm35_ctx->gpio_ss_rdy) == 0 &&
	    qm35_ctx->qmrom_qm_ready) {
		/*
		 * qmrom_qm_ready should be cleared if ready line is low
		 * did we run into a glitch on ss-rdy? or did the soc
		 * crash?
		 */
		qm35_ctx->qmrom_qm_ready = false;
	}
	spin_unlock_irqrestore(&qm35_ctx->lock, flags);

	wait_event_interruptible_timeout(qm35_ctx->qmrom_wq_ready,
					 qm35_ctx->qmrom_qm_ready,
					 msecs_to_jiffies(timeout_ms));
	return gpiod_get_value(qm35_ctx->gpio_ss_rdy) > 0 ? 0 : -1;
}

int qmrom_spi_read_irq_line(void *handle)
{
	return gpiod_get_value(handle);
}

void qmrom_spi_set_freq(unsigned int freq)
{
	speed_hz = freq;
}

unsigned int qmrom_spi_get_freq(void)
{
	return speed_hz;
}

int qmrom_check_fw_boot_state(struct qmrom_handle *handle,
			      unsigned int timeout_ms)
{
	uint8_t raw_flags;
	struct stc hstc, sstc;
	int retries = handle->comms_retries;
	struct qm35_ctx *qm35_ctx = (struct qm35_ctx *)handle->ss_rdy_handle;
	struct spi_device *spi = qm35_ctx->spi;

	/* Check if the ss_irq line is already high */
	int ss_irq_gpio_val = qmrom_spi_read_irq_line(handle->ss_irq_handle);
	if (ss_irq_gpio_val == 0) {
		/* Enable the ss_irq */
		qm35_ctx->hsspi.odw_cleared(&qm35_ctx->hsspi);
		/* Clear the ss_irq flag, otherwise
		 * wait_event_interruptible_timeout() will return immediately */
		clear_bit(HSSPI_FLAGS_SS_IRQ, qm35_ctx->hsspi.flags);
		/* wait for the ss_irq line to become high */
		wait_event_interruptible_timeout(
			qm35_ctx->hsspi.wq,
			test_bit(HSSPI_FLAGS_SS_IRQ, qm35_ctx->hsspi.flags),
			msecs_to_jiffies(timeout_ms));
		ss_irq_gpio_val =
			qmrom_spi_read_irq_line(handle->ss_irq_handle);
		if (ss_irq_gpio_val == 0) {
			dev_err(&spi->dev,
				"%s: Waiting for ss-irq failed with %d\n",
				__func__, 1);
			return -1;
		}
	}

	/* Poll the QM until sstc.all is set or retry limit reached */
	sstc.all = 0;
	hstc.all = 0;
	do {
		qmrom_spi_transfer(handle->spi_handle, (char *)&sstc,
				   (const char *)&hstc, sizeof(hstc));
		qmrom_spi_wait_for_ready_line(handle->ss_rdy_handle,
					      timeout_ms);
	} while (sstc.all == 0 && --retries);

	raw_flags = sstc.raw_flags;
	/* The ROM code sends the same quartets for the first byte of each xfers */
	if (((raw_flags & 0xf0) >> 4) == (raw_flags & 0xf)) {
		dev_err(&spi->dev, "%s: firmware not properly started: %#x\n",
			__func__, raw_flags);
		return -2;
	}
	return 0;
}
