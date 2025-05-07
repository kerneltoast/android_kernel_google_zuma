// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2023 Qorvo US, Inc.
 *
 */

#include <stddef.h>
#include <qmrom_spi.h>
#include <qmrom_log.h>
#include <qmrom_utils.h>

#include "unit_tests.h"

void run_fwupdater_read_test(void *spi_handle)
{
	u_int8_t rx[MESSAGE_LEN];
	u_int8_t tx_poll[STC_LEN] = { 0 };
	u_int8_t tx[MESSAGE_LEN] = { 0x80, 0x00, MESSAGE_LEN - 4,
				     0x00, 0x01, 0x02,
				     0x03, 0x4,	 0x5,
				     0x6,  0x7,	 0x8 };
	qmrom_msleep(100);

	qmrom_spi_wait_for_ready_line(spi_handle, 100);
	qmrom_spi_transfer(spi_handle, rx, tx_poll, sizeof(tx_poll));
	memset(rx, 0, sizeof(tx_poll));

	for (int i = 0; i < NB_RX_MESSAGES; i++) {
		qmrom_spi_wait_for_ready_line(spi_handle, 100);
		qmrom_spi_transfer(spi_handle, rx, tx, sizeof(tx));
		tx[4]++;
		LOG_INFO("received:\n");
		hexdump(LOG_INFO, rx, sizeof(rx));
		memset(rx, 0, sizeof(rx));
	}

	LOG_INFO("%s done\n", __func__);
}

void run_fwupdater_write_test(void *spi_handle)
{
	u_int8_t rx[MESSAGE_LEN];
	u_int8_t tx_prd[STC_LEN] = { 0x40, 0x00, 0x00, 0x00 };
	u_int8_t tx[MESSAGE_LEN] = { 0x20, 0x00, MESSAGE_LEN - 4,
				     0x00, 0x01, 0x02,
				     0x03, 0x4,	 0x5,
				     0x6,  0x7,	 0x8 };

	for (int i = 0; i < NB_TX_MESSAGES; i++) {
		qmrom_spi_wait_for_ready_line(spi_handle, 100);
		qmrom_spi_transfer(spi_handle, rx, tx_prd, sizeof(tx_prd));
		qmrom_spi_wait_for_ready_line(spi_handle, 100);
		qmrom_spi_transfer(spi_handle, rx, tx, sizeof(tx));
		tx[4]++;
		LOG_INFO("received:\n");
		hexdump(LOG_INFO, rx, sizeof(rx));
		memset(rx, 0, sizeof(rx));
	}

	LOG_INFO("%s done\n", __func__);
}
