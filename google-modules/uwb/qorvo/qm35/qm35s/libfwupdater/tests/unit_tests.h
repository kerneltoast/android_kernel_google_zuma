// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2023 Qorvo US, Inc.
 *
 */

#ifndef __UNITTESTS_H__
#define __UNITTESTS_H__

#define NB_RX_MESSAGES 5
#define NB_TX_MESSAGES 5
#define MESSAGE_LEN 16
#define STC_LEN 4

void run_fwupdater_read_test(void *spi_handle);
void run_fwupdater_write_test(void *spi_handle);

void run_fwupdater_download_fwpkg_test(void *spi_handle);
void run_fwupdater_download_img_hdr_test(void *spi_handle);
void run_fwupdater_download_pld_chk_test(void *spi_handle);

#endif /* __UNITTESTS_H__ */
