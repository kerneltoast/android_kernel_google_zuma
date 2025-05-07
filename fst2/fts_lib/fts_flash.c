/*
  *
  **************************************************************************
  **                        STMicroelectronics                            **
  **************************************************************************
  *                                                                        *
  *                      FTS API for Flashing the IC                       *
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file ftsFlash.c
  * \brief Contains all the functions to handle the FW update process
  */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <stdarg.h>
#include <linux/serio.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>

#include "fts_io.h"
#include "fts_flash.h"
#include "fts_error.h"

#ifdef FW_H_FILE
#include "../fts_fw.h"
#endif


/* decleration of global variable containing System Info Data */
struct sys_info system_info;

/**
  * calculate crc for the given data
  * @param message pointer to data to perform crc
  * @param size varaible for size of data
  * @return OK if success or an error code which specify the type of error
  */
unsigned int calculate_crc(unsigned char *message, int size)
{
	int i, j;
	unsigned int byte, crc, mask;

	i = 0;
	crc = 0xFFFFFFFF;
	while (i < size) {
		byte = message[i];
		crc = crc ^ byte;
		for (j = 7; j >= 0; j--) {
			mask = -(crc & 1);
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
		i = i + 1;
	}
	return ~crc;
}

/** @addtogroup system_info
  * @{
  */

/**
  * Read the System Info data from memory.FW Request is send to load system info
  * to memory and then read from there.
  * @return OK if success or an error code which specify the type of error
  */
int read_sys_info(void)
{
	int res;
	u8 data[SYS_INFO_SIZE] = { 0 };
	int index = 0;
	int i = 0;

	res = fts_request_hdm(HDM_REQ_SYS_INFO);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	res = fts_read_hdm(FRAME_BUFFER_ADDR, data, SYS_INFO_SIZE);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	log_info(1, "%s type: %02X, cnt: %02X, len: %d words\n",
		__func__, data[0], data[1],
		(u16)((data[3] << 8) + data[2]));
	if (data[0] != HDM_REQ_SYS_INFO) {
		log_info(1, "%s parsing ERROR %08X\n", __func__,
			ERROR_TIMEOUT);
		return ERROR_TIMEOUT;
	}
	index += 4;
	u8_to_u16(&data[index], &system_info.u16_api_ver_rev);
	index += 1;
	system_info.u8_api_ver_major = data[++index];
	system_info.u8_api_ver_minor = data[++index];
	index++;
	u8_to_u16(&data[index], &system_info.u16_chip0_id);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_chip0_ver);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_chip1_id);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_chip1_ver);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_fw_ver);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_svn_rev);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_pe_ver);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_reg_ver);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_scr_x_res);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_scr_y_res);
	index += 2;
	system_info.u8_scr_tx_len = data[index];
	system_info.u8_scr_rx_len = data[++index];
	index += 3;
	for (i = 0; i < DIE_INFO_SIZE; i++)
		system_info.u8_die_info[i] = data[index++];
	for (i = 0; i < RELEASE_INFO_SIZE; i++)
		system_info.u8_release_info[i] = data[index++];
	u8_to_u32(&data[index], &system_info.u32_flash_org_info);
	index += 8;
	system_info.u8_cfg_afe_ver = data[index++];
	index += 1;
	system_info.u8_ms_scr_afe_ver = data[index++];
	system_info.u8_ms_scr_gv_ver = data[index++];
	system_info.u8_ms_scr_lp_afe_ver = data[index++];
	system_info.u8_ms_scr_lp_gv_ver = data[index++];
	system_info.u8_ss_tch_afe_ver = data[index++];
	system_info.u8_ss_tch_gv_ver = data[index++];
	system_info.u8_ss_det_afe_ver = data[index++];
	system_info.u8_ss_det_gv_ver = data[index++];
	index += 54;
	u8_to_u16(&data[index], &system_info.u16_dbg_info_addr);
	index += 8;
	u8_to_u16(&data[index], &system_info.u16_ms_scr_raw_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ms_scr_filter_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ms_scr_strength_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ms_scr_baseline_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_tx_raw_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_tx_filter_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_tx_strength_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_tx_baseline_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_rx_raw_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_rx_filter_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_rx_strength_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_tch_rx_baseline_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_tx_raw_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_tx_filter_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_tx_strength_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_tx_baseline_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_rx_raw_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_rx_filter_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_rx_strength_addr);
	index += 2;
	u8_to_u16(&data[index], &system_info.u16_ss_det_rx_baseline_addr);
	index += 18;
	u8_to_u32(&data[index], &system_info.u32_reg_default_sect_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_misc_sect_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_cx_ms_scr_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_cx_ms_scr_lp_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_cx_ss_tch_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_cx_ss_det_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_ioff_ms_scr_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_ioff_ms_scr_lp_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_ioff_ss_tch_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_ioff_ss_det_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_pure_raw_ms_scr_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_pure_raw_ms_scr_lp_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_pure_raw_ss_tch_flash_addr);
	index += 4;
	u8_to_u32(&data[index], &system_info.u32_pure_raw_ss_det_flash_addr);

	log_info(1, "%s: API Version: 0x%04X\n",
		__func__, system_info.u16_api_ver_rev);
	log_info(1, "%s: API Major Version: 0x%02X\n",
		__func__, system_info.u8_api_ver_major);
	log_info(1, "%s: API Minor Version: 0x%02X\n",
		__func__, system_info.u8_api_ver_minor);
	log_info(1, "%s: ChipId0: 0x%04X\n",
		__func__, system_info.u16_chip0_id);
	log_info(1, "%s: ChipVer0: 0x%04X\n",
		__func__, system_info.u16_chip0_ver);
	log_info(1, "%s: ChipId1: 0x%04X\n",
		__func__, system_info.u16_chip1_id);
	log_info(1, "%s: ChipVer1: 0x%04X\n",
		__func__, system_info.u16_chip1_ver);
	log_info(1, "%s: FW Version: 0x%04X\n",
		__func__, system_info.u16_fw_ver);
	log_info(1, "%s: SVN Revision: 0x%04X\n",
		__func__, system_info.u16_svn_rev);
	log_info(1, "%s: PE Version: 0x%04X\n",
		__func__, system_info.u16_pe_ver);
	log_info(1, "%s: REG Revision: 0x%04X\n",
		__func__, system_info.u16_reg_ver);
	log_info(1, "%s: Scr-X Resolution: %d\n",
		__func__, system_info.u16_scr_x_res);
	log_info(1, "%s: Scr-Y Resolution: %d\n",
		__func__, system_info.u16_scr_y_res);
	log_info(1, "%s: Tx Length: %d\n",
		__func__, system_info.u8_scr_tx_len);
	log_info(1, "%s: Rx Length: %d\n",
		__func__, system_info.u8_scr_rx_len);
	log_info(1, "%s: DIE Info: ", __func__);
	for (i = 0; i < DIE_INFO_SIZE; i++)
		printk("%02X ", system_info.u8_die_info[i]);
	printk("\n");
	log_info(1, "%s: External Release Info Info: ", __func__);
	for (i = 0; i < RELEASE_INFO_SIZE; i++)
		printk("%02X ", system_info.u8_release_info[i]);
	printk("\n");
	log_info(1, "%s: Flash Org Info: 0x%08X\n",
		__func__, system_info.u32_flash_org_info);
	log_info(1, "%s: Config Afe Ver: 0x%02X\n",
		__func__, system_info.u8_cfg_afe_ver);
	log_info(1, "%s: Mutual Afe Ver: 0x%02X\n",
		__func__, system_info.u8_ms_scr_afe_ver);
	log_info(1, "%s: Mutual GV Ver: 0x%02X\n",
		__func__, system_info.u8_ms_scr_gv_ver);
	log_info(1, "%s: Mutual LP Afe Ver: 0x%02X\n",
		__func__, system_info.u8_ms_scr_lp_afe_ver);
	log_info(1, "%s: Mutual LP GV Ver: 0x%02X\n",
		__func__, system_info.u8_ms_scr_lp_gv_ver);
	log_info(1, "%s: Self Afe Ver: 0x%02X\n",
		__func__, system_info.u8_ss_tch_afe_ver);
	log_info(1, "%s: Self GV Ver: 0x%02X\n",
		__func__, system_info.u8_ss_tch_gv_ver);
	log_info(1, "%s: Self Detect Afe Ver: 0x%02X\n",
		__func__, system_info.u8_ss_det_afe_ver);
	log_info(1, "%s: Self Detect GV Ver: 0x%02X\n",
		__func__, system_info.u8_ss_det_gv_ver);
	log_info(1, "%s: Debug Info Address: 0x%04X\n",
		__func__, system_info.u16_dbg_info_addr);
	log_info(1, "%s: Mutual Raw Address: 0x%04X\n",
		__func__, system_info.u16_ms_scr_raw_addr);
	log_info(1, "%s: Mutual Filter Address: 0x%04X\n",
		__func__, system_info.u16_ms_scr_filter_addr);
	log_info(1, "%s: Mutual Strength Address: 0x%04X\n",
		__func__, system_info.u16_ms_scr_strength_addr);
	log_info(1, "%s: Mutual Baseline Address: 0x%04X\n",
		__func__, system_info.u16_ms_scr_baseline_addr);
	log_info(1, "%s: Self Tx Raw Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_tx_raw_addr);
	log_info(1, "%s: Self Tx Filter Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_tx_filter_addr);
	log_info(1, "%s: Self Tx Strength Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_tx_strength_addr);
	log_info(1, "%s: Self Tx Baseline Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_tx_baseline_addr);
	log_info(1, "%s: Self Rx Raw Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_rx_raw_addr);
	log_info(1, "%s: Self Rx Filter Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_rx_filter_addr);
	log_info(1, "%s: Self Rx Strength Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_rx_strength_addr);
	log_info(1, "%s: Self Rx Baseline Address: 0x%04X\n",
		__func__, system_info.u16_ss_tch_rx_baseline_addr);
	log_info(1, "%s: Self Detect Tx Raw Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_tx_raw_addr);
	log_info(1, "%s: Self Detect Tx Filter Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_tx_filter_addr);
	log_info(1, "%s: Self Detect Tx Strength Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_tx_strength_addr);
	log_info(1, "%s: Self Detect Tx Baseline Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_tx_baseline_addr);
	log_info(1, "%s: Self Detect Rx Raw Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_rx_raw_addr);
	log_info(1, "%s: Self Detect Rx Filter Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_rx_filter_addr);
	log_info(1, "%s: Self Detect Rx Strength Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_rx_strength_addr);
	log_info(1, "%s: Self Detect Rx Baseline Address: 0x%04X\n",
		__func__, system_info.u16_ss_det_rx_baseline_addr);
	log_info(1, "%s: Default Flash Address: 0x%08X\n",
		__func__, system_info.u32_reg_default_sect_flash_addr);
	log_info(1, "%s: Misc Flash Address: 0x%08X\n",
		__func__, system_info.u32_misc_sect_flash_addr);
	log_info(1, "%s: Cx Mutual Flash Address: 0x%08X\n",
		__func__, system_info.u32_cx_ms_scr_flash_addr);
	log_info(1, "%s: Cx Mutual LP Flash Address: 0x%08X\n",
		__func__, system_info.u32_cx_ms_scr_lp_flash_addr);
	log_info(1, "%s: Cx Self Flash Address: 0x%08X\n",
		__func__, system_info.u32_cx_ss_tch_flash_addr);
	log_info(1, "%s: Cx Self Detect Flash Address: 0x%08X\n",
		__func__, system_info.u32_cx_ss_det_flash_addr);
	log_info(1, "%s: Ioff Mutual Flash Address: 0x%08X\n",
		__func__, system_info.u32_ioff_ms_scr_flash_addr);
	log_info(1, "%s: Ioff Mutual LP Flash Address: 0x%08X\n",
		__func__, system_info.u32_ioff_ms_scr_lp_flash_addr);
	log_info(1, "%s: Ioff Self LP Flash Address: 0x%08X\n",
		__func__, system_info.u32_ioff_ss_tch_flash_addr);
	log_info(1, "%s: Ioff Self Detect Flash Address: 0x%08X\n",
		__func__, system_info.u32_ioff_ss_det_flash_addr);
	log_info(1, "%s: Pure Raw Mutual Flash Address: 0x%08X\n",
		__func__, system_info.u32_pure_raw_ms_scr_flash_addr);
	log_info(1, "%s: Pure Raw Mutual Lp Flash Address: 0x%08X\n",
		__func__, system_info.u32_pure_raw_ms_scr_lp_flash_addr);
	log_info(1, "%s: Pure Raw Self Flash Address: 0x%08X\n",
		__func__, system_info.u32_pure_raw_ss_tch_flash_addr);
	log_info(1, "%s: Pure Raw Self Detect Flash Address: 0x%08X\n",
		__func__, system_info.u32_pure_raw_ss_det_flash_addr);
	return res;
}
/** @}*/

/**
  * Retrieve the actual FW data from the system (ubin file or header file)
  * @param path_to_file name of FW file to load or "NULL" if the FW data should
  * be loaded by a .h file
  * @param data pointer to the pointer which will contains the FW data
  * @param size pointer to a variable which will contain the size of the loaded
  * data
  * @return OK if success or an error code which specify the type of error
  */
int get_fw_file_data(const char *path_to_file, u8 **data, int *size)
{
	const struct firmware *fw = NULL;
	struct device *dev = NULL;
	int res = 0;
	int from = 0;
	char *path = (char *)path_to_file;

	log_info(1, "%s: Getting FW file data...\n", __func__);
	if (strncmp(path_to_file, "NULL", 4) == 0) {
		from = 1;
		path = PATH_FILE_FW;
		log_info(1, "%s: Getting FW file data...\n", __func__);
	}
	/* keep the switch case because if the argument passed is null but
	  * the option from .h is not set we still try to load from bin */
	switch (from) {
#ifdef FW_H_FILE
	case 1:
		log_info(1, "%s: Read FW from .h file!\n", __func__);
		*size = FW_SIZE_NAME;
		*data = (u8 *)kmalloc((*size) * sizeof(u8), GFP_KERNEL);
		if (*data == NULL) {
			log_info(1,
				"%s: Impossible to allocate memory! ERROR %08X\n",
				__func__, ERROR_ALLOC);
			return ERROR_ALLOC;
		}
		memcpy(*data, (u8 *)FW_ARRAY_NAME, (*size));

		break;
#endif
	default:
		log_info(1, "%s: Read FW from BIN file %s !\n", __func__, path);
		dev = get_dev();

		if (dev != NULL) {
			res = request_firmware(&fw, path, dev);
			if (res == 0) {
				*size = fw->size;
				*data = (u8 *)kmalloc((*size) * sizeof(u8),
					GFP_KERNEL);
				if (*data == NULL) {
					log_info(1,
					"%s: Impossible to allocate memory! ERROR %08X\n",
						__func__, ERROR_ALLOC);
					release_firmware(fw);
					return ERROR_ALLOC;
				}
				memcpy(*data, (u8 *)fw->data, (*size));
				release_firmware(fw);
			} else {
				log_info(1,
					"%s: No File found! ERROR %08X\n",
					__func__ , ERROR_FILE_NOT_FOUND);
				return ERROR_FILE_NOT_FOUND;
			}
		} else {
			log_info(1,
				"%s: No device found! ERROR %08X\n",
				__func__, ERROR_OP_NOT_ALLOW);
			return ERROR_OP_NOT_ALLOW;
		}
	}

	log_info(1, "%s: get fw file data finished!\n", __func__);
	return OK;
}

/**
  * Parse the raw data read from a FW file in order to fill properly the fields
  * of a Firmware variable
  * @param ubin_data raw FW data loaded from system
  * @param ubin_size size of ubin_data
  * @param fw_data pointer to a Firmware variable which will contain the
  *processed data
  * @return OK if success or an error code which specify the type of error
  */
int parse_bin_file(u8 *ubin_data, int ubin_size,
		struct firmware_file *fw_data)
{
	int index = 0;
	u32 temp = 0;
	u16 u16_temp = 0;
	u8 sec_index = 0;
	int code_data_found = 0;
	u32 crc = 0;

	crc = calculate_crc(ubin_data + 4, ubin_size - 4);
	if (crc == (u32)((ubin_data[0] << 24) + (ubin_data[1] << 16) +
			(ubin_data[2] << 8) + ubin_data[3]))
		log_info(1, "%s: BIN CRC OK\n",  __func__);
	else {
		log_info(1, "%s: BIN CRC error... ERROR %08X\n",
					__func__, ERROR_FILE_PARSE);
		return ERROR_FILE_PARSE;
	}
	index += 4;
	if (ubin_size <= (BIN_HEADER_SIZE + SECTION_HEADER_SIZE) ||
		ubin_data == NULL) {
		log_info(1,
		"%s: Read only %d instead of %d... ERROR %08X\n",
		__func__, ubin_size, BIN_HEADER_SIZE, ERROR_FILE_PARSE);
		return ERROR_FILE_PARSE;
	}
	u8_to_u32_be(&ubin_data[index], &temp);
	if (temp != BIN_HEADER) {
		log_info(1,
			"%s: Wrong Signature 0x%08X ... ERROR %08X\n",
			__func__, temp, ERROR_FILE_PARSE);
		return ERROR_FILE_PARSE;
	}
	index += 5;
	u8_to_u16_be(&ubin_data[index], &u16_temp);
	if (u16_temp != CHIP_ID) {
		log_info(1,
			"%s: Wrong Chip ID 0x%04X ... ERROR %08X\n",
			__func__, u16_temp, ERROR_FILE_PARSE);
		return ERROR_FILE_PARSE;
	}
	log_info(1, "%s: Chip ID: 0x%04X\n", __func__, u16_temp);
	index += 27;
	while (index < ubin_size) {
		u8_to_u32_be(&ubin_data[index], &temp);
		if (temp != SECTION_HEADER) {
			log_info(1,
				"%s: Wrong Section Signature %08X ... ERROR %08X\n",
				__func__, temp, ERROR_FILE_PARSE);
			return ERROR_FILE_PARSE;
		}
		index += 4;
		u8_to_u16_be(&ubin_data[index], &u16_temp);
		if (u16_temp == FINGERTIP_FW_CODE) {
			if (code_data_found) {
				log_info(1,
				"%s: Cannot have more than one code memh ... ERROR %08X\n",
					__func__, ERROR_FILE_PARSE);
				return ERROR_FILE_PARSE;
			}
			code_data_found = 1;
			index += 4;
			u8_to_u32_be(&ubin_data[index], &temp);
			fw_data->fw_code_size = temp;
			if (fw_data->fw_code_size == 0) {
				log_info(1,
				"%s: Code data cannot be empty ... ERROR %08X\n",
					__func__, ERROR_FILE_PARSE);
				return ERROR_FILE_PARSE;
			}
			fw_data->fw_code_data =
				(u8 *)kmalloc(fw_data->fw_code_size *
				sizeof(u8), GFP_KERNEL);
			if (fw_data->fw_code_data == NULL) {
				log_info(1, "%s: Error allocating memory... ERROR %08X\n",
					__func__, ERROR_FILE_PARSE);
				return ERROR_FILE_PARSE;
			}
			fw_data->num_code_pages =
			(fw_data->fw_code_size / FLASH_PAGE_SIZE);
			if (fw_data->fw_code_size % FLASH_PAGE_SIZE)
				fw_data->num_code_pages++;

			log_info(1, "%s: code pages: %d\n",
				__func__, fw_data->num_code_pages);
			log_info(1, "%s: code size: %d bytes\n",
				__func__, fw_data->fw_code_size);
			index += 12;
			memcpy(fw_data->fw_code_data,
			&ubin_data[index], fw_data->fw_code_size);
			index += fw_data->fw_code_size;
			fw_data->fw_ver =
			(u16)((fw_data->fw_code_data[209] << 8) +
					fw_data->fw_code_data[208]);
			log_info(1, "%s: FW version: 0x%04X\n",
				__func__, fw_data->fw_ver);
			log_info(1, "%s: SVN revision: 0x%04X\n",
				__func__,
				(u16)((fw_data->fw_code_data[211]
				<< 8) +	fw_data->fw_code_data[210]));
			fw_data->flash_code_pages =
				fw_data->fw_code_data[216];
			fw_data->panel_info_pages =
				fw_data->fw_code_data[217];
			log_info(1, "%s: Code Pages(in org info): %02X,Panel Info Pages(in org info): %02X\n",
				__func__, fw_data->flash_code_pages,
				fw_data->panel_info_pages);
			if ((fw_data->flash_code_pages +
			fw_data->panel_info_pages) > NUM_FLASH_PAGES) {
				log_info(1,
					"%s:FW code + panel Info pages(%d) is more the maximum flash pages(%d)\n",
				__func__, (fw_data->flash_code_pages +
				fw_data->panel_info_pages),
				NUM_FLASH_PAGES);
				return ERROR_FILE_PARSE;
			}
			if (fw_data->num_code_pages >
				fw_data->flash_code_pages) {
				log_info(1, "%s: FW code size in the bin file(%d) is more than the FW code pages(%d) allocated by FW\n",
				__func__, fw_data->num_code_pages,
					fw_data->flash_code_pages);
				return ERROR_FILE_PARSE;
			}
		} else {
			fw_data->num_sections++;
			fw_data->sections[sec_index].sec_id = u16_temp;
			index += 4;
			u8_to_u32_be(&ubin_data[index], &temp);
			fw_data->sections[sec_index].sec_size = temp;
			if (fw_data->sections[sec_index].
				sec_size == 0) {
				log_info(1,
					"%s: section data cannot be empty ... ERROR %08X\n",
					__func__, ERROR_FILE_PARSE);
				return ERROR_FILE_PARSE;
			}
			fw_data->sections[sec_index].sec_data =
				(u8 *)kmalloc(fw_data->
				sections[sec_index].sec_size *
				sizeof(u8), GFP_KERNEL);
			if (fw_data->sections[sec_index].
				sec_data == NULL) {
				log_info(1, "%s: Error allocating memory... ERROR %08X\n",
					__func__, ERROR_FILE_PARSE);
				return ERROR_FILE_PARSE;
			}
			log_info(1, "%s: section%d type : 0x%02X\n",
				__func__, sec_index,
				fw_data->sections[sec_index].sec_id);
			log_info(1, "%s: section%d size : %d bytes\n",
				__func__, sec_index,
				fw_data->sections[sec_index].sec_size);
			index += 12;
			memcpy(fw_data->sections[sec_index].sec_data,
				&ubin_data[index],
				fw_data->sections[sec_index].sec_size);
			index += fw_data->sections[sec_index].sec_size;
			if (fw_data->sections[sec_index].sec_id ==
				FINGERTIP_FW_REG) {
				fw_data->sections[sec_index].sec_ver =
				(u16)((fw_data->sections[sec_index].
				sec_data[15] << 8) +
				fw_data->sections[sec_index].
				sec_data[14]);
				log_info(1, "%s: section version : 0x%04X\n",
				__func__,
				fw_data->sections[sec_index].sec_ver);
			}
			sec_index++;
		}
	}
	log_info(1, "%s: Total number of sections : %d\n", __func__,
		fw_data->num_sections);
	return OK;
}

/**
  * Perform all the steps to read the FW that should be burnt in the IC from
  * the system and parse it in order to fill a Firmware struct with the relevant
  * info
  * @param path name of FW file to load or "NULL" if the FW data should be
  *loaded by a .h file
  * @param fw_file pointer to a Firmware variable which will contains
  * the FW data and info
  * @return OK if success or an error code which specify the type of error
  */
int read_fw_file(const char *path, struct firmware_file *fw_file)
{
	int orig_size;
	u8 *orig_data = NULL;
	int res = OK;

	res = get_fw_file_data(path, &orig_data, &orig_size);
	if (res < OK) {
		log_info(1,
			"%s: Impossible to retrieve FW file data... ERROR %08X\n",
			__func__,
			ERROR_MEMH_READ);
		res |= ERROR_MEMH_READ;
		goto goto_end;
	}
	res = parse_bin_file(orig_data, orig_size, fw_file);
	if (res < OK) {
		log_info(1, "%s: BIN file parse ERROR %08X\n", __func__,
			ERROR_MEMH_READ);
		res |= ERROR_MEMH_READ;
		goto goto_end;
	}

goto_end:
	if (orig_data != NULL) {
		kfree(orig_data);
		orig_data = NULL;
	}

	return res;
}
/**
  * To configure spi mode to 4 wire ,by default spi configuration is in 3
  * wire mode up on power up
  * @return OK if success or an error code which specify the type of error
  */
int configure_spi4(void)
{
	int res = OK;
	u8 data = 0x02;

	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, GPIO_GPIO_PU_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	data = 0x07;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE,
		GPIO_MISO_CONFIG_ADDR, &data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	data = 0x02;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, SPI4_CONFIG_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	return res;
}

/**
  * Initaite the mandatory steps to perform flash erase/program
  * including system reset,enable UVLO,flash unlock
  * @return OK if success or an error code which specify the type of error
  */
int flash_update_preset(void)
{
	int res = OK;
	u8 data = 0x01;

	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, SYS_RST_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

#ifndef I2C_INTERFACE
#ifdef SPI4_WIRE
	log_info(1, "%s: Configuring SPI4..\n", __func__);
	res = configure_spi4();
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
#endif
#endif

	data = 0x66;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, UVLO_CTRL_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	data = 0x13;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE,
		FLASH_FSM_CTRL_ADDR, &data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	data = 0x20;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, BOOT_OPT_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	data = 0x00;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, PAGE_SEL_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	res = fts_write_read_u8ux(FTS_CMD_HW_REG_R, HW_ADDR_SIZE,
		FLASH_CTRL_ADDR, &data, 1, DUMMY_BYTE);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	data |= 0x03;
	/*Bit 7 in FLASH_CTRL_ADDR should be set if GPIO6 not to be used,
	by default its 0, so gpio6 should be used*/
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, FLASH_CTRL_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	return res;
}

/**
  * Poll the Flash Status Registers after the execution of a command to check
  * if the Flash becomes ready within a timeout
  * @param type register to check according to the previous command sent
  * @return OK if success or an error code which specify the type of error
  */
int wait_for_flash_ready(u8 type)
{
	u8 cmd[5] = { FTS_CMD_HW_REG_R, 0x20, 0x00, 0x00, type };
	u8 read_data[2] = { 0 };
	int i, res = -1;

	log_info(1, "%s Waiting for flash ready ...\n", __func__);
	for (i = 0; i < FLASH_RETRY_COUNT && res != 0; i++) {
		res = fts_write_read(cmd, 5, read_data, 2);
		if (res < OK)
			log_info(1, "%s wait_for_flash_ready: ERROR %08X\n",
				__func__, ERROR_BUS_W);
		else {
#ifdef I2C_INTERFACE
			res = read_data[0] & 0x80;
#else
			res = read_data[1] & 0x80;
#endif
			log_info(1, "%s flash status = %d\n", __func__, res);
		}
		msleep(FLASH_WAIT_BEFORE_RETRY);
	}

	if (i == FLASH_RETRY_COUNT && res != 0) {
		log_info(1, "%s Wait for flash TIMEOUT! ERROR %08X\n", __func__,
			ERROR_TIMEOUT);
		return ERROR_TIMEOUT;
	}

	log_info(1, "%s Flash READY!\n", __func__);
	return OK;
}

/**
  * Erase the multiple pages in  flash
  * @param flash_pages total page number to be erased
  * @return OK if success or an error code which specify the type of error
  */

int flash_erase(int flash_pages)
{
	u8 i = 0;
	u8 mask[6] = { 0 };
	u8 mask_cnt = 6;
	int res = OK;
	u8 data = 0x00;

	for (i = 0; i < flash_pages; i++) {
		res = from_id_to_mask(i, mask, mask_cnt);
		if (res != OK)
			return res;
	}

	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE,
			FLASH_PAGE_MASK_ADDR, mask, mask_cnt);
	if (res < OK) {
		log_info(1, "%s: mask set ERROR %08X\n", __func__, res);
		return res;
	}
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, PAGE_SEL_ADDR,
						&data, 1);
	if (res < OK) {
		log_info(1, "%s: ERROR %08X\n", __func__, res);
		return res;
	}

	res = fts_write_read_u8ux(FTS_CMD_HW_REG_R, HW_ADDR_SIZE,
			FLASH_MULTI_PAGE_ERASE_ADDR, &data, 1, DUMMY_BYTE);
	if (res < OK) {
		log_info(1, "%s: ERROR %08X\n", __func__, res);
		return res;
	}
	data |= 0x80;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE,
			FLASH_MULTI_PAGE_ERASE_ADDR, &data, 1);
	if (res < OK) {
		log_info(1, "%s: ERROR %08X\n", __func__, res);
		return res;
	}
	data = 0x80;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE,
		FLASH_ERASE_CTRL_ADDR, &data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	res = wait_for_flash_ready(FLASH_ERASE_READY_VAL);

	if (res != OK) {
		log_info(1, "%s: ERROR %08X\n", __func__,
				res | ERROR_FLASH_NOT_READY);
		return res | ERROR_FLASH_NOT_READY;
		/* Flash not ready within the chosen time, better exit! */
	}

	log_info(1, "%s: Erase flash page by page DONE!\n", __func__);

	return OK;
}

/**
  * Start the DMA procedure which actually transfer and burn the data loaded
  * from memory into the Flash
  * @return OK if success or an error code which specify the type of error
  */
int start_flash_dma(void)
{
	int res;

	u8 cmd[12] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00,
		0x6B, 0x00, 0xFF, 0x1C, 0x10, 0x00, 0x00,
		FLASH_DMA_CODE_VAL };

	/* write the command to erase the flash */
	log_info(1, "%s: Command flash DMA ...\n", __func__);
	if (fts_write(cmd, 12) < OK) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	res = wait_for_flash_ready(FLASH_PGM_READY_VAL);

	if (res != OK) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			res | ERROR_FLASH_NOT_READY);
		return res | ERROR_FLASH_NOT_READY;
		/* Flash not ready within the chosen time, better exit! */
	}

	log_info(1, "%s: flash DMA DONE!\n", __func__);

	return OK;
}

/**
  * Prepare DMA procedure by loading data to dram  and Start the DMA procedure
  * @return OK if success or an error code which specify the type of error
  */
int flash_dma(u32 address, u8 *data, int size)
{
	int res = 0;
	u16 word_address = (u16)(address / 4);
	u16 write_count = (u16)((size / 4) - 1);
	u8 cmd[7] = { 0x00, 0x00,
		(word_address & 0xFF),
		((word_address & 0xFF00) >> 8),
		(write_count & 0xFF),
		((write_count & 0xFF00) >> 8),
		0x00 };
	u32 dram_address = 0x00100000;

	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, dram_address,
						data, size);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, FLASH_DMA_ADDR,
						cmd, 7);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	res = start_flash_dma();
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	return res;
}

/**
  * Copy the FW data that should be burnt in the Flash into the memory and then
  * the DMA will take care about burning it into the Flash
  * @param address address in memory where to copy the data for flashing code
  * @param data pointer to an array of byte which contain the data that should
  * be copied into the memory
  * @param size size of data
  * @return OK if success or an error code which specify the type of error
  */
int fill_flash(u32 address, u8 *data, int size)
{
	int remaining = size;
	int to_write = 0;
	int res;
	u32 start_address = address;
	int written_already = 0;

	while (remaining > 0) {
		if (remaining >= FLASH_CHUNK) {
			to_write = FLASH_CHUNK;
			remaining -= FLASH_CHUNK;
		} else {
			to_write = remaining;
			remaining = 0;
		}
		log_info(1, "%s Flash address: 0x%08X, write_count: %d bytes\n",
				__func__, start_address, to_write);
		res = flash_dma(start_address, data + written_already,
				to_write);
		if (res < OK) {
			log_info(1, "%s ERROR %08X\n", __func__, res);
			return res;
		}

		start_address += to_write;
		written_already += to_write;
	}
	return OK;
}

/**
  * Copy the FW section data via hdm write ,then request to fw for save in to flash
  * @param fw  struture includes info and data of fw bin for section update
  * @param section section id for specific section
  * @param save_to_flash flag to enable request for saving to flash
  * @return OK if success or an error code which specify the type of error
  */
int flash_section_burn(struct firmware_file fw,
			fw_section_t section, u8 save_to_flash)
{
	int res = 0;
	int i = 0;

	for (i = 0; i < fw.num_sections; i++) {
		if (fw.sections[i].sec_id == section) {
			res = fts_write_hdm(FRAME_BUFFER_ADDR,
			fw.sections[i].sec_data, fw.sections[i].sec_size);
			if (res < OK) {
				log_info(1, "%s ERROR %08X\n",
				__func__, res | ERROR_FLASH_SEC_UPDATE);
				return res | ERROR_FLASH_SEC_UPDATE;
			}

			res = fts_hdm_write_request(0);
			if (res < OK) {
				log_info(1, "%s ERROR %08X\n",
				__func__, res | ERROR_FLASH_SEC_UPDATE);
				return res | ERROR_FLASH_SEC_UPDATE;
			}
			break;
		}
	}
	if (save_to_flash) {
		res = fts_fw_request(FLASH_SAVE_ADDR, 7, 1,
			TIMEOUT_FW_REG_STATUS);
		if (res < OK) {
			res |=  ERROR_FLASH_SEC_UPDATE;
			log_info(1, "%s ERROR while saving to flash: %08X\n",
					__func__, res);
		}
	}
	return res;
}

/**
  * Execute the procedure to burn a FW in FTM4/FTI IC
  * @param fw structure which contain the FW to be burnt
  * @param force_burn if >0, the flashing procedure will be forced and executed
  * regardless the additional info, otherwise the FW in the file will be burnt
  * only if it is newer than the one running in the IC
  * @return OK if success or an error code which specify the type of error
  */
int flash_burn(struct firmware_file fw, struct force_update_flag *force_burn)
{
	int res = OK;
	u8 data[4] = { 0x00 };
	int section_updated = 0;

	log_info(1, "%s: FW code version: Current FW|Bin FW: 0x%04X|0x%04X\n",
				__func__, system_info.u16_fw_ver, fw.fw_ver);
	if (!force_burn->code_update) {
		if (system_info.u16_fw_ver != fw.fw_ver) {
			log_info(1, "%s: Different FW version: force updating the FW..\n",
						__func__);
			force_burn->code_update = 1;
		} else
			log_info(1, "%s: FW version is same.. No need to update FW..\n",
					__func__);

	}

	log_info(1, "%s: flash code pages allocated: Current|Bin: %d|%d\n",
		__func__, (system_info.u32_flash_org_info & 0xFF),
				fw.flash_code_pages);
	log_info(1, "%s: flash panel info pages allocated: Current|Bin: %d|%d\n",
		__func__, ((system_info.u32_flash_org_info & 0xFF00) >> 8),
		fw.panel_info_pages);
	if (fw.flash_code_pages > (system_info.u32_flash_org_info & 0xFF))
		log_info(1,
		"%s: WARNING!! No FW or There is change in the number of pages allocated for FW code.Flashing the new FW will delete the CX/Reg/Panel config data already saved in the flash..Touch may not work\n",
		__func__);

	if (force_burn->code_update) {
		res = flash_update_preset();
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
					res | ERROR_FLASH_CODE_UPDATE);
			return res | ERROR_FLASH_CODE_UPDATE;
		}
		log_info(1, "%s: Erasing flash..\n", __func__);
		res = flash_erase(fw.num_code_pages);
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
					res | ERROR_FLASH_CODE_UPDATE);
			return res | ERROR_FLASH_CODE_UPDATE;
		}
		log_info(1, "%s: Updating Flash FW Code..\n", __func__);
		res = fill_flash(FLASH_START_ADDR, fw.fw_code_data,
				fw.fw_code_size);
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
					res | ERROR_FLASH_CODE_UPDATE);
			return res | ERROR_FLASH_CODE_UPDATE;
		}
		log_info(1, "%s: Flash Code update finished..\n", __func__);

		res = fts_system_reset(1);
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
					res | ERROR_FLASH_CODE_UPDATE);
			return res | ERROR_FLASH_CODE_UPDATE;
		}

		res = read_sys_info();
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
					res | ERROR_FLASH_CODE_UPDATE);
			return res | ERROR_FLASH_CODE_UPDATE;
		}
		log_info(1, "%s: FW version after FW code update, New FW|Bin FW: 0x%04X|0x%04X\n",
			__func__, system_info.u16_fw_ver, fw.fw_ver);
		if (system_info.u16_fw_ver != fw.fw_ver) {
			log_info(1, "%s: Different FW version after FW code update\n",
					__func__);
			return ERROR_FLASH_CODE_UPDATE;
		}
	}

	res = fts_read_fw_reg(SYS_ERROR_ADDR + 4, data, 4);
	if (res < OK) {
		log_info(1, "%s: ERROR reading system error registers %08X\n",
				__func__, res);
		return ERROR_FLASH_UPDATE;
	}
	log_info(1, "%s: Section System Errors: reg section: %02X, ms_section: %02X, ss_section: %02X\n",
		__func__, (data[0] & REG_CRC_MASK), (data[1] & MS_CRC_MASK),
		(data[1] & SS_CRC_MASK));
	log_info(1, "%s: System Crc Errors: misc: %02X, ioff: %02X, pure_raw_ms: %02X\n",
		__func__, (data[0] & REG_MISC_MASK), (data[2] & IOFF_CRC_MASK),
		(data[3] & RAWMS_CRC_MASK));
	force_burn->section_update[0] = (force_burn->section_update[0] == 1) ?
	force_burn->section_update[0] : ((data[0] & REG_CRC_MASK) != 0);
	force_burn->section_update[1] = (force_burn->section_update[1] == 1) ?
	force_burn->section_update[1] : ((data[1] & MS_CRC_MASK) != 0);
	force_burn->section_update[2] = (force_burn->section_update[2] == 1) ?
	force_burn->section_update[2] : ((data[1] & SS_CRC_MASK) != 0);
	force_burn->panel_init = (force_burn->panel_init == 1) ?
		force_burn->panel_init : (((data[0] & REG_MISC_MASK) != 0) ||
		((data[2] & IOFF_CRC_MASK) != 0) ||
			((data[3] & RAWMS_CRC_MASK) != 0));
	log_info(1, "%s: Force update flags: reg section: %02X, ms_section:%02X, ss_section: %02X, panel_init: %02X\n",
		__func__, force_burn->section_update[0],
		force_burn->section_update[1], force_burn->section_update[2],
		force_burn->panel_init);
	log_info(1, "%s: Reg version before update, Current reg|Bin reg: 0x%04X|0x%04X\n",
		__func__, system_info.u16_reg_ver, fw.sections[0].sec_ver);
	if ((force_burn->section_update[0]) ||
		(system_info.u16_reg_ver != fw.sections[0].sec_ver)) {
		section_updated = 1;
		log_info(1, "%s: Updating reg section..\n", __func__);
		res = flash_section_burn(fw, FINGERTIP_FW_REG, 1);
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__, res);
			return res | ERROR_FLASH_SEC_UPDATE;
		}

		log_info(1, "%s: Flash Reg update done..checking for errors..\n",
			__func__);
	} else
		log_info(1, "%s: No need to update reg section..\n",
			__func__);

#ifdef MS_GV_METHOD
	/*check cfg_afe_ver with ms_scr_gv_ver/ms_scr_lp_gv_ver
	MSCX - Golden Value - ToDo
#endif

#ifdef SS_GV_METHOD
	check cfg_afe_ver with ss_tch_gv_ver/ss_det_gv_ver
	SSCX - Golden Value - ToDo */
#endif
	if (section_updated) {
		res = fts_system_reset(1);
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
				res | ERROR_FLASH_UPDATE);
			return res | ERROR_FLASH_UPDATE;
		}
		res = read_sys_info();
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
				res | ERROR_FLASH_UPDATE);
			return res | ERROR_FLASH_UPDATE;
		}
		res = fts_read_fw_reg(SYS_ERROR_ADDR + 4, data, 2);
		if (res < OK) {
			log_info(1, "%s: ERROR reading system error registers %08X\n",
				__func__, res);
			return ERROR_FLASH_UPDATE;
		}
		log_info(1, "%s: Section System Errors After section update: reg section: %02X, ms_section: %02X, ss_section: %02X\n",
			__func__, (data[0] & REG_CRC_MASK),
			(data[1] & MS_CRC_MASK), (data[1] & SS_CRC_MASK));
		if (((data[0] & REG_CRC_MASK) != 0) ||
			(system_info.u16_reg_ver != fw.sections[0].sec_ver)) {
			log_info(1, "%s: Error updating flash reg section\n",
				__func__);
			return ERROR_FLASH_UPDATE;
		}
		/* to add MS/SS CRC CHECKS AFTER GV FLASH*/
	}
	return res;
}

/**
  * Perform full panel initilisation based on the cx versions and crc status
  * @param force_update , flags  that will  force the flash procedure for each
  * sections and executed regardless the additional info, otherwise the FW in
  * the file will be burnt only if it is newer than the one running in the IC
  * @return OK if success or an error code which specify the type of error
  */
int full_panel_init(struct force_update_flag *force_update)
{
	int res = OK;
	int event_to_search = EVT_ID_NOEVENT;
	u8 read_data[8] = { 0x00 };

	if (!force_update->panel_init) {
#ifndef MS_GV_METHOD
		if ((force_update->section_update[1]) ||
		(system_info.u8_cfg_afe_ver != system_info.u8_ms_scr_afe_ver) ||
		(system_info.u8_cfg_afe_ver !=
		system_info.u8_ms_scr_lp_afe_ver)) {
			force_update->panel_init = 1;
		}
#endif
#ifndef SS_GV_METHOD
		if ((force_update->section_update[2])
			|| (system_info.u8_cfg_afe_ver !=
			system_info.u8_ss_tch_afe_ver) ||
			(system_info.u8_cfg_afe_ver !=
			system_info.u8_ss_det_afe_ver)) {
			force_update->panel_init = 1;
		}
#endif
	}

	if (force_update->panel_init) {
		log_info(1, "%s: Starting Init..\n", __func__);
		res = fts_fw_request(PI_ADDR, 1, 1, TIMEOUT_FPI);
		if (res < OK) {
			log_info(1, "%s: Error performing autotune.. %08X\n",
				__func__, res | ERROR_INIT);
			return res | ERROR_INIT;
		}

		res = poll_for_event(&event_to_search, 1, read_data,
			TIMEOUT_GENERAL);
		if (res < OK)
			log_info(1, "%s ERROR %08X\n", __func__, res);

		res = fts_system_reset(1);
		if (res < OK) {
			log_info(1, "%s: ERROR %08X\n", __func__,
				res | ERROR_INIT);
			return res | ERROR_INIT;
		}
		res = read_sys_info();
		if (res < OK) {
			log_info(1, "%s: Error reading sys info %08X\n",
				__func__, res);
			res |= ERROR_INIT;
		}
		res = fts_read_sys_errors();

#ifndef MS_GV_METHOD
		if ((system_info.u8_cfg_afe_ver !=
			system_info.u8_ms_scr_afe_ver) ||
			(system_info.u8_cfg_afe_ver !=
				system_info.u8_ms_scr_lp_afe_ver)) {
			res |= ERROR_INIT;
			log_info(1,
				"%s: config afe version doesnt match with MS CX fields after autotune.. Touch may not work. %08X\n",
				__func__, res);
		}
#endif
#ifndef SS_GV_METHOD
		if ((system_info.u8_cfg_afe_ver !=
			system_info.u8_ss_tch_afe_ver) ||
			(system_info.u8_cfg_afe_ver !=
			system_info.u8_ss_det_afe_ver)) {
			res |= ERROR_INIT;
			log_info(1,
				"%s: config afe version doesnt match with SS CX fields after autotune.. Touch may not work. %08X\n",
				__func__, res);
		}
#endif
		log_info(1, "%s: Init completed..\n", __func__);
	} else
		log_info(1, "%s: No need to start Init..\n", __func__);
	return res;
}

/**
  * Perform all the steps necessary to burn the FW into the IC
  * @param force_update , flags  that will  force the flash procedure for each
  * sections and executed regardless the additional info, otherwise the FW in
  * the file will be burnt only if it is newer than the one running in the IC
  * @return OK if success or an error code which specify the type of error
  */
int flash_update(struct force_update_flag *force_update)
{
	int res;
	int i = 0;
	struct firmware_file fw;
	fw.fw_code_data = NULL;
	fw.num_sections = 0;
	fw.flash_code_pages = 0;
	fw.num_code_pages = 0;
	fw.fw_code_size = 0;
	fw.fw_ver = 0;
	fw.panel_info_pages = 0;

	for (i = 0; i < FLASH_MAX_SECTIONS; i++) {
		fw.sections[i].sec_data = NULL;
		fw.sections[i].sec_id =
			fw.sections[i].sec_ver = fw.sections[i].sec_size = 0;
	}
	res = read_fw_file(PATH_FILE_FW, &fw);
	if (res < OK) {
		log_info(1, "%s: ERROR reading file %08X\n", __func__, res);
		goto goto_end;
	}
	res = fts_system_reset(1);
	if (res < OK) {
		log_info(1, "%s: Cannot read Controller Ready..No FW or Connection issue.. ERROR %08X\n",
			__func__, res);
		force_update->code_update = 1;
	}

	res = flash_burn(fw, force_update);
	if (res < OK) {
		log_info(1, "%s: ERROR flash update %08X\n", __func__, res);
		goto goto_end;
	}

	res = full_panel_init(force_update);
	if (res < OK) {
		log_info(1, "%s: ERROR auto tune %08X\n", __func__, res);
		res = OK;
		force_update->panel_init = 0;
		log_info(1, "%s: Continue with boot up, production test is skipped and touch may not work\n",
			 __func__, res);
		goto goto_end;
	}

goto_end:
	if (fw.fw_code_data != NULL) {
		kfree(fw.fw_code_data);
		fw.fw_code_data = NULL;
	}
	for (i = 0; i < fw.num_sections; i++) {
		if (fw.sections[i].sec_data != NULL) {
			kfree(fw.sections[i].sec_data);
			fw.sections[i].sec_data = NULL;
		}
	}
	return res;
}
