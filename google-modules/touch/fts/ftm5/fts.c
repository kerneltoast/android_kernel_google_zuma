/**
  * fts.c
  *
  * FTS Capacitive touch screen controller (FingerTipS)
  *
  * Copyright (C) 2016, STMicroelectronics Limited.
  * Authors: AMG(Analog Mems Group)
  *
  *		marco.cali@st.com
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
  *
  * THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
  * OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
  * PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
  * AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
  * INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM
  * THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
  */


/*!
  * \file fts.c
  * \brief It is the main file which contains all the most important functions
  * generally used by a device driver the driver
  */
#include <linux/device.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spi.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/of.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#ifdef KERNEL_ABOVE_2_6_38
#include <linux/input/mt.h>
#endif

#include "fts.h"
#include "fts_lib/ftsCompensation.h"
#include "fts_lib/ftsCore.h"
#include "fts_lib/ftsIO.h"
#include "fts_lib/ftsError.h"
#include "fts_lib/ftsFlash.h"
#include "fts_lib/ftsFrame.h"
#include "fts_lib/ftsGesture.h"
#include "fts_lib/ftsTest.h"
#include "fts_lib/ftsTime.h"
#include "fts_lib/ftsTool.h"

/**
  * Event handler installer helpers
  */
#define event_id(_e)		(EVT_ID_##_e >> 4)
#define handler_name(_h)	fts_##_h##_event_handler

#define install_handler(_i, _evt, _hnd) \
	do { \
		_i->event_dispatch_table[event_id(_evt)] = handler_name(_hnd); \
	} while (0)


/* Use decimal-formatted raw data */
#define RAW_DATA_FORMAT_DEC

#ifdef KERNEL_ABOVE_2_6_38
#define TYPE_B_PROTOCOL
#endif

#ifdef GESTURE_MODE
extern struct mutex gestureMask_mutex;
#endif

#ifdef GESTURE_MODE
static u8 mask[GESTURE_MASK_SIZE + 2];
extern u16 gesture_coordinates_x[GESTURE_MAX_COORDS_PAIRS_REPORT];
extern u16 gesture_coordinates_y[GESTURE_MAX_COORDS_PAIRS_REPORT];
extern int gesture_coords_reported;
extern struct mutex gestureMask_mutex;
#endif

static int fts_init_sensing(struct fts_ts_info *info);
static int fts_mode_handler(struct fts_ts_info *info, int force);
static void fts_pinctrl_setup(struct fts_ts_info *info, bool active);

static int fts_chip_initialization(struct fts_ts_info *info, int init_type);

static const struct dev_pm_ops fts_pm_ops;

/**
  * Clear touch flags
  * @param info pointer to fts_ts_info which contains info about device/hw setup
  */
void clear_touch_flags(struct fts_ts_info *info)
{
	info->touch_id = 0;
	info->palm_touch_mask = 0;
	info->grip_touch_mask = 0;
#ifdef STYLUS_MODE
	info->stylus_id = 0;
#endif
}

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
/**
  * Release all the touches in the linux input subsystem
  * @param info pointer to fts_ts_info which contains info about device/hw setup
  */
void release_all_touches(struct fts_ts_info *info)
{
	unsigned int type = MT_TOOL_FINGER;
	int i;

	mutex_lock(&info->input_report_mutex);

	for (i = 0; i < TOUCH_ID_MAX; i++) {
#ifdef STYLUS_MODE
		if (test_bit(i, &info->stylus_id))
			type = MT_TOOL_PEN;
		else
			type = MT_TOOL_FINGER;
#endif
		input_mt_slot(info->input_dev, i);
		input_report_abs(info->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(info->input_dev, type, 0);
		input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, -1);
	}
	input_report_key(info->input_dev, BTN_TOUCH, 0);
	input_sync(info->input_dev);

	mutex_unlock(&info->input_report_mutex);

	clear_touch_flags(info);
}
#endif

/**
  * @defgroup file_nodes Driver File Nodes
  * Driver publish a series of file nodes used to provide several utilities
  * to the host and give him access to different API.
  * @{
  */

/**
  * @defgroup device_file_nodes Device File Nodes
  * @ingroup file_nodes
  * Device File Nodes \n
  * There are several file nodes that are associated to the device and which
  *  are designed to be used by the host to enable/disable features or trigger
  * some system specific actions \n
  * Usually their final path depend on the definition of device tree node of
  * the IC (e.g /sys/devices/soc.0/f9928000.i2c/i2c-6/6-0049)
  * @{
  */
/***************************************** FW UPGGRADE
 * ***************************************************/

/**
  * File node function to Update firmware from shell \n
  * echo path_to_fw X Y > fwupdate   perform a fw update \n
  * where: \n
  * path_to_fw = file name or path of the the FW to burn, if "NULL" the default
  * approach selected in the driver will be used\n
  * X = 0/1 to force the FW update whichever fw_version and config_id;
  * 0=perform a fw update only if the fw in the file is newer than the fw in the
  * chip \n
  * Y = 0/1 keep the initialization data; 0 = will erase the initialization data
  * from flash, 1 = will keep the initialization data
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent an error code (00000000 no
  * error) \n
  * } = end byte
  */
static ssize_t fwupdate_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	/* default(if not specified by user) set force = 0 and keep_cx to 1 */
	int force = 0;
	int keep_cx = CX_KEEP;
	char path[100 + 1]; /* extra byte to hold '\0'*/
	struct fts_ts_info *info = dev_get_drvdata(dev);

	/* reading out firmware upgrade parameters */
	if (sscanf(buf, "%100s %d %d", path, &force, &keep_cx) >= 1) {
		dev_info(dev, "%s: file = %s, force = %d, keep_cx = %d\n", __func__,
			path, force, keep_cx);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		goog_pm_wake_lock(info->gti, GTI_PM_WAKELOCK_TYPE_FW_UPDATE, false);
#endif
		if (info->sensor_sleep)
			ret = ERROR_BUS_WR;
		else {
#ifdef COMPUTE_INIT_METHOD
			if (keep_cx == CX_ERASE) {
				fts_system_reset(info);
				flushFIFO(info);
				/* Set MPFlag to MP_FLAG_NEED_FPI since we will overwrite MS CX and
				 * SS IX by firmware golden value (if exist).
				 */
				ret = saveMpFlag(info, MP_FLAG_NEED_FPI);
				if (ret < OK)
					dev_err(info->dev,
						"Error while saving MP FLAG! ERROR %08X\n", ret);
				else
					dev_info(info->dev, "MP FLAG saving OK!\n");
			}
#endif
			ret = flashProcedure(info, path, force, keep_cx);
		}

		info->fwupdate_stat = ret;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		goog_pm_wake_unlock(info->gti, GTI_PM_WAKELOCK_TYPE_FW_UPDATE);
#endif

		if (ret == ERROR_BUS_WR)
			dev_err(dev, "%s: bus is not accessible. ERROR %08X\n",
				__func__, ret);
		else if (ret < OK)
			dev_err(dev, "%s Unable to upgrade firmware! ERROR %08X\n",
				__func__, ret);
	} else
		dev_err(dev, "%s: Wrong number of parameters! ERROR %08X\n",
			__func__, ERROR_OP_NOT_ALLOW);
	return count;
}

static ssize_t fwupdate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);

	/* fwupdate_stat: ERROR code Returned by flashProcedure. */
	return scnprintf(buf, PAGE_SIZE, "{ %08X }\n", info->fwupdate_stat);
}

/***************************************** UTILITIES
  * (current fw_ver/conf_id, active mode, file fw_ver/conf_id)
  ***************************************************/
static ssize_t get_fw_info(struct fts_ts_info *info, char *buf, size_t buf_size)
{
	ssize_t buf_idx = 0;
	char temp[35];

	buf_idx += scnprintf(buf + buf_idx, buf_size - buf_idx,
			     "\nREL: %s\n",
			     printHex("",
				      info->systemInfo.u8_releaseInfo,
				      EXTERNAL_RELEASE_INFO_SIZE,
				      temp,
				      sizeof(temp)));
	buf_idx += scnprintf(buf + buf_idx, buf_size - buf_idx,
			     "FW: %04X\nCFG: %04X\nAFE: %02X\nProject: %04X\n",
			     info->systemInfo.u16_fwVer,
			     info->systemInfo.u16_cfgVer,
			     info->systemInfo.u8_cfgAfeVer,
			     info->systemInfo.u16_cfgProjectId);
	buf_idx += scnprintf(buf + buf_idx, buf_size - buf_idx,
			     "FW file: %s\n", info->board->fw_name);

	buf_idx += scnprintf(buf + buf_idx, buf_size - buf_idx,
			     "Extended display info: ");
	if (!info->extinfo.is_read)
		buf_idx += scnprintf(buf + buf_idx, buf_size - buf_idx,
			     "[pending]");
	else if (info->extinfo.size == 0)
		buf_idx += scnprintf(buf + buf_idx, buf_size - buf_idx,
			     "[none]");
	else if (info->extinfo.size * 2 < buf_size - buf_idx) {
		bin2hex(buf + buf_idx, info->extinfo.data, info->extinfo.size);
		buf_idx += info->extinfo.size * 2;
	}

	buf_idx += scnprintf(buf + buf_idx, buf_size - buf_idx,
			     "\nMPFlag: %02X\n",
			     info->systemInfo.u8_mpFlag);
	return buf_idx;
}

/**
  * File node to show on terminal external release version in Little Endian \n
  * (first the less significant byte) \n
  * cat appid	show the external release version of the FW running in the IC
  */
static ssize_t appid_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);

	ssize_t buf_idx = get_fw_info(info, buf, PAGE_SIZE);

	return buf_idx;
}

/**
  * File node to show on terminal the mode that is active on the IC \n
  * cat mode_active		    to show the bitmask which indicate
  * the modes/features which are running on the IC in a specific instant of time
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1 = 1 byte in HEX format which represent the actual running scan mode
  * (@link scan_opt Scan Mode Options @endlink) \n
  * X2 = 1 byte in HEX format which represent the bitmask on which is running
  * the actual scan mode \n
  * X3X4 = 2 bytes in HEX format which represent a bitmask of the features that
  * are enabled at this moment (@link feat_opt Feature Selection Options
  * @endlink) \n
  * } = end byte
  * @see fts_mode_handler()
  */
static ssize_t mode_active_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);

	dev_info(dev, "Current mode active = %08X\n", info->mode);
	return scnprintf(buf, PAGE_SIZE, "{ %08X }\n", info->mode);
}

/**
  * File node to show the fw_ver and config_id of the FW file
  * cat fw_file_test			show on the kernel log external release
  * of the FW stored in the fw file/header file
  */
static ssize_t fw_file_test_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	Firmware fw;
	int ret;
	char temp[100] = { 0 };

	fw.data = NULL;
	ret = readFwFile(info, info->board->fw_name, &fw, 0);

	if (ret < OK)
		dev_err(dev, "Error during reading FW file! ERROR %08X\n", ret);
	else
		dev_info(dev, "%s, size = %d bytes\n",
			 printHex("EXT Release = ",
				  info->systemInfo.u8_releaseInfo,
				  EXTERNAL_RELEASE_INFO_SIZE,
				  temp,
				  sizeof(temp)),
			 fw.data_size);
	kfree(fw.data);
	return 0;
}

static ssize_t status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	u8 *dump = NULL;
	int dumpSize = ERROR_DUMP_ROW_SIZE * ERROR_DUMP_COL_SIZE;
	u8 reg;
	int written = 0;
	int res;
	int i;

	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Mode: 0x%08X\n", info->mode);

	res = fts_writeReadU8UX(info, FTS_CMD_HW_REG_R, ADDR_SIZE_HW_REG,
				ADDR_ICR, &reg, 1, DUMMY_HW_REG);
	if (res < 0)
		dev_err(dev, "%s: failed to read ICR.\n", __func__);
	else
		written += scnprintf(buf + written, PAGE_SIZE - written,
			     "ICR: 0x%02X\n", reg);

	dump = kzalloc(dumpSize, GFP_KERNEL);
	if (!dump) {
		written += strlcat(buf + written, "Buffer allocation failed!\n",
				   PAGE_SIZE - written);
		goto exit;
	}

	res = dumpErrorInfo(info, dump,
			    ERROR_DUMP_ROW_SIZE * ERROR_DUMP_COL_SIZE);
	if (res >= 0) {
		written += strlcat(buf + written, "Error dump:",
				   PAGE_SIZE - written);
		for (i = 0; i < dumpSize; i++) {
			if (i % 8 == 0)
				written += scnprintf(buf + written,
						     PAGE_SIZE - written,
						     "\n%02X: ", i);
			written += scnprintf(buf + written,
					     PAGE_SIZE - written,
					     "%02X ", dump[i]);
		}
		written += strlcat(buf + written, "\n", PAGE_SIZE - written);
	}

exit:
	kfree(dump);
	return written;
}

#if 0
/**
  * File node to obtain and show strength frame
  * cat strength_frame			to obtain strength data \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent an error code (00000000 no
  *error) \n
  * **** if error code is all 0s **** \n
  * FF = 1 byte in HEX format number of rows \n
  * SS = 1 byte in HEX format number of columns \n
  * N1, ... = the decimal value of each node separated by a coma \n
  * ********************************* \n
  * } = end byte
  */
static ssize_t fts_strength_frame_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	MutualSenseFrame frame;
	int res, count, j, size = (6 * 2) + 1, index = 0;
	char *all_strbuff = NULL;
	/* char buff[CMD_STR_LEN] = {0}; */
	/* struct i2c_client *client = to_i2c_client(dev); */
	struct fts_ts_info *info = dev_get_drvdata(dev);
	size_t buf_size = 0;

	frame.node_data = NULL;

	res = fts_enableInterrupt(info, false);
	if (res < OK)
		goto END;

	res = senseOn(info);
	if (res < OK) {
		dev_err(dev, "%s: could not start scanning! ERROR %08X\n",
			__func__, res);
		goto END;
	}
	mdelay(WAIT_FOR_FRESH_FRAMES);
	res = senseOff(info);
	if (res < OK) {
		dev_err(dev, "%s: could not finish scanning! ERROR %08X\n",
			__func__, res);
		goto END;
	}

	mdelay(WAIT_AFTER_SENSEOFF);
	flushFIFO(info);

	res = getMSFrame3(info, MS_STRENGTH, &frame);
	if (res < OK) {
		dev_err(dev, "%s: could not get the frame! ERROR %08X\n",
			__func__, res);
		goto END;
	} else {
		size += (res * 6);
		dev_info(dev, "The frame size is %d words\n", res);
		res = OK;
		print_frame_short(info, "MS Strength frame =",
				  array1dTo2d_short(
				  frame.node_data, frame.node_data_size,
				  frame.header.sense_node),
				  frame.header.force_node,
				  frame.header.sense_node);
	}

END:
	flushFIFO(info);
	release_all_touches(info);
	fts_mode_handler(info, 1);

	buf_size = size * sizeof(char);
	all_strbuff = (char *)kzalloc(buf_size, GFP_KERNEL);

	if (all_strbuff != NULL) {
		index += scnprintf(all_strbuff + index, buf_size - index, "{ %08X", res);
		if (res >= OK) {
			index += scnprintf(all_strbuff + index, buf_size - index, "%02X",
				 (u8)frame.header.force_node);
			index += scnprintf(all_strbuff + index, buf_size - index, "%02X",
				 (u8)frame.header.sense_node);
			for (j = 0; j < frame.node_data_size; j++) {
				index += scnprintf(all_strbuff + index, buf_size - index, "%d,%n",
					 frame.node_data[j], &count);
			}
			kfree(frame.node_data);
		}
		index += scnprintf(all_strbuff + index, buf_size - index, " }");

		count = scnprintf(buf, TSP_BUF_SIZE, "%s\n", all_strbuff);
		kfree(all_strbuff);
	} else
		dev_err(dev, "%s: Unable to allocate all_strbuff! ERROR %08X\n",
			__func__, ERROR_ALLOC);

	fts_enableInterrupt(info, true);
	return count;
}
#endif

/***************************************** FEATURES
  ***************************************************/

/* TODO: edit this function according to the features policy to allow during
  * the screen on/off, following is shown an example but check always with ST
  * for more details */
/**
  * Check if there is any conflict in enable/disable a particular feature
  * considering the features already enabled and running
  * @param info pointer to fts_ts_info which contains info about the device
  * and its hw setup
  * @param feature code of the feature that want to be tested
  * @return OK if is possible to enable/disable feature, ERROR_OP_NOT_ALLOW
  * in case of any other conflict
  */
int check_feature_feasibility(struct fts_ts_info *info, unsigned int feature)
{
	int res = OK;

/* Example based on the status of the screen and on the feature
  * that is trying to enable */
	/*res=ERROR_OP_NOT_ALLOW;
	  * if(info->resume_bit ==0){
	  *      switch(feature){
	  #ifdef GESTURE_MODE
	  *              case FEAT_SEL_GESTURE:
	  *                      res = OK;
	  *              break;
	  #endif
	  *              default:
	  *                      dev_err(dev, "%s: Feature not allowed in this
	  * operating mode! ERROR %08X\n", __func__, res);
	  *              break;
	  *
	  *      }
	  * }else{
	  *      switch(feature){
	  #ifdef GESTURE_MODE
	  *              case FEAT_SEL_GESTURE:
	  #endif
	  *              case FEAT__SEL_GLOVE: // glove mode can only activate
	  *during sense on
	  *                      res = OK;
	  *              break;
	  *
	  *              default:
	  *                      dev_err(dev, "%s: Feature not allowed in this
	  * operating mode! ERROR %08X\n", __func__, res);
	  *              break;
	  *
	  *      }
	  * }*/


/* Example based only on the feature that is going to be activated */
	switch (feature) {
	case FEAT_SEL_GESTURE:
		if (info->cover_enabled == 1) {
			res = ERROR_OP_NOT_ALLOW;
			dev_err(info->dev, "%s: Feature not allowed when in Cover mode! ERROR %08X\n",
				__func__, res);
			/* for example here can be placed a code for disabling
			  * the cover mode when gesture is activated */
		}
		break;

	case FEAT_SEL_GLOVE:
		if (info->gesture_enabled == 1) {
			res = ERROR_OP_NOT_ALLOW;
			dev_err(info->dev, "%s: Feature not allowed when Gestures enabled! ERROR %08X\n",
				__func__, res);
			/* for example here can be placed a code for disabling
			  * the gesture mode when cover is activated
			  * (that means that cover mode has
			  * an higher priority on gesture mode) */
		}
		break;

	default:
		dev_info(info->dev, "%s: Feature Allowed!\n", __func__);
	}

	return res;
}

#ifdef USE_ONE_FILE_NODE
/**
  * File node to enable some feature
  * echo XX 00/01 > feature_enable		to enable/disable XX
  * (possible values @link feat_opt Feature Selection Options @endlink) feature
  * cat feature_enable		to show the result of enabling/disabling process
  * echo 01/00 > feature_enable; cat feature_enable		to perform
  * both actions stated before in just one call \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent an error code (00000000 =
  * no error) \n
  * } = end byte
  */
static ssize_t feature_enable_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	char *p = (char *)buf;
	unsigned int temp, temp2;
	int res = OK;

	if ((count - 2 + 1) / 3 != 1)
		dev_err(dev, "fts_feature_enable: Number of parameter wrong! %d > %d\n",
			(count - 2 + 1) / 3, 1);
	else {
		if (sscanf(p, "%02X %02X ", &temp, &temp2) == 2) {
			p += 3;
			res = check_feature_feasibility(info, temp);
			if (res >= OK) {
				switch (temp) {
		#ifdef GESTURE_MODE
				case FEAT_SEL_GESTURE:
					info->gesture_enabled = temp2;
					dev_info(dev, "fts_feature_enable: Gesture Enabled = %d\n",
						info->gesture_enabled);
					break;
		#endif

		#ifdef GLOVE_MODE
				case FEAT_SEL_GLOVE:
					info->glove_enabled = temp2;
					dev_info(dev, "fts_feature_enable: Glove Enabled = %d\n",
						info->glove_enabled);
					break;
		#endif

		#ifdef STYLUS_MODE
				case FEAT_SEL_STYLUS:
					info->stylus_enabled = temp2;
					dev_info(dev, "fts_feature_enable: Stylus Enabled = %d\n",
						info->stylus_enabled);
					break;
		#endif

		#ifdef COVER_MODE
				case FEAT_SEL_COVER:
					info->cover_enabled = temp2;
					dev_info(dev, "fts_feature_enable: Cover Enabled = %d\n",
						info->cover_enabled);
					break;
		#endif

		#ifdef CHARGER_MODE
				case FEAT_SEL_CHARGER:
					info->charger_enabled = temp2;
					dev_info(dev, "fts_feature_enable: Charger Enabled = %d\n",
						info->charger_enabled);
					break;
		#endif

		#ifdef GRIP_MODE
				case FEAT_SEL_GRIP:
					info->grip_enabled = temp2;
					dev_info(dev, "fts_feature_enable: Grip Enabled = %d\n",
						info->grip_enabled);
					break;
		#endif



				default:
					dev_err(dev, "fts_feature_enable: Feature %08X not valid! ERROR %08X\n",
						temp, ERROR_OP_NOT_ALLOW);
					res = ERROR_OP_NOT_ALLOW;
				}
				info->feature_feasibility = res;
			}
			if (info->feature_feasibility >= OK)
				info->feature_feasibility = fts_mode_handler(info, 1);
			else
				dev_err(dev, "%s: Call echo XX 00/01 > feature_enable with a correct feature value (XX)! ERROR %08X\n",
					__func__, res);
		} else
			dev_err(dev, "%s: Error when reading with sscanf!\n",
				__func__);
	}

	return count;
}



static ssize_t feature_enable_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	int count = 0;

	if (info->feature_feasibility < OK)
		dev_err(dev, "%s: Call before echo XX 00/01 > feature_enable with a correct feature value (XX)! ERROR %08X\n",
			__func__, info->feature_feasibility);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n",
			   info->feature_feasibility);

	info->feature_feasibility = ERROR_OP_NOT_ALLOW;
	return count;
}

#else


#ifdef GRIP_MODE
/**
  * File node to set the grip mode
  * echo 01/00 > grip_mode	to enable/disable glove mode \n
  * cat grip_mode		to show the status of the grip_enabled switch \n
  * echo 01/00 > grip_mode; cat grip_mode		to enable/disable grip
  *mode
  * and see the switch status in just one call \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent the value
  * info->grip_enabled (1 = enabled; 0= disabled) \n
  * } = end byte
  */
static ssize_t grip_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int count = 0;

	struct fts_ts_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s: grip_enabled = %d\n", __func__,
		 info->grip_enabled);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n",
			   info->grip_enabled);

	return count;
}


static ssize_t grip_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	char *p = (char *)buf;
	unsigned int temp;
	int res;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	/* in case of a different elaboration of the input, just modify
	  * this initial part of the code according to customer needs */
	if ((count + 1) / 3 != 1)
		dev_err(dev, "%s: Number of bytes of parameter wrong! %zu != 1 byte\n",
			__func__, (count + 1) / 3);
	else {
		if (sscanf(p, "%02X ", &temp) == 1) {
			p += 3;

/* standard code that should be always used when a feature is enabled! */
/* first step : check if the wanted feature can be enabled */
/* second step: call fts_mode_handler to actually enable it */
/* NOTE: Disabling a feature is always allowed by default */
			res = check_feature_feasibility(info, FEAT_SEL_GRIP);
			if (res >= OK || temp == FEAT_DISABLE) {
				info->grip_enabled = temp;
				res = fts_mode_handler(info, 1);
				if (res < OK)
					dev_err(dev, "%s: Error during fts_mode_handler! ERROR %08X\n",
						__func__, res);
			}
		} else
			dev_err(dev, "%s: Error when reading with sscanf!\n",
				__func__);
	}

	return count;
}
#endif

#ifdef CHARGER_MODE
/**
  * File node to set the glove mode
  * echo XX/00 > charger_mode		to value >0 to enable
  * (possible values: @link charger_opt Charger Options @endlink),
  * 00 to disable charger mode \n
  * cat charger_mode	to show the status of the charger_enabled switch \n
  * echo 01/00 > charger_mode; cat charger_mode		to enable/disable
  * charger mode and see the switch status in just one call \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent the value
  * info->charger_enabled (>0 = enabled; 0= disabled) \n
  * } = end byte
  */
static ssize_t charger_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int count = 0;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s: charger_enabled = %d\n", __func__,
		 info->charger_enabled);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n",
			   info->charger_enabled);
	return count;
}


static ssize_t charger_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	char *p = (char *)buf;
	unsigned int temp;
	int res;
	struct fts_ts_info *info = dev_get_drvdata(dev);

/* in case of a different elaboration of the input, just modify this
  * initial part of the code according to customer needs */
	if ((count + 1) / 3 != 1)
		dev_err(dev, "%s: Number of bytes of parameter wrong! %zu != 1 byte\n",
			__func__, (count + 1) / 3);
	else {
		if (sscanf(p, "%02X ", &temp) == 1) {
			p += 3;

/** standard code that should be always used when a feature is enabled!
  * first step : check if the wanted feature can be enabled
  * second step: call fts_mode_handler to actually enable it
  * NOTE: Disabling a feature is always allowed by default
  */
			res = check_feature_feasibility(info, FEAT_SEL_CHARGER);
			if (res >= OK || temp == FEAT_DISABLE) {
				info->charger_enabled = temp;
				res = fts_mode_handler(info, 1);
				if (res < OK)
					dev_err(dev, "%s: Error during fts_mode_handler! ERROR %08X\n",
						__func__, res);
			}
		} else
			dev_err(dev, "%s: Error when reading with sscanf!\n",
				__func__);

	}

	return count;
}
#endif

#ifdef GLOVE_MODE
/**
  * File node to set the glove mode
  * echo 01/00 > glove_mode	to enable/disable glove mode \n
  * cat glove_mode	to show the status of the glove_enabled switch \n
  * echo 01/00 > glove_mode; cat glove_mode	to enable/disable glove mode and
  *  see the switch status in just one call \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent the of value
  * info->glove_enabled (1 = enabled; 0= disabled) \n
  * } = end byte
  */
static ssize_t glove_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int count = 0;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s: glove_enabled = %d\n", __func__, info->glove_enabled);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n",
			   info->glove_enabled);

	return count;
}


static ssize_t glove_mode_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	char *p = (char *)buf;
	unsigned int temp;
	int res;
	struct fts_ts_info *info = dev_get_drvdata(dev);

/* in case of a different elaboration of the input, just modify this
  * initial part of the code according to customer needs */
	if ((count + 1) / 3 != 1)
		dev_err(dev, "%s: Number of bytes of parameter wrong! %zu != 1 byte\n",
			__func__, (count + 1) / 3);
	else {
		if (sscanf(p, "%02X ", &temp) == 1) {
			p += 3;

/* standard code that should be always used when a feature is enabled! */
/* first step : check if the wanted feature can be enabled */
/* second step: call fts_mode_handler to actually enable it */
/* NOTE: Disabling a feature is always allowed by default */
			res = check_feature_feasibility(info, FEAT_SEL_GLOVE);
			if (res >= OK || temp == FEAT_DISABLE) {
				info->glove_enabled = temp;
				res = fts_mode_handler(info, 1);
				if (res < OK)
					dev_err(dev, "%s: Error during fts_mode_handler! ERROR %08X\n",
						__func__, res);
			}
		} else
			dev_err(dev, "%s: Error when reading with sscanf!\n",
				__func__);
	}

	return count;
}
#endif


#ifdef COVER_MODE
/* echo 01/00 > cover_mode     to enable/disable cover mode */
/* cat cover_mode	to show the status of the cover_enabled switch
 * (example output in the terminal = "AA00000001BB" if the switch is enabled) */
/* echo 01/00 > cover_mode; cat cover_mode	to enable/disable cover mode and
  * see the switch status in just one call */
/* NOTE: the cover can be handled also using a notifier, in this case the body
  * of these functions should be copied in the notifier callback */
/**
  * File node to set the cover mode
  * echo 01/00 > cover_mode	to enable/disable cover mode \n
  * cat cover_mode	to show the status of the cover_enabled switch \n
  * echo 01/00 > cover_mode; cat cover_mode	to enable/disable cover mode
  * and see the switch status in just one call \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which is the value of info->cover_enabled
  * (1 = enabled; 0= disabled)\n
  * } = end byte \n
  * NOTE: \n
  * the cover can be handled also using a notifier, in this case the body of
  * these functions should be copied in the notifier callback
  */
static ssize_t cover_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int count = 0;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s: cover_enabled = %d\n", __func__, info->cover_enabled);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n",
			   info->cover_enabled);

	return count;
}


static ssize_t cover_mode_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	char *p = (char *)buf;
	unsigned int temp;
	int res;
	struct fts_ts_info *info = dev_get_drvdata(dev);

/* in case of a different elaboration of the input, just modify this
  * initial part of the code according to customer needs */
	if ((count + 1) / 3 != 1)
		dev_err(dev, "%s: Number of bytes of parameter wrong! %zu != 1 byte\n",
			__func__, (count + 1) / 3);
	else {
		if (sscanf(p, "%02X ", &temp) == 1) {
			p += 3;

/* standard code that should be always used when a feature is enabled! */
/* first step : check if the wanted feature can be enabled */
/* second step: call fts_mode_handler to actually enable it */
/* NOTE: Disabling a feature is always allowed by default */
			res = check_feature_feasibility(info, FEAT_SEL_COVER);
			if (res >= OK || temp == FEAT_DISABLE) {
				info->cover_enabled = temp;
				res = fts_mode_handler(info, 1);
				if (res < OK)
					dev_err(dev, "%s: Error during fts_mode_handler! ERROR %08X\n",
						__func__, res);
			}
		} else
			dev_err(dev, "%s: Error when reading with sscanf!\n",
				__func__);
	}

	return count;
}
#endif

#ifdef STYLUS_MODE
/**
  * File node to enable the stylus report
  * echo 01/00 > stylus_mode		to enable/disable stylus mode \n
  * cat stylus_mode	to show the status of the stylus_enabled switch \n
  * echo 01/00 > stylus_mode; cat stylus_mode	to enable/disable stylus mode
  * and see the switch status in just one call \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which is the value of info->stylus_enabled
  * (1 = enabled; 0= disabled)\n
  * } = end byte
  */
static ssize_t stylus_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int count = 0;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s: stylus_enabled = %d\n", __func__, info->stylus_enabled);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n",
			   info->stylus_enabled);

	return count;
}


static ssize_t stylus_mode_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	char *p = (char *)buf;
	unsigned int temp;
	struct fts_ts_info *info = dev_get_drvdata(dev);


/* in case of a different elaboration of the input, just modify this
  * initial part of the code according to customer needs */
	if ((count + 1) / 3 != 1)
		dev_err(dev, "%s: Number of bytes of parameter wrong! %zu != 1 byte\n",
			__func__, (count + 1) / 3);
	else {
		if (sscanf(p, "%02X ", &temp) == 1) {
			p += 3;
			info->stylus_enabled = temp;
		} else
			dev_err(dev, "%s: Error when reading with sscanf!\n",
				__func__);
	}

	return count;
}
#endif

#endif

/***************************************** GESTURES
  ***************************************************/
#ifdef GESTURE_MODE
#ifdef USE_GESTURE_MASK	/* if this define is used, a gesture bit mask
			  * is used as method to select the gestures to
			  * enable/disable */

/**
  * File node used by the host to set the gesture mask to enable or disable
  * echo EE X1 X2 ~~ > gesture_mask  set the gesture to disable/enable;
  * EE = 00(disable) or 01(enable) \n
  * X1 ~~  = gesture mask (example 06 00 ~~ 00 this gesture mask represents
  * the gestures with ID = 1 and 2) can be specified
  * from 1 to GESTURE_MASK_SIZE bytes, \n
  * if less than GESTURE_MASK_SIZE bytes are passed as arguments,
  * the omit bytes of the mask maintain the previous settings  \n
  * if one or more gestures is enabled the driver will automatically
  * enable the gesture mode, If all the gestures are disabled the driver
  * automatically will disable the gesture mode \n
  * cat gesture_mask   set inside the specified mask and return an error code
  * for the operation \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent an error code for enabling
  * the mask (00000000 = no error)\n
  * } = end byte \n\n
  * if USE_GESTURE_MASK is not define the usage of the function become: \n\n
  * echo EE X1 X2 ~~ > gesture_mask   set the gesture to disable/enable;
  * EE = 00(disable) or 01(enable) \n
  * X1 ~~ = gesture IDs (example 01 02 05 represent the gestures with ID = 1, 2
  * and 5)
  * there is no limit of the IDs passed as arguments, (@link gesture_opt Gesture
  * IDs @endlink) \n
  * if one or more gestures is enabled the driver will automatically enable
  * the gesture mode. If all the gestures are disabled the driver automatically
  * will disable the gesture mode. \n
  * cat gesture_mask     to show the status of the gesture enabled switch \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which is the value of info->gesture_enabled
  * (1 = enabled; 0= disabled)\n
  * } = end byte
  */
static ssize_t gesture_mask_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int count = 0, res, temp;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	if (mask[0] == 0) {
		res = ERROR_OP_NOT_ALLOW;
		dev_err(dev, "%s: Call before echo enable/disable xx xx .... > gesture_mask with a correct number of parameters! ERROR %08X\n",
			__func__, res);
	} else {
		if (mask[1] == FEAT_ENABLE || mask[1] == FEAT_DISABLE)
			res = updateGestureMask(&mask[2], mask[0], mask[1]);
		else
			res = ERROR_OP_NOT_ALLOW;

		if (res < OK)
			dev_err(dev, "%s: ERROR %08X\n", __func__, res);
	}
	res |= check_feature_feasibility(info, FEAT_SEL_GESTURE);
	temp = isAnyGestureActive();
	if (res >= OK || temp == FEAT_DISABLE)
		info->gesture_enabled = temp;

	dev_info(dev, "%s: Gesture Enabled = %d\n", __func__,
		 info->gesture_enabled);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n", res);
	mask[0] = 0;

	return count;
}


static ssize_t gesture_mask_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	char *p = (char *)buf;
	int n;
	unsigned int temp;

	if ((count + 1) / 3 > GESTURE_MASK_SIZE + 1) {
		dev_err(dev, "%s: Number of bytes of parameter wrong! %zu > (enable/disable + %d )\n",
			__func__, (count + 1) / 3, GESTURE_MASK_SIZE);
		mask[0] = 0;
	} else {
		mask[0] = ((count + 1) / 3) - 1;
		for (n = 1; n <= (count + 1) / 3; n++) {
			if (sscanf(p, "%02X ", &temp) == 1) {
				p += 3;
				mask[n] = (u8)temp;
				dev_info(dev, "mask[%d] = %02X\n", n, mask[n]);
			} else
				dev_err(dev, "%s: Error when reading with sscanf!\n",
					__func__);
		}
	}

	return count;
}

#else	/* if this define is not used, to select the gestures to enable/disable
	  * are used the IDs of the gestures */
/* echo EE X1 X2 ... > gesture_mask     set the gesture to disable/enable;
  * EE = 00(disable) or 01(enable); X1 ... = gesture IDs
  * (example 01 02 05... represent the gestures with ID = 1, 2 and 5)
  * there is no limit of the parameters that can be passed,
  * of course the gesture IDs should be valid (all the valid IDs are listed in
  * ftsGesture.h) */
/* cat gesture_mask	enable/disable the given gestures, if one or more
  * gestures is enabled the driver will automatically enable the gesture mode.
  * If all the gestures are disabled the driver automatically will disable the
  * gesture mode.
  * At the end an error code will be printed
  *  (example output in the terminal = "AA00000000BB" if there are no errors) */
/* echo EE X1 X2 ... > gesture_mask; cat gesture_mask	perform in one command
  * both actions stated before */
/**
  * File node used by the host to set the gesture mask to enable or disable
  * echo EE X1 X2 ~~ > gesture_mask	set the gesture to disable/enable;
  * EE = 00(disable) or 01(enable) \n
  * X1 ~ = gesture IDs (example 01 02 05 represent the gestures with ID = 1, 2
  * and 5)
  * there is no limit of the IDs passed as arguments, (@link gesture_opt Gesture
  * IDs @endlink) \n
  * if one or more gestures is enabled the driver will automatically enable
  * the gesture mode, If all the gestures are disabled the driver automatically
  * will disable the gesture mode \n
  * cat gesture_mask	 to show the status of the gesture enabled switch \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which is the value of info->gesture_enabled
  * (1 = enabled; 0= disabled)\n
  * } = end byte
  */
static ssize_t gesture_mask_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int count = 0;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s: gesture_enabled = %d\n", __func__,
		info->gesture_enabled);

	count += scnprintf(buf + count,
			   PAGE_SIZE - count, "{ %08X }\n",
			   info->gesture_enabled);


	return count;
}


static ssize_t gesture_mask_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	char *p = (char *)buf;
	int n;
	unsigned int temp;
	int res;
	struct fts_ts_info *info = dev_get_drvdata(dev);

	if ((count + 1) / 3 < 2 || (count + 1) / 3 > GESTURE_MASK_SIZE + 1) {
		dev_err(dev, "%s: Number of bytes of parameter wrong! %d < or > (enable/disable + at least one gestureID or max %d bytes)\n",
			__func__, (count + 1) / 3, GESTURE_MASK_SIZE);
		mask[0] = 0;
	} else {
		memset(mask, 0, GESTURE_MASK_SIZE + 2);
		mask[0] = ((count + 1) / 3) - 1;
		if (sscanf(p, "%02X ", &temp) == 1) {
			p += 3;
			mask[1] = (u8)temp;
			for (n = 1; n < (count + 1) / 3; n++) {
				if (sscanf(p, "%02X ", &temp) == 1) {
					p += 3;
					fromIDtoMask((u8)temp, &mask[2],
						GESTURE_MASK_SIZE);
				} else {
					dev_err(dev, "%s: Error when reading with sscanf!\n",
						__func__);
					mask[0] = 0;
					goto END;
				}
			}

			for (n = 0; n < GESTURE_MASK_SIZE + 2; n++)
				dev_info(dev, "mask[%d] = %02X\n", n, mask[n]);
		} else {
			dev_err(dev, "%s: Error when reading with sscanf!\n",
				__func__);
			mask[0] = 0;
		}
	}

END:
	if (mask[0] == 0) {
		res = ERROR_OP_NOT_ALLOW;
		dev_err(dev, "%s: Call before echo enable/disable xx xx .... > gesture_mask with a correct number of parameters! ERROR %08X\n",
			__func__, res);
	} else {
		if (mask[1] == FEAT_ENABLE || mask[1] == FEAT_DISABLE)
			res = updateGestureMask(&mask[2], mask[0], mask[1]);
		else
			res = ERROR_OP_NOT_ALLOW;

		if (res < OK)
			dev_err(dev, "%s: ERROR %08X\n", __func__, res);
	}

	res = check_feature_feasibility(info, FEAT_SEL_GESTURE);
	temp = isAnyGestureActive();
	if (res >= OK || temp == FEAT_DISABLE)
		info->gesture_enabled = temp;
	res = fts_mode_handler(info, 0);

	return count;
}


#endif


/**
  * File node to read the coordinates of the last gesture drawn by the user \n
  * cat gesture_coordinates	to obtain the gesture coordinates \n
  * the string returned in the shell follow this up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent an error code (00000000 =
  *OK) \n
  * \n if error code = 00000000 \n
  * CC = 1 byte in HEX format number of coords (pair of x,y) returned \n
  * XXiYYi ... = XXi 2 bytes in HEX format for x[i] and
  * YYi 2 bytes in HEX format for y[i] (big endian) \n
  * \n
  * } = end byte
  */
static ssize_t gesture_coordinates_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int size = PAGE_SIZE;
	int count = 0, res, i = 0;

	dev_info(dev, "%s: Getting gestures coordinates...\n", __func__);

	if (gesture_coords_reported < OK) {
		dev_err(dev, "%s: invalid coordinates! ERROR %08X\n",
			 __func__, gesture_coords_reported);
		res = gesture_coords_reported;
	} else {
		size += gesture_coords_reported * 2 * 4 + 2;
		/* coords are pairs of x,y (*2) where each coord is
		  * short(2bytes=4char)(*4) + 1 byte(2char) num of coords (+2)
		  **/
		res = OK;	/* set error code to OK */
	}


	count += scnprintf(buf + count,
			   size - count, "{ %08X", res);

	if (res >= OK) {
		count += scnprintf(buf + count,
				   size - count, "%02X",
				   gesture_coords_reported);

		for (i = 0; i < gesture_coords_reported; i++) {
			count += scnprintf(buf + count,
					   size - count,
					   "%04X",
					   gesture_coordinates_x[i]);
			count += scnprintf(buf + count,
					   size - count,
					   "%04X",
					   gesture_coordinates_y[i]);
		}
	}

	count += scnprintf(buf + count, size - count, " }\n");
	dev_info(dev, "%s: Getting gestures coordinates FINISHED!\n", __func__);

	return count;
}
#endif


/***************************************** PRODUCTION TEST
  ***************************************************/

/**
  * File node to execute the Mass Production Test or to get data from the IC
  * (raw or ms/ss init data)
  * echo cmd > stm_fts_cmd	to execute a command \n
  * cat stm_fts_cmd	to show the result of the command \n
  * echo cmd > stm_fts_cmd; cat stm_fts_cmd	to execute and show the result
  * in just one call \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * X1X2X3X4 = 4 bytes in HEX format which represent an error_code (00000000 =
  * OK)\n
  * (optional) data = data coming from the command executed represented as HEX
  * string \n
  *                   Not all the command return additional data \n
  * } = end byte \n
  * \n
  * Possible commands (cmd): \n
  * - 00 = MP Test -> return error_code \n
  * - 01 = ITO Test -> return error_code \n
  * - 03 = MS Raw Test -> return error_code \n
  * - 04 = MS Init Data Test -> return error_code \n
  * - 05 = SS Raw Test -> return error_code \n
  * - 06 = SS Init Data Test -> return error_code \n
  * - 13 = Read 1 MS Raw Frame -> return additional data: MS frame row after row
  * \n
  * - 14 = Read MS Init Data -> return additional data: MS init data row after
  * row \n
  * - 15 = Read 1 SS Raw Frame -> return additional data: SS frame,
  * force channels followed by sense channels \n
  * - 16 = Read SS Init Data -> return additional data: SS Init data,
  * first IX for force and sense channels and then CX for force and sense
  * channels \n
  * - F0 = Perform a system reset -> return error_code \n
  * - F1 = Perform a system reset and reenable the sensing and the interrupt
  */
static ssize_t stm_fts_cmd_write(struct file *fp, struct kobject *kobj,
				 struct bin_attribute *battr, char *buf,
				 loff_t offset, size_t count)
{
	u8 result, n = 0;
	struct device *dev = kobj_to_dev(kobj);
	struct fts_ts_info *info = dev_get_drvdata(dev);
	char *p, *temp_buf, *token;
	size_t token_len = 0;
	ssize_t retval = count;

	if (offset != 0)
		return count;

	if (!count) {
		dev_err(dev, "%s: Invalid input buffer length!\n", __func__);
		retval = -EINVAL;
		goto out;
	}

	if (!info) {
		dev_err(dev, "%s: Unable to access driver data\n", __func__);
		retval = -EINVAL;
		goto out;
	}

	if (!mutex_trylock(&info->diag_cmd_lock)) {
		dev_err(dev, "%s: Blocking concurrent access\n", __func__);
		retval = -EBUSY;
		goto out;
	}

	memset(info->typeOfCommand, 0, sizeof(info->typeOfCommand));

	temp_buf = kstrdup(buf, GFP_KERNEL);
	if (!temp_buf) {
		dev_err(dev, "%s: memory allocation failed!",
			__func__);
		retval = -ENOMEM;
		goto unlock;
	}

	p = temp_buf;

	/* Parse the input string to retrieve 2 hex-digit width cmds/args
	 * separated by one or more spaces.
	 * Any input not equal to 2 hex-digit width are ignored.
	 * A single 2 hex-digit width  command w/ or w/o space is allowed.
	 * Inputs not in the valid hex range are also ignored.
	 * In case of encountering any of the above failure, the entire input
	 * buffer is discarded.
	 */
	while (p && (n < CMD_STR_LEN)) {

		while (isspace(*p)) {
			p++;
		}

		token = strsep(&p, " ");

		if (!token || *token == '\0') {
			break;
		}

		token_len = strlen(token);

		/* handle last token case */
		if (token_len == 3 && token[2] == '\n')
			token[2] = '\0';
		else if (token_len != 2) {
			dev_err(dev, "%s: bad len. len=%zu\n",
				 __func__, token_len);
			n = 0;
			break;
		}

		if (kstrtou8(token, 16, &result)) {
			/* Conversion failed due to bad input.
			* Discard the entire buffer.
			*/
			dev_err(dev, "%s: bad input\n", __func__);
			n = 0;
			break;
		}

		/* found a valid cmd/args */
		info->typeOfCommand[n] = result;
		dev_info(dev, "%s: typeOfCommand[%d]=%02X\n",
			__func__, n, info->typeOfCommand[n]);

		n++;
	}

	if (n == 0) {
		dev_err(dev, "%s: Found invalid cmd/arg\n", __func__);
		retval = -EINVAL;
	}

	info->numberParameters = n;
	dev_info(dev, "%s: Number of Parameters = %d\n", __func__, info->numberParameters);

	kfree(temp_buf);

unlock:
	mutex_unlock(&info->diag_cmd_lock);
out:
	return retval;
}

static ssize_t stm_fts_cmd_read(struct file *fp, struct kobject *kobj,
				struct bin_attribute *battr, char *buf,
				loff_t offset, size_t count)
{
	int res, j, doClean = 0, index = 0;
	int size = (6 * 2) + 1;
	int nodes = 0;
	int init_type = SPECIAL_PANEL_INIT;
	u8 *all_strbuff;
	struct device *dev = kobj_to_dev(kobj);
	struct fts_ts_info *info = dev_get_drvdata(dev);
	const char *limits_file = info->board->limits_name;
	char *label[2];

	MutualSenseData compData;
	SelfSenseData comData;
	MutualSenseFrame frameMS;
	SelfSenseFrame frameSS;
	u16 ito_max_val[2] = {0x00};

	u8 report = 0;

	if (offset > 0)
		goto offset_reading;

	if (info->stm_fts_cmd_buff) {
		memset(info->stm_fts_cmd_buff, 0, MAX_RAWDATA_STR_SIZE);
		dev_warn(dev, "info->stm_fts_cmd_buff existed.\n");
	} else {
		info->stm_fts_cmd_buff = (u8 *)kmalloc(MAX_RAWDATA_STR_SIZE,
						       GFP_KERNEL);
	}
	all_strbuff = info->stm_fts_cmd_buff;

	if (!info) {
		dev_err(dev, "%s: Unable to access driver data\n", __func__);
		return  -EINVAL;
	}

	if (!mutex_trylock(&info->diag_cmd_lock)) {
		dev_err(dev, "%s: Blocking concurrent access\n", __func__);
		return -EBUSY;
	}

	if (info->numberParameters >= 1) {
		res = fts_enableInterrupt(info, false);
		if (res < 0) {
			dev_err(dev, "fts_enableInterrupt: ERROR %08X\n", res);
			res = (res | ERROR_DISABLE_INTER);
			goto END;
		}

		switch (info->typeOfCommand[0]) {
		/*ITO TEST*/
		case 0x01:
			frameMS.node_data = NULL;
			res = production_test_ito(info, limits_file,
						  &frameMS, ito_max_val);
			/* report MS raw frame only if was successfully
			 * acquired */
			if (frameMS.node_data != NULL) {
				size += (frameMS.node_data_size *
						sizeof(short) + 2) * 2;
				report = 1;
			}
			break;

		/*PRODUCTION TEST*/
		case 0x02:
			if (info->systemInfo.u8_cfgAfeVer !=
				info->systemInfo.u8_cxAfeVer) {
				res = ERROR_OP_NOT_ALLOW;
				dev_err(dev, "Miss match in CX version! MP test not allowed with wrong CX memory! ERROR %08X\n",
					res);
				break;
			}
			res = production_test_initialization(info, init_type);
			break;

		case 0x00:
#ifndef COMPUTE_INIT_METHOD
			if (info->systemInfo.u8_cfgAfeVer !=
				info->systemInfo.u8_cxAfeVer) {
				res = ERROR_OP_NOT_ALLOW;
				dev_err(dev, "Miss match in CX version! MP test not allowed with wrong CX memory! ERROR %08X\n",
					res);
				break;
			}
#else
			if (info->systemInfo.u8_mpFlag != MP_FLAG_FACTORY) {
				init_type = SPECIAL_FULL_PANEL_INIT;
				dev_info(dev, "Select Full Panel Init!\n");
			} else {
				init_type = NO_INIT;
				dev_info(dev, "Skip Full Panel Init!\n");
			}
#endif
			res = production_test_main(info, limits_file, 1,
						   init_type, MP_FLAG_FACTORY);
			break;

		/*read mutual raw*/
		case 0x13:
			dev_info(dev, "Get 1 MS Frame\n");
			if (info->numberParameters >= 2 &&
				info->typeOfCommand[1] == LOCKED_LP_ACTIVE)
				setScanMode(info, SCAN_MODE_LOCKED, LOCKED_LP_ACTIVE);
			else
				setScanMode(info, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
			msleep(WAIT_FOR_FRESH_FRAMES);
			/* Skip sensing off when typeOfCommand[2]=0x01
			 * to avoid sense on force cal after reading raw data
			 */
			if (!(info->numberParameters >= 3 &&
				info->typeOfCommand[2] == 0x01)) {
				setScanMode(info, SCAN_MODE_ACTIVE, 0x00);
				msleep(WAIT_AFTER_SENSEOFF);
				/* Delete the events related to some touch
				 * (allow to call this function while touching
				 * the screen without having a flooding of the
				 * FIFO)
				 */
				flushFIFO(info);
			}
#ifdef READ_FILTERED_RAW
			res = getMSFrame3(info, MS_FILTER, &frameMS);
#else
			res = getMSFrame3(info, MS_RAW, &frameMS);
#endif
			if (res < 0) {
				dev_err(dev, "Error while taking the MS frame... ERROR %08X\n",
					res);
			} else {
				dev_info(dev, "The frame size is %d words\n",
					res);
#ifdef RAW_DATA_FORMAT_DEC
				size += 3 * 2 +
				    (7 * frameMS.header.sense_node + 1)
				    * frameMS.header.force_node;
#else
				size += (res * sizeof(short) + 2) * 2;
#endif
				/* set res to OK because if getMSFrame is
				 * successful res = number of words read
				 */
				res = OK;
				if (info->typeOfCommand[1] == LOCKED_ACTIVE)
					label[0] = "CmRaw =";
				else if (info->typeOfCommand[1] ==
					 LOCKED_LP_ACTIVE)
					label[0] = "CmRaw_LP =";
				else
					label[0] = "MS Frame =";
				print_frame_short(info,
					label[0],
					array1dTo2d_short(
					frameMS.node_data,
					frameMS.node_data_size,
					frameMS.header.sense_node),
					frameMS.header.force_node,
					frameMS.header.sense_node);
			}
			break;
		/*read self raw*/
		case 0x15:
			dev_info(dev, "Get 1 SS Frame\n");
			if (info->numberParameters >= 2 &&
				info->typeOfCommand[1] == LOCKED_LP_DETECT)
				setScanMode(info, SCAN_MODE_LOCKED, LOCKED_LP_DETECT);
			else
				setScanMode(info, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
			msleep(WAIT_FOR_FRESH_FRAMES);
			/* Skip sensing off when typeOfCommand[2]=0x01
			 * to avoid sense on force cal after reading raw data
			 */
			if (!(info->numberParameters >= 3 &&
				info->typeOfCommand[2] == 0x01)) {
				setScanMode(info, SCAN_MODE_ACTIVE, 0x00);
				msleep(WAIT_AFTER_SENSEOFF);
				flushFIFO(info);
				/* delete the events related to some touch
				 * (allow to call this function while touching
				 * the screen without having a flooding of the
				 * FIFO)
				 */
			}
			if (info->numberParameters >= 2 &&
				info->typeOfCommand[1] == LOCKED_LP_DETECT)
#ifdef READ_FILTERED_RAW
				res = getSSFrame3(info, SS_DETECT_FILTER,
						  &frameSS);
#else
				res = getSSFrame3(info, SS_DETECT_RAW,
						  &frameSS);
#endif
			else
#ifdef READ_FILTERED_RAW
				res = getSSFrame3(info, SS_FILTER, &frameSS);
#else
				res = getSSFrame3(info, SS_RAW, &frameSS);
#endif
			if (res < OK) {
				dev_err(dev, "Error while taking the SS frame... ERROR %08X\n",
					res);
			} else {
				dev_info(dev, "The frame size is %d words\n", res);
#ifdef RAW_DATA_FORMAT_DEC
				size += 3 * 2 + 5 +
					(frameSS.header.sense_node +
					 frameSS.header.force_node) * 7;
#else
				size += (res * sizeof(short) + 2) * 2;
#endif
				/* set res to OK because if getMSFrame is
				 * successful res = number of words read
				 */
				res = OK;
				if (info->typeOfCommand[1] == LOCKED_ACTIVE) {
					label[0] = "CsRaw_Tx =";
					label[1] = "CsRaw_Rx =";
				} else if (info->typeOfCommand[1] ==
					   LOCKED_LP_DETECT) {
					label[0] = "CsRaw_Tx_LP =";
					label[1] = "CsRaw_Rx_LP =";
				} else {
					label[0] = "SS force frame =";
					label[1] = "SS sense frame =";
				}
				print_frame_short(info,
					label[0],
					array1dTo2d_short(
					frameSS.force_data,
					frameSS.header.force_node,
					frameSS.header.force_node),
					1, frameSS.header.force_node);
				print_frame_short(info,
					label[1],
					array1dTo2d_short(
					frameSS.sense_data,
					frameSS.header.sense_node,
					frameSS.header.sense_node),
					1, frameSS.header.sense_node);
			}
			break;

		case 0x14:	/* read mutual comp data */
			dev_info(dev, "Get MS Compensation Data\n");
			res = readMutualSenseCompensationData(info,
							      LOAD_CX_MS_TOUCH,
							      &compData);

			if (res < 0)
				dev_err(dev, "Error reading MS compensation data ERROR %08X\n",
					res);
			else {
				dev_info(dev, "MS Compensation Data Reading Finished!\n");
				size += ((compData.node_data_size + 3) *
					 sizeof(u8)) * 2;
				print_frame_i8(info, "MS Data (Cx2) =",
					       array1dTo2d_i8(
						       compData.node_data,
						       compData.
						       node_data_size,
						       compData.header.
						       sense_node),
					       compData.header.force_node,
					       compData.header.sense_node);
			}
			break;

		case 0x16:	/* read self comp data */
			dev_info(dev, "Get SS Compensation Data...\n");
			res = readSelfSenseCompensationData(info,
							    LOAD_CX_SS_TOUCH,
							    &comData);
			if (res < 0)
				dev_err(dev, "Error reading SS compensation data ERROR %08X\n",
					res);
			else {
				dev_info(dev, "SS Compensation Data Reading Finished!\n");
				size += ((comData.header.force_node +
					  comData.header.sense_node) * 2 + 8) *
					sizeof(u8) * 2;
				print_frame_u8(info, "SS Data Ix2_fm = ",
					       array1dTo2d_u8(comData.ix2_fm,
							      comData.header.
							      force_node, 1),
					       comData.header.force_node, 1);
				print_frame_i8(info, "SS Data Cx2_fm = ",
					       array1dTo2d_i8(comData.cx2_fm,
							      comData.header.
							      force_node, 1),
					       comData.header.force_node, 1);
				print_frame_u8(info, "SS Data Ix2_sn = ",
					       array1dTo2d_u8(comData.ix2_sn,
							      comData.header.
							      sense_node,
							      comData.header.
							      sense_node), 1,
					       comData.header.sense_node);
				print_frame_i8(info, "SS Data Cx2_sn = ",
					       array1dTo2d_i8(comData.cx2_sn,
							      comData.header.
							      sense_node,
							      comData.header.
							      sense_node), 1,
					       comData.header.sense_node);
			}
			break;
		case 0x17:	/* Read mutual strength */
			dev_info(dev, "Get 1 MS Strength\n");
			/* Skip sensing off when typeOfCommand[1]=0x01
			 * to avoid sense on force cal after reading raw data
			 */
			if (!(info->numberParameters >= 2 &&
				info->typeOfCommand[1] == 0x01)) {
				setScanMode(info, SCAN_MODE_ACTIVE, 0xFF);
				msleep(WAIT_FOR_FRESH_FRAMES);
				setScanMode(info, SCAN_MODE_ACTIVE, 0x00);
				msleep(WAIT_AFTER_SENSEOFF);
				/* Flush outstanding touch events */
				flushFIFO(info);
			}
			nodes = getMSFrame3(info, MS_STRENGTH, &frameMS);
			if (nodes < 0) {
				res = nodes;
				dev_err(dev, "Error while taking the MS strength... ERROR %08X\n",
					res);
			} else {
				dev_info(dev, "The frame size is %d words\n", nodes);
#ifdef RAW_DATA_FORMAT_DEC
				size += 3 * 2 +
				    (7 * frameMS.header.sense_node + 1)
				    * frameMS.header.force_node;
#else
				size += (nodes * sizeof(short) + 2) * 2;
#endif
				print_frame_short(info, "MS strength =",
				    array1dTo2d_short(frameMS.node_data,
						frameMS.node_data_size,
						frameMS.header.sense_node),
				    frameMS.header.force_node,
				    frameMS.header.sense_node);
				res = OK;
			}
			break;
		case 0x03:	/* MS Raw DATA TEST */
			res = fts_system_reset(info);
			if (res >= OK)
				res = production_test_ms_raw(info,
							     limits_file, 1);
			break;

		case 0x04:	/* MS CX DATA TEST */
			res = fts_system_reset(info);
			if (res >= OK)
				res = production_test_ms_cx(info,
							    limits_file, 1);
			break;

		case 0x05:	/* SS RAW DATA TEST */
			res = fts_system_reset(info);
			if (res >= OK)
				res = production_test_ss_raw(info,
							     limits_file, 1);
			break;

		case 0x06:	/* SS IX CX DATA TEST */
			res = fts_system_reset(info);
			if (res >= OK)
				res = production_test_ss_ix_cx(info,
							       limits_file, 1);
			break;


		case 0xF0:
		case 0xF1:	/* TOUCH ENABLE/DISABLE */
			doClean = (int)(info->typeOfCommand[0] & 0x01);
			res = cleanUp(info, doClean);
			break;

		default:
			dev_err(dev, "COMMAND NOT VALID!! Insert a proper value ...\n");
			res = ERROR_OP_NOT_ALLOW;
			break;
		}

		doClean = fts_mode_handler(info, 1);
		if (info->typeOfCommand[0] != 0xF0)
			doClean |= fts_enableInterrupt(info, true);
		if (doClean < 0)
			dev_err(dev, "%s: ERROR %08X\n", __func__,
				 (doClean | ERROR_ENABLE_INTER));
	} else {
		dev_err(dev, "NO COMMAND SPECIFIED!!! do: 'echo [cmd_code] [args] > stm_fts_cmd' before looking for result!\n");
		res = ERROR_OP_NOT_ALLOW;
	}

END:
	/* here start the reporting phase, assembling the data
	  * to send in the file node */
	size = MAX_RAWDATA_STR_SIZE;
	index = 0;
	index += scnprintf(all_strbuff + index, size - index, "{ %08X", res);

	if (res >= OK || report) {
		/*all the other cases are already fine printing only the res.*/
		switch (info->typeOfCommand[0]) {
		case 0x01:
		case 0x13:
		case 0x17:

			if (frameMS.node_data == NULL)
				break;

#ifdef RAW_DATA_FORMAT_DEC
			index += scnprintf(all_strbuff + index, size - index,
					   "%3d",
					   (u8)frameMS.header.force_node);
			index += scnprintf(all_strbuff + index, size - index,
					   "%3d",
					   (u8)frameMS.header.sense_node);
			if (info->typeOfCommand[0] == 0x01) {
				index += scnprintf(all_strbuff + index,
						size - index, " %d ",
						ito_max_val[0]);
				index += scnprintf(all_strbuff + index,
						size - index, "%d ",
						ito_max_val[1]);
			}
#else
			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (u8)frameMS.header.force_node);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (u8)frameMS.header.sense_node);
			if (info->typeOfCommand[0] == 0x01) {
				index += scnprintf(all_strbuff + index,
						size - index,
						"%02X%02X",
						(ito_max_val[0] & 0xFF00) >> 8,
						ito_max_val[0] & 0xFF);

				index += scnprintf(all_strbuff + index,
						size - index,
						"%02X%02X",
						(ito_max_val[1] & 0xFF00) >> 8,
						ito_max_val[1] & 0xFF);
			}
#endif

			for (j = 0; j < frameMS.node_data_size; j++) {
#ifdef RAW_DATA_FORMAT_DEC
				if (j % frameMS.header.sense_node == 0)
					index += scnprintf(all_strbuff + index,
							   size - index, "\n");
				index += scnprintf(all_strbuff + index,
						   size - index, "%d ",
						   frameMS.node_data[j]);
#else
				index += scnprintf(all_strbuff + index,
					   size - index,
					   "%02X%02X",
					   (frameMS.node_data[j] & 0xFF00) >> 8,
					   frameMS.node_data[j] & 0xFF);
#endif
			}

			kfree(frameMS.node_data);
			break;

		case 0x15:
#ifdef RAW_DATA_FORMAT_DEC
			index += scnprintf(all_strbuff + index, size - index,
					   "%3d",
					   (u8)frameSS.header.force_node);
			index += scnprintf(all_strbuff + index, size - index,
					   "%3d",
					   (u8)frameSS.header.sense_node);
			index += scnprintf(all_strbuff + index, size - index,
					   "\n");
#else
			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (u8)frameSS.header.force_node);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (u8)frameSS.header.sense_node);
#endif

			/* Copying self raw data Force */
			for (j = 0; j < frameSS.header.force_node; j++) {
#ifdef RAW_DATA_FORMAT_DEC
				index += scnprintf(all_strbuff + index,
						   size - index,
						   "%d ",
						   frameSS.force_data[j]);
#else
				index += scnprintf(all_strbuff + index,
					  size - index,
					  "%02X%02X",
					  (frameSS.force_data[j] & 0xFF00) >> 8,
					  frameSS.force_data[j] & 0xFF);
#endif
			}



#ifdef RAW_DATA_FORMAT_DEC
			index += scnprintf(all_strbuff + index, size - index,
					   "\n");
#endif

			/* Copying self raw data Sense */
			for (j = 0; j < frameSS.header.sense_node; j++) {
#ifdef RAW_DATA_FORMAT_DEC
				index += scnprintf(all_strbuff + index,
						   size - index, "%d ",
						   frameSS.sense_data[j]);
#else
				index += scnprintf(all_strbuff + index,
					  size - index,
					  "%02X%02X",
					  (frameSS.sense_data[j] & 0xFF00) >> 8,
					  frameSS.sense_data[j] & 0xFF);
#endif
			}

			kfree(frameSS.force_data);
			kfree(frameSS.sense_data);
			break;

		case 0x14:
			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (u8)compData.header.force_node);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (u8)compData.header.sense_node);

			/* Cpying CX1 value */
			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (compData.cx1) & 0xFF);

			/* Copying CX2 values */
			for (j = 0; j < compData.node_data_size; j++) {
				index += scnprintf(all_strbuff + index,
						size - index,
						"%02X",
						(compData.node_data[j]) & 0xFF);
			}

			kfree(compData.node_data);
			break;

		case 0x16:
			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   comData.header.force_node);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   comData.header.sense_node);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (comData.f_ix1) & 0xFF);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (comData.s_ix1) & 0xFF);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (comData.f_cx1) & 0xFF);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (comData.s_cx1) & 0xFF);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (comData.f_ix0) & 0xFF);

			index += scnprintf(all_strbuff + index,
					   size - index, "%02X",
					   (comData.s_ix0) & 0xFF);

			/* Copying IX2 Force */
			for (j = 0; j < comData.header.force_node; j++) {
				index += scnprintf(all_strbuff + index,
						   size - index,
						   "%02X",
						   comData.ix2_fm[j] & 0xFF);
			}

			/* Copying IX2 Sense */
			for (j = 0; j < comData.header.sense_node; j++) {
				index += scnprintf(all_strbuff + index,
						   size - index,
						   "%02X",
						   comData.ix2_sn[j] & 0xFF);
			}

			/* Copying CX2 Force */
			for (j = 0; j < comData.header.force_node; j++) {
				index += scnprintf(all_strbuff + index,
						   size - index,
						   "%02X",
						   comData.cx2_fm[j] & 0xFF);
			}

			/* Copying CX2 Sense */
			for (j = 0; j < comData.header.sense_node; j++) {
				index += scnprintf(all_strbuff + index,
						   size - index,
						   "%02X",
						   comData.cx2_sn[j] & 0xFF);
			}

			kfree(comData.ix2_fm);
			kfree(comData.ix2_sn);
			kfree(comData.cx2_fm);
			kfree(comData.cx2_sn);
			break;

		default:
			break;
		}
	}

	index += scnprintf(all_strbuff + index, size - index, " }\n");
	info->numberParameters = 0;
	/* need to reset the number of parameters in order to wait the
	 * next command, comment if you want to repeat the last command sent
	 * just doing a cat */
	/* dev_err(dev, "numberParameters = %d\n", info->numberParameters); */

	mutex_unlock(&info->diag_cmd_lock);

	info->stm_fts_cmd_buff_len = index;

offset_reading:
	if (info->stm_fts_cmd_buff_len == 0) {
		kfree(info->stm_fts_cmd_buff);
		info->stm_fts_cmd_buff = NULL;
		return 0;
	} else if (info->stm_fts_cmd_buff_len > PAGE_SIZE) {
		index = PAGE_SIZE;
	} else {
		index = info->stm_fts_cmd_buff_len;
	}
	dev_info(dev, "%s: remaining length: %lld, offset: %lld.\n", __func__,
		info->stm_fts_cmd_buff_len, offset);

	memcpy(buf, info->stm_fts_cmd_buff + offset, index);
	info->stm_fts_cmd_buff_len = info->stm_fts_cmd_buff_len - index;

	return index;
}

static ssize_t autotune_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	int ret = 0;
	bool val = false;

	if ((kstrtobool(buf, &val) < 0) || !val) {
		ret = -EINVAL;
		goto err_args;
	}

	ret = production_test_main(info, info->board->limits_name, 1,
				   SPECIAL_FULL_PANEL_INIT, MP_FLAG_BOOT);

	cleanUp(info, true);

	info->autotune_stat = ret;

err_args:

	return ret < 0 ? ret : count;
}

static ssize_t autotune_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "{ %08X }\n", info->autotune_stat);
}

static ssize_t infoblock_getdata_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	int count = 0;
	int res = 0;
	u8 control_reg = 0;
	u8 flash_status = 0;
	u8 *data = NULL;
	int addr = 0;
	int i = 0;

	res = fts_writeReadU8UX(info, FTS_CMD_HW_REG_R, ADDR_SIZE_HW_REG,
				ADDR_FLASH_STATUS, &control_reg,
				1, DUMMY_HW_REG);

	if (res < OK) {
		count += scnprintf(&buf[count], PAGE_SIZE - count,
				   "ADDR_FLASH_STATUS read failed\n");
		goto END;
	}
	flash_status = (control_reg & 0xFC);

	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "The value:0x%X 0x%X\n", control_reg, flash_status);

	res = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
			    ADDR_FLASH_STATUS, &flash_status, 1);
	if (res < OK) {
		count += scnprintf(&buf[count], PAGE_SIZE - count,
				   "ADDR_FLASH_STATUS write failed\n");
		goto END;
	}
	data = kmalloc(INFO_BLOCK_SIZE * sizeof(u8), GFP_KERNEL);
	if (data == NULL) {
		count += scnprintf(&buf[count], PAGE_SIZE - count,
				   "kmalloc failed\n");
		goto END;
	}
	res = fts_writeReadU8UX(info, FTS_CMD_HW_REG_R, ADDR_SIZE_HW_REG,
				ADDR_INFOBLOCK, data, INFO_BLOCK_SIZE,
				DUMMY_HW_REG);
	if (res < OK) {
		count += scnprintf(&buf[count], PAGE_SIZE - count,
				   "ADDR_INFOBLOCK read failed\n");
		goto END;
	}
	addr = INFO_BLOCK_LOCKDOWN;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Lock down info the first 4bytes:0X%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Lock down info the second 4bytes:0X%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	count += scnprintf(&buf[count], PAGE_SIZE - count, "0x%04X\n", addr);
	addr = INFO_BLOCK_AOFFSET;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Aoffset magic number:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Aoffset crc:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Aoffset ~crcr:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Aoffset len:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Aoffset ~len:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "Aoffset ver:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count, "0x%04X\n", addr);
	for (i = 0; i < 38; i++) {
		count += scnprintf(&buf[count], PAGE_SIZE - count,
				   "Aoffset CH[%d] Quar:0X%02X,Half:0X%02X,Full:0X%02X%02X\n",
				   i, data[addr+3], data[addr+2], data[addr+1],
				   data[addr]);
		addr += 4;
	}
	count += scnprintf(&buf[count], PAGE_SIZE - count, "0x%04X\n", addr);
	for (i = 0; i < 4; i++) {
		count += scnprintf(&buf[count], PAGE_SIZE - count,
				   "Aoffset CA[%d] Quar:0X%02X,Half:0X%02X,Full:0X%02X%02X\n",
				   i, data[addr+3], data[addr+2], data[addr+1],
				   data[addr]);
		addr += 4;
	}
	addr = INFO_BLOCK_OSC;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim magic number:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim crc:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim len:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim ~crcr:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim ~len:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim ver:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim major ver:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim cen bg:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim frequency bg:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim frequency afe:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim cen bg valid:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);
	addr += 4;
	count += scnprintf(&buf[count], PAGE_SIZE - count,
			   "OscTrim cen afe valid:0x%02X%02X%02X%02X\n",
			   data[addr+3], data[addr+2], data[addr+1],
			   data[addr]);

END:
	kfree(data);

	if (control_reg != flash_status)
		fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG,
			      ADDR_FLASH_STATUS, &control_reg, 1);

	return count;
}

static DEVICE_ATTR_RO(infoblock_getdata);
static DEVICE_ATTR_RW(fwupdate);
static DEVICE_ATTR_RO(appid);
static DEVICE_ATTR_RO(mode_active);
static DEVICE_ATTR_RO(fw_file_test);
static DEVICE_ATTR_RO(status);
#ifdef USE_ONE_FILE_NODE
static DEVICE_ATTR_RW(feature_enable);
#else


#ifdef GRIP_MODE
static DEVICE_ATTR_RW(grip_mode);
#endif

#ifdef CHARGER_MODE
static DEVICE_ATTR_RW(charger_mode);
#endif

#ifdef GLOVE_MODE
static DEVICE_ATTR_RW(glove_mode);
#endif

#ifdef COVER_MODE
static DEVICE_ATTR_RW(cover_mode);
#endif

#ifdef STYLUS_MODE
static DEVICE_ATTR_RW(stylus_mode);
#endif

#endif

#ifdef GESTURE_MODE
static DEVICE_ATTR_RW(gesture_mask);
static DEVICE_ATTR_RO(gesture_coordinates);
#endif
static DEVICE_ATTR_RW(autotune);

static BIN_ATTR_RW(stm_fts_cmd, 0);

static struct bin_attribute *fts_bin_attr_group[] = {
	&bin_attr_stm_fts_cmd,
	NULL,
};

static struct attribute *fts_attr_group[] = {
	 &dev_attr_infoblock_getdata.attr,
	&dev_attr_fwupdate.attr,
	&dev_attr_appid.attr,
	&dev_attr_mode_active.attr,
	&dev_attr_fw_file_test.attr,
	&dev_attr_status.attr,
#ifdef USE_ONE_FILE_NODE
	&dev_attr_feature_enable.attr,
#else

#ifdef GRIP_MODE
	&dev_attr_grip_mode.attr,
#endif
#ifdef CHARGER_MODE
	&dev_attr_charger_mode.attr,
#endif
#ifdef GLOVE_MODE
	&dev_attr_glove_mode.attr,
#endif
#ifdef COVER_MODE
	&dev_attr_cover_mode.attr,
#endif
#ifdef STYLUS_MODE
	&dev_attr_stylus_mode.attr,
#endif

#endif

#ifdef GESTURE_MODE
	&dev_attr_gesture_mask.attr,
	&dev_attr_gesture_coordinates.attr,
#endif
	&dev_attr_autotune.attr,
	NULL,
};

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
static int gti_default_handler(void *private_data, enum gti_cmd_type cmd_type,
	struct gti_union_cmd_data *cmd)
{
	int ret = 0;

	switch (cmd_type) {
	case GTI_CMD_NOTIFY_DISPLAY_STATE:
	case GTI_CMD_NOTIFY_DISPLAY_VREFRESH:
		ret = -EOPNOTSUPP;
		break;
	case GTI_CMD_SET_HEATMAP_ENABLED:
		/* Heatmap is always enabled. */
		ret = 0;
		break;
	case GTI_CMD_GET_CONTEXT_DRIVER:
	case GTI_CMD_GET_CONTEXT_STYLUS:
		/* There is no context from this driver. */
		ret = 0;
		break;
	default:
		ret = -ESRCH;
		break;
	}

	return ret;
}

static int get_fw_version(void *private_data, struct gti_fw_version_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int cmd_buffer_size = sizeof(cmd->buffer);

	get_fw_info(info, cmd->buffer, cmd_buffer_size);
	return 0;
}

static int get_mutual_sensor_data(void *private_data, struct gti_sensor_data_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int max_x = getForceLen(info);
	int max_y = getSenseLen(info);
	int result;
	uint32_t frame_index = 0, x, y;
	uint32_t x_val, y_val;
	int16_t heatmap_value;
	u16 addr_offset;

	cmd->size = max_x * max_y * BYTES_PER_NODE;

	if (!info->mutual_data) {
		info->mutual_data = devm_kzalloc(info->dev, cmd->size, GFP_KERNEL);
		if (!info->mutual_data) {
			dev_err(info->dev, "Failed to allocate mutual_data.\n");
			result = -ENOMEM;
			goto get_data_err;
		}
	}

	cmd->buffer = (u8 *)info->mutual_data;

	if (!info->data_buffer || (info->data_buffer_size < cmd->size)) {
		devm_kfree(info->dev, info->data_buffer);
		info->data_buffer = NULL;
		info->data_buffer_size = 0;
		info->data_buffer = devm_kzalloc(info->dev, cmd->size, GFP_KERNEL);
		if (!info->data_buffer) {
			dev_err(info->dev, "Failed to allocate data_buffer.\n");
			result = -ENOMEM;
			goto get_data_err;
		}
		info->data_buffer_size = cmd->size;
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_MS_RAW:
		addr_offset = info->systemInfo.u16_msTchRawAddr;
		break;
	case GTI_SENSOR_DATA_TYPE_MS_BASELINE:
		addr_offset = info->systemInfo.u16_msTchBaselineAddr;
		break;
	case GTI_SENSOR_DATA_TYPE_MS:
	case GTI_SENSOR_DATA_TYPE_MS_DIFF:
	default:
		addr_offset = info->systemInfo.u16_msTchStrenAddr;
		break;
	}

	result = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16,
			addr_offset,
			(u8 *)info->data_buffer,
			cmd->size, DUMMY_FRAMEBUFFER);
	if (result < 0) {
		dev_err(info->dev, "get mutual data failed with result=0x%08X.\n", result);
		goto get_data_err;
	}

	for (y = 0; y < max_y; y++) {
		for (x = 0; x < max_x; x++) {
			/* Rotate frame counter-clockwise and invert
			 * if necessary.
			 */
			if (info->board->sensor_inverted_x)
				x_val = (max_x - 1) - x;
			else
				x_val = x;
			if (info->board->sensor_inverted_y)
				y_val = (max_y - 1) - y;
			else
				y_val = y;

			if (info->board->tx_rx_dir_swap)
				heatmap_value = info->data_buffer[y_val * max_x + x_val];
			else
				heatmap_value = info->data_buffer[x_val * max_y + y_val];

			info->mutual_data[frame_index++] = heatmap_value;
		}
	}

	return 0;

get_data_err:
	cmd->size = 0;
	cmd->buffer = NULL;
	return result;
}

static int get_self_sensor_data(void *private_data, struct gti_sensor_data_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int i;
	int result;
	int16_t *tx_src, *rx_src, *tx_dst, *rx_dst;
	int tx_size = getForceLen(info);
	int rx_size = getSenseLen(info);
	u16 tx_addr_offset, rx_addr_offset;

	cmd->size = (tx_size + rx_size) * BYTES_PER_NODE;

	if (!info->self_data) {
		info->self_data = devm_kzalloc(info->dev, cmd->size, GFP_KERNEL);
		if (!info->self_data) {
			dev_err(info->dev, "Failed to allocate self_data.\n");
			result = -ENOMEM;
			goto get_data_err;
		}
	}

	cmd->buffer = (u8 *)info->self_data;

	if (!info->data_buffer || (info->data_buffer_size < cmd->size)) {
		devm_kfree(info->dev, info->data_buffer);
		info->data_buffer = NULL;
		info->data_buffer_size = 0;
		info->data_buffer = devm_kzalloc(info->dev, cmd->size, GFP_KERNEL);
		if (!info->data_buffer) {
			dev_err(info->dev, "Failed to allocate data_buffer.\n");
			result = -ENOMEM;
			goto get_data_err;
		}
		info->data_buffer_size = cmd->size;
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_SS_RAW:
		tx_addr_offset = info->systemInfo.u16_ssTchTxRawAddr;
		rx_addr_offset = info->systemInfo.u16_ssTchRxRawAddr;
		break;
	case GTI_SENSOR_DATA_TYPE_SS_BASELINE:
		tx_addr_offset = info->systemInfo.u16_ssTchTxBaselineAddr;
		rx_addr_offset = info->systemInfo.u16_ssTchRxBaselineAddr;
		break;
	case GTI_SENSOR_DATA_TYPE_SS:
	case GTI_SENSOR_DATA_TYPE_SS_DIFF:
	default:
		tx_addr_offset = info->systemInfo.u16_ssTchTxStrenAddr;
		rx_addr_offset = info->systemInfo.u16_ssTchRxStrenAddr;
		break;
	}

	tx_src = info->data_buffer;
	rx_src = info->data_buffer + tx_size;

	result = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16,
			tx_addr_offset,
			(u8 *)tx_src,
			tx_size * BYTES_PER_NODE, DUMMY_FRAMEBUFFER);
	if (result < 0) {
		dev_err(info->dev, "get tx data failed with result=0x%08X.\n", result);
		goto get_data_err;
	}

	result = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16,
			rx_addr_offset,
			(u8 *)rx_src,
			rx_size * BYTES_PER_NODE, DUMMY_FRAMEBUFFER);
	if (result < 0) {
		dev_err(info->dev, "get rx data failed with result=0x%08X.\n", result);
		goto get_data_err;
	}

	/* tx, rx data order is fixed in TouchOffloadData1d */
	tx_dst = info->self_data;
	rx_dst = &info->self_data[tx_size];

	/* If the tx data is flipped, copy in left-to-right order */
	if (info->board->sensor_inverted_x) {
		for (i = 0; i < tx_size; i++)
			tx_dst[i] = tx_src[tx_size - 1 - i];
	} else {
		memcpy(tx_dst, tx_src, sizeof(int16_t) * tx_size);
	}

	/* If the rx data is flipped, copy in top-to-bottom order */
	if (info->board->sensor_inverted_y) {
		for (i = 0; i < rx_size; i++)
			rx_dst[i] = rx_src[rx_size - 1 - i];
	} else {
		memcpy(rx_dst, rx_src, sizeof(int16_t) * rx_size);
	}

	return 0;

get_data_err:
	cmd->size = 0;
	cmd->buffer = NULL;
	return result;
}

static int set_continuous_report(void *private_data, struct gti_continuous_report_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u8 write[3];

	write[0] = (u8) FTS_CMD_CUSTOM_W;
	write[1] = (u8) CUSTOM_CMD_CONTINUOUS_REPORT;
	write[2] = cmd->setting == GTI_CONTINUOUS_REPORT_ENABLE ? 1 : 0;

	return fts_write(info, write, sizeof(write));
}

static int set_screen_protector_mode(
	void *private_data, struct gti_screen_protector_mode_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u8 write[3];
	u8 enable;
	int ret;

	enable = cmd->setting == GTI_SCREEN_PROTECTOR_MODE_ENABLE ? 1 : 0;

	write[0] = (u8) FTS_CMD_CUSTOM_W;
	write[1] = (u8) CUSTOM_CMD_HIGH_SENSITIVITY;
	write[2] = enable;

	ret = fts_write(info, write, sizeof(write));
	if (ret) {
		dev_err(info->dev, "Failed to %s screen protector mode.\n",
			enable ? "enable" : "disable");
	} else {
		info->glove_enabled = enable;
		dev_info(info->dev, "%s screen protector mode.\n",
			info->glove_enabled ? "Enable" : "Disable");
	}

	return ret;
}

static int get_screen_protector_mode(
	void *private_data, struct gti_screen_protector_mode_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	cmd->setting = info->glove_enabled == 1 ? GTI_SCREEN_PROTECTOR_MODE_ENABLE :
		GTI_SCREEN_PROTECTOR_MODE_DISABLE;
	return 0;
}

/**
 * Grip parameters definition.
 * buf[0]: FTS_CMD_CUSTOM_W
 * buf[1]: CUSTOM_CMD_GRIP_SUPPRESSION
 * buf[2]: Grip type tune.
 * buf[3]: Feature enable/disable.
 *         Bit0-7 for left/right/top/bottom/bottom left/bottom right/top left/top right.
 * buf[4]: Feature enable/disable.
 *         Bit0 for left edge palm, Bit1 for right edge palm.
 *         Bit2-7 Reserved.
 * buf[5-8]: Reserved.
 */
static int set_grip_mode(void *private_data, struct gti_grip_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u8 write[9] = { 0 };
	u8 enable;
	int ret;

	enable = cmd->setting == GTI_GRIP_ENABLE ? 1 : 0;

	write[0] = (u8) FTS_CMD_CUSTOM_W;
	write[1] = (u8) CUSTOM_CMD_GRIP_SUPPRESSION;
	write[2] = GRIP_FEATURE_ENABLE;
	write[3] = enable ? 0xff : 0x00;
	write[4] = enable ? 0x03 : 0x00;

	ret = fts_write(info, write, sizeof(write));
	if (ret) {
		dev_err(info->dev, "Failed to %s firmware grip suppression.\n",
			enable ? "enable" : "disable");
	} else {
		info->grip_enabled = enable;
		dev_info(info->dev, "%s firmware grip suppression.\n",
			info->grip_enabled ? "Enable" : "Disable");
	}

	return ret;
}

static int get_grip_mode(void *private_data, struct gti_grip_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	cmd->setting = info->grip_enabled == 1 ? GTI_GRIP_ENABLE : GTI_GRIP_DISABLE;
	return 0;
}

/**
 * Palm mode definition.
 * 0 : Palm rejection disable
 * 1 : Palm rejection enable. All rejected after detected palm, and returned
 *     to normal after all left.
 * 2 : Palm rejection enable. Palm rejected only after detected palm, and
 *     returned to normal after palm left.
 * 3 : Palm rejection enable. All rejected after detected palm, and returned
 *     to normal after palm left
 */
/* Use palm mode 3 when it's enabled. */
#define FTS_PALM_MODE 3
static int set_palm_mode(void *private_data, struct gti_palm_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u8 write[3];
	u8 enable;
	int ret;

	enable = cmd->setting == GTI_PALM_ENABLE ? 1 : 0;

	write[0] = (u8) FTS_CMD_CUSTOM_W;
	write[1] = (u8) CUSTOM_CMD_PALM_REJECTION;
	write[2] = enable ? FTS_PALM_MODE : 0;

	ret = fts_write(info, write, sizeof(write));
	if (ret) {
		dev_err(info->dev, "Failed to %s firmware palm rejection.\n",
			enable ? "enable" : "disable");
	} else {
		info->palm_enabled = enable;
		dev_info(info->dev, "%s firmware palm rejection.\n",
			info->palm_enabled ? "Enable" : "Disable");
	}

	return ret;
}

static int get_palm_mode(void *private_data, struct gti_palm_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	cmd->setting = info->palm_enabled == 1 ? GTI_PALM_ENABLE : GTI_PALM_DISABLE;
	return 0;
}

static int set_coord_filter_enabled(void *private_data, struct gti_coord_filter_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u8 write[3];
	u8 disable;
	int ret;

	disable = cmd->setting == GTI_COORD_FILTER_DISABLE ? 1 : 0;

	write[0] = (u8) FTS_CMD_CUSTOM_W;
	write[1] = (u8) CUSTOM_CMD_DISABLE_COORD_FILTER;
	write[2] = disable;

	ret = fts_write(info, write, sizeof(write));
	if (ret) {
		dev_err(info->dev, "Failed to %s firmware coordinate filter.\n",
			disable ? "disable" : "enable");
	} else {
		info->coord_filter_disabled = disable;
		dev_info(info->dev, "%s firmware coordinate filter.\n",
			info->coord_filter_disabled ? "Disable" : "Enable");
	}

	return ret;
}

static int get_coord_filter_enabled(void *private_data, struct gti_coord_filter_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	cmd->setting = info->coord_filter_disabled ?
			GTI_COORD_FILTER_DISABLE : GTI_COORD_FILTER_ENABLE;
	return 0;
}

/**
 * Set the custom touch report rate.
 * buf[0]: FTS_CMD_CUSTOM_W
 * buf[1]: CUSTOM_CMD_REPORT_RATE
 * buf[2]: Enable/Disable.
 * buf[3]: Report rate unit. (100 us)
 */
static int set_report_rate(void *private_data, struct gti_report_rate_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u8 write[4];
	int ret;

	if (cmd->setting == 0) {
		dev_err(info->dev, "Invalid report rate.\n");
		return -EINVAL;
	}

	write[0] = (u8) FTS_CMD_CUSTOM_W;
	write[1] = (u8) CUSTOM_CMD_REPORT_RATE;
	write[2] = (goog_get_max_touch_report_rate(info->gti) == cmd->setting)? 0 : 1;
	write[3] = MSEC_PER_SEC * 10 / cmd->setting;

	ret = fts_write(info, write, sizeof(write));
	if (ret) {
		dev_err(info->dev, "Failed to set report rate.\n");
	} else {
		dev_info(info->dev, "Set touch report rate as %dHz.\n", cmd->setting);
	}

	return ret;
}

static int get_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	cmd->setting = info->irq_enabled ? GTI_IRQ_MODE_ENABLE : GTI_IRQ_MODE_DISABLE;

	return 0;
}

static int set_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	return fts_enableInterrupt(info, cmd->setting == GTI_IRQ_MODE_ENABLE);
}

static int set_reset(void *private_data, struct gti_reset_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	/* Reset then sense on. */
	if (cmd->setting == GTI_RESET_MODE_HW || cmd->setting == GTI_RESET_MODE_AUTO)
		return cleanUp(info, true);
	else
		return -EOPNOTSUPP;
}

static int ping(void *private_data, struct gti_ping_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u16 chip_id;
	int ret = 0;

	ret = fts_writeReadU8UX(info, FTS_CMD_HW_REG_R, ADDR_SIZE_HW_REG,
			ADDR_DCHIP_ID, (u8 *)&chip_id, 2, DUMMY_HW_REG);
	if (ret) {
		dev_err(info->dev, "Failed to read chip ID, ret = %#x.\n", ret);
		return ret;
	}

	if (chip_id != ((info->board->dchip_id[1] << 8) | info->board->dchip_id[0])) {
		dev_err(info->dev, "Wrong chip ID\n");
		return -EINVAL;
	}

	return 0;
}

static int calibrate(void *private_data, struct gti_calibrate_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int ret = 0;

	ret = production_test_main(info, info->board->limits_name, 1,
				   SPECIAL_FULL_PANEL_INIT, MP_FLAG_BOOT);

	/* Reset, then sense on */
	cleanUp(info, true);

	if (ret == 0)
		cmd->result = GTI_CALIBRATE_RESULT_DONE;
	else
		cmd->result = GTI_CALIBRATE_RESULT_FAIL;
	return ret;
}

static int selftest(void *private_data, struct gti_selftest_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	const char *limits_file = info->board->limits_name;
	MutualSenseFrame frameMS;
	u16 ito_max_val[2] = {0x00};
	int ret;

	frameMS.node_data = NULL;
	ret = production_test_ito(info, limits_file, &frameMS,
				  ito_max_val);

	/* Free the allocated frame */
	kfree(frameMS.node_data);
	frameMS.node_data = NULL;

	/* Reset, then sense on. */
	cleanUp(info, true);

	if (ret == 0)
		cmd->result = GTI_SELFTEST_RESULT_DONE;
	else
		cmd->result = GTI_SELFTEST_RESULT_NA;
	return ret;
}
#endif

/**
  * @defgroup isr Interrupt Service Routine (Event Handler)
  * The most important part of the driver is the ISR (Interrupt Service Routine)
  * called also as Event Handler \n
  * As soon as the interrupt pin goes low, fts_interrupt_handler() is called and
  * the chain to read and parse the event read from the FIFO start.\n
  * For any different kind of EVT_ID there is a specific event handler
  * which will take the correct action to report the proper info to the host. \n
  * The most important events are the one related to touch information, status
  * update or user report.
  * @{
  */

/**
  * Report to the linux input system the pressure and release of a button
  * handling concurrency
  * @param info pointer to fts_ts_info which contains info about the device
  * and its hw setup
  * @param key_code	button value
  */
void fts_input_report_key(struct fts_ts_info *info, int key_code)
{
	input_report_key(info->input_dev, key_code, 1);
	input_sync(info->input_dev);
	input_report_key(info->input_dev, key_code, 0);
	input_sync(info->input_dev);
}

/**
  * Event Handler for no events (EVT_ID_NOEVENT)
  */
static bool fts_nop_event_handler(struct fts_ts_info *info, unsigned
				  char *event)
{
	dev_info(info->dev, "%s: Doing nothing for event = %02X %02X %02X %02X %02X %02X %02X %02X\n",
		__func__, event[0], event[1], event[2], event[3],
		event[4],
		event[5], event[6], event[7]);
	return false;
}

/**
  * Event handler for enter and motion events (EVT_ID_ENTER_POINT,
  * EVT_ID_MOTION_POINT )
  * report touch coordinates and additional information
  * to the linux input system
  */
static bool fts_enter_pointer_event_handler(struct fts_ts_info *info, unsigned
					    char *event)
{
	unsigned char touchId;
	unsigned int touch_condition = 1, tool = MT_TOOL_FINGER;
	int x, y, z, major, minor, distance;
	u8 touchType;

	if (!info->resume_bit)
		goto no_report;

	touchType = event[1] & 0x0F;
	touchId = (event[1] & 0xF0) >> 4;
	if (touchId >= TOUCH_ID_MAX) {
		dev_err(info->dev, "%s : Invalid touch ID = %d ! No Report...\n",
			__func__, touchId);
		goto no_report;
	}

	x = (((int)event[3] & 0x0F) << 8) | (event[2]);
	y = ((int)event[4] << 4) | ((event[3] & 0xF0) >> 4);
	z = (int)event[5];
	if (z <= 0) {
		/* Should not happen, because zero pressure implies contact has
		 * left, so this function should not be invoked. For safety, to
		 * prevent this touch from being dropped, set to smallest
		 * pressure value instead
		 */
#ifndef SKIP_PRESSURE
		dev_err(info->dev, "%s: Pressure is %i, but pointer is not leaving.\n",
		       __func__, z);
#endif
		z = 1; /* smallest non-zero pressure value */
	}
	major = (int)((((event[0] & 0x0C) << 2) | ((event[6] & 0xF0) >> 4)) * AREA_SCALE);
	minor = (int)((((event[7] & 0xC0) >> 2) | (event[6] & 0x0F)) * AREA_SCALE);
	/* TODO: check with fw how they will report distance */
	distance = 0;	/* if the tool is touching the display
			  * the distance should be 0 */

	if (x > info->board->x_axis_max)
		x = info->board->x_axis_max;

	if (y > info->board->y_axis_max)
		y = info->board->y_axis_max;

	switch (touchType) {
#ifdef STYLUS_MODE
	case TOUCH_TYPE_STYLUS:
		dev_dbg(info->dev, "%s : touch type = %d!\n", __func__);
		if (info->stylus_enabled == 1) {
			/* if stylus_enabled is not ==1
			  * it will be reported as normal touch */
			tool = MT_TOOL_PEN;
			touch_condition = 1;
			__set_bit(touchId, &info->stylus_id);
			break;
		}
#endif
	/* TODO: customer can implement a different strategy for each kind of
	 * touch */
	case TOUCH_TYPE_FINGER:
	case TOUCH_TYPE_GLOVE:
		dev_dbg(info->dev, "%s : touch type = %d!\n", __func__, touchType);
		if (info->palm_touch_mask)
			tool = MT_TOOL_PALM;
		else
			tool = MT_TOOL_FINGER;
		touch_condition = 1;
		__set_bit(touchId, &info->touch_id);
		__clear_bit(touchId, &info->palm_touch_mask);
		__clear_bit(touchId, &info->grip_touch_mask);
		break;
	case TOUCH_TYPE_PALM:
		touch_condition = 1;
		__set_bit(touchId, &info->touch_id);
		__clear_bit(touchId, &info->grip_touch_mask);
		if (!info->palm_enabled) {
			dev_err(info->dev, "%s : Unexpected touch type = %d!\n",
				__func__, touchType);
			tool = MT_TOOL_FINGER;
			__clear_bit(touchId, &info->palm_touch_mask);
		} else {
			dev_dbg(info->dev, "%s : touch type = %d!\n",
				__func__, touchType);
			tool = MT_TOOL_PALM;
			__set_bit(touchId, &info->palm_touch_mask);
		}

		break;
	case TOUCH_TYPE_GRIP:
		touch_condition = 1;
		__set_bit(touchId, &info->touch_id);
		__clear_bit(touchId, &info->palm_touch_mask);
		if (!info->grip_enabled) {
			dev_err(info->dev, "%s : Unexpected touch type = %d!\n",
				__func__, touchType);
			tool = MT_TOOL_FINGER;
			__clear_bit(touchId, &info->grip_touch_mask);
		} else {
			dev_dbg(info->dev, "%s : touch type = %d!\n",
				__func__, touchType);
			tool = MT_TOOL_PALM;
			__set_bit(touchId, &info->grip_touch_mask);
		}
		break;

	case TOUCH_TYPE_HOVER:
		dev_dbg(info->dev, "%s : touch type = %d!\n", __func__, touchType);
		tool = MT_TOOL_FINGER;
		touch_condition = 0;	/* need to hover */
		z = 0;	/* no pressure */
		__set_bit(touchId, &info->touch_id);
		distance = DISTANCE_MAX;/* check with fw report the hovering
					  * distance */
		break;

	default:
		dev_err(info->dev, "%s : Invalid touch type = %d! No Report...\n",
			__func__, touchType);
		goto no_report;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_mt_slot(info->gti, info->input_dev, touchId);
	goog_input_report_key(info->gti, info->input_dev, BTN_TOUCH, touch_condition);
	goog_input_mt_report_slot_state(info->gti, info->input_dev, tool, 1);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_POSITION_X, x);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_POSITION_Y, y);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_TOUCH_MAJOR, major);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_TOUCH_MINOR, minor);
#ifndef SKIP_PRESSURE
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_PRESSURE, z);
#endif

#ifndef SKIP_DISTANCE
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_DISTANCE, distance);
#endif
#else
	input_mt_slot(info->input_dev, touchId);
	input_report_key(info->input_dev, BTN_TOUCH, touch_condition);
	input_mt_report_slot_state(info->input_dev, tool, 1);
	input_report_abs(info->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(info->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, major);
	input_report_abs(info->input_dev, ABS_MT_TOUCH_MINOR, minor);
#ifndef SKIP_PRESSURE
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, z);
#endif

#ifndef SKIP_DISTANCE
	input_report_abs(info->input_dev, ABS_MT_DISTANCE, distance);
#endif
#endif

	/* dev_info(info->dev, "%s :  Event 0x%02x - ID[%d], (x, y) = (%3d, %3d)
	 * Size = %d\n",
	 *	__func__, *event, touchId, x, y, touchType); */

	return true;
no_report:
	return false;
}

/**
  * Event handler for leave event (EVT_ID_LEAVE_POINT )
  * Report to the linux input system that one touch left the display
  */
static bool fts_leave_pointer_event_handler(struct fts_ts_info *info, unsigned
					    char *event)
{
	unsigned char touchId;
	unsigned int tool = MT_TOOL_FINGER;
	u8 touchType;

	touchType = event[1] & 0x0F;
	touchId = (event[1] & 0xF0) >> 4;
	if (touchId >= TOUCH_ID_MAX) {
		dev_err(info->dev, "%s : Invalid touch ID = %d ! No Report...\n",
			__func__, touchId);
		return false;
	}

	switch (touchType) {
#ifdef STYLUS_MODE
	case TOUCH_TYPE_STYLUS:
		dev_dbg(info->dev, "%s : touch type = %d!\n", __func__);
		if (info->stylus_enabled == 1) {
			/* if stylus_enabled is not ==1 it will be reported as
			 * normal touch */
			tool = MT_TOOL_PEN;
			__clear_bit(touchId, &info->stylus_id);
			break;
		}
#endif
	case TOUCH_TYPE_FINGER:
	case TOUCH_TYPE_GLOVE:
	case TOUCH_TYPE_PALM:
	case TOUCH_TYPE_GRIP:
	case TOUCH_TYPE_HOVER:
		dev_dbg(info->dev, "%s : touch type = %d!\n", __func__, touchType);
		tool = MT_TOOL_FINGER;
		__clear_bit(touchId, &info->touch_id);
		__clear_bit(touchId, &info->palm_touch_mask);
		__clear_bit(touchId, &info->grip_touch_mask);
		break;

	default:
		dev_err(info->dev, "%s : Invalid touch type = %d! No Report...\n",
			__func__, touchType);
		return false;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_mt_slot(info->gti, info->input_dev, touchId);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_PRESSURE, 0);
	goog_input_mt_report_slot_state(info->gti, info->input_dev, tool, 0);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_TRACKING_ID, -1);
#else
	input_mt_slot(info->input_dev, touchId);
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, 0);
	input_mt_report_slot_state(info->input_dev, tool, 0);
	input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, -1);
#endif

	return true;
}

/* EventId : EVT_ID_MOTION_POINT */
#define fts_motion_pointer_event_handler fts_enter_pointer_event_handler
/* remap the motion event handler to the same function which handle the enter
 * event */

/**
  * Event handler for error events (EVT_ID_ERROR)
  * Handle unexpected error events implementing recovery strategy and
  * restoring the sensing status that the IC had before the error occurred
  */
static bool fts_error_event_handler(struct fts_ts_info *info, unsigned
				    char *event)
{
	int error = 0;

	dev_info(info->dev, "%s: Received event %02X %02X %02X %02X %02X %02X %02X %02X\n",
		 __func__, event[0], event[1], event[2], event[3], event[4],
		 event[5],
		 event[6], event[7]);

	switch (event[1]) {
	case EVT_TYPE_ERROR_ESD:/* esd */
	{
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		/* The GTI handles to release all touches */
		clear_touch_flags(info);
#else
		/* before reset clear all slot */
		release_all_touches(info);
#endif
		fts_chip_powercycle(info);

		error = fts_system_reset(info);
		error |= fts_mode_handler(info, 0);
		error |= fts_enableInterrupt(info, true);
		if (error < OK)
			dev_err(info->dev, "%s Cannot restore the device ERROR %08X\n",
				__func__, error);
	}
	break;
	case EVT_TYPE_ERROR_HARD_FAULT:	/* hard fault */
	case EVT_TYPE_ERROR_WATCHDOG:	/* watch dog timer */
	{
		dumpErrorInfo(info, NULL, 0);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		/* The GTI handles to release all touches */
		clear_touch_flags(info);
#else
		/* before reset clear all slots */
		release_all_touches(info);
#endif
		error = fts_system_reset(info);
		error |= fts_mode_handler(info, 0);
		error |= fts_enableInterrupt(info, true);
		if (error < OK)
			dev_err(info->dev, "%s Cannot reset the device ERROR %08X\n",
				__func__, error);
	}
	break;
	}
	return false;
}

/**
  * Event handler for controller ready event (EVT_ID_CONTROLLER_READY)
  * Handle controller events received after unexpected reset of the IC updating
  * the resets flag and restoring the proper sensing status
  */
static bool fts_controller_ready_event_handler(struct fts_ts_info *info,
					       unsigned char *event)
{
	int error;

	dev_info(info->dev, "%s: Received event %02X %02X %02X %02X %02X %02X %02X %02X\n",
		__func__, event[0], event[1], event[2], event[3], event[4],
		event[5], event[6], event[7]);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* The GTI handles to release all touches. */
	clear_touch_flags(info);
#else
	release_all_touches(info);
#endif
	setSystemResetedUp(info, 1);
	setSystemResetedDown(info, 1);
	error = fts_mode_handler(info, 0);
	if (error < OK)
		dev_err(info->dev, "%s Cannot restore the device status ERROR %08X\n",
			__func__, error);

	return false;
}

/**
  * Event handler for status events (EVT_ID_STATUS_UPDATE)
  * Handle status update events
  */
static bool fts_status_event_handler(struct fts_ts_info *info, unsigned
				     char *event)
{
	u8 grid_touch_status;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct gti_fw_status_data data = {0};
#endif

	switch (event[1]) {
	case EVT_TYPE_STATUS_ECHO:
		dev_dbg(info->dev, "%s: Echo event of command = %02X %02X %02X %02X %02X %02X\n",
			__func__, event[2], event[3], event[4], event[5],
			event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_GPIO_CHAR_DET:
		dev_info(info->dev, "%s: GPIO Charger Detect ="
			" %02X %02X %02X %02X %02X %02X\n",
			__func__, event[2], event[3], event[4], event[5],
			event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_FORCE_CAL:
		switch (event[2]) {
		case 0x01:
			dev_info(info->dev, "%s: Sense on Force cal = %02X %02X"
				" %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x02:
			dev_info(info->dev, "%s: Host command Force cal = %02X %02X"
				" %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x10:
			dev_info(info->dev, "%s: Mutual frame drop Force cal = %02X %02X"
			" %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x11:
			dev_info(info->dev, "%s: Mutual pure raw Force cal = %02X %02X"
			" %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x20:
			dev_info(info->dev, "%s: Self detect negative Force cal = %02X"
			" %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x21:
			dev_info(info->dev, "%s: Self touch negative Force cal = %02X"
			" %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x22:
			dev_info(info->dev, "%s: Self detect frame flatness Force cal ="
			" %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x23:
			dev_info(info->dev, "%s: Self touch frame flatness Force cal ="
			" %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x30:
			dev_info(info->dev, "%s: Invalid mutual Force cal = %02X"
			" %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x31:
			dev_info(info->dev, "%s: Invalid differential mutual Force cal ="
			" %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x32:
			dev_info(info->dev, "%s: Invalid Self Force cal = %02X"
			" %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x33:
			dev_info(info->dev, "%s: Invalid Self island Force cal = %02X"
			" %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x34:
			dev_info(info->dev, "%s: Invalid Self force touch Force cal ="
			" %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x35:
			dev_info(info->dev, "%s: Mutual frame flatness Force cal ="
			" %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		default:
			dev_info(info->dev, "%s: Unknown force cal = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		}
		break;

	case EVT_TYPE_STATUS_FRAME_DROP:
			dev_info(info->dev, "%s: Frame drop = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_SS_RAW_SAT:
		if (event[2] == 1)
			dev_info(info->dev, "%s: SS Raw Saturated = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		else
			dev_info(info->dev, "%s: SS Raw No more Saturated = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_WATER:
		switch (event[2]) {
		case 0x00:
			dev_info(info->dev, "%s: Water Mode Entry by BLD with real"
				" raw frame = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_ENTER, NULL);
#endif
			break;

		case 0x01:
			dev_info(info->dev, "%s: Water Mode Entry by BLD with rom"
				" raw frame = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_ENTER, NULL);
#endif
			break;

		case 0x02:
			dev_info(info->dev, "%s: Water Mode Entry by MID with real"
				" raw frame = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_ENTER, NULL);
#endif
			break;

		case 0x03:
			dev_info(info->dev, "%s: Water Mode leave by BLD with real"
				" raw frame = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_EXIT, NULL);
#endif
			break;

		case 0x04:
			dev_info(info->dev, "%s: Water Mode leave by BLD with rom"
				" raw frame = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_EXIT, NULL);
#endif
			break;

		case 0x05:
			dev_info(info->dev, "%s: Water Mode leave by MID with real"
				" raw frame = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_EXIT, NULL);
#endif
			break;

		default:
			dev_info(info->dev, "%s: Unknown water mode = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		}
		break;

	case EVT_TYPE_STATUS_PRE_WAT_DET:
		if (event[2] == 1)
			dev_info(info->dev, "%s: Previous Water entry ="
			" %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		else
			dev_info(info->dev, "%s: Previous Water leave ="
				" %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_NOISE:
		if(info->scanning_frequency != event[3]) {
			dev_info(info->dev, "%s: Scanning frequency changed from %02X to %02X\n",
				__func__, info->scanning_frequency, event[3]);
			dev_info(info->dev, "%s: Noise Status Event = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3],
				event[4], event[5], event[6], event[7]);
			info->scanning_frequency = event[3];
		} else {
			dev_dbg(info->dev, "%s: Noise Status Event = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		}
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		data.noise_level = event[2] & 0x0F;
		goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_NOISE_MODE, &data);
#endif
		break;

	case EVT_TYPE_STATUS_STIMPAD:
		switch (event[2]) {
		case 0x00:
			dev_dbg(info->dev, "%s: Stimpad disable event"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x01:
			dev_dbg(info->dev, "%s: Stimpad enable event"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x02:
			dev_dbg(info->dev, "%s: Stimpad disable by signature invalid"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x03:
			dev_dbg(info->dev, "%s: Stimpad disable by nodes count invalid"
				" raw frame = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		default:
			dev_dbg(info->dev, "%s: Unknown stimpad status = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		}
		break;

	case EVT_TYPE_STATUS_NO_TOUCH:
		dev_info(info->dev, "%s: No Touch Status Event = %02X %02X"
		" %02X %02X %02X %02X\n",
			__func__, event[2], event[3], event[4], event[5],
			event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_IDLE:
		dev_info(info->dev, "%s: Idle Status Event = %02X %02X"
		" %02X %02X %02X %02X\n",
			__func__, event[2], event[3], event[4], event[5],
			event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_PALM_TOUCH:
		switch (event[2]) {
		case 0x01:
			dev_info(info->dev, "%s: Palm block entry event"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x02:
			dev_info(info->dev, "%s: Palm block release event"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		default:
			dev_info(info->dev, "%s: Unknown palm touch status = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		}
		break;

	case EVT_TYPE_STATUS_GRIP_TOUCH:
		grid_touch_status = (event[2] & 0xF0) >> 4;
		switch (grid_touch_status) {
		case 0x01:
			dev_info(info->dev, "%s: Grip Touch entry event"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x02:
			dev_info(info->dev, "%s: Grip Touch release event"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		default:
			dev_info(info->dev, "%s: Unknown grip touch status = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		}
		break;

	case EVT_TYPE_STATUS_GOLDEN_RAW_VAL:
		switch (event[2]) {
		case 0x01:
			dev_info(info->dev, "%s: Golden Raw Validation Pass"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		case 0x02:
			dev_info(info->dev, "%s: Golden Raw Validation Fail"
				" = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
			break;

		default:
			dev_info(info->dev, "%s: Unknown golden raw validation status = %02X %02X %02X %02X %02X %02X\n",
				__func__, event[2], event[3], event[4],
				event[5], event[6], event[7]);
		}
		break;

	case EVT_TYPE_STATUS_GOLDEN_RAW_ERR:
		dev_info(info->dev, "%s: Golden Raw Data Abnormal"
			" = %02X %02X %02X %02X %02X %02X\n",
			__func__, event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_HIGH_SENSITY:
		dev_info(info->dev, "%s: High Sensitity %s ="
			" %02X %02X %02X %02X %02X %02X\n",
			__func__, (event[2] == 1) ? "enabled" : "disabled",
			event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;

	default:
		dev_info(info->dev, "%s: Received unknown status event = %02X %02X %02X %02X %02X %02X %02X %02X\n",
			__func__, event[0], event[1], event[2], event[3],
			event[4], event[5], event[6], event[7]);
		break;
	}
	return false;
}


/* key events reported in the user report */
#ifdef PHONE_KEY
/* TODO: the customer should handle the events coming from the keys according
 * his needs
  * (this is just an sample code that report the click of a button after a
  * press->release action) */
/**
  * Event handler for status events (EVT_TYPE_USER_KEY)
  * Handle keys update events, the third byte of the event is a bitmask,
  * if the bit set means that the corresponding key is pressed.
  */
static void fts_key_event_handler(struct fts_ts_info *info,
				  unsigned char *event)
{
	/* int value; */
	dev_info(info->dev, "%s: Received event %02X %02X %02X %02X %02X %02X %02X %02X\n",
		__func__, event[0], event[1], event[2], event[3], event[4],
		event[5], event[6], event[7]);

	if (event[0] == EVT_ID_USER_REPORT && event[1] == EVT_TYPE_USER_KEY) {
		/* event[2] contain the bitmask of the keys that are actually
		 * pressed */

		if ((event[2] & FTS_KEY_0) == 0 && (info->key_mask & FTS_KEY_0) > 0) {
			dev_info(info->dev, "%s: Button HOME pressed and released!\n",
				__func__);
			fts_input_report_key(info, KEY_HOMEPAGE);
		}

		if ((event[2] & FTS_KEY_1) == 0 && (info->key_mask & FTS_KEY_1) > 0) {
			dev_info(info->dev, "%s: Button Back pressed and released!\n",
				__func__);
			fts_input_report_key(info, KEY_BACK);
		}

		if ((event[2] & FTS_KEY_2) == 0 && (info->key_mask & FTS_KEY_2) > 0) {
			dev_info(info->dev, "%s: Button Menu pressed!\n", __func__);
			fts_input_report_key(info, KEY_MENU);
		}

		info->key_mask = event[2];
	} else
		dev_err(info->dev, "%s: Invalid event passed as argument!\n", __func__);
}
#endif

/* gesture event must be handled in the user event handler */
#ifdef GESTURE_MODE
/* TODO: Customer should implement their own actions in respond of a gesture
 * event.
  * This is an example that simply print the gesture received and simulate
  * the click on a different button for each gesture. */
/**
  * Event handler for gesture events (EVT_TYPE_USER_GESTURE)
  * Handle gesture events and simulate the click on a different button
  * for any gesture detected (@link gesture_opt Gesture IDs @endlink)
  */
static void fts_gesture_event_handler(struct fts_ts_info *info, unsigned
				      char *event)
{
	int value;
	int needCoords = 0;

	dev_info(info->dev, "gesture event data: %02X %02X %02X %02X %02X %02X %02X %02X\n",
		event[0], event[1], event[2], event[3], event[4],
		event[5], event[6], event[7]);

	if (event[0] == EVT_ID_USER_REPORT && event[1] ==
	    EVT_TYPE_USER_GESTURE) {
		needCoords = 1;
		/* default read the coordinates for all gestures excluding
		 * double tap */

		switch (event[2]) {
		case GEST_ID_DBLTAP:
			value = KEY_WAKEUP;
			dev_info(info->dev, "%s: double tap !\n", __func__);
			needCoords = 0;
			break;

		case GEST_ID_AT:
			value = KEY_WWW;
			dev_info(info->dev, "%s: @ !\n", __func__);
			break;

		case GEST_ID_C:
			value = KEY_C;
			dev_info(info->dev, "%s: C !\n", __func__);
			break;

		case GEST_ID_E:
			value = KEY_E;
			dev_info(info->dev, "%s: e !\n", __func__);
			break;

		case GEST_ID_F:
			value = KEY_F;
			dev_info(info->dev, "%s: F !\n", __func__);
			break;

		case GEST_ID_L:
			value = KEY_L;
			dev_info(info->dev, "%s: L !\n", __func__);
			break;

		case GEST_ID_M:
			value = KEY_M;
			dev_info(info->dev, "%s: M !\n", __func__);
			break;

		case GEST_ID_O:
			value = KEY_O;
			dev_info(info->dev, "%s: O !\n", __func__);
			break;

		case GEST_ID_S:
			value = KEY_S;
			dev_info(info->dev, "%s: S !\n", __func__);
			break;

		case GEST_ID_V:
			value = KEY_V;
			dev_info(info->dev, "%s:  V !\n", __func__);
			break;

		case GEST_ID_W:
			value = KEY_W;
			dev_info(info->dev, "%s:  W !\n", __func__);
			break;

		case GEST_ID_Z:
			value = KEY_Z;
			dev_info(info->dev, "%s:  Z !\n", __func__);
			break;

		case GEST_ID_RIGHT_1F:
			value = KEY_RIGHT;
			dev_info(info->dev, "%s:  -> !\n", __func__);
			break;

		case GEST_ID_LEFT_1F:
			value = KEY_LEFT;
			dev_info(info->dev, "%s:  <- !\n", __func__);
			break;

		case GEST_ID_UP_1F:
			value = KEY_UP;
			dev_info(info->dev, "%s:  UP !\n", __func__);
			break;

		case GEST_ID_DOWN_1F:
			value = KEY_DOWN;
			dev_info(info->dev, "%s:  DOWN !\n", __func__);
			break;

		case GEST_ID_CARET:
			value = KEY_APOSTROPHE;
			dev_info(info->dev, "%s:  ^ !\n", __func__);
			break;

		case GEST_ID_LEFTBRACE:
			value = KEY_LEFTBRACE;
			dev_info(info->dev, "%s:  < !\n", __func__);
			break;

		case GEST_ID_RIGHTBRACE:
			value = KEY_RIGHTBRACE;
			dev_info(info->dev, "%s:  > !\n", __func__);
			break;

		default:
			dev_err(info->dev, "%s:  No valid GestureID!\n", __func__);
			goto gesture_done;
		}

		if (needCoords == 1)
			readGestureCoords(info, event);

		fts_input_report_key(info, value);

gesture_done:
		return;
	} else
		dev_err(info->dev, "%s: Invalid event passed as argument!\n", __func__);
}
#endif


/**
  * Event handler for user report events (EVT_ID_USER_REPORT)
  * Handle user events reported by the FW due to some interaction triggered
  * by an external user (press keys, perform gestures, etc.)
  */
static bool fts_user_report_event_handler(struct fts_ts_info *info, unsigned
					  char *event)
{
	switch (event[1]) {
#ifdef PHONE_KEY
	case EVT_TYPE_USER_KEY:
		fts_key_event_handler(info, event);
		break;
#endif

	case EVT_TYPE_USER_PROXIMITY:
		if (event[2] == 0)
			dev_err(info->dev, "%s No proximity!\n", __func__);
		else
			dev_err(info->dev, "%s Proximity Detected!\n", __func__);
		break;

#ifdef GESTURE_MODE
	case EVT_TYPE_USER_GESTURE:
		fts_gesture_event_handler(info, event);
		break;
#endif
	default:
		dev_err(info->dev, "%s: Received unhandled user report event = %02X %02X %02X %02X %02X %02X %02X %02X\n",
			__func__, event[0], event[1], event[2], event[3],
			event[4], event[5], event[6], event[7]);
		break;
	}
	return false;
}

/**
  * Bottom Half Interrupt Handler function
  * This handler is called each time there is at least one new event in the FIFO
  * and the interrupt pin of the IC goes low. It will read all the events from
  * the FIFO and dispatch them to the proper event handler according the event
  * ID
  */
static irqreturn_t fts_interrupt_handler(int irq, void *handle)
{
	struct fts_ts_info *info = handle;
	int error = 0, count = 0;
	unsigned char regAdd = FIFO_CMD_READALL;
	unsigned char data[FIFO_EVENT_SIZE * FIFO_DEPTH];
	unsigned char eventId;
	const unsigned char EVENTS_REMAINING_POS = 7;
	const unsigned char EVENTS_REMAINING_MASK = 0x1F;
	unsigned char events_remaining = 0;
	unsigned char *evt_data;
	bool has_pointer_event = false;
	int event_start_idx = -1;

	/* Read the first FIFO event and the number of events remaining */
	error = fts_writeReadU8UX(info, regAdd, 0, 0, data, FIFO_EVENT_SIZE,
				  DUMMY_FIFO);
	events_remaining = data[EVENTS_REMAINING_POS] & EVENTS_REMAINING_MASK;
	events_remaining = (events_remaining > FIFO_DEPTH - 1) ?
			   FIFO_DEPTH - 1 : events_remaining;

	/* Drain the rest of the FIFO, up to 31 events */
	if (error == OK && events_remaining > 0) {
		error = fts_writeReadU8UX(info, regAdd, 0, 0,
					  &data[FIFO_EVENT_SIZE],
					  FIFO_EVENT_SIZE * events_remaining,
					  DUMMY_FIFO);
	}
	if (error != OK) {
		dev_err(info->dev, "Error (%08X) while reading from FIFO in fts_event_handler\n",
			error);
	} else {
		evt_data = &data[0];
		if (evt_data[0] == EVT_ID_NOEVENT)
			goto exit;
		/*
		 * Parsing all the events ID and specifically handle the
		 * EVT_ID_CONTROLLER_READY and EVT_ID_ERROR at first.
		 */
		for (count = 0; count < events_remaining + 1; count++) {
			evt_data = &data[count * FIFO_EVENT_SIZE];

			if (!VALID_EVENT_TYPE(evt_data[0])) {
				dev_err(info->dev, "Got invalid event type: %*ph\n", 8, evt_data);
				goto exit;
			}

			switch (GET_EVENT_TYPE(evt_data[0])) {
			case EVT_ID_CONTROLLER_READY:
			case EVT_ID_ERROR:
				eventId = evt_data[0] >> 4;
				info->event_dispatch_table[eventId](info, evt_data);

				event_start_idx = count;
				break;

			case EVT_ID_ENTER_POINT:
			case EVT_ID_MOTION_POINT:
			case EVT_ID_LEAVE_POINT:
				has_pointer_event |= true;
				break;

			default:
				break;
			}
		}

		/* Only lock input report when there is pointer event. */
		if (has_pointer_event) {
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_input_lock(info->gti);
			goog_input_set_timestamp(info->gti, info->input_dev, info->timestamp);
#else
			mutex_lock(&info->input_report_mutex);
			input_set_timestamp(info->input_dev, info->timestamp);
#endif
		}

		/*
		 * Handle the remaining events except for
		 * EVT_ID_CONTROLLER_READY and EVT_ID_ERROR.
		 */
		for (count = max(event_start_idx + 1, 0); count < events_remaining + 1; count++) {
			evt_data = &data[count * FIFO_EVENT_SIZE];

			eventId = evt_data[0] >> 4;

			/* Ensure event ID is within bounds */
			if (eventId < NUM_EVT_ID)
				info->event_dispatch_table[eventId](info, evt_data);
		}

		if (has_pointer_event) {
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			if (info->touch_id == 0)
				goog_input_report_key(info->gti, info->input_dev, BTN_TOUCH, 0);

			goog_input_sync(info->gti, info->input_dev);
			goog_input_unlock(info->gti);
#else
			if (info->touch_id == 0)
				input_report_key(info->input_dev, BTN_TOUCH, 0);

			input_sync(info->input_dev);
			mutex_unlock(&info->input_report_mutex);
#endif
		}
	}

exit:
	return IRQ_HANDLED;
}

/*
 * Read the display panel's extinfo from the display driver.
 *
 * The display driver finds out the extinfo is available for a panel based on
 * the device tree, but cannot read the extinfo itself until the DSI bus is
 * initialized. Since the extinfo is not guaranteed to be available at the time
 * the touch driver is probed or even when the automatic firmware update work is
 * run. The display driver's API for reading extinfo does allow a client to
 * query the size of the expected data and whether it is available.
 *
 * This function retrieves the extinfo from the display driver with an optional
 * retry period to poll the display driver before giving up.
 *
 * @return	0 if success, -EBUSY if timeout
 */
#if 1
static int fts_read_panel_extinfo(struct fts_ts_info *info, int wait_seconds)
{
	return 0;
}
#else
static int fts_read_panel_extinfo(struct fts_ts_info *info, int wait_seconds)
{
	const int RETRIES_PER_S = 4;
	const int MS_PER_RETRY = 1000 / RETRIES_PER_S;
	ssize_t len = -EBUSY;
	int retries = (wait_seconds <= 0) ? 0 : wait_seconds * RETRIES_PER_S;
	int ret = 0;

	/* Was extinfo previously retrieved? */
	if (info->extinfo.is_read)
		return 0;

	/* Extinfo should not be retrieved if the driver was unable to identify
	 * the panel via its panelmap. Consider the extinfo zero-length.
	 */
	if (!info->board->panel) {
		info->extinfo.is_read = true;
		info->extinfo.size = 0;
		return 0;
	}

	/* Obtain buffer size */
	len = dsi_panel_read_vendor_extinfo(info->board->panel, NULL, 0);
	if (len == 0) {
		/* No extinfo to be consumed */
		info->extinfo.size = 0;
		info->extinfo.is_read = true;
		return 0;
	} else if (len < 0) {
		ret = len;
		dev_err(info->dev, "%s: dsi_panel_read_vendor_extinfo returned unexpected error = %d.\n",
		       __func__, ret);
		goto error;
	} else {
		info->extinfo.data = kzalloc(len, GFP_KERNEL);
		if (!info->extinfo.data) {
			dev_err(info->dev, "%s: failed to allocate extinfo. len=%d.\n",
			       __func__, len);
			ret = -ENOMEM;
			goto error;
		}
		info->extinfo.size = len;
	}

	/* Read vendor extinfo data */
	do {
		len = dsi_panel_read_vendor_extinfo(info->board->panel,
						    info->extinfo.data,
						    info->extinfo.size);
		if (len == -EBUSY) {
			dev_dbg(info->dev, "%s: sleeping %dms.\n", __func__,
				 MS_PER_RETRY);
			msleep(MS_PER_RETRY);
		} else if (len == info->extinfo.size) {
			info->extinfo.is_read = true;
			dev_dbg(info->dev, "%s: Ultimately waited %d seconds.\n",
				 __func__,
				 wait_seconds - (retries / RETRIES_PER_S));
			return 0;
		} else {
			dev_err(info->dev, "%s: dsi_panel_read_vendor_extinfo returned error = %d\n",
			       __func__, len);
			ret = len;
			goto error;
		}
	} while (--retries > 0);

	/* Time out after retrying for wait_seconds */
	dev_err(info->dev, "%s: Timed out after waiting %d seconds.\n", __func__,
	       wait_seconds);
	ret = -EBUSY;

error:
	kfree(info->extinfo.data);
	info->extinfo.data = NULL;
	info->extinfo.size = 0;
	info->extinfo.is_read = false;
	return ret;
}
#endif

/*
 * Determine the display panel based on the device tree and any extinfo read
 * from the panel.
 *
 * Basic panel detection (e.g., unique part numbers) is performed by polling for
 * connected drm_panels. Next, an override table from the device tree is used to
 * parse the panel's extended info to distinguish between panel varients that
 * require different firmware.
 */
static int fts_identify_panel(struct fts_ts_info *info)
{
	/* Formatting of EXTINFO rows provided in the device trees */
	const int EXTINFO_ROW_ELEMS = 5;
	const int EXTINFO_ROW_SIZE = EXTINFO_ROW_ELEMS * sizeof(u32);

	struct device_node *np = info->dev->of_node;
	u32 panel_index = info->board->initial_panel_index;
	int extinfo_rows;
	u32 filter_panel_index, filter_extinfo_index, filter_extinfo_mask;
	u32 filter_extinfo_value, filter_extinfo_fw;
	const char *name;
	int i;
	int ret = 0;

	if (!info->extinfo.is_read) {
		/* Extinfo was not read. Attempt one read before aborting */
		ret = fts_read_panel_extinfo(info, 0);
		if (ret < 0) {
			dev_err(info->dev, "%s: fts_read_panel_extinfo failed with ret=%d.\n",
				__func__, ret);
			goto get_panel_info_failed;
		}
	}

	/* Read the extinfo override table to determine if there are is any
	 * reason to select a different firmware for the panel.
	 */
	if (of_property_read_bool(np, "st,extinfo_override_table")) {
		extinfo_rows = of_property_count_elems_of_size(
					np, "st,extinfo_override_table",
					EXTINFO_ROW_SIZE);

		for (i = 0; i < extinfo_rows; i++) {
			of_property_read_u32_index(
					np, "st,extinfo_override_table",
					i * EXTINFO_ROW_ELEMS + 0,
					&filter_panel_index);

			of_property_read_u32_index(
					np, "st,extinfo_override_table",
					i * EXTINFO_ROW_ELEMS + 1,
					&filter_extinfo_index);

			of_property_read_u32_index(
					np, "st,extinfo_override_table",
					i * EXTINFO_ROW_ELEMS + 2,
					&filter_extinfo_mask);

			of_property_read_u32_index(
					np, "st,extinfo_override_table",
					i * EXTINFO_ROW_ELEMS + 3,
					&filter_extinfo_value);

			of_property_read_u32_index(
					np, "st,extinfo_override_table",
					i * EXTINFO_ROW_ELEMS + 4,
					&filter_extinfo_fw);

			if (panel_index != filter_panel_index)
				continue;
			else if (filter_extinfo_index >= info->extinfo.size) {
				dev_err(info->dev, "%s: extinfo index is out of bounds (%d >= %d) in row %d of extinfo_override_table.\n",
					__func__, filter_extinfo_index,
					info->extinfo.size, i);
				continue;
			} else if ((info->extinfo.data[filter_extinfo_index] &
				      filter_extinfo_mask) ==
				   filter_extinfo_value) {
				/* Override the panel_index as specified in the
				 * override table.
				 */
				panel_index = filter_extinfo_fw;
				dev_info(info->dev, "%s: Overriding with row=%d, panel_index=%d.\n",
					 __func__, i, panel_index);
				break;
			}
		}
	} else {
		dev_err(info->dev, "%s: of_property_read_bool(np, \"st,extinfo_override_table\") failed.\n",
			__func__);
	}

	//---------------------------------------------------------------------
	// Read firmware name, limits file name, and sensor inversion based on
	// the final panel index. In order to handle the case where the DRM
	// panel was not detected from the list in the device tree, fall back to
	// using predefined FW and limits paths hardcoded into the driver.
	// --------------------------------------------------------------------
get_panel_info_failed:
	name = NULL;
	of_property_read_string_index(np, "st,firmware_names",
				      panel_index, &name);
	if (!name)
		info->board->fw_name = PATH_FILE_FW;
	else
		info->board->fw_name = name;
	dev_info(info->dev, "firmware name = %s\n", info->board->fw_name);

	name = NULL;
	of_property_read_string_index(np, "st,limits_names",
				      panel_index, &name);
	if (!name)
		info->board->limits_name = LIMITS_FILE;
	else
		info->board->limits_name = name;
	dev_info(info->dev, "limits name = %s\n", info->board->limits_name);

	return ret;
}

/**
  *	Implement the fw update and initialization flow of the IC that should
  *	be executed at every boot up. The function perform a fw update of the
  *	IC in case of crc error or a new fw version and then understand if the
  *	IC need to be re-initialized again.
  *
  *	@return  OK if success or an error code which specify the type of error
  *	encountered
  */
static int fts_fw_update(struct fts_ts_info *info)
{
	u8 error_to_search[4] = { EVT_TYPE_ERROR_CRC_CX_HEAD,
				  EVT_TYPE_ERROR_CRC_CX,
				  EVT_TYPE_ERROR_CRC_CX_SUB_HEAD,
				  EVT_TYPE_ERROR_CRC_CX_SUB };
	int ret;
	int error = 0;
	int init_type = NO_INIT;
	int index;
	int prop_len = 0;
	struct device_node *np = info->dev->of_node;

#if defined(PRE_SAVED_METHOD) || defined(COMPUTE_INIT_METHOD)
	/* Not decided yet.
	 * Still need the firmware CX AFE version to decide the final value.
	 */
	int keep_cx = FTS_CX_DEFAULT_MODE;
#else
	int keep_cx = CX_ERASE;
#endif

	/* Read extinfo from display driver. Wait for up to ten seconds if
	 * there is extinfo to read but is not yet available.
	 */
	ret = fts_read_panel_extinfo(info, 10);
	if (ret < 0)
		dev_err(info->dev, "%s: Failed or timed out during read of extinfo. ret=%d\n",
			__func__, ret);

	/* Identify panel given extinfo that may have been received. */
	ret = fts_identify_panel(info);
	if (ret < 0) {
		dev_err(info->dev, "%s: Encountered error while identifying display panel. ret=%d\n",
			__func__, ret);
		goto out;
	}

	dev_info(info->dev, "Fw Auto Update is starting...\n");

	/* Check CRC status */
	ret = fts_crc_check(info);
	if (ret > OK) {
		dev_err(info->dev, "%s: CRC Error or NO FW!\n", __func__);
		info->reflash_fw = 1;
	} else {
		dev_info(info->dev, "%s: NO CRC Error or Impossible to read CRC register!\n",
			__func__);
	}

	if (of_property_read_bool(np, "st,force-pi-cfg-ver-map")) {
		prop_len = of_property_count_u32_elems(np,
			"st,force-pi-cfg-ver-map");
		info->board->force_pi_cfg_ver = devm_kzalloc(info->dev,
			sizeof(u32) * prop_len, GFP_KERNEL);
		if (info->board->force_pi_cfg_ver != NULL) {
			for (index = 0; index < prop_len; index++) {
				of_property_read_u32_index(np,
					"st,force-pi-cfg-ver-map",
					index,
					&info->board->force_pi_cfg_ver[index]);
				dev_info(info->dev, "%s: force PI config version: %04X",
					__func__,
					info->board->force_pi_cfg_ver[index]);
				if(info->systemInfo.u16_cfgVer ==
					info->board->force_pi_cfg_ver[index]) {
					dev_info(info->dev, "%s System config version %04X, do panel init",
					__func__, info->systemInfo.u16_cfgVer);
					init_type = SPECIAL_PANEL_INIT;
				}
			}
		} else {
			dev_err(info->dev, "%s: force_pi_cfg_ver is NULL", __func__);
		}
	} else {
		dev_info(info->dev, "%s: of_property_read_bool(np, \"st,force-pi-cfg-ver-map\") failed.\n",
		__func__);
	}

	if (info->board->auto_fw_update) {
		ret = flashProcedure(info, info->board->fw_name, info->reflash_fw,
				     keep_cx);
		if ((ret & 0xF000000F) == ERROR_FILE_NOT_FOUND) {
			dev_err(info->dev, "%s: firmware file not found. Bypassing update.\n",
				__func__);
			ret = 0;
			goto out;
		} else if ((ret & 0xFF000000) == ERROR_FLASH_PROCEDURE) {
			dev_err(info->dev, "%s: firmware update failed; retrying. ERROR %08X\n",
				__func__, ret);
			/* Power cycle the touch IC */
			fts_chip_powercycle(info);
			ret = flashProcedure(info, info->board->fw_name,
					     info->reflash_fw, keep_cx);
			if ((ret & 0xFF000000) == ERROR_FLASH_PROCEDURE) {
				dev_err(info->dev, "%s: firmware update failed again! ERROR %08X\n",
					__func__, ret);
				dev_err(info->dev, "Fw Auto Update Failed!\n");
			}
		}
		info->reflash_fw = 0;
		info->fw_no_response = false;
	}

	dev_info(info->dev, "%s: Verifying if CX CRC Error...\n", __func__);
	ret = fts_system_reset(info);
	if (ret >= OK) {
		ret = pollForErrorType(info, error_to_search, 4);
		if (ret < OK) {
			dev_info(info->dev, "%s: No Cx CRC Error Found!\n", __func__);
			dev_info(info->dev, "%s: Verifying if Panel CRC Error...\n",
				__func__);
			error_to_search[0] = EVT_TYPE_ERROR_CRC_PANEL_HEAD;
			error_to_search[1] = EVT_TYPE_ERROR_CRC_PANEL;
			ret = pollForErrorType(info, error_to_search, 2);
			if (ret < OK) {
				dev_info(info->dev, "%s: No Panel CRC Error Found!\n",
					__func__);
			} else {
				dev_err(info->dev, "%s: Panel CRC Error FOUND! CRC ERROR = %02X\n",
					__func__, ret);
				init_type = SPECIAL_PANEL_INIT;
			}
		} else {
			dev_err(info->dev, "%s: Cx CRC Error FOUND! CRC ERROR = %02X\n",
				__func__, ret);

			/** This path of the code is used only in case there is
			  * a CRC error in code or config which not allow the fw
			  * to compute the CRC in the CX before
			  */
#ifndef COMPUTE_INIT_METHOD
			dev_info(info->dev, "%s: Try to recovery with CX in fw file...\n",
				__func__);
			ret = flashProcedure(info, info->board->fw_name, CRC_CX, 0);
			dev_info(info->dev, "%s: Refresh panel init data", __func__);
#else
			dev_info(info->dev, "%s: Select Full Panel Init...\n", __func__);
			init_type = SPECIAL_FULL_PANEL_INIT;
#endif
		}
	} else {
		/* Skip initialization because the real state is unknown */
		dev_err(info->dev, "%s: Error while executing system reset! ERROR %08X\n",
			__func__, ret);
	}

	if (init_type != SPECIAL_FULL_PANEL_INIT) {
#if defined(PRE_SAVED_METHOD) || defined(COMPUTE_INIT_METHOD)
		if ((info->systemInfo.u8_cfgAfeVer !=
			info->systemInfo.u8_cxAfeVer)
#ifdef COMPUTE_INIT_METHOD
			|| ((info->systemInfo.u8_mpFlag != MP_FLAG_BOOT) &&
				(info->systemInfo.u8_mpFlag != MP_FLAG_FACTORY) &&
				(info->systemInfo.u8_mpFlag != MP_FLAG_NEED_FPI) &&
				/* If skip_fpi_for_unset_mpflag is not set,
				 * bypass MP_FLAG_UNSET check.
				 * If skip_fpi_for_unset_mpflag is set,
				 * then check if mpFlag != MP_FLAG_UNSET.
				 */
				((info->board->skip_fpi_for_unset_mpflag == false) ||
				 (info->systemInfo.u8_mpFlag != MP_FLAG_UNSET))
				)
#endif
			) {
			init_type = SPECIAL_FULL_PANEL_INIT;
			dev_err(info->dev,
				"%s: Different CX AFE Ver: %02X != %02X or MpFlag = %02X... Execute FULL Panel Init!\n",
				__func__, info->systemInfo.u8_cfgAfeVer,
				info->systemInfo.u8_cxAfeVer,
				info->systemInfo.u8_mpFlag);
		} else
#endif
		if (info->systemInfo.u8_cfgAfeVer !=
			info->systemInfo.u8_panelCfgAfeVer) {
			init_type = SPECIAL_PANEL_INIT;
			dev_err(info->dev, "%s: Different Panel AFE Ver: %02X != %02X... Execute Panel Init!\n",
				__func__, info->systemInfo.u8_cfgAfeVer,
				info->systemInfo.u8_panelCfgAfeVer);
		}
	}

out:

	if (init_type != NO_INIT) { /* initialization status not correct or
				     * after FW complete update, do
				     * initialization.
				     */
		error = fts_chip_initialization(info, init_type);
		if (error < OK) {
			dev_err(info->dev, "%s: Cannot initialize the chip ERROR %08X\n",
				__func__, error);
		}

		/* Reset after initialization */
		ret = fts_system_reset(info);
		if (ret < OK) {
			dev_err(info->dev, "%s: Reset failed, ERROR %08X\n", __func__,
				ret);
		}
	}

	error = fts_init_sensing(info);
	if (error < OK) {
		dev_err(info->dev, "Cannot initialize the hardware device ERROR %08X\n",
			error);
	}

	dev_err(info->dev, "Fw Update Finished! error = %08X\n", error);
	return error;
}

/**
  *	Function called by the delayed workthread executed after the probe in
  * order to perform the fw update flow
  *	@see  fts_fw_update()
  */
static void fts_fw_update_auto(struct work_struct *work)
{
	struct delayed_work *fwu_work = container_of(work, struct delayed_work,
						     work);
	struct fts_ts_info *info = container_of(fwu_work, struct fts_ts_info,
						fwu_work);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_pm_wake_lock(info->gti, GTI_PM_WAKELOCK_TYPE_FW_UPDATE, false);
#endif
	fts_fw_update(info);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_pm_wake_unlock_nosync(info->gti, GTI_PM_WAKELOCK_TYPE_FW_UPDATE);
#endif
}

/**
 *  Save the golden MS raw data to the touch IC if firmware has separated it
 *  from the PI process.
 */
int save_golden_ms_raw(struct fts_ts_info *info)
{
	u8 cmd[3] = {0xC0, 0x01, 0x01};
	int ret = 0;

	ret = fts_write(info, cmd, 3);
	if (ret < 0)
		dev_err(info->dev, "Fail to save golden MS raw, ret = %d", ret);
	else {
		mdelay(150);	/* Time to secure the saving process (90 ms) */
		dev_info(info->dev, "Golden MS raw is saved!");
	}
	return ret;
}

/* TODO: define if need to do the full mp at the boot */
/**
  *	Execute the initialization of the IC (supporting a retry mechanism),
  * checking also the resulting data
  *	@see  production_test_main()
  */
static int fts_chip_initialization(struct fts_ts_info *info, int init_type)
{
	int ret2 = 0;
	int retry;
	int initretrycnt = 0;
#ifdef COMPUTE_INIT_METHOD
	const char *limits_file = info->board->limits_name;
#endif

	/* initialization error, retry initialization */
	for (retry = 0; retry < RETRY_INIT_BOOT; retry++) {
#ifndef COMPUTE_INIT_METHOD
		ret2 = production_test_initialization(info, init_type);
		if (ret2 == OK &&
		    info->board->separate_save_golden_ms_raw_cmd)
			save_golden_ms_raw(info);
#else
		ret2 = production_test_main(info, limits_file, 1, init_type,
					    MP_FLAG_BOOT);
#endif
		if (ret2 == OK)
			break;
		initretrycnt++;
		dev_err(info->dev, "initialization cycle count = %04d - ERROR %08X\n",
			initretrycnt, ret2);
		fts_chip_powercycle(info);
	}

	if (ret2 < OK)	/* initialization error */
		dev_err(info->dev, "fts initialization failed %d times\n",
			RETRY_INIT_BOOT);

	return ret2;
}


static irqreturn_t fts_isr(int irq, void *handle)
{
	struct fts_ts_info *info = handle;

	info->timestamp = ktime_get();

	return IRQ_WAKE_THREAD;
}

/**
  * Initialize the dispatch table with the event handlers for any possible event
  * ID
  * Set IRQ pin behavior (level triggered low)
  * Register top half interrupt handler function.
  * @see fts_interrupt_handler()
  */
static int fts_interrupt_install(struct fts_ts_info *info)
{
	int i, error = 0;

	info->event_dispatch_table = kzalloc(sizeof(event_dispatch_handler_t) *
					     NUM_EVT_ID, GFP_KERNEL);

	if (!info->event_dispatch_table) {
		dev_err(info->dev, "OOM allocating event dispatch table\n");
		return -ENOMEM;
	}

	for (i = 0; i < NUM_EVT_ID; i++)
		info->event_dispatch_table[i] = fts_nop_event_handler;

	install_handler(info, ENTER_POINT, enter_pointer);
	install_handler(info, LEAVE_POINT, leave_pointer);
	install_handler(info, MOTION_POINT, motion_pointer);
	install_handler(info, ERROR, error);
	install_handler(info, CONTROLLER_READY, controller_ready);
	install_handler(info, STATUS_UPDATE, status);
	install_handler(info, USER_REPORT, user_report);

	error = goog_request_threaded_irq(info->gti, info->client->irq, fts_isr,
			fts_interrupt_handler, IRQF_ONESHOT | IRQF_TRIGGER_LOW,
			FTS_TS_DRV_NAME, info);
	info->irq_enabled = true;

	if (error) {
		dev_err(info->dev, "Request irq failed\n");
		kfree(info->event_dispatch_table);
		goto exit;
	}

	/* disable interrupts in any case */
	error = fts_enableInterrupt(info, false);

exit:
	return error;
}

/**
  *	Clean the dispatch table and the free the IRQ.
  *	This function is called when the driver need to be removed
  */
static void fts_interrupt_uninstall(struct fts_ts_info *info)
{
	fts_enableInterrupt(info, false);

	kfree(info->event_dispatch_table);

	free_irq(info->client->irq, info);
}

/**@}*/

/**
  * This function try to attempt to communicate with the IC for the first time
  * during the boot up process in order to read the necessary info for the
  * following stages.
  * The function execute a system reset, read fundamental info (system info)
  * @return OK if success or an error code which specify the type of error
  */
static int fts_init(struct fts_ts_info *info)
{
	int error;

	error = fts_system_reset(info);
	/*
	 * If it's not bus error, it's possible that there is no FW or FW
	 * broken so we continue to flash.
	 */
	if (error < OK && isBusError(error)) {
		dev_err(info->dev, "Cannot reset the device! ERROR %08X\n", error);
		return error;
	}

	if (error == (ERROR_TIMEOUT | ERROR_SYSTEM_RESET_FAIL)) {
		info->fw_no_response = true;
		dev_err(info->dev, "Setting default Sys INFO!\n");
		error = defaultSysInfo(info, 0);
	} else {
		error = readSysInfo(info, 0);	/* system reset OK */
		if (error < OK) {
			if (!isBusError(error))
				error = OK;
			dev_err(info->dev, "Cannot read Sys Info! ERROR %08X\n",
				error);
		}
	}

	return error;
}

/**
  * Execute a power cycle in the IC, toggling the power lines (AVDD and DVDD)
  * @param info pointer to fts_ts_info struct which contain information of the
  * regulators
  * @return 0 if success or another value if fail
  */
int fts_chip_powercycle(struct fts_ts_info *info)
{
	int error = 0;

	dev_info(info->dev, "%s: Power Cycle Starting...\n", __func__);
	dev_info(info->dev, "%s: Disabling IRQ...\n", __func__);
	/** if IRQ pin is short with DVDD a call to the ISR will triggered when
	  * the regulator is turned off if IRQ not disabled */
	fts_enableInterrupt(info, false);

	if (info->vdd_reg) {
		error = regulator_disable(info->vdd_reg);
		if (error < 0)
			dev_err(info->dev, "%s: Failed to disable DVDD regulator\n",
				__func__);
	}

	if (info->avdd_reg) {
		error = regulator_disable(info->avdd_reg);
		if (error < 0)
			dev_err(info->dev, "%s: Failed to disable AVDD regulator\n",
				__func__);
	}

	if (info->board->reset_gpio != GPIO_NOT_DEFINED)
		gpio_set_value(info->board->reset_gpio, 0);
	else
		mdelay(300);

	/* in FTI power up first the digital and then the analog */
	if (info->vdd_reg) {
		error = regulator_enable(info->vdd_reg);
		if (error < 0)
			dev_err(info->dev, "%s: Failed to enable DVDD regulator\n",
				__func__);
	}

	mdelay(1);

	if (info->avdd_reg) {
		error = regulator_enable(info->avdd_reg);
		if (error < 0)
			dev_err(info->dev, "%s: Failed to enable AVDD regulator\n",
				__func__);
	}

	mdelay(5);	/* time needed by the regulators for reaching the regime
			 * values */


	if (info->board->reset_gpio != GPIO_NOT_DEFINED) {
		mdelay(10);	/* time to wait before bring up the reset
				  * gpio after the power up of the regulators */
		gpio_set_value(info->board->reset_gpio, 1);
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* The GTI handles to release all touches */
	clear_touch_flags(info);
#else
	release_all_touches(info);
#endif

	dev_info(info->dev, "%s: Power Cycle Finished! ERROR CODE = %08x\n",
		__func__, error);
	setSystemResetedUp(info, 1);
	setSystemResetedDown(info, 1);
	return error;
}


/**
  * Complete the boot up process, initializing the sensing of the IC according
  * to the current setting chosen by the host
  * Register the notifier for the suspend/resume actions and the event handler
  * @return OK if success or an error code which specify the type of error
  */
static int fts_init_sensing(struct fts_ts_info *info)
{
	int error = 0;

	error |= fts_interrupt_install(info);	  /* register event handler */
	error |= fts_mode_handler(info, 0);	  /* enable the features and
						   * sensing */
	error |= fts_enableInterrupt(info, true); /* enable the interrupt */

	if (error < OK)
		dev_err(info->dev, "%s Init after Probe error (ERROR = %08X)\n",
			__func__, error);

	return error;
}

/* TODO: change this function according with the needs of customer in terms
  * of feature to enable/disable */

/**
  * @ingroup mode_section
  * @{
  */
/**
  * The function handle the switching of the mode in the IC enabling/disabling
  * the sensing and the features set from the host
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param force if 1, the enabling/disabling command will be send even
  * if the feature was already enabled/disabled otherwise it will judge if
  * the feature changed status or the IC had a system reset
  * @return OK if success or an error code which specify the type of error
  */
static int fts_mode_handler(struct fts_ts_info *info, int force)
{
	int res = OK;
	int ret = OK;
	u8 settings[4] = { 0 };

	/* disable irq wake because resuming from gesture mode */
	if (IS_POWER_MODE(info->mode, SCAN_MODE_LOW_POWER) &&
	    (info->resume_bit == 1))
		disable_irq_wake(info->client->irq);

	info->mode = MODE_NOTHING;	/* initialize the mode to nothing
					  * in order to be updated depending
					  * on the features enabled */

	dev_dbg(info->dev, "%s: Mode Handler starting...\n", __func__);
	switch (info->resume_bit) {
	case 0:	/* screen down */
		dev_dbg(info->dev, "%s: Screen OFF...\n", __func__);
		/* do sense off in order to avoid the flooding of the fifo with
		  * touch events if someone is touching the panel during suspend
		  **/
		dev_info(info->dev, "%s: Sense OFF!\n", __func__);
		/* for speed reason (no need to check echo in this case and
		  * interrupt can be enabled) */
		ret = setScanMode(info, SCAN_MODE_ACTIVE, 0x00);
		res |= ret;	/* to avoid warning unsused ret variable when a
				  * ll the features are disabled */

#ifdef GESTURE_MODE
		if (info->gesture_enabled == 1) {
			dev_info(info->dev, "%s: enter in gesture mode !\n",
				 __func__);
			res = enterGestureMode(info, isSystemResettedDown(info));
			if (res >= OK) {
				enable_irq_wake(info->client->irq);
				fromIDtoMask(FEAT_SEL_GESTURE,
					     (u8 *)&info->mode,
					     sizeof(info->mode));
				MODE_LOW_POWER(info->mode, 0);
			} else
				dev_err(info->dev, "%s: enterGestureMode failed! ERROR %08X recovery in senseOff...\n",
					__func__, res);
		}
#endif

		setSystemResetedDown(info, 0);
		fts_system_reset(info);
		flushFIFO(info);
		break;

	case 1:	/* screen up */
		dev_dbg(info->dev, "%s: Screen ON...\n", __func__);

/* Set the features from GTI if GTI is enabled. */
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_RESET, NULL);
#else
#ifdef GLOVE_MODE
		if ((info->glove_enabled == FEAT_ENABLE &&
		     isSystemResettedUp(info)) || force == 1) {
			dev_info(info->dev, "%s: Glove Mode setting...\n", __func__);
			settings[0] = info->glove_enabled;
			/* required to satisfy also the disable case */
			ret = setFeatures(info, FEAT_SEL_GLOVE, settings, 1);
			if (ret < OK)
				dev_err(info->dev, "%s: error during setting GLOVE_MODE! ERROR %08X\n",
					__func__, ret);
			res |= ret;

			if (ret >= OK && info->glove_enabled == FEAT_ENABLE) {
				fromIDtoMask(FEAT_SEL_GLOVE, (u8 *)&info->mode,
					     sizeof(info->mode));
				dev_info(info->dev, "%s: GLOVE_MODE Enabled!\n", __func__);
			} else
				dev_info(info->dev, "%s: GLOVE_MODE Disabled!\n", __func__);
		}

#endif
#ifdef COVER_MODE
		if ((info->cover_enabled == FEAT_ENABLE &&
		     isSystemResettedUp(info)) || force == 1) {
			dev_info(info->dev, "%s: Cover Mode setting...\n", __func__);
			settings[0] = info->cover_enabled;
			ret = setFeatures(info, FEAT_SEL_COVER, settings, 1);
			if (ret < OK)
				dev_err(info->dev, "%s: error during setting COVER_MODE! ERROR %08X\n",
					__func__, ret);
			res |= ret;

			if (ret >= OK && info->cover_enabled == FEAT_ENABLE) {
				fromIDtoMask(FEAT_SEL_COVER, (u8 *)&info->mode,
					     sizeof(info->mode));
				dev_info(info->dev, "%s: COVER_MODE Enabled!\n", __func__);
			} else
				dev_info(info->dev, "%s: COVER_MODE Disabled!\n", __func__);
		}
#endif
#ifdef CHARGER_MODE
		if ((info->charger_enabled > 0 && isSystemResettedUp(info)) ||
		    force == 1) {
			dev_info(info->dev, "%s: Charger Mode setting...\n", __func__);

			settings[0] = info->charger_enabled;
			ret = setFeatures(info, FEAT_SEL_CHARGER, settings, 1);
			if (ret < OK)
				dev_err(info->dev, "%s: error during setting CHARGER_MODE! ERROR %08X\n",
					__func__, ret);
			res |= ret;

			if (ret >= OK && info->charger_enabled == FEAT_ENABLE) {
				fromIDtoMask(FEAT_SEL_CHARGER,
					     (u8 *)&info->mode,
					     sizeof(info->mode));
				dev_info(info->dev, "%s: CHARGER_MODE Enabled!\n",
					__func__);
			} else
				dev_info(info->dev, "%s: CHARGER_MODE Disabled!\n",
					__func__);
		}
#endif
#ifdef GRIP_MODE
		if ((info->grip_enabled == FEAT_ENABLE &&
		     isSystemResettedUp(info)) || force == 1) {
			dev_info(info->dev, "%s: Grip Mode setting...\n", __func__);
			settings[0] = info->grip_enabled;
			ret = setFeatures(info, FEAT_SEL_GRIP, settings, 1);
			if (ret < OK)
				dev_err(info->dev, "%s: error during setting GRIP_MODE! ERROR %08X\n",
					__func__, ret);
			res |= ret;

			if (ret >= OK && info->grip_enabled == FEAT_ENABLE) {
				fromIDtoMask(FEAT_SEL_GRIP, (u8 *)&info->mode,
					     sizeof(info->mode));
				dev_info(info->dev, "%s: GRIP_MODE Enabled!\n", __func__);
			} else
				dev_info(info->dev, "%s: GRIP_MODE Disabled!\n", __func__);
		}
#endif
#endif /* IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */

		/* If some selective scan want to be enabled can be done
		 * an or of the following options
		 */
		/* settings[0] = ACTIVE_MULTI_TOUCH | ACTIVE_KEY |
		 *		ACTIVE_HOVER | ACTIVE_PROXIMITY |
		 *		ACTIVE_FORCE;
		 */
		settings[0] = 0xFF;	/* enable all the possible scans mode
					 * supported by the config */
		dev_info(info->dev, "%s: Sense ON!\n", __func__);
		res |= setScanMode(info, SCAN_MODE_ACTIVE, settings[0]);
		info->mode |= (SCAN_MODE_ACTIVE << 24);
		MODE_ACTIVE(info->mode, settings[0]);

		setSystemResetedUp(info, 0);
		break;

	default:
		dev_err(info->dev, "%s: invalid resume_bit value = %d! ERROR %08X\n",
			__func__, info->resume_bit, ERROR_OP_NOT_ALLOW);
		res = ERROR_OP_NOT_ALLOW;
	}

	dev_dbg(info->dev, "%s: Mode Handler finished! res = %08X mode = %08X\n",
		__func__, res, info->mode);
	return res;
}

/* Report a finger down event on the long press gesture area then immediately
 * report a cancel event(MT_TOOL_PALM).
 */
static void report_cancel_event(struct fts_ts_info *info)
{
	dev_info(info->dev, "%s\n", __func__);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_lock(info->gti);
#else
	mutex_lock(&info->input_report_mutex);
#endif

	/* Finger down. */
	input_mt_slot(info->input_dev, 0);
	input_report_key(info->input_dev, BTN_TOUCH, 1);
	input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, 1);
	input_report_abs(info->input_dev, ABS_MT_POSITION_X, info->board->udfps_x);
	input_report_abs(info->input_dev, ABS_MT_POSITION_Y, info->board->udfps_y);
	input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, 200);
	input_report_abs(info->input_dev, ABS_MT_TOUCH_MINOR, 200);
#ifndef SKIP_PRESSURE
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, 1);
#endif
	input_report_abs(info->input_dev, ABS_MT_ORIENTATION, 0);
	input_sync(info->input_dev);

	/* Report MT_TOOL_PALM for canceling the touch event. */
	input_mt_slot(info->input_dev, 0);
	input_report_key(info->input_dev, BTN_TOUCH, 1);
	input_mt_report_slot_state(info->input_dev, MT_TOOL_PALM, 1);
	input_sync(info->input_dev);

	/* Release touches. */
	input_mt_slot(info->input_dev, 0);
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, 0);
	input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, 0);
	input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, -1);
	input_report_key(info->input_dev, BTN_TOUCH, 0);
	input_sync(info->input_dev);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_unlock(info->gti);
#else
	mutex_unlock(&info->input_report_mutex);
#endif
}

/* Check the finger status on long press gesture area. */
static void check_finger_status(struct fts_ts_info *info)
{
	u8 command[3] = { FTS_CMD_SYSTEM, SYS_CMD_LOAD_DATA, LOAD_DEBUG_INFO };
	u8 data[DEBUG_INFO_SIZE] = { 0 };
	ktime_t ktime_start = ktime_get();
	int retry = 0;
	int ret;

	dev_info(info->dev, "%s\n", __func__);

	while (ktime_ms_delta(ktime_get(), ktime_start) < 500) {
		retry++;
		ret = fts_write(info, command, ARRAY_SIZE(command));
		if (ret < OK) {
			dev_err(info->dev,
				"%s: error while writing the sys cmd ERROR %08X\n",
				__func__, ret);
			msleep(10);
			continue;
		}

		ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16,
					ADDR_FRAMEBUFFER, data, DEBUG_INFO_SIZE,
					DUMMY_FRAMEBUFFER);
		if (ret < OK) {
			dev_err(info->dev,
				"%s: error while write/read cmd ERROR %08X\n",
				__func__, ret);
			msleep(10);
			continue;
		}

		/* Check header. */
		if (data[0] != HEADER_SIGNATURE || data[1] != LOAD_DEBUG_INFO) {
			dev_err(info->dev,
				"%s: Fail to get debug info, header = %#x %#x, read next frame.\n",
				__func__, data[0], data[1]);
			msleep(10);
			continue;
		}

		/* Check scan mode (data[4]).
		0x05: low power detect mode.
		0x06: low power active mode. */
		if (data[4] != DEBUG_INFO_LP_DETECT && data[4] != DEBUG_INFO_LP_ACTIVE)
			return;

		/* Check finger count (data[60]). */
		if (data[60] == 0) {
			/* Report cancel event when finger count is 0. */
			report_cancel_event(info);
			break;
		} else if (data[60] > 1) {
			/* Skip the process when the count is abnormal. */
			break;
		}
		msleep(10);
	}
}

/**
  * Resume function which perform a system reset, clean all the touches
  * from the linux input system and prepare the ground for enabling the sensing
  */
static void fts_resume(struct fts_ts_info *info)
{
	if (!info->sensor_sleep) return;

	fts_pinctrl_setup(info, true);
	if (goog_get_lptw_triggered(info->gti) == true)
		check_finger_status(info);
	fts_system_reset(info);
	info->resume_bit = 1;
	fts_mode_handler(info, 0);
	fts_enableInterrupt(info, true);
	info->sensor_sleep = false;
}

/**
  * Suspend function which clean all the touches from Linux input system
  * and prepare the ground to disabling the sensing or enter in gesture mode
  */
static void fts_suspend(struct fts_ts_info *info)
{
	if (info->sensor_sleep) return;

	info->sensor_sleep = true;
	fts_enableInterrupt(info, false);
	info->resume_bit = 0;
	fts_mode_handler(info, 0);
	fts_pinctrl_setup(info, false);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* The GTI handles to release all touches */
	clear_touch_flags(info);
#else
	release_all_touches(info);
#endif
}

/**
  * From the name of the power regulator get/put the actual regulator structs
  * (copying their references into fts_ts_info variable)
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param get if 1, the regulators are get otherwise they are put (released)
  * back to the system
  * @return OK if success or an error code which specify the type of error
  */
static int fts_get_reg(struct fts_ts_info *info, bool get)
{
	int retval;

	if (!get) {
		retval = 0;
		goto regulator_put;
	}

	if (of_property_read_bool(info->dev->of_node, "vdd-supply")) {
		info->vdd_reg = regulator_get(info->dev, "vdd");
		if (IS_ERR(info->vdd_reg)) {
			dev_err(info->dev, "%s: Failed to get power regulator\n", __func__);
			retval = -EPROBE_DEFER;
			goto regulator_put;
		}
	}

	if (of_property_read_bool(info->dev->of_node, "avdd-supply")) {
		info->avdd_reg = regulator_get(info->dev, "avdd");
		if (IS_ERR(info->avdd_reg)) {
			dev_err(info->dev, "%s: Failed to get bus pullup regulator\n",
			__func__);
			retval = -EPROBE_DEFER;
			goto regulator_put;
		}
	}

	return OK;

regulator_put:
	if (info->vdd_reg) {
		regulator_put(info->vdd_reg);
		info->vdd_reg = NULL;
	}

	if (info->avdd_reg) {
		regulator_put(info->avdd_reg);
		info->avdd_reg = NULL;
	}

	return retval;
}


/**
  * Enable or disable the power regulators
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param enable if 1, the power regulators are turned on otherwise they are
  * turned off
  * @return OK if success or an error code which specify the type of error
  */
static int fts_enable_reg(struct fts_ts_info *info, bool enable)
{
	int retval;

	if (!enable) {
		retval = 0;
		goto disable_pwr_reg;
	}

	if (info->vdd_reg) {
		retval = regulator_enable(info->vdd_reg);
		if (retval < 0) {
			dev_err(info->dev, "%s: Failed to enable bus regulator\n",
				__func__);
			goto exit;
		}
	}

	if (info->avdd_reg) {
		retval = regulator_enable(info->avdd_reg);
		if (retval < 0) {
			dev_err(info->dev, "%s: Failed to enable power regulator\n",
				__func__);
			goto disable_bus_reg;
		}
	}

	return OK;

disable_pwr_reg:
	if (info->avdd_reg)
		regulator_disable(info->avdd_reg);

disable_bus_reg:
	if (info->vdd_reg)
		regulator_disable(info->vdd_reg);

exit:
	return retval;
}

/**
  * Configure a GPIO according to the parameters
  * @param gpio gpio number
  * @param config if true, the gpio is set up otherwise it is free
  * @param dir direction of the gpio, 0 = in, 1 = out
  * @param state initial value (if the direction is in, this parameter is
  * ignored)
  * return error code
  */
static int fts_gpio_setup(int gpio, bool config, int dir, int state)
{
	int retval = 0;
	unsigned char buf[16];

	if (config) {
		scnprintf(buf, sizeof(buf), "fts_gpio_%u\n", gpio);

		retval = gpio_request(gpio, buf);
		if (retval) {
			pr_err("%s: Failed to get gpio %d (code: %d)",
				__func__, gpio, retval);
			return retval;
		}

		if (dir == 0)
			retval = gpio_direction_input(gpio);
		else
			retval = gpio_direction_output(gpio, state);
		if (retval) {
			pr_err("%s: Failed to set gpio %d direction",
				__func__, gpio);
			return retval;
		}
	} else
		gpio_free(gpio);

	return retval;
}

/**
  * Setup the IRQ and RESET (if present) gpios.
  * If the Reset Gpio is present it will perform a cycle HIGH-LOW-HIGH in order
  * to assure that the IC has been reset properly
  */
static int fts_set_gpio(struct fts_ts_info *info)
{
	int retval;
	struct fts_hw_platform_data *bdata = info->board;

	retval = fts_gpio_setup(bdata->irq_gpio, true, 0, 0);
	if (retval < 0) {
		dev_err(info->dev, "%s: Failed to configure irq GPIO\n", __func__);
		goto err_gpio_irq;
	}

	if (bdata->reset_gpio >= 0) {
		retval = fts_gpio_setup(bdata->reset_gpio, true, 1, 0);
		if (retval < 0) {
			dev_err(info->dev, "%s: Failed to configure reset GPIO\n",
				__func__);
			goto err_gpio_reset;
		}
	}
	if (bdata->reset_gpio >= 0) {
		gpio_set_value(bdata->reset_gpio, 0);
		mdelay(10);
		gpio_set_value(bdata->reset_gpio, 1);
	}

	return OK;

err_gpio_reset:
	fts_gpio_setup(bdata->irq_gpio, false, 0, 0);
	bdata->reset_gpio = GPIO_NOT_DEFINED;
err_gpio_irq:
	return retval;
}

/** Set pin state to active or suspend
  * @param active 1 for active while 0 for suspend
  */
static void fts_pinctrl_setup(struct fts_ts_info *info, bool active)
{
	int retval;

	if (info->ts_pinctrl) {
		/*
		 * Pinctrl setup is optional.
		 * If pinctrl is found, set pins to active/suspend state.
		 * Otherwise, go on without showing error messages.
		 */
		retval = pinctrl_select_state(info->ts_pinctrl, active ?
				info->pinctrl_state_active :
				info->pinctrl_state_suspend);
		if (retval < 0) {
			dev_err(info->dev, "Failed to select %s pinstate %d\n", active ?
				PINCTRL_STATE_ACTIVE : PINCTRL_STATE_SUSPEND,
				retval);
		}
	} else {
		dev_warn(info->dev, "ts_pinctrl is NULL\n");
	}
}

/**
  * Get/put the touch pinctrl from the specific names. If pinctrl is used, the
  * active and suspend pin control names and states are necessary.
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param get if 1, the pinctrl is get otherwise it is put (released) back to
  * the system
  * @return OK if success or an error code which specify the type of error
  */
static int fts_pinctrl_get(struct fts_ts_info *info, bool get)
{
	int retval;

	if (!get) {
		retval = 0;
		goto pinctrl_put;
	}

	info->ts_pinctrl = devm_pinctrl_get(info->dev);
	if (IS_ERR_OR_NULL(info->ts_pinctrl)) {
		retval = PTR_ERR(info->ts_pinctrl);
		dev_info(info->dev, "Target does not use pinctrl %d\n", retval);
		goto err_pinctrl_get;
	}

	info->pinctrl_state_active
		= pinctrl_lookup_state(info->ts_pinctrl, PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(info->pinctrl_state_active)) {
		retval = PTR_ERR(info->pinctrl_state_active);
		dev_err(info->dev, "Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_ACTIVE, retval);
		goto err_pinctrl_lookup;
	}

	info->pinctrl_state_suspend
		= pinctrl_lookup_state(info->ts_pinctrl, PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(info->pinctrl_state_suspend)) {
		retval = PTR_ERR(info->pinctrl_state_suspend);
		dev_err(info->dev, "Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_SUSPEND, retval);
		goto err_pinctrl_lookup;
	}

	info->pinctrl_state_release
		= pinctrl_lookup_state(info->ts_pinctrl, PINCTRL_STATE_RELEASE);
	if (IS_ERR_OR_NULL(info->pinctrl_state_release)) {
		retval = PTR_ERR(info->pinctrl_state_release);
		dev_warn(info->dev, "Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_RELEASE, retval);
	}

	return OK;

err_pinctrl_lookup:
	devm_pinctrl_put(info->ts_pinctrl);
err_pinctrl_get:
	info->ts_pinctrl = NULL;
pinctrl_put:
	if (info->ts_pinctrl) {
		if (IS_ERR_OR_NULL(info->pinctrl_state_release)) {
			devm_pinctrl_put(info->ts_pinctrl);
			info->ts_pinctrl = NULL;
		} else {
			if (pinctrl_select_state(
					info->ts_pinctrl,
					info->pinctrl_state_release))
				dev_warn(info->dev, "Failed to select release pinstate\n");
		}
	}
	return retval;
}

/**
  * Retrieve and parse the hw information from the device tree node defined in
  * the system.
  * the most important information to obtain are: IRQ and RESET gpio numbers,
  * power regulator names
  * In the device file node is possible to define additional optional
  * information
  * that can be parsed here.
  */
static int parse_dt(struct device *dev, struct fts_hw_platform_data *bdata)
{
	int retval;
	int index;
	struct of_phandle_args panelmap;
	struct drm_panel *panel = NULL;
	struct device_node *np = dev->of_node;
	u32 coords[2];

	if (of_property_read_u8_array(np, "st,dchip_id", bdata->dchip_id, 2)) {
		dev_err(dev, "st,dchip_id not found. Use default DCHIP_ID <0x%02X 0x%02X>.\n",
		       DCHIP_ID_0, DCHIP_ID_1);
		bdata->dchip_id[0] = DCHIP_ID_0;
		bdata->dchip_id[1] = DCHIP_ID_1;
		bdata->flash_chunk = FLASH_CHUNK;
	} else if (bdata->dchip_id[0] == ALIX_DCHIP_ID_0 &&
		   bdata->dchip_id[1] == ALIX_DCHIP_ID_1)
		bdata->flash_chunk = (32 * 1024);
	else
		bdata->flash_chunk = (64 * 1024);
	dev_info(dev, "Flash chunk = %d\n", bdata->flash_chunk);

	if (of_property_read_bool(np, "st,panel_map")) {
		for (index = 0 ;; index++) {
			retval = of_parse_phandle_with_fixed_args(np,
								  "st,panel_map",
								  1,
								  index,
								  &panelmap);
			if (retval)
				return -EPROBE_DEFER;
			panel = of_drm_find_panel(panelmap.np);
			of_node_put(panelmap.np);
			if (!IS_ERR_OR_NULL(panel)) {
				bdata->panel = panel;
				bdata->initial_panel_index = panelmap.args[0];
				break;
			}
		}
	}

	bdata->irq_gpio = of_get_named_gpio_flags(np, "st,irq-gpio", 0, NULL);
	dev_info(dev, "irq_gpio = %d\n", bdata->irq_gpio);

	if (of_property_read_bool(np, "st,reset-gpio")) {
		bdata->reset_gpio = of_get_named_gpio_flags(np,
							    "st,reset-gpio", 0,
							    NULL);
		dev_info(dev, "reset_gpio = %d\n", bdata->reset_gpio);
	} else
		bdata->reset_gpio = GPIO_NOT_DEFINED;

	bdata->auto_fw_update = true;
	if (of_property_read_bool(np, "st,disable-auto-fw-update")) {
		bdata->auto_fw_update = false;
		dev_info(dev, "Automatic firmware update disabled\n");
	}

	bdata->separate_save_golden_ms_raw_cmd = false;
	if (of_property_read_bool(np, "st,save-golden-ms-raw")) {
		bdata->separate_save_golden_ms_raw_cmd = true;
		dev_info(dev, "Separate \"Save Golden MS Raw\" command from PI command.\n");
	}

	bdata->skip_fpi_for_unset_mpflag = false;
	if (of_property_read_bool(np, "st,skip-fpi-for-unset-mpflag")) {
		bdata->skip_fpi_for_unset_mpflag = true;
		dev_info(dev, "Skip boot-time FPI for unset MP flag.\n");
	}

	if (of_property_read_u32_array(np, "st,max-coords", coords, 2)) {
		dev_err(dev, "st,max-coords not found, using 1440x2560\n");
		coords[0] = 1440 - 1;
		coords[1] = 2560 - 1;
	}
	bdata->x_axis_max = coords[0];
	bdata->y_axis_max = coords[1];

	if (of_property_read_u32_array(np, "st,udfps-coords", coords, 2) == 0) {
		bdata->udfps_x = coords[0];
		bdata->udfps_y = coords[1];
	}
	dev_info(dev, "st,udfps-coords: %d %d\n", bdata->udfps_x, bdata->udfps_y);

	bdata->sensor_inverted_x = 0;
	if (of_property_read_bool(np, "st,sensor_inverted_x"))
		bdata->sensor_inverted_x = 1;
	dev_info(dev, "Sensor inverted x = %u\n", bdata->sensor_inverted_x);

	bdata->sensor_inverted_y = 0;
	if (of_property_read_bool(np, "st,sensor_inverted_y"))
		bdata->sensor_inverted_y = 1;
	dev_info(dev, "Sensor inverted y = %u\n", bdata->sensor_inverted_y);

	bdata->tx_rx_dir_swap = 0;
	if (of_property_read_bool(np, "st,tx_rx_dir_swap"))
		bdata->tx_rx_dir_swap = 1;
	dev_info(dev, "tx_rx_dir_swap = %u\n",
		bdata->tx_rx_dir_swap);

	bdata->device_name = NULL;
	of_property_read_string(np, "st,device_name",
				&bdata->device_name);
	if(!bdata->device_name)
		bdata->device_name = FTS_TS_DRV_NAME;

	dev_info(dev, "device_name = %s\n", bdata->device_name);

	if (of_property_read_u8(np, "st,grip_area", &bdata->fw_grip_area))
		bdata->fw_grip_area = 0;
	dev_info(dev, "Firmware grip area = %u\n", bdata->fw_grip_area);

	return OK;
}

/**
  * Probe function, called when the driver it is matched with a device
  * with the same name compatible name
  * This function allocate, initialize all the most important functions and flow
  * those are used by the driver to operate with the IC.
  * It allocates device variables, initialize queues and schedule works,
  * registers the IRQ handler, suspend/resume callbacks, registers the device
  * to the linux input subsystem etc.
  */
#ifdef I2C_INTERFACE
static int fts_probe(struct i2c_client *client, const struct i2c_device_id *idp)
{
#else
static int fts_probe(struct spi_device *client)
{
#endif

	struct fts_ts_info *info = NULL;
	int error = 0;
	struct device_node *dp = client->dev.of_node;
	int retval;
	int input_dev_free_flag = 0;
	u16 bus_type;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct gti_optional_configuration *options;
#endif

	dev_info(&client->dev, "%s: driver probe begin!\n", __func__);
	dev_info(&client->dev, "driver ver. %s\n", FTS_TS_DRV_VERSION);

	dev_info(&client->dev, "SET Bus Functionality :\n");

	info = kzalloc(sizeof(struct fts_ts_info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev, "Out of memory... Impossible to allocate struct info!\n");
		error = -ENOMEM;
		goto ProbeErrorExit_0;
	}

#ifdef I2C_INTERFACE
	dev_info(&client->dev, "I2C interface...\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Unsupported I2C functionality\n");
		error = -EIO;
		goto ProbeErrorExit_1;
	}

	dev_info(&client->dev, "i2c address: %x\n", client->addr);
	bus_type = BUS_I2C;
#else
	if (client->controller->rt == false) {
		client->rt = true;
		retval = spi_setup(client);
		if (retval < 0) {
			dev_err(&client->dev, "%s: setup SPI rt failed(%d)\n",
				__func__, retval);
		}

		info->dma_mode = goog_check_spi_dma_enabled(client);
	}
	dev_info(&client->dev, "SPI interface...\n");
	bus_type = BUS_SPI;
#endif
	dev_info(&client->dev, "SET Device driver INFO:\n");

	info->client = client;
	info->dev = &info->client->dev;

	dev_set_drvdata(info->dev, info);

	if (dp) {
		info->board = devm_kzalloc(&client->dev,
					   sizeof(struct fts_hw_platform_data),
					   GFP_KERNEL);
		if (!info->board) {
			dev_err(info->dev, "ERROR:info.board kzalloc failed\n");
			goto ProbeErrorExit_1;
		}
		error = parse_dt(&client->dev, info->board);
		if (error)
			goto ProbeErrorExit_1;
	}

	dev_info(info->dev, "SET Regulators:\n");
	error = fts_get_reg(info, true);
	if (error < 0) {
		dev_err(info->dev, "ERROR: %s: Failed to get regulators\n", __func__);
		goto ProbeErrorExit_1;
	}

	error = fts_enable_reg(info, true);
	if (error < 0) {
		dev_err(info->dev, "%s: ERROR Failed to enable regulators\n", __func__);
		goto ProbeErrorExit_2;
	}

	dev_info(info->dev, "SET GPIOS:\n");
	error = fts_set_gpio(info);
	if (error < 0) {
		dev_err(info->dev, "%s: ERROR Failed to set up GPIO's\n", __func__);
		goto ProbeErrorExit_2;
	}
	info->client->irq = gpio_to_irq(info->board->irq_gpio);

	dev_info(info->dev, "SET Pinctrl:\n");
	retval = fts_pinctrl_get(info, true);
	if (!retval)
		fts_pinctrl_setup(info, true);

	dev_info(info->dev, "SET Input Device Property:\n");
	info->dev = &info->client->dev;
	info->input_dev = input_allocate_device();
	if (!info->input_dev) {
		dev_err(info->dev, "ERROR: No such input device defined!\n");
		error = -ENODEV;
		goto ProbeErrorExit_3;
	}
	info->input_dev->dev.parent = &client->dev;
	info->input_dev->name = info->board->device_name;
	scnprintf(info->fts_ts_phys, sizeof(info->fts_ts_phys), "%s/input0",
		 info->input_dev->name);
	info->input_dev->phys = info->fts_ts_phys;
	info->input_dev->uniq = info->input_dev->name;
	info->input_dev->id.bustype = bus_type;
	info->input_dev->id.vendor = 0x0001;
	info->input_dev->id.product = 0x0002;
	info->input_dev->id.version = 0x0100;

	__set_bit(EV_SYN, info->input_dev->evbit);
	__set_bit(EV_KEY, info->input_dev->evbit);
	__set_bit(EV_ABS, info->input_dev->evbit);
	__set_bit(BTN_TOUCH, info->input_dev->keybit);
	/* __set_bit(BTN_TOOL_FINGER, info->input_dev->keybit); */
	/* __set_bit(BTN_TOOL_PEN, info->input_dev->keybit); */

	input_mt_init_slots(info->input_dev, TOUCH_ID_MAX, INPUT_MT_DIRECT);

	/* input_mt_init_slots(info->input_dev, TOUCH_ID_MAX); */

	input_set_abs_params(info->input_dev, ABS_MT_POSITION_X, X_AXIS_MIN,
			     info->board->x_axis_max, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_POSITION_Y, Y_AXIS_MIN,
			     info->board->y_axis_max, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MAJOR, AREA_MIN,
			     AREA_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MINOR, AREA_MIN,
			     AREA_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER,
			     MT_TOOL_FINGER, 0, 0);
#ifndef SKIP_PRESSURE
	input_set_abs_params(info->input_dev, ABS_MT_PRESSURE, PRESSURE_MIN,
		PRESSURE_MAX, 0, 0);
#endif
#ifndef SKIP_DISTANCE
	input_set_abs_params(info->input_dev, ABS_MT_DISTANCE, DISTANCE_MIN,
			     DISTANCE_MAX, 0, 0);
#endif

	/* Units are (-4096, 4096), representing the range between rotation
	 * 90 degrees to left and 90 degrees to the right.
	 */
	input_set_abs_params(info->input_dev, ABS_MT_ORIENTATION, -4096, 4096,
			     0, 0);

#ifdef GESTURE_MODE
	input_set_capability(info->input_dev, EV_KEY, KEY_WAKEUP);

	input_set_capability(info->input_dev, EV_KEY, KEY_M);
	input_set_capability(info->input_dev, EV_KEY, KEY_O);
	input_set_capability(info->input_dev, EV_KEY, KEY_E);
	input_set_capability(info->input_dev, EV_KEY, KEY_W);
	input_set_capability(info->input_dev, EV_KEY, KEY_C);
	input_set_capability(info->input_dev, EV_KEY, KEY_L);
	input_set_capability(info->input_dev, EV_KEY, KEY_F);
	input_set_capability(info->input_dev, EV_KEY, KEY_V);
	input_set_capability(info->input_dev, EV_KEY, KEY_S);
	input_set_capability(info->input_dev, EV_KEY, KEY_Z);
	input_set_capability(info->input_dev, EV_KEY, KEY_WWW);

	input_set_capability(info->input_dev, EV_KEY, KEY_LEFT);
	input_set_capability(info->input_dev, EV_KEY, KEY_RIGHT);
	input_set_capability(info->input_dev, EV_KEY, KEY_UP);
	input_set_capability(info->input_dev, EV_KEY, KEY_DOWN);

	input_set_capability(info->input_dev, EV_KEY, KEY_F1);
	input_set_capability(info->input_dev, EV_KEY, KEY_F2);
	input_set_capability(info->input_dev, EV_KEY, KEY_F3);
	input_set_capability(info->input_dev, EV_KEY, KEY_F4);
	input_set_capability(info->input_dev, EV_KEY, KEY_F5);

	input_set_capability(info->input_dev, EV_KEY, KEY_LEFTBRACE);
	input_set_capability(info->input_dev, EV_KEY, KEY_RIGHTBRACE);
#endif

#ifdef PHONE_KEY
	/* KEY associated to the touch screen buttons */
	input_set_capability(info->input_dev, EV_KEY, KEY_HOMEPAGE);
	input_set_capability(info->input_dev, EV_KEY, KEY_BACK);
	input_set_capability(info->input_dev, EV_KEY, KEY_MENU);
#endif

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	mutex_init(&info->input_report_mutex);
#endif

	mutex_init(&info->diag_cmd_lock);
	mutex_init(&info->io_mutex);

#ifdef GESTURE_MODE
	mutex_init(&gestureMask_mutex);
#endif

	spin_lock_init(&info->fts_int);

	/* register the multi-touch input device */
	error = input_register_device(info->input_dev);
	if (error) {
		dev_err(info->dev, "ERROR: No such input device\n");
		error = -ENODEV;
		goto ProbeErrorExit_4;
	}

	input_dev_free_flag = 1;
	/* track slots */
	info->touch_id = 0;
	info->palm_touch_mask = 0;
	info->grip_touch_mask = 0;
#ifdef STYLUS_MODE
	info->stylus_id = 0;
#endif

	/* init feature switches (by default all the features are disable,
	  * if one feature want to be enabled from the start,
	  * set the corresponding value to 1)*/
	info->gesture_enabled = 0;
	info->glove_enabled = 0;
	info->charger_enabled = 0;
	info->cover_enabled = 0;
	info->grip_enabled = 0;

	info->resume_bit = 1;

	dev_info(info->dev, "Init Core Lib:\n");
	initCore(info);
	/* init hardware device */
	dev_info(info->dev, "Device Initialization:\n");
	error = fts_init(info);
	if (error < OK) {
		dev_err(info->dev, "Cannot initialize the device ERROR %08X\n", error);
		error = -ENODEV;
		goto ProbeErrorExit_5;
	}

#if defined(FW_UPDATE_ON_PROBE) && defined(FW_H_FILE)
	dev_info(info->dev, "FW Update and Sensing Initialization:\n");
	error = fts_fw_update(info);
	if (error < OK) {
		dev_err(info->dev, "Cannot execute fw upgrade the device ERROR %08X\n",
			error);
		error = -ENODEV;
		goto ProbeErrorExit_5;
	}

#else
	dev_info(info->dev, "SET Auto Fw Update:\n");
	info->fwu_workqueue = alloc_workqueue("fts-fwu-queue",
					      WQ_UNBOUND | WQ_HIGHPRI |
					      WQ_CPU_INTENSIVE, 1);
	if (!info->fwu_workqueue) {
		dev_err(info->dev, "ERROR: Cannot create fwu work thread\n");
		goto ProbeErrorExit_5;
	}
	INIT_DELAYED_WORK(&info->fwu_work, fts_fw_update_auto);
#endif

	dev_info(info->dev, "SET Device File Nodes:\n");
	/* sysfs stuff */
	info->attrs.attrs = fts_attr_group;
	info->attrs.bin_attrs = fts_bin_attr_group;
	error = sysfs_create_group(&client->dev.kobj, &info->attrs);
	if (error) {
		dev_err(info->dev, "ERROR: Cannot create sysfs structure!\n");
		error = -ENODEV;
		goto ProbeErrorExit_5;
	}

	retval = fts_proc_init(info);
	if (retval < OK)
		dev_err(info->dev, "Error: can not create /proc file!\n");
	info->diag_node_open = false;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	options = devm_kzalloc(info->dev, sizeof(struct gti_optional_configuration), GFP_KERNEL);
	if (!options) {
		dev_err(info->dev, "GTI optional configuration kzalloc failed.\n");
	}

	options->get_fw_version = get_fw_version;
	options->get_mutual_sensor_data = get_mutual_sensor_data;
	options->get_self_sensor_data = get_self_sensor_data;
	options->set_continuous_report = set_continuous_report;
	options->set_screen_protector_mode = set_screen_protector_mode;
	options->get_screen_protector_mode = get_screen_protector_mode;
	options->set_grip_mode = set_grip_mode;
	options->get_grip_mode = get_grip_mode;
	options->set_palm_mode = set_palm_mode;
	options->get_palm_mode = get_palm_mode;
	options->set_coord_filter_enabled = set_coord_filter_enabled;
	options->get_coord_filter_enabled = get_coord_filter_enabled;
	options->set_report_rate = set_report_rate;
	options->get_irq_mode = get_irq_mode;
	options->set_irq_mode = set_irq_mode;
	options->reset = set_reset;
	options->ping = ping;

	options->calibrate = calibrate;
	options->selftest = selftest;

	info->gti = goog_touch_interface_probe(
		info, info->dev, info->input_dev, gti_default_handler, options);

	retval = goog_pm_register_notification(info->gti, &fts_pm_ops);
	if (retval < 0) {
		dev_info(info->dev, "Failed to register gti pm");
		goto ProbeErrorExit_6;
	}
#endif

	if (info->fwu_workqueue)
		queue_delayed_work(info->fwu_workqueue, &info->fwu_work,
				   msecs_to_jiffies(EXP_FN_WORK_DELAY_MS));

	dev_info(info->dev, "Probe Finished!\n");

	return OK;


ProbeErrorExit_6:
	sysfs_remove_group(&client->dev.kobj, &info->attrs);

ProbeErrorExit_5:
	input_unregister_device(info->input_dev);

ProbeErrorExit_4:
	/* This function should only be used if input_register_device()
	 * was not called yet or if it failed. */
	if (input_dev_free_flag != 1)
		input_free_device(info->input_dev);

ProbeErrorExit_3:
	fts_pinctrl_get(info, false);

	fts_enable_reg(info, false);

ProbeErrorExit_2:
	fts_get_reg(info, false);

ProbeErrorExit_1:
	kfree(info);

ProbeErrorExit_0:
	if (error != -EPROBE_DEFER)
		dev_err(info->dev, "Probe Failed!\n");

	return error;
}


/**
  * Clear and free all the resources associated to the driver.
  * This function is called when the driver need to be removed.
  */
#ifdef I2C_INTERFACE
static void fts_remove(struct i2c_client *client)
{
#else
static void fts_remove(struct spi_device *client)
{
#endif

	struct fts_ts_info *info = dev_get_drvdata(&(client->dev));

	dev_info(info->dev, "%s\n", __func__);

	fts_proc_remove(info);

	/* sysfs stuff */
	sysfs_remove_group(&client->dev.kobj, &info->attrs);

	/* remove interrupt and event handlers */
	fts_interrupt_uninstall(info);

	/* unregister the device */
	input_unregister_device(info->input_dev);

	if (info->fwu_workqueue)
		destroy_workqueue(info->fwu_workqueue);

	fts_pinctrl_get(info, false);

	fts_enable_reg(info, false);
	fts_get_reg(info, false);

	/* free gpio */
	if (gpio_is_valid(info->board->irq_gpio))
		gpio_free(info->board->irq_gpio);
	if (gpio_is_valid(info->board->reset_gpio))
		gpio_free(info->board->reset_gpio);

	/* free any extinfo */
	kfree(info->extinfo.data);

	/* free all */
	kfree(info);
}

#ifdef CONFIG_PM
static int fts_pm_suspend(struct device *dev)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	fts_suspend(info);
	return 0;
}

static int fts_pm_resume(struct device *dev)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	fts_resume(info);
	return 0;
}

static SIMPLE_DEV_PM_OPS(fts_pm_ops, fts_pm_suspend, fts_pm_resume);
#endif

/**
  * Struct which contains the compatible names that need to match with
  * the definition of the device in the device tree node
  */
static struct of_device_id fts_of_match_table[] = {
	{
		.compatible = "st,fts",
	},
	{},
};

#ifdef I2C_INTERFACE
static const struct i2c_device_id fts_device_id[] = {
	{ FTS_TS_DRV_NAME, 0 },
	{}
};

static struct i2c_driver fts_i2c_driver = {
	.driver			= {
		.name		= FTS_TS_DRV_NAME,
		.of_match_table = fts_of_match_table,
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm		= &fts_pm_ops,
#endif
	},
	.probe			= fts_probe,
	.remove			= fts_remove,
	.id_table		= fts_device_id,
};
#else
static struct spi_driver fts_spi_driver = {
	.driver			= {
		.name		= FTS_TS_DRV_NAME,
		.of_match_table = fts_of_match_table,
		.owner		= THIS_MODULE,
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm		= &fts_pm_ops,
#endif
	},
	.probe			= fts_probe,
	.remove			= fts_remove,
};
#endif




static int __init fts_driver_init(void)
{
#ifdef I2C_INTERFACE
	return i2c_add_driver(&fts_i2c_driver);
#else
	return spi_register_driver(&fts_spi_driver);
#endif
}

static void __exit fts_driver_exit(void)
{
	pr_info("%s\n", __func__);
#ifdef I2C_INTERFACE
	i2c_del_driver(&fts_i2c_driver);
#else
	spi_unregister_driver(&fts_spi_driver);
#endif
}

MODULE_DESCRIPTION("STMicroelectronics MultiTouch IC Driver");
MODULE_AUTHOR("STMicroelectronics");
MODULE_LICENSE("GPL v2");

late_initcall(fts_driver_init);
module_exit(fts_driver_exit);
