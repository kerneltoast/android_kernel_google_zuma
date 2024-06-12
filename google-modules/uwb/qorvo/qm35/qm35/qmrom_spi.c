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

#include <linux/spi/spi.h>

#include <qmrom_spi.h>
#include <spi_rom_protocol.h>

#include "qm35.h"

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
	int rc;

	struct spi_transfer xfer[] = {
		{
			.tx_buf = wbuf,
			.rx_buf = rbuf,
			.len = size,
			.speed_hz = qmrom_spi_get_freq(),
		},
	};

	rc = spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));

	if (trace_spi_xfers) {
		print_hex_dump(KERN_DEBUG, "tx:", DUMP_PREFIX_NONE, 16, 1, wbuf,
			       size, false);
		print_hex_dump(KERN_DEBUG, "rx:", DUMP_PREFIX_NONE, 16, 1, rbuf,
			       size, false);
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
					      bool use_prod_fw)
{
	const struct firmware *fw;
	struct spi_device *spi = handle;
	char _fw_name[16]; /* enough room to store "qm35_xx_xxx.bin" */
	const char *fw_name = _fw_name;
	int ret;
	enum chip_revision_e revision = qmrom_h->chip_rev;
	int lcs_state = qmrom_h->lcs_state;

	if (!fwname) {
		if (revision == CHIP_REVISION_A0)
			fw_name = "qm35_a0.bin";
		else if (lcs_state == CC_BSV_SECURE_LCS) {
			if (use_prod_fw)
				fw_name = "qm35_b0_oem_prod.bin";
			else
				fw_name = "qm35_b0_oem.bin";
		} else
			fw_name = "qm35_b0_icv.bin";
	} else {
		fw_name = fwname;
	}
	dev_info(&spi->dev, "Requesting fw %s!\n", fw_name);

	ret = request_firmware(&fw, fw_name, &spi->dev);
	if (ret) {
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
	int count_down = (int)timeout_ms;
	while (!gpiod_get_value(handle) && (--count_down >= 0)) {
		usleep_range(1000, 1100);
	}
	return gpiod_get_value(handle) ? 0 : -1;
}

void qmrom_spi_set_freq(unsigned int freq)
{
	speed_hz = freq;
}

unsigned int qmrom_spi_get_freq()
{
	return speed_hz;
}
