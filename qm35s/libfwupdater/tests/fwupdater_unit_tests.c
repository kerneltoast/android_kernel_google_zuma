// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2023 Qorvo US, Inc.
 *
 */

#include "unit_tests.h"

void run_fwupdater_unit_tests(void *spi_handle)
{
	run_fwupdater_download_fwpkg_test(spi_handle);
	run_fwupdater_download_img_hdr_test(spi_handle);
	run_fwupdater_download_pld_chk_test(spi_handle);
	run_fwupdater_read_test(spi_handle);
	run_fwupdater_write_test(spi_handle);
}
