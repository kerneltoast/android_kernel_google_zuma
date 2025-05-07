/*
  *
  **************************************************************************
  **                        STMicroelectronics                            **
  **************************************************************************
  **                        marco.cali@st.com                             **
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

#include "ftsCore.h"
#include "ftsCompensation.h"
#include "ftsError.h"
#include "ftsFlash.h"
#include "ftsFrame.h"
#include "ftsIO.h"
#include "ftsSoftware.h"
#include "ftsTest.h"
#include "ftsTime.h"
#include "ftsTool.h"
#include "../fts.h"	/* needed for including the define FW_H_FILE */


#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/stdarg.h>
#include <linux/serio.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>


#ifdef FW_H_FILE
#include "../fts_fw.h"
#endif


/**
  * Retrieve the actual FW data from the system (bin file or header file)
  * @param pathToFile name of FW file to load or "NULL" if the FW data should be
  * loaded by a .h file
  * @param data pointer to the pointer which will contains the FW data
  * @param size pointer to a variable which will contain the size of the loaded
  * data
  * @return OK if success or an error code which specify the type of error
  */
int getFWdata(struct fts_ts_info *info, const char *pathToFile, u8 **data,
	      int *size)
{
	const struct firmware *fw = NULL;
	int res, from = 0;
	char *path = (char *)pathToFile;

	dev_info(info->dev, "getFWdata starting ...\n");
	if (strncmp(pathToFile, "NULL", 4) == 0) {
		from = 1;
		if (info != NULL && info->board)
			path = (char *)info->board->fw_name;
		else
			path = PATH_FILE_FW;
	}
	/* keep the switch case because if the argument passed is null but
	 * the option from .h is not set we still try to load from bin */
	switch (from) {
#ifdef FW_H_FILE
	case 1:
		dev_info(info->dev, "Read FW from .h file!\n");
		*size = FW_SIZE_NAME;
		*data = (u8 *)kmalloc((*size) * sizeof(u8), GFP_KERNEL);
		if (*data == NULL) {
			dev_err(info->dev, "getFWdata: Impossible to allocate memory! ERROR %08X\n",
				ERROR_ALLOC);
			return ERROR_ALLOC;
		}
		memcpy(*data, (u8 *)FW_ARRAY_NAME, (*size));

		break;
#endif
	default:
		dev_info(info->dev, "Read FW from BIN file %s !\n", path);

		if (info->dev != NULL) {
			res = request_firmware(&fw, path, info->dev);
			if (res == 0) {
				*size = fw->size;
				*data = (u8 *)kmalloc((*size) * sizeof(u8),
						      GFP_KERNEL);
				if (*data == NULL) {
					dev_err(info->dev, "getFWdata: Impossible to allocate memory! ERROR %08X\n",
						ERROR_ALLOC);
					release_firmware(fw);
					return ERROR_ALLOC;
				}
				memcpy(*data, (u8 *)fw->data, (*size));
				release_firmware(fw);
			} else {
				dev_err(info->dev, "getFWdata: No File found! ERROR %08X\n",
					ERROR_FILE_NOT_FOUND);
				return ERROR_FILE_NOT_FOUND;
			}
		} else {
			dev_err(info->dev, "getFWdata: No device found! ERROR %08X\n",
				ERROR_OP_NOT_ALLOW);
			return ERROR_OP_NOT_ALLOW;
		}
	}

	dev_info(info->dev, "getFWdata Finished!\n");
	return OK;
}


/**
  * Perform all the steps to read the FW that should be burnt in the IC from
  * the system and parse it in order to fill a Firmware struct with the relevant
  * info
  * @param path name of FW file to load or "NULL" if the FW data should be
  * loaded by a .h file
  * @param fw pointer to a Firmware variable which will contains the FW data and
  * info
  * @param keep_cx if 1, the CX area will be loaded otherwise will be skipped
  * @return OK if success or an error code which specify the type of error
  */
int readFwFile(struct fts_ts_info *info, const char *path, Firmware *fw,
	       int keep_cx)
{
	int res;
	int orig_size;
	u8 *orig_data = NULL;


	res = getFWdata(info, path, &orig_data, &orig_size);
	if (res < OK) {
		dev_err(info->dev, "readFwFile: impossible retrieve FW... ERROR %08X\n",
			ERROR_MEMH_READ);
		return res | ERROR_MEMH_READ;
	}
	res = parseBinFile(info, orig_data, orig_size, fw, keep_cx);
	if (res < OK) {
		dev_err(info->dev, "readFwFile: impossible parse ERROR %08X\n",
			 ERROR_MEMH_READ);
		return res | ERROR_MEMH_READ;
	}

	return OK;
}

/**
  * Perform all the steps necessary to burn the FW into the IC
  * @param path name of FW file to load or "NULL" if the FW data should be
  * loaded by a .h file
  * @param force if 1, the flashing procedure will be forced and executed
  * regardless the additional info, otherwise the FW in the file will be burnt
  * only if it is newer than the one running in the IC
  * @param keep_cx if 2, load and burn the CX area if the chip and firmware bin
  * file CX AFE version are different, otherwise untouch this area.
  * if 1, the CX area will be loaded and burnt otherwise will be skipped and the
  * area will be untouched.
  * @return OK if success or an error code which specify the type of error
  */
int flashProcedure(struct fts_ts_info *info, const char *path, int force,
		   int keep_cx)
{
	Firmware fw;
	int res;

	fw.data = NULL;
	dev_info(info->dev, "Reading Fw file...\n");
	res = readFwFile(info, path, &fw, keep_cx);
	if (res < OK) {
		dev_err(info->dev, "flashProcedure: ERROR %08X\n",
			 (res | ERROR_FLASH_PROCEDURE));
		kfree(fw.data);
		return res | ERROR_FLASH_PROCEDURE;
	}
	dev_info(info->dev, "Fw file read COMPLETED!\n");

#ifdef COMPUTE_INIT_METHOD
	if (keep_cx == CX_CHECK_AFE_VER) {
		if (fw.cx_afe_ver != (uint32_t)info->systemInfo.u8_cxAfeVer) {
			keep_cx = CX_ERASE;
			saveMpFlag(info, MP_FLAG_CX_AFE_CHG);
		} else {
			keep_cx = CX_KEEP;
		}
		dev_info(info->dev, "Update keep_cx to %d\n", keep_cx);
	}
#endif

	dev_info(info->dev, "Starting flashing procedure...\n");
	res = flash_burn(info, fw, force, keep_cx);
	if (res < OK && res != (ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED)) {
		dev_err(info->dev, "flashProcedure: ERROR %08X\n",
			 ERROR_FLASH_PROCEDURE);
		kfree(fw.data);
		return res | ERROR_FLASH_PROCEDURE;
	}
	dev_info(info->dev, "flashing procedure Finished!\n");
	kfree(fw.data);

	return res;
}

/**
  * Poll the Flash Status Registers after the execution of a command to check
  * if the Flash becomes ready within a timeout
  * @param type register to check according to the previous command sent
  * @return OK if success or an error code which specify the type of error
  */
int wait_for_flash_ready(struct fts_ts_info *info, u8 type)
{
	u8 cmd[5] = { FTS_CMD_HW_REG_R, 0x20, 0x00, 0x00, type };

	u8 readData[2] = { 0 };
	int i, res = -1;

	dev_info(info->dev, "Waiting for flash ready ...\n");
	for (i = 0; i < FLASH_RETRY_COUNT && res != 0; i++) {
		res = fts_writeRead(info, cmd, ARRAY_SIZE(cmd), readData, 2);
		if (res < OK)
			dev_err(info->dev, "wait_for_flash_ready: ERROR %08X\n",
				ERROR_BUS_W);
		else {
#ifdef I2C_INTERFACE	/* in case of spi there is a dummy byte */
			res = readData[0] & 0x80;
#else
			res = readData[1] & 0x80;
#endif

			dev_info(info->dev, "flash status = %d\n", res);
		}
		mdelay(FLASH_WAIT_BEFORE_RETRY);
	}

	if (i == FLASH_RETRY_COUNT && res != 0) {
		dev_err(info->dev, "Wait for flash TIMEOUT! ERROR %08X\n",
			 ERROR_TIMEOUT);
		return ERROR_TIMEOUT;
	}

	dev_info(info->dev, "Flash READY!\n");
	return OK;
}


/**
  * Put the M3 in hold
  * @return OK if success or an error code which specify the type of error
  */
int hold_m3(struct fts_ts_info *info)
{
	int ret;
	u8 cmd[1] = { 0x01 };
	u64 address = 0x0;

	dev_info(info->dev, "Command m3 hold...\n");
	ret = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
			    ADDR_SYSTEM_RESET, cmd, 1);
	if (ret < OK) {
		dev_err(info->dev, "%s: ERROR %08X\n", __func__, ret);
		return ret;
	}
	dev_info(info->dev, "Hold M3 DONE!\n");

#if !defined(I2C_INTERFACE)
	if (info->client &&
		(info->client->mode & SPI_3WIRE) == 0) {
		/* configure manually SPI4 because when no fw is running the
		 * chip use SPI3 by default */
		dev_info(info->dev, "Setting SPI4 mode...\n");
		cmd[0] = 0x10;
		ret = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
				    ADDR_GPIO_DIRECTION, cmd, 1);
		if (ret < OK) {
			dev_err(info->dev, "%s: can not set gpio dir ERROR %08X\n",
				__func__, ret);
			return ret;
		}

		cmd[0] = 0x02;
		ret = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
				    ADDR_GPIO_PULLUP, cmd, 1);
		if (ret < OK) {
			dev_err(info->dev, "%s: can not set gpio pull-up ERROR %08X\n",
				__func__, ret);
			return ret;
		}

		if (info->board->dchip_id[0] == ALIX_DCHIP_ID_0 &&
		    info->board->dchip_id[1] == ALIX_DCHIP_ID_1) {
			cmd[0] = 0x70;
			address = ADDR_GPIO_CONFIG_REG3;
		} else {
			cmd[0] = 0x07;
			address = ADDR_GPIO_CONFIG_REG2;
		}
		ret = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
				    address, cmd, 1);
		if (ret < OK) {
			dev_err(info->dev, "%s: can not set gpio config ERROR %08X\n",
				__func__, ret);
			return ret;
		}

		cmd[0] = 0x30;
		ret = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
				    ADDR_GPIO_CONFIG_REG0, cmd, 1);
		if (ret < OK) {
			dev_err(info->dev, "%s: can not set gpio config ERROR %08X\n",
				__func__, ret);
			return ret;
		}

		cmd[0] = SPI4_MASK;
		ret = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
			ADDR_ICR, cmd, 1);
		if (ret < OK) {
			dev_err(info->dev, "%s: can not set spi4 mode ERROR %08X\n",
				__func__, ret);
			return ret;
		}
		mdelay(1);	/* wait for the GPIO to stabilize */
	}
#endif

	return OK;
}




/**
  * Parse the raw data read from a FW file in order to fill properly the fields
  * of a Firmware variable
  * @param fw_data raw FW data loaded from system
  * @param fw_size size of fw_data
  * @param fwData pointer to a Firmware variable which will contain the
  * processed data
  * @param keep_cx if 1, the CX area will be loaded and burnt otherwise will be
  * skipped and the area will be untouched
  * @return OK if success or an error code which specify the type of error
  */
int parseBinFile(struct fts_ts_info *info, u8 *fw_data, int fw_size,
		 Firmware *fwData, int keep_cx)
{
	int dimension, index = 0;
	u32 temp;
	int res, i;
	char buff[(2 + 1) * EXTERNAL_RELEASE_INFO_SIZE + 1];
	int buff_len = sizeof(buff);
	int buff_index = 0;

	/* the file should contain at least the header plus the content_crc */
	if (fw_size < FW_HEADER_SIZE + FW_BYTES_ALIGN || fw_data == NULL) {
		dev_err(info->dev, "parseBinFile: Read only %d instead of %d... ERROR %08X\n",
			fw_size, FW_HEADER_SIZE + FW_BYTES_ALIGN,
			ERROR_FILE_PARSE);
		res = ERROR_FILE_PARSE;
		goto END;
	} else {
		/* start parsing of bytes */
		u8ToU32(&fw_data[index], &temp);
		if (temp != FW_HEADER_SIGNATURE) {
			dev_err(info->dev, "parseBinFile: Wrong Signature %08X ... ERROR %08X\n",
				temp, ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}
		dev_info(info->dev, "parseBinFile: Fw Signature OK!\n");
		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		if (temp != FW_FTB_VER) {
			dev_err(info->dev, "parseBinFile: Wrong ftb_version %08X ... ERROR %08X\n",
				temp, ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}
		dev_info(info->dev, "parseBinFile: ftb_version OK!\n");
		index += FW_BYTES_ALIGN;
		if (fw_data[index] != info->board->dchip_id[0] ||
		    fw_data[index + 1] != info->board->dchip_id[1]) {
			dev_err(info->dev, "parseBinFile: Wrong target %02X != %02X  %02X != %02X ... ERROR %08X\n",
				fw_data[index], info->board->dchip_id[0],
				fw_data[index + 1],
				info->board->dchip_id[1], ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}
		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		dev_info(info->dev, "parseBinFile: FILE SVN REV = %08X\n", temp);

		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->fw_ver = temp;
		dev_info(info->dev, "parseBinFile: FILE Fw Version = %04X\n",
			fwData->fw_ver);

		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		dev_info(info->dev, "parseBinFile: FILE Config Project ID = %08X\n", temp);

		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->config_ver = temp;
		dev_info(info->dev, "parseBinFile: FILE Config Version = %08X\n",
			fwData->config_ver);

		index += FW_BYTES_ALIGN * 2;	/* skip reserved data */

		index += FW_BYTES_ALIGN;
		for (i = 0; i < EXTERNAL_RELEASE_INFO_SIZE; i++) {
			fwData->externalRelease[i] = fw_data[index++];
			buff_index += scnprintf(buff + buff_index,
						buff_len - buff_index,
						"%02X ",
						fwData->externalRelease[i]);
		}
		dev_info(info->dev, "parseBinFile: File External Release = %s\n", buff);

		/* index+=FW_BYTES_ALIGN; */
		u8ToU32(&fw_data[index], &temp);
		fwData->sec0_size = temp;
		dev_info(info->dev, "parseBinFile:  sec0_size = %08X (%d bytes)\n",
			fwData->sec0_size, fwData->sec0_size);

		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->sec1_size = temp;
		dev_info(info->dev, "parseBinFile:  sec1_size = %08X (%d bytes)\n",
			fwData->sec1_size, fwData->sec1_size);

		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->sec2_size = temp;
		dev_info(info->dev, "parseBinFile:  sec2_size = %08X (%d bytes)\n",
			fwData->sec2_size, fwData->sec2_size);

		index += FW_BYTES_ALIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->sec3_size = temp;
		dev_info(info->dev, "parseBinFile:  sec3_size = %08X (%d bytes)\n",
			fwData->sec3_size, fwData->sec3_size);

		index += FW_BYTES_ALIGN;/* skip header crc */

		/* if (!keep_cx) */
		/* { */
		dimension = fwData->sec0_size + fwData->sec1_size +
			    fwData->sec2_size + fwData->sec3_size;
		temp = fw_size;
		/*} else
		 * {
		 *      dimension = fwData->sec0_size + fwData->sec1_size;
		 *      temp = fw_size - fwData->sec2_size - fwData->sec3_size;
		 *      fwData->sec2_size = 0;
		 *      fwData->sec3_size = 0;
		 * }*/

		if (dimension + FW_HEADER_SIZE + FW_BYTES_ALIGN != temp) {
			dev_err(info->dev, "parseBinFile: Read only %d instead of %d... ERROR %08X\n",
				fw_size, dimension + FW_HEADER_SIZE +
				FW_BYTES_ALIGN, ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}

		fwData->data = (u8 *)kmalloc(dimension * sizeof(u8),
					     GFP_KERNEL);
		if (fwData->data == NULL) {
			dev_err(info->dev, "parseBinFile: ERROR %08X\n", ERROR_ALLOC);
			res = ERROR_ALLOC;
			goto END;
		}

		index += FW_BYTES_ALIGN;
		memcpy(fwData->data, &fw_data[index], dimension);
		if (fwData->sec2_size != 0) {
			u8ToU16(&fwData->data[fwData->sec0_size +
					     fwData->sec1_size +
					FW_CX_VERSION], &fwData->cx_ver);
			fwData->cx_afe_ver = fwData->data[fwData->sec0_size +
						fwData->sec1_size +
						FW_CX_AFE_VERSION];
		} else {
			dev_info(info->dev, "parseBinFile: Initialize cx_ver "
				 "and cx_afe_ver to default value!\n");
			fwData->cx_ver = info->systemInfo.u16_cxVer;
			fwData->cx_afe_ver = info->systemInfo.u8_cxAfeVer;
		}
		if (fwData->sec1_size != 0)
			fwData->cfg_afe_ver = fwData->data[fwData->sec0_size +
						FW_CFG_AFE_VERSION];
		else {
			dev_info(info->dev, "parseBinFile: Initialize cfg_ver to "
				 "default value from sysinfo!\n");
			fwData->cfg_afe_ver = info->systemInfo.u8_cfgAfeVer;
		}
		dev_info(info->dev, "parseBinFile: CX Version = %04X\n",
			 fwData->cx_ver);
		dev_info(info->dev, "parseBinFile: CX AFE Version = %02X\n",
			fwData->cx_afe_ver);
		dev_info(info->dev, "parseBinFile: CFG AFE Version = %02X\n",
			 fwData->cfg_afe_ver);

		fwData->data_size = dimension;
		index = FLASH_ORG_INFO_INDEX;
		fwData->fw_code_size = fw_data[index++];
		fwData->panel_config_size = fw_data[index++];
		fwData->cx_area_size = fw_data[index++];
		fwData->fw_config_size = fw_data[index];

		dev_info(info->dev, "parseBinFile: Code Pages: %d panel area Pages: %d"
			" cx area Pages: %d fw config Pages: %d !\n",
			fwData->fw_code_size, fwData->panel_config_size,
			fwData->cx_area_size, fwData->fw_config_size);

		if ((fwData->fw_code_size == 0) ||
			(fwData->panel_config_size == 0) ||
			(fwData->cx_area_size == 0) ||
			(fwData->fw_config_size == 0)) {
			dev_info(info->dev, "parseBinFile: Using default flash Address\n");
			fwData->code_start_addr = FLASH_ADDR_CODE;
			fwData->cx_start_addr = FLASH_ADDR_CX;
			fwData->config_start_addr = FLASH_ADDR_CONFIG;
		} else {
			fwData->code_start_addr = FLASH_ADDR_CODE;
			fwData->cx_start_addr = (FLASH_ADDR_CODE +
						(((fwData->fw_code_size +
						fwData->panel_config_size) *
						FLASH_PAGE_SIZE) / 4));
			fwData->config_start_addr = (FLASH_ADDR_CODE +
						(((fwData->fw_code_size +
						fwData->panel_config_size +
						fwData->cx_area_size) *
						FLASH_PAGE_SIZE) / 4));
		}

		dev_info(info->dev, "parseBinFile: Code start addr: 0x%08X"
			" cx start addr: 0x%08X"
			" fw start addr: 0x%08X !\n",
			fwData->code_start_addr, fwData->cx_start_addr,
			fwData->config_start_addr);

		dev_info(info->dev, "READ FW DONE %d bytes!\n", fwData->data_size);
		res = OK;
		goto END;
	}

END:
	kfree(fw_data);
	return res;
}

/*
 * Enable UVLO and Auto Power Down Mode
 * @return OK if success or an error code which specify the type of error
 */
int flash_enable_uvlo_autopowerdown(struct fts_ts_info *info)
{
	u8 cmd[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00,
			FLASH_UVLO_ENABLE_CODE0,
			FLASH_UVLO_ENABLE_CODE1 };
	u8 cmd1[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00,
			FLASH_AUTOPOWERDOWN_ENABLE_CODE0,
			FLASH_AUTOPOWERDOWN_ENABLE_CODE1 };

	dev_info(info->dev, "Command enable uvlo ...\n");
	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		dev_err(info->dev, "%s: ERROR %08X\n", __func__, ERROR_BUS_W);
		return ERROR_BUS_W;
	}
	if (fts_write(info, cmd1, ARRAY_SIZE(cmd1)) < OK) {
		dev_err(info->dev, "%s: ERROR %08X\n", __func__, ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	dev_info(info->dev, "Enable uvlo and flash auto power down  DONE!\n");

	return OK;
}

/**
  * Unlock the flash to be programmed
  * @return OK if success or an error code which specify the type of error
  */
int flash_unlock(struct fts_ts_info *info)
{
	u8 cmd[6] = { FTS_CMD_HW_REG_W,	  0x20,	  0x00,	  0x00,
		      FLASH_UNLOCK_CODE0, FLASH_UNLOCK_CODE1 };

	u8 cmd1[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00,
		       FLASH_UNLOCK_CODE2, FLASH_UNLOCK_CODE3 };

	dev_info(info->dev, "Command unlock ...\n");
	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		dev_err(info->dev, "flash_unlock: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	if (fts_write(info, cmd1, ARRAY_SIZE(cmd1)) < OK) {
		dev_err(info->dev, "Command unlock: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	dev_info(info->dev, "Unlock flash DONE!\n");

	return OK;
}

/**
  * Unlock the flash to be erased
  * @return OK if success or an error code which specify the type of error
  */
int flash_erase_unlock(struct fts_ts_info *info)
{
	u8 cmd[6] = { FTS_CMD_HW_REG_W,		0x20,	      0x00,
		      0x00,
		      FLASH_ERASE_UNLOCK_CODE0, FLASH_ERASE_UNLOCK_CODE1 };

	dev_info(info->dev, "Try to erase unlock flash...\n");

	dev_info(info->dev, "Command erase unlock ...\n");
	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < 0) {
		dev_err(info->dev, "flash_erase_unlock: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	dev_info(info->dev, "Erase Unlock flash DONE!\n");

	return OK;
}

/**
  * Erase the full flash
  * @return OK if success or an error code which specify the type of error
  */
int flash_full_erase(struct fts_ts_info *info)
{
	int status;

	u8 cmd1[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00,
		       FLASH_ERASE_CODE0 + 1, 0x00 };
	u8 cmd[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00,
		      FLASH_ERASE_CODE0, FLASH_ERASE_CODE1 };

	if (fts_write(info, cmd1, ARRAY_SIZE(cmd1)) < OK) {
		dev_err(info->dev, "flash_erase_page_by_page: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	dev_info(info->dev, "Command full erase sent ...\n");
	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		dev_err(info->dev, "flash_full_erase: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	status = wait_for_flash_ready(info, FLASH_ERASE_CODE0);

	if (status != OK) {
		dev_err(info->dev, "flash_full_erase: ERROR %08X\n", ERROR_FLASH_NOT_READY);
		return status | ERROR_FLASH_NOT_READY;
		/* Flash not ready within the chosen time, better exit! */
	}

	dev_info(info->dev, "Full Erase flash DONE!\n");

	return OK;
}

/**
  * Erase the flash page by page, giving the possibility to skip the CX area and
  * maintain therefore its value
  * @param keep_cx if SKIP_PANEL_INIT the Panel Init pages will be skipped,
  * if > SKIP_PANEL_CX_INIT Cx and Panel Init pages otherwise all the pages will
  * be deleted
  * @return OK if success or an error code which specify the type of error
  */
int flash_erase_page_by_page(struct fts_ts_info *info, ErasePage keep_cx,
			     Firmware *fw)
{
	u8 status, i = 0;

	u8 flash_cx_start_page = FLASH_CX_PAGE_START;
	u8 flash_cx_end_page = FLASH_CX_PAGE_END;
	u8 flash_panel_start_page = FLASH_PANEL_PAGE_START;
	u8 flash_panel_end_page = FLASH_PANEL_PAGE_END;

	u8 cmd1[6] = { FTS_CMD_HW_REG_W,      0x20,	 0x00,	    0x00,
		       FLASH_ERASE_CODE0 + 1, 0x00 };
	u8 cmd[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_ERASE_CODE0,
		      0xA0 };
	u8 cmd2[9] = { FTS_CMD_HW_REG_W, 0x20,		   0x00,
		       0x01,		 0x28,
		       0xFF,
		       0xFF,		 0xFF,		   0xFF };
	u8 mask[4] = { 0 };
	char buff[(2 + 1) * 4 + 1];
	int buff_len = sizeof(buff);
	int index = 0;

	if ((fw->fw_code_size == 0) ||
	(fw->panel_config_size == 0) ||
	(fw->cx_area_size == 0) || (fw->fw_config_size == 0)) {
		dev_info(info->dev, " using default page address!\n");
	} else {
		flash_panel_start_page = fw->fw_code_size;
		if (fw->panel_config_size > 1)
			flash_panel_end_page = flash_panel_start_page +
				(fw->panel_config_size - 1);
		else
			flash_panel_end_page = flash_panel_start_page;

		flash_cx_start_page = flash_panel_end_page + 1;
		if (fw->cx_area_size > 1)
			flash_cx_end_page = flash_cx_start_page +
				(fw->cx_area_size - 1);
		else
			flash_cx_end_page = flash_cx_start_page;
	}

	dev_info(info->dev, " CX Start page: %d CX end page: %d Panel Start Page: %d"
		"Panel End page: %d!\n", flash_cx_start_page,
		flash_cx_end_page, flash_panel_start_page,
		flash_panel_end_page);

	for (i = flash_cx_start_page; i <= flash_cx_end_page && keep_cx >=
	     SKIP_PANEL_CX_INIT; i++) {
		dev_info(info->dev, "Skipping erase CX page %d!\n", i);
		fromIDtoMask(i, mask, 4);
	}


	for (i = flash_panel_start_page; i <= flash_panel_end_page && keep_cx >=
	     SKIP_PANEL_INIT; i++) {
		dev_info(info->dev, "Skipping erase Panel Init page %d!\n", i);
		fromIDtoMask(i, mask, 4);
	}

	for (i = 0; i < 4; i++) {
		cmd2[5 + i] = cmd2[5 + i] & (~mask[i]);
		index += scnprintf(buff + index, buff_len - index,
					"%02X ", cmd2[5 + i]);
	}
	dev_info(info->dev, "Setting the page mask = %s\n", buff);

	dev_info(info->dev, "Writing page mask...\n");
	if (fts_write(info, cmd2, ARRAY_SIZE(cmd2)) < OK) {
		dev_err(info->dev, "flash_erase_page_by_page: Page mask ERROR %08X\n",
			ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	if (fts_write(info, cmd1, ARRAY_SIZE(cmd1)) < OK) {
		dev_err(info->dev, "flash_erase_page_by_page: Disable info ERROR %08X\n",
			ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	dev_info(info->dev, "Command erase pages sent ...\n");
	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		dev_err(info->dev, "flash_erase_page_by_page: Erase ERROR %08X\n",
			ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	status = wait_for_flash_ready(info, FLASH_ERASE_CODE0);

	if (status != OK) {
		dev_err(info->dev, "flash_erase_page_by_page: ERROR %08X\n",
			 ERROR_FLASH_NOT_READY);
		return status | ERROR_FLASH_NOT_READY;
		/* Flash not ready within the chosen time, better exit! */
	}

	dev_info(info->dev, "Erase flash page by page DONE!\n");

	return OK;
}

/**
  * Start the DMA procedure which actually transfer and burn the data loaded
  * from memory into the Flash
  * @return OK if success or an error code which specify the type of error
  */
int start_flash_dma(struct fts_ts_info *info)
{
	int status;
	u8 cmd[12] = { FLASH_CMD_WRITE_REGISTER, 0x20, 0x00, 0x00,
		      0x6B, 0x00, 0x40, 0x42, 0x0F, 0x00, 0x00,
			FLASH_DMA_CODE1 };

	/* write the command to erase the flash */

	dev_info(info->dev, "Command flash DMA ...\n");
	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		dev_err(info->dev, "start_flash_dma: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	status = wait_for_flash_ready(info, FLASH_DMA_CODE0);

	if (status != OK) {
		dev_err(info->dev, "start_flash_dma: ERROR %08X\n", ERROR_FLASH_NOT_READY);
		return status | ERROR_FLASH_NOT_READY;
		/* Flash not ready within the chosen time, better exit! */
	}

	dev_info(info->dev, "flash DMA DONE!\n");

	return OK;
}

/**
  * Copy the FW data that should be burnt in the Flash into the memory and then
  * the DMA will take care about burning it into the Flash
  * @param address address in memory where to copy the data, possible values
  * are FLASH_ADDR_CODE, FLASH_ADDR_CONFIG, FLASH_ADDR_CX
  * @param data pointer to an array of byte which contain the data that should
  * be copied into the memory
  * @param size size of data
  * @return OK if success or an error code which specify the type of error
  */
int fillFlash(struct fts_ts_info *info, u32 address, u8 *data, int size)
{
	int remaining = size, index = 0;
	int toWrite = 0;
	int byteBlock = 0;
	int wheel = 0;
	u32 addr = 0;
	int res;
	int delta;
	u8 *buff = NULL;

	buff = kmalloc(max(DMA_CHUNK + 5, 12), GFP_KERNEL);
	if (buff == NULL) {
		dev_err(info->dev, "fillFlash: ERROR %08X\n", ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	while (remaining > 0) {
		byteBlock = 0;
		addr = 0x00100000;

		while (byteBlock < info->board->flash_chunk && remaining > 0) {
			index = 0;
			if (remaining >= DMA_CHUNK) {
				if ((byteBlock + DMA_CHUNK) <= info->board->flash_chunk) {
					/* dev_err(info->dev, "fillFlash: 1\n"); */
					toWrite = DMA_CHUNK;
					remaining -= DMA_CHUNK;
					byteBlock += DMA_CHUNK;
				} else {
					/* dev_err(info->dev, "fillFlash: 2\n); */
					delta = info->board->flash_chunk - byteBlock;
					toWrite = delta;
					remaining -= delta;
					byteBlock += delta;
				}
			} else {
				if ((byteBlock + remaining) <= info->board->flash_chunk) {
					/* dev_err(info->dev, "fillFlash: 3\n"); */
					toWrite = remaining;
					byteBlock += remaining;
					remaining = 0;
				} else {
					/* dev_err(info->dev, "fillFlash: 4\n"); */
					delta = info->board->flash_chunk - byteBlock;
					toWrite = delta;
					remaining -= delta;
					byteBlock += delta;
				}
			}

			buff[index++] = FTS_CMD_HW_REG_W;
			buff[index++] = (u8)((addr & 0xFF000000) >> 24);
			buff[index++] = (u8)((addr & 0x00FF0000) >> 16);
			buff[index++] = (u8)((addr & 0x0000FF00) >> 8);
			buff[index++] = (u8)(addr & 0x000000FF);

			memcpy(&buff[index], data, toWrite);
			/* dev_err(info->dev, "Command = %02X , address = %02X %02X
			 * , bytes = %d, data =  %02X %02X, %02X %02X\n",
			 * buff[0], buff[1], buff[2], toWrite, buff[3],
			 * buff[4], buff[3 + toWrite-2],
			 * buff[3 + toWrite-1]); */
			if (fts_write_heap(info, buff, index + toWrite) < OK) {
				dev_err(info->dev, "fillFlash: ERROR %08X\n", ERROR_BUS_W);
				kfree(buff);
				return ERROR_BUS_W;
			}

			/* mdelay(5); */
			addr += toWrite;
			data += toWrite;
		}


		/* configuring the DMA */
		byteBlock = byteBlock / 4 - 1;
		index = 0;

		buff[index++] = FLASH_CMD_WRITE_REGISTER;
		buff[index++] = 0x20;
		buff[index++] = 0x00;
		buff[index++] = 0x00;
		buff[index++] = FLASH_DMA_CONFIG;
		buff[index++] = 0x00;
		buff[index++] = 0x00;

		addr = address + ((wheel * info->board->flash_chunk) / 4);
		buff[index++] = (u8)((addr & 0x000000FF));
		buff[index++] = (u8)((addr & 0x0000FF00) >> 8);
		buff[index++] = (u8)(byteBlock & 0x000000FF);
		buff[index++] = (u8)((byteBlock & 0x0000FF00) >> 8);
		buff[index++] = 0x00;

		dev_info(info->dev, "DMA Command = %02X , address = %02X %02X, words =  %02X %02X\n",
			buff[0], buff[8], buff[7], buff[10], buff[9]);

		if (fts_write_heap(info, buff, index) < OK) {
			dev_err(info->dev, "Error during filling Flash! ERROR %08X\n",
				ERROR_BUS_W);
			kfree(buff);
			return ERROR_BUS_W;
		}

		res = start_flash_dma(info);
		if (res < OK) {
			dev_err(info->dev, "Error during flashing DMA! ERROR %08X\n", res);
			kfree(buff);
			return res;
		}
		wheel++;
	}
	kfree(buff);
	return OK;
}


/*
  * Execute the procedure to burn a FW on FTM5/FTI IC
  *
  * @param fw - structure which contain the FW to be burnt
  * @param force_burn - if >0, the flashing procedure will be forced and
  * executed
  *	regardless the additional info, otherwise the FW in the file will be
  *	burned only if it is different from the one running in the IC
  * @param keep_cx - if 1, the function preserves the CX/Panel Init area.
  *	Otherwise, it will be cleared.
  *
  * @return OK if success or an error code which specifies the type of error
  *	encountered
  */
int flash_burn(struct fts_ts_info *info, Firmware fw, int force_burn,
	       int keep_cx)
{
	int res;
	SysInfo systemInfo;
	memcpy(&systemInfo, &info->systemInfo, sizeof(systemInfo));

	if (!force_burn) {
		/* Compare firmware, config, and CX versions */
		if (fw.fw_ver != (uint32_t)systemInfo.u16_fwVer ||
		    fw.config_ver != (uint32_t)systemInfo.u16_cfgVer ||
		    fw.cx_afe_ver != (uint32_t)systemInfo.u8_cxAfeVer)
			goto start;

		for (res = EXTERNAL_RELEASE_INFO_SIZE - 1; res >= 0; res--) {
			if (fw.externalRelease[res] !=
			    systemInfo.u8_releaseInfo[res])
				goto start;
		}

		dev_info(info->dev, "flash_burn: Firmware in the chip matches the firmware to flash! NO UPDATE ERROR %08X\n",
			ERROR_FW_NO_UPDATE);
		return ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED;
	} else if (force_burn == CRC_CX && fw.sec2_size == 0) {
		/* burn procedure to update the CX memory, if not present just
		 * skip it!
		 */
		for (res = EXTERNAL_RELEASE_INFO_SIZE - 1; res >= 0; res--) {
			if (fw.externalRelease[res] !=
			    systemInfo.u8_releaseInfo[res]) {
				/* Avoid loading the CX because it is missing
				 * in the bin file, it just need to update
				 * to last fw+cfg because a new release */
				force_burn = 0;
				goto start;
			}
		}
		dev_info(info->dev, "flash_burn: CRC in CX but fw does not contain CX data! NO UPDATE ERROR %08X\n",
			ERROR_FW_NO_UPDATE);
		return ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED;
	}

	/* Programming procedure start */
start:
	dev_info(info->dev, "Programming Procedure for flashing started:\n\n");

	dev_info(info->dev, " 1) SYSTEM RESET:\n");
	res = fts_system_reset(info);
	if (res < 0) {
		dev_err(info->dev, "    system reset FAILED!\n");
		/* If there is no firmware, there is no controller ready event
		 * and there will be a timeout, we can keep going. But if
		 * there is an I2C error, we must exit.
		 */
		if (res != (ERROR_SYSTEM_RESET_FAIL | ERROR_TIMEOUT))
			return res | ERROR_FLASH_BURN_FAILED;
	} else
		dev_info(info->dev, "   system reset COMPLETED!\n\n");

	msleep(100); /* required by HW for safe flash procedure */

	dev_info(info->dev, " 2) HOLD M3 :\n");

	res = hold_m3(info);
	if (res < OK) {
		dev_err(info->dev, "    hold_m3 FAILED!\n");
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_info(info->dev, "    hold_m3 COMPLETED!\n\n");

	dev_info(info->dev, "3) ENABLE UVLO AND AUTO POWER DOWN MODE :\n");
	res = flash_enable_uvlo_autopowerdown(info);
	if (res < OK) {
		dev_err(info->dev, "    flash_enable_uvlo_autopowerdown FAILED!\n");
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_err(info->dev, "    flash_enable_uvlo_autopowerdown COMPLETED!\n\n");

	dev_info(info->dev, " 4) FLASH UNLOCK:\n");
	res = flash_unlock(info);
	if (res < OK) {
		dev_err(info->dev, "   flash unlock FAILED! ERROR %08X\n",
			 ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_info(info->dev, "   flash unlock COMPLETED!\n\n");

	dev_info(info->dev, " 5) FLASH ERASE UNLOCK:\n");
	res = flash_erase_unlock(info);
	if (res < 0) {
		dev_err(info->dev, "   flash unlock FAILED! ERROR %08X\n",
			 ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_info(info->dev, "   flash unlock COMPLETED!\n\n");

	dev_info(info->dev, " 6) FLASH ERASE:\n");
	if (keep_cx > 0) {
		if (fw.sec2_size != 0 && force_burn == CRC_CX)
			res = flash_erase_page_by_page(info, SKIP_PANEL_INIT,
						       &fw);
		else
			res = flash_erase_page_by_page(info, SKIP_PANEL_CX_INIT,
						       &fw);
	} else {
		res = flash_erase_page_by_page(info, SKIP_PANEL_INIT, &fw);
		if (fw.sec2_size == 0)
			dev_err(info->dev, "WARNING!!! Erasing CX memory but no CX in fw file! touch will not work right after fw update!\n");
	}

	if (res < OK) {
		dev_err(info->dev, "   flash erase FAILED! ERROR %08X\n",
			 ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_info(info->dev, "   flash erase COMPLETED!\n\n");

	dev_info(info->dev, " 7) LOAD PROGRAM:\n");
	res = fillFlash(info, fw.code_start_addr, &fw.data[0], fw.sec0_size);
	if (res < OK) {
		dev_err(info->dev, "   load program ERROR %08X\n",
			 ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_info(info->dev, "   load program DONE!\n");

	dev_info(info->dev, " 8) LOAD CONFIG:\n");
	res = fillFlash(info, fw.config_start_addr, &(fw.data[fw.sec0_size]),
			fw.sec1_size);
	if (res < OK) {
		dev_err(info->dev, "   load config ERROR %08X\n",
			 ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_info(info->dev, "   load config DONE!\n");

	if (fw.sec2_size != 0 && (force_burn == CRC_CX || keep_cx <= 0)) {
		dev_info(info->dev, " 8.1) LOAD CX:\n");
		res = fillFlash(info, fw.cx_start_addr,
				&(fw.data[fw.sec0_size + fw.sec1_size]),
				fw.sec2_size);
		if (res < OK) {
			dev_err(info->dev, "   load cx ERROR %08X\n",
				 ERROR_FLASH_BURN_FAILED);
			return res | ERROR_FLASH_BURN_FAILED;
		}
		dev_info(info->dev, "   load cx DONE!\n");
	}

	dev_info(info->dev, "   Flash burn COMPLETED!\n\n");

	dev_info(info->dev, " 9) SYSTEM RESET:\n");
	res = fts_system_reset(info);
	if (res < 0) {
		dev_err(info->dev, "    system reset FAILED! ERROR %08X\n",
			 ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}
	dev_info(info->dev, "   system reset COMPLETED!\n\n");

	dev_info(info->dev, " 10) FINAL CHECK:\n");
	res = readSysInfo(info, 0);
	if (res < 0) {
		dev_err(info->dev, "flash_burn: Unable to retrieve Chip INFO! ERROR %08X\n",
			ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}

	for (res = 0; res < EXTERNAL_RELEASE_INFO_SIZE; res++) {
		if (fw.externalRelease[res] != info->systemInfo.u8_releaseInfo[res]) {
			/* External release is printed during readSysInfo */
			dev_info(info->dev, "  Firmware in the chip different from the one that was burn!\n");
			return ERROR_FLASH_BURN_FAILED;
		}
	}

	dev_info(info->dev, "   Final check OK!\n");

	return OK;
}
