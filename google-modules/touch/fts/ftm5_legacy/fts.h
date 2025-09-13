/*
  * fts.h
  *
  * FTS Capacitive touch screen controller (FingerTipS)
  *
  * Copyright (C) 2017, STMicroelectronics
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
  * THE
  * CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
  * INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
  */

/*!
  * \file fts.h
  * \brief Contains all the definitions and structs used generally by the driver
  */

#ifndef _LINUX_FTS_I2C_H_
#define _LINUX_FTS_I2C_H_

#include <linux/device.h>
#if IS_ENABLED(CONFIG_TOUCHSCREEN_HEATMAP)
#include <heatmap.h>
#endif
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OFFLOAD)
#include <touch_offload.h>
#endif
#include <linux/pm_qos.h>
#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include "fts_lib/ftsSoftware.h"
#include "fts_lib/ftsHardware.h"

#if IS_ENABLED(CONFIG_TOUCHSCREEN_TBN)
#include <touch_bus_negotiator.h>
#endif

#include <linux/proc_fs.h>

#undef DYNAMIC_REFRESH_RATE

/****************** CONFIGURATION SECTION ******************/
/** @defgroup conf_section	 Driver Configuration Section
  * Settings of the driver code in order to suit the HW set up and the
  *application behavior
  * @{
  */
/* **** CODE CONFIGURATION **** */
#define FTS_TS_DRV_NAME		"fts"	/* driver name */
#define FTS_TS_DRV_VERSION	"5.2.16.16"	/* driver version string */
#define FTS_TS_DRV_VER		0x05021010	/* driver version u32 format */

/* #define DEBUG */	/* /< define to print more logs in the kernel log
			 * and better follow the code flow */
#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) "[ FTS ] " fmt
#endif

#define PINCTRL_STATE_ACTIVE    "pmx_ts_active"
#define PINCTRL_STATE_SUSPEND   "pmx_ts_suspend"
#define PINCTRL_STATE_RELEASE   "pmx_ts_release"

#define DRIVER_TEST	/* /< if defined allow to use and test special functions
			  * of the driver and fts_lib from command shell
			  * (useful for enginering/debug operations) */

/* If both COMPUTE_INIT_METHOD and PRE_SAVED_METHOD are not defined,
 * driver will be automatically configured as GOLDEN_VALUE_METHOD
 */
#define COMPUTE_INIT_METHOD	/* Allow to compute init data on phone during
				 * production
				 */
#define SKIP_PRODUCTION_TEST	/* Allow to skip Production test */

#ifndef COMPUTE_INIT_METHOD
#define PRE_SAVED_METHOD	/* Pre-Saved Method used during production */
#endif

/*#define FW_H_FILE*/			/* include the FW data as header file */
#ifdef FW_H_FILE
#define FW_SIZE_NAME	myArray_size	/* FW data array size */
#define FW_ARRAY_NAME	myArray	/* FW data array name */
/*#define FW_UPDATE_ON_PROBE*/		/* No delay updating FW */
#endif

#ifndef FW_UPDATE_ON_PROBE
/* Include the Production Limit File as header file, can be commented to use a
  * .csv file instead */
/* #define LIMITS_H_FILE */
#ifdef LIMITS_H_FILE
	#define LIMITS_SIZE_NAME	myArray2_size	/* /< name of the
							 * variable
							  * in the limits header
							  *file which
							  * specified the
							  *dimension of
							  * the limits data
							  *array */
	#define LIMITS_ARRAY_NAME	myArray2	/* /< name of the
							 * variable in
							  * the limits header
							  *file which
							  * specified the limits
							  *data array */
#endif
#else
/* if execute fw update in the probe the limit file must be a .h */
#define LIMITS_H_FILE	/* /< include the Production Limit File as header file,
			 * DO NOT COMMENT! */
#define LIMITS_SIZE_NAME		myArray2_size	/* /< name of the
							 * variable
							  * in the limits header
							  *file
							  * which specified the
							  *dimension
							  * of the limits data
							  *array */
#define LIMITS_ARRAY_NAME		myArray2	/* /< name of the
							 * variable in the
							  * limits header file
							  *which specified
							  * the limits data
							  *array */
#endif

/* #define USE_ONE_FILE_NODE */	/* /< allow to enable/disable all the features
  * just using one file node */

#ifndef FW_UPDATE_ON_PROBE
#define EXP_FN_WORK_DELAY_MS 1000	/* /< time in ms elapsed after the probe
					  * to start the work which execute FW
					  *update
					  * and the Initialization of the IC */
#endif

/* **** END **** */


/* **** FEATURES USED IN THE IC **** */
/* Enable the support of keys */
/* #define PHONE_KEY */

#undef GESTURE_MODE	/* /< enable the support of the gestures */
#ifdef GESTURE_MODE
	#define USE_GESTURE_MASK	/* /< the gestures to select are
					 * referred using
					  * a gesture bitmask instead of their
					  *gesture IDs */
#endif


#undef CHARGER_MODE	/* /< enable the support to charger mode feature
			 * (comment to disable) */

#define GLOVE_MODE	/* /< enable the support to glove mode feature (comment
			 * to disable) */

#undef COVER_MODE	/* /< enable the support to cover mode feature (comment
			 * to disable) */

#undef STYLUS_MODE	/* /< enable the support to stylus mode feature (comment
			 * to disable) */

#undef GRIP_MODE	/* /< enable the support to grip mode feature (comment
			 * to disable) */


/* **** END **** */


/* **** PANEL SPECIFICATION **** */
#define X_AXIS_MIN	0	/* /< min X coordinate of the display */
#define Y_AXIS_MIN	0	/* /< min Y coordinate of the display */
#define Y_AXIS_MAX	2959	/* /< Max Y coordinate of the display */
#define X_AXIS_MAX	1440	/* /< Max X coordinate of the display */

#define PRESSURE_MIN	0	/* /< min value of pressure reported */
#define PRESSURE_MAX	127	/* /< Max value of pressure reported */

#define DISTANCE_MIN	0	/* /< min distance between the tool and the
				 * display */
#define DISTANCE_MAX	127	/* /< Max distance between the tool and the
				 * display */

#define TOUCH_ID_MAX	10	/* /< Max number of simoultaneous touches
				 * reported */

#define AREA_SCALE	16	/* /< Scale for major/minor axis calculation */
#define AREA_MIN	(PRESSURE_MIN * AREA_SCALE)	/* /< Min value of
							 * major/minor axis
							 * reported */
#define AREA_MAX	(PRESSURE_MAX * AREA_SCALE)	/* /< Max value of
							 * major/minor axis
							 * reported */
/* **** END **** */

/* #define SKIP_PRESSURE */

/**@}*/
/*********************************************************/
#if IS_ENABLED(CONFIG_TOUCHSCREEN_HEATMAP)
/* **** LOCAL HEATMAP FEATURE *** */
#define LOCAL_HEATMAP_WIDTH 7
#define LOCAL_HEATMAP_HEIGHT 7
#define LOCAL_HEATMAP_MODE 0xC1

struct heatmap_report {
	uint8_t prefix; /* always should be 0xA0 */
	uint8_t mode; /* mode should be 0xC1 for heatmap */

	uint16_t counter; /* LE order, should increment on each heatmap read */
	int8_t offset_x;
	uint8_t size_x;
	int8_t offset_y;
	uint8_t size_y;
	/* data is in LE order; order should be enforced after data is read */
	strength_t data[LOCAL_HEATMAP_WIDTH * LOCAL_HEATMAP_HEIGHT];
} __attribute__((packed));
/* **** END **** */

struct heatmap_data {
	ktime_t timestamp;
	uint16_t size_x;
	uint16_t size_y;
	uint8_t *data;
} __attribute__((packed));
#endif
/*
  * Configuration mode
  *
  * bitmask which can assume the value defined as features in ftsSoftware.h or
  * the following values
  */

/** @defgroup mode_section	 IC Status Mode
  * Bitmask which keeps track of the features and working mode enabled in the
  * IC.
  * The meaning of the the LSB of the bitmask must be interpreted considering
  * that the value defined in @link feat_opt Feature Selection Option @endlink
  * correspond to the position of the corresponding bit in the mask
  * @{
  */
#define MODE_NOTHING 0x00000000	/* /< nothing enabled (sense off) */
#define MODE_ACTIVE(_mask, _sett)	\
	(_mask |= (SCAN_MODE_ACTIVE << 24) | (_sett << 16))
/* /< store the status of scan mode active and its setting */
#define MODE_LOW_POWER(_mask, _sett)	\
	(_mask |= (SCAN_MODE_LOW_POWER << 24) | (_sett << 16))
/* /< store the status of scan mode low power and its setting */
#define IS_POWER_MODE(_mask, _mode)	((_mask&(_mode<<24)) != 0x00)
/* /< check the current mode of the IC */

/** @}*/

#define CMD_STR_LEN	32	/* /< max number of parameters that can accept
				 * the
				  * MP file node (stm_fts_cmd) */

#define TSP_BUF_SIZE	PAGE_SIZE	/* /< max number of bytes printable on
					  * the shell in the normal file nodes
					  **/

#define MAX_RAWDATA_STR_SIZE	PAGE_SIZE * 3

/* Encapsulate display extinfo
 *
 * For some panels, it is insufficient to simply detect the panel ID and load
 * one corresponding firmware. The display driver exposes extended info read
 * from the display, but it is up to the touch driver to parse the data.
 */
struct fts_disp_extinfo {
	bool is_read;
	u8 size;
	u8 *data;
};

/**
  * Struct which contains information about the HW platform and set up
  */
struct fts_hw_platform_data {
	u8 dchip_id[2];	/* DCHIPID_ID_0 and DCHIPID_ID_1 in ftsHardware.h */
	int flash_chunk; /* Max number of bytes that the DMA can burn on flash
			  * in one shot in FTI */
	int (*power) (bool on);
	int switch_gpio;/* (optional) I2C switch */
	int irq_gpio;	/* /< number of the gpio associated to the interrupt pin
			 * */
	int reset_gpio;	/* /< number of the gpio associated to the reset pin */
	int disp_rate_gpio; /* disp_rate gpio: LOW=60Hz, HIGH=90Hz */
	const char *fw_name;
	const char *limits_name;
	const char *device_name;
	bool sensor_inverted;
	int x_axis_max;
	int y_axis_max;
	int udfps_x;
	int udfps_y;
	bool auto_fw_update;
	bool separate_save_golden_ms_raw_cmd;
	bool skip_fpi_for_unset_mpflag;
	bool sensor_inverted_x;
	bool sensor_inverted_y;
	bool tx_rx_dir_swap; /* Set as TRUE if Tx direction is same as x-axis. */
#if IS_ENABLED(CONFIG_TOUCHSCREEN_HEATMAP)
	bool heatmap_mode_full_init;
#endif
	struct drm_panel *panel;
	u32 initial_panel_index;
	u32 *force_pi_cfg_ver;
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OFFLOAD)
	u32 offload_id;
#endif
	u8 fw_grip_area;
};

/* Bits for the bus reference mask */
enum {
	FTS_BUS_REF_SCREEN_ON		= 0x01,
	FTS_BUS_REF_IRQ			= 0x02,
	FTS_BUS_REF_FW_UPDATE		= 0x04,
	FTS_BUS_REF_SYSFS		= 0x08,
	FTS_BUS_REF_FORCE_ACTIVE	= 0x10,
	FTS_BUS_REF_BUGREPORT		= 0x20,
};

/* Motion filter finite state machine (FSM) states
 * FTS_MF_FILTERED        - default coordinate filtering
 * FTS_MF_UNFILTERED      - unfiltered single-touch coordinates
 * FTS_MF_FILTERED_LOCKED - filtered coordinates. Locked until touch is lifted.
 */
typedef enum {
	FTS_MF_FILTERED		= 0,
	FTS_MF_UNFILTERED	= 1,
	FTS_MF_FILTERED_LOCKED	= 2
} motion_filter_state_t;

/* Heatmap mode selection
 * FTS_HEATMAP_OFF	- no data read
 * FTS_HEATMAP_PARTIAL	- read partial frame
 *			(LOCAL_HEATMAP_WIDTH * LOCAL_HEATMAP_HEIGHT)
 * FTS_HEATMAP_FULL	- read full mutual sense strength frame
 */
#if IS_ENABLED(CONFIG_TOUCHSCREEN_HEATMAP)
enum {
	FTS_HEATMAP_OFF		= 0,
	FTS_HEATMAP_PARTIAL	= 1,
	FTS_HEATMAP_FULL	= 2
};
#endif
/*
  * Forward declaration
  */
struct fts_ts_info;

/*
  * Dispatch event handler
  * Return true if the handler has processed a pointer event
  */
typedef bool (*event_dispatch_handler_t)
	(struct fts_ts_info *info, unsigned char *data);

/**
  * Driver touch simulation details
  */
struct fts_touchsim{
	/* touch simulation coordinates */
	int x, y, x_step, y_step;

	/* timer to run the touch simulation code */
	struct hrtimer hr_timer;

	struct work_struct work;
	struct workqueue_struct *wq;

	/* True if the touch simulation is currently running */
	bool is_running;
};

/**
  * Struct which store an ordered list of the errors events encountered during
  *the polling of a FIFO.
  * The max number of error events that can be stored is equal to FIFO_DEPTH
  */
typedef struct {
	u8 list[FIFO_DEPTH * FIFO_EVENT_SIZE];	/* /< byte array which contains
						 * the series of error events
						 * encountered from the last
						 * reset of the list. */
	int count;	/* /< number of error events stored in the list */
	int last_index;	/* /< index of the list where will be stored the next
			 * error event. Subtract -1 to have the index of the
			 * last error event! */
} ErrorList;

/**
  * Struct used to specify which test perform during the Mass Production Test.
  * For each test item selected in this structure, there should be one or
  * more labels associated in the Limit file from where load the thresholds
  */
typedef struct {
	int MutualRaw;	/* /< MS Raw min/Max test */
	int MutualRawMap;	/* /< MS Raw min/Max test for each node */
	int MutualRawGap;	/* /< MS Raw Gap(max-min) test */
	int MutualRawAdj;	/* /< MS Raw Adjacent test */
	int MutualRawAdjGap;	/* /< MS Raw Adjacent Gap (max-min) test */
	int MutualRawAdjPeak;	/* /< MS Raw Adjacent Peak
				 * max(max(adjv),max(adjh)) test
				 */
	int MutualRawLP;	/* /< MS Low Power Raw min/Max test */
	int MutualRawMapLP;	/* /< MS Low Power Raw min/Max test
				 * for each node
				 */
	int MutualRawGapLP;	/* /< MS Low Power Raw Gap(max-min) test */
	int MutualRawAdjLP;	/* /< MS Low Power Raw Adjacent test */
	int MutualRawAdjITO;	/* /< MS Raw Adjacent test during ITO test */
	int MutualRawMapITO; 	/* /< MS Raw ITO min/Max test */

	int MutualCx1;	/* /< MS Cx1 min/Max test */
	int MutualCx2;	/* /< MS Cx2 min/Max (for each node) test */
	int MutualCx2Adj;	/* /< MS Vertical and Horizontal Adj Cx2 min/Max
				 *  (for each node) test */
	int MutualCxTotal;	/* /< MS Total Cx min/Max (for each node) test
				 * */
	int MutualCxTotalAdj;	/* /< MS Total vertical and Horizontal Adj Cx2
				 * min/Max (for each node) test
				 */

	int MutualCx1LP;	/* /< MS LowPower Cx1 min/Max test */
	int MutualCx2LP;	/* /< MS LowPower Cx2 min/Max (for each node)
				 * test
				 */
	int MutualCx2AdjLP;	/* /< MS LowPower Vertical and Horizontal Adj
				 * Cx2 min/Max
				 * (for each node) test
				 */
	int MutualCxTotalLP;	/* /< MS Total LowPower Cx min/Max
				 * (for each node) test */
	int MutualCxTotalAdjLP;	/* /< MS Total LowPower vertical and Horizontal
				 * Adj Cx2 min/Max (for each node) test
				 */

	int MutualKeyRaw;	/* /< MS Raw Key min/Max test */
	int MutualKeyCx1;	/* /< MS Cx1 Key min/Max test */
	int MutualKeyCx2;	/* /< MS Cx2 Key min/Max (for each node) test */
	int MutualKeyCxTotal;	/* /< MS Total Cx Key min/Max (for each node)
				 * test */

	int SelfForceRaw;	/* /< SS Force Raw min/Max test */
	int SelfForceRawGap;	/* /< SS Force Raw Gap(max-min) test */
	int SelfForceRawMap;	/* /< SS Force Raw min/Max Map test */
	int SelfForceRawLP;	/* /< SS Low Power Force Raw min/Max test */
	int SelfForceRawGapLP; /* /< SS Low Power Force Raw Gap(max-min) test */
	int SelfForceRawMapLP;	/* /< SS Low Power Force Raw min/Max Map test */

	int SelfForceIx1;	/* /< SS Force Ix1 min/Max test */
	int SelfForceIx2;	/* /< SS Force Ix2 min/Max (for each node) test
				 * */
	int SelfForceIx2Adj;	/* /< SS Vertical Adj Force Ix2 min/Max
				 * (for each node) test */
	int SelfForceIxTotal;	/* /< SS Total Force Ix min/Max (for each node)
				 * test */
	int SelfForceIxTotalAdj;	/* /< SS Total Vertical Adj Force Ix
					 * min/Max
					 * (for each node) test */
	int SelfForceCx1;	/* /< SS Force Cx1 min/Max test */
	int SelfForceCx2; /* /< SS Force Cx2 min/Max (for each node) test */
	int SelfForceCx2Adj;	/* /< SS Vertical Adj Force Cx2 min/Max (for
				 * each node) test */
	int SelfForceCxTotal;	/* /< SS Total Force Cx min/Max (for each node)
				 * test */
	int SelfForceCxTotalAdj;	/* /< SS Total Vertical Adj Force Cx
					 * min/Max (for each node) test
					 */

	int SelfForceIx1LP;	/* /< SS LP Force Ix1 min/Max test */
	int SelfForceIx2LP;	/* /< SS LP Force Ix2 min/Max (for each node)
				 *  test
				 */
	int SelfForceIx2AdjLP;	/* /< SS LP Vertical Adj Force Ix2 min/Max
					 * (for each node) test */
	int SelfForceIxTotalLP;	/* /< SS LP Total Force Ix min/Max
				 * (for each node) test
				 */
	int SelfForceIxTotalAdjLP;	/* /< SS LP Total Vertical Adj Force Ix
					 * min/Max (for each node) test
					 */
	int SelfForceCx1LP;	/* /< SS LP Force Cx1 min/Max test */
	int SelfForceCx2LP;	/* /< SS LP Force Cx2 min/Max (for each node)
				 * test
				 */
	int SelfForceCx2AdjLP;	/* /< SS LP Vertical Adj Force Cx2 min/Max (for
				 * each node) test
				 */
	int SelfForceCxTotalLP;	/* /< SS LP Total Force Cx min/Max
				 * (for each node) test
				 */
	int SelfForceCxTotalAdjLP;	/* /< SS LP Total Vertical Adj Force Cx
					 * min/Max (for each node) test
					 */

	int SelfSenseRaw;	/* /< SS Sense Raw min/Max test */
	int SelfSenseRawGap;	/* /< SS Sense Raw Gap(max-min) test */
	int SelfSenseRawMap;	/* /< SS Sense Raw min/Max test for each node */
	int SelfSenseRawLP;	/* /< SS Low Power Sense Raw min/Max test */
	int SelfSenseRawGapLP; /* /< SS Low Power Sense Raw Gap(max-min) test */
	int SelfSenseRawMapLP;	/* /< SS Low Power Sense Raw min/Max test for
				 * each node
				 */

	int SelfSenseIx1;	/* /< SS Sense Ix1 min/Max test */
	int SelfSenseIx2; /* /< SS Sense Ix2 min/Max (for each node) test */
	int SelfSenseIx2Adj;	/* /< SS Horizontal Adj Sense Ix2 min/Max
				  * (for each node) test */
	int SelfSenseIxTotal;	/* /< SS Total Horizontal Sense Ix min/Max
				  * (for each node) test */
	int SelfSenseIxTotalAdj;	/* /< SS Total Horizontal Adj Sense Ix
					 * min/Max
					 * (for each node) test */
	int SelfSenseCx1;	/* /< SS Sense Cx1 min/Max test */
	int SelfSenseCx2; /* /< SS Sense Cx2 min/Max (for each node) test */
	int SelfSenseCx2Adj;	/* /< SS Horizontal Adj Sense Cx2 min/Max
				  * (for each node) test */
	int SelfSenseCxTotal;	/* /< SS Total Sense Cx min/Max (for each node)
				 * test */
	int SelfSenseCxTotalAdj;	/* /< SS Total Horizontal Adj Sense Cx
					 * min/Max
					 * (for each node) test */
	int SelfSenseIx1LP;	/* /< SS LP Sense Ix1 min/Max test */
	int SelfSenseIx2LP; /* /< SS LP Sense Ix2 min/Max (for each node)
			     * test
			     */
	int SelfSenseIx2AdjLP;	/* /< SS LP Horizontal Adj Sense Ix2 min/Max
				 * (for each node) test
				 */
	int SelfSenseIxTotalLP;	/* /< SS LP Total Horizontal Sense Ix min/Max
				 * (for each node) test
				 */
	int SelfSenseIxTotalAdjLP; /* /< SS LP Total Horizontal Adj Sense Ix
				    * min/Max (for each node) test
				    */
	int SelfSenseCx1LP;	/* /< SS LP Sense Cx1 min/Max test */
	int SelfSenseCx2LP; /* /< SS LP Sense Cx2 min/Max (for each node)
			     * test
			     */
	int SelfSenseCx2AdjLP;	/* /< SS LP Horizontal Adj Sense Cx2 min/Max
				 * (for each node) test
				 */
	int SelfSenseCxTotalLP;	/* /< SS LP Total Sense Cx min/Max
				 * (for each node) test
				 */
	int SelfSenseCxTotalAdjLP; /* /< SS LP Total Horizontal Adj Sense Cx
				    * min/Max (for each node) test
				    */
} TestToDo;

#define DIE_INFO_SIZE			16	/* Num bytes of external release
						 * in config */
#define EXTERNAL_RELEASE_INFO_SIZE	8	/* Num bytes of release info in
						 * sys info
						 *  (first bytes are external
						 * release) */
#define RELEASE_INFO_SIZE		(EXTERNAL_RELEASE_INFO_SIZE)

/**
  * Struct which contains fundamental information about the chip and its
  * configuration
  */
typedef struct {
	u16 u16_apiVer_rev;	/* /< API revision version */
	u8 u8_apiVer_minor;	/* /< API minor version */
	u8 u8_apiVer_major;	/* /< API major version */
	u16 u16_chip0Ver;	/* /< Dev0 version */
	u16 u16_chip0Id;	/* /< Dev0 ID */
	u16 u16_chip1Ver;	/* /< Dev1 version */
	u16 u16_chip1Id;	/* /< Dev1 ID */
	u16 u16_fwVer;	/* /< Fw version */
	u16 u16_svnRev;	/* /< SVN Revision */
	u16 u16_cfgVer;	/* /< Config Version */
	u16 u16_cfgProjectId;	/* /< Config Project ID */
	u16 u16_cxVer;	/* /< Cx Version */
	u16 u16_cxProjectId;	/* /< Cx Project ID */
	u8 u8_cfgAfeVer;	/* /< AFE version in Config */
	u8 u8_cxAfeVer;	/* /< AFE version in CX */
	u8 u8_panelCfgAfeVer;	/* /< AFE version in PanelMem */
	u8 u8_protocol;	/* /< Touch Report Protocol */
	u8 u8_dieInfo[DIE_INFO_SIZE];	/* /< Die information */
	u8 u8_releaseInfo[RELEASE_INFO_SIZE];	/* /< Release information */
	u32 u32_fwCrc;	/* /< Crc of FW */
	u32 u32_cfgCrc;	/* /< Crc of config */
	u8 u8_mpFlag; /* /< MP Flag */
	u8 u8_ssDetScanSet; /* /< Type of Detect Scan Selected */

	u16 u16_scrResX;/* /< X resolution on main screen */
	u16 u16_scrResY;/* /< Y resolution on main screen */
	u8 u8_scrTxLen;	/* /< Tx length */
	u8 u8_scrRxLen;	/* /< Rx length */
	u8 u8_keyLen;	/* /< Key Len */
	u8 u8_forceLen;	/* /< Force Len */
	u32 u32_productionTimestamp;	/* /< Production Timestamp */

	u16 u16_dbgInfoAddr;	/* /< Offset of debug Info structure */

	u16 u16_msTchRawAddr;	/* /< Offset of MS touch raw frame */
	u16 u16_msTchFilterAddr;/* /< Offset of MS touch filter frame */
	u16 u16_msTchStrenAddr;	/* /< Offset of MS touch strength frame */
	u16 u16_msTchBaselineAddr;	/* /< Offset of MS touch baseline frame
					 * */

	u16 u16_ssTchTxRawAddr;	/* /< Offset of SS touch force raw frame */
	u16 u16_ssTchTxFilterAddr;	/* /< Offset of SS touch force filter
					 * frame */
	u16 u16_ssTchTxStrenAddr;/* /< Offset of SS touch force strength frame
				 * */
	u16 u16_ssTchTxBaselineAddr;	/* /< Offset of SS touch force baseline
					 * frame */

	u16 u16_ssTchRxRawAddr;	/* /< Offset of SS touch sense raw frame */
	u16 u16_ssTchRxFilterAddr;	/* /< Offset of SS touch sense filter
					 * frame */
	u16 u16_ssTchRxStrenAddr;/* /< Offset of SS touch sense strength frame
				 * */
	u16 u16_ssTchRxBaselineAddr;	/* /< Offset of SS touch sense baseline
					 * frame */

	u16 u16_keyRawAddr;	/* /< Offset of key raw frame */
	u16 u16_keyFilterAddr;	/* /< Offset of key filter frame */
	u16 u16_keyStrenAddr;	/* /< Offset of key strength frame */
	u16 u16_keyBaselineAddr;	/* /< Offset of key baseline frame */

	u16 u16_frcRawAddr;	/* /< Offset of force touch raw frame */
	u16 u16_frcFilterAddr;	/* /< Offset of force touch filter frame */
	u16 u16_frcStrenAddr;	/* /< Offset of force touch strength frame */
	u16 u16_frcBaselineAddr;/* /< Offset of force touch baseline frame */

	u16 u16_ssHvrTxRawAddr;	/* /< Offset of SS hover Force raw frame */
	u16 u16_ssHvrTxFilterAddr;	/* /< Offset of SS hover Force filter
					 * frame */
	u16 u16_ssHvrTxStrenAddr;/* /< Offset of SS hover Force strength frame
				  * */
	u16 u16_ssHvrTxBaselineAddr;	/* /< Offset of SS hover Force baseline
					 * frame */

	u16 u16_ssHvrRxRawAddr;	/* /< Offset of SS hover Sense raw frame */
	u16 u16_ssHvrRxFilterAddr;	/* /< Offset of SS hover Sense filter
					 * frame */
	u16 u16_ssHvrRxStrenAddr;	/* /< Offset of SS hover Sense strength
					 * frame */
	u16 u16_ssHvrRxBaselineAddr;	/* /< Offset of SS hover Sense baseline
					 * frame */

	u16 u16_ssPrxTxRawAddr;	/* /< Offset of SS proximity force raw frame */
	u16 u16_ssPrxTxFilterAddr;	/* /< Offset of SS proximity force
					 * filter frame */
	u16 u16_ssPrxTxStrenAddr;/* /< Offset of SS proximity force strength
				 * frame */
	u16 u16_ssPrxTxBaselineAddr;	/* /< Offset of SS proximity force
					 * baseline frame */

	u16 u16_ssPrxRxRawAddr;	/* /< Offset of SS proximity sense raw frame */
	u16 u16_ssPrxRxFilterAddr;	/* /< Offset of SS proximity sense
					 * filter frame */
	u16 u16_ssPrxRxStrenAddr;/* /< Offset of SS proximity sense strength
				  * frame */
	u16 u16_ssPrxRxBaselineAddr;	/* /< Offset of SS proximity sense
					 * baseline frame */

	u16 u16_ssDetRawAddr;		/* /< Offset of SS detect raw frame */
	u16 u16_ssDetFilterAddr;	/* /< Offset of SS detect filter
					 * frame */
	u16 u16_ssDetStrenAddr;		/* /< Offset of SS detect strength
					 * frame */
	u16 u16_ssDetBaselineAddr;	/* /< Offset of SS detect baseline
					 * frame */
} SysInfo;

#define MAX_LIMIT_FILE_NAME 100	/* max number of chars of the limit file name
				 */
#define CHUNK_PROC	1024	/* Max chunk of data printed on the sequential
				 * file in each iteration */

/**
  * Struct which store the data coming from a Production Limit File
  */
typedef struct {
	char *data;	/* /< pointer to an array of char which contains
			  * the content of the Production Limit File */
	int size;	/* /< size of data */
	char name[MAX_LIMIT_FILE_NAME];	/* /< identifier of the source from
					  * where the limits data were loaded
					  * (if loaded from a file it will be
					  * the file name, while if loaded
					  * from .h will be "NULL") */
} LimitFile;

/**
  * Possible actions that can be requested by an host
  */
typedef enum {
	ACTION_WRITE				= (u16) 0x0001,	/* /< Bus Write
								 * */
	ACTION_READ				= (u16) 0x0002,	/* /< Bus Read
								 * */
	ACTION_WRITE_READ			= (u16) 0x0003,	/* /< Bus Write
								 * followed by a
								 * Read */
	ACTION_GET_VERSION			= (u16) 0x0004,	/* /< Get
								 * Version of
								 * the protocol
								 * (equal to the
								 * first 2 bye
								 * of driver
								 * version) */
	ACTION_WRITEU8UX			= (u16) 0x0011,	/* /< Bus Write
								 * with support
								 * to different
								 * address size
								 * */
	ACTION_WRITEREADU8UX			= (u16) 0x0012,	/* /< Bus
								 * writeRead
								 * with support
								 * to different
								 * address size
								 * */
	ACTION_WRITETHENWRITEREAD		= (u16) 0x0013,	/* /< Bus write
								 * followed by a
								 * writeRead */
	ACTION_WRITEU8XTHENWRITEREADU8UX	= (u16) 0x0014,	/* /< Bus write
								 * followed by a
								 * writeRead
								 * with support
								 * to different
								 * address size
								 * */
	ACTION_WRITEU8UXTHENWRITEU8UX		= (u16) 0x0015,	/* /< Bus write
								 * followed by a
								 * write with
								 * support to
								 * different
								 * address size
								 * */
	ACTION_GET_FW				= (u16) 0x1000,	/* /< Get Fw
								 * file content
								 * used by the
								 * driver */
	ACTION_GET_LIMIT			= (u16) 0x1001	/* /< Get Limit
								 * File content
								 * used by the
								 * driver */
} Actions;
/**
  * Struct used to contain info of the message received by the host in
  * Scriptless mode
  */
typedef struct {
	u16 msg_size;	/* /< total size of the message in bytes */
	u16 counter;	/* /< counter ID to identify a message */
	Actions action;	/* /< type of operation requested by the host @see
			 * Actions */
	u8 dummy;	/* /< (optional)in case of any kind of read operations,
			 * specify if the first byte is dummy */
} Message;

/**
  * FTS capacitive touch screen device information
  * - dev             Pointer to the structure device \n
  * - client          client structure \n
  * - input_dev       Input device structure \n
  * - work            Work thread \n
  * - event_wq        Event queue for work thread \n
  * - event_dispatch_table  Event dispatch table handlers \n
  * - attrs           SysFS attributes \n
  * - mode            Device operating mode (bitmask) \n
  * - touch_id        Bitmask for touch id (mapped to input slots) \n
  * - stylus_id       Bitmask for tracking the stylus touches (mapped using the
  *                   touchId) \n
  * - timer           Timer when operating in polling mode \n
  * - power           Power on/off routine \n
  * - board           HW info retrieved from device tree \n
  * - vdd_reg         DVDD power regulator \n
  * - avdd_reg        AVDD power regulator \n
  * - resume_bit      Indicate if screen off/on \n
  * - fwupdate_stat   Store the result of a fw update triggered by the host \n
  * - notifier        Used for be notified from a suspend/resume event \n
  * - sensor_sleep    true suspend was called, false resume was called \n
  * - wakesrc         Wakeup Source struct \n
  * - input_report_mutex  mutex for handling the pressure of keys \n
  * - series_of_switches  to store the enabling status of a particular feature
  *                       from the host \n
  * - tbn             Touch Bus Negotiator context
  */
struct fts_ts_info {
	struct device           *dev;	/* Pointer to the device */
#ifdef I2C_INTERFACE
	struct i2c_client       *client;	/* I2C client structure */
#else
	struct spi_device       *client;	/* SPI client structure */
#endif
	struct input_dev        *input_dev;	/* Input device structure */

	/* buffer which store the input device name assigned by the kernel */
	char fts_ts_phys[64];

	struct work_struct suspend_work;	/* Suspend work thread */
	struct work_struct resume_work;	/* Resume work thread */
	struct workqueue_struct *event_wq;	/* Used for event handler, */
						/* suspend, resume threads */

	struct completion bus_resumed;		/* resume_work complete */

	struct pm_qos_request pm_qos_req;
#if IS_ENABLED(CONFIG_TOUCHSCREEN_HEATMAP)
	struct v4l2_heatmap v4l2;
	struct heatmap_data mutual_strength_heatmap;
#endif

#if IS_ENABLED(CONFIG_TOUCHSCREEN_OFFLOAD)
	struct touch_offload_context offload;
	struct delayed_work offload_resume_work;
	int touch_offload_active_coords;
#endif

	bool enable_palm_data_dump;
	struct delayed_work palm_data_dump_work;
	struct delayed_work fwu_work;	/* Work for fw update */
	struct workqueue_struct *fwu_workqueue;	/* Fw update work queue */
	event_dispatch_handler_t *event_dispatch_table;	/* Dispatch table */

	struct attribute_group attrs;	/* SysFS attributes */

	unsigned int mode;	/* Device operating mode */
				/* MSB - active or lpm */
	unsigned long touch_id;	/* Bitmask for touch id */
	unsigned long palm_touch_mask; /* Bitmask for palm touch */
	unsigned long grip_touch_mask; /* Bitmask for grip touch */
#ifdef STYLUS_MODE
	unsigned long stylus_id;	/* Bitmask for the stylus */
#endif

	ktime_t timestamp; /* time that the event was first received from the
		touch IC, acquired during hard interrupt, in CLOCK_MONOTONIC */

	struct fts_hw_platform_data     *board;	/* HW info from device tree */
	struct regulator        *vdd_reg;	/* DVDD power regulator */
	struct regulator        *avdd_reg;	/* AVDD power regulator */

	struct pinctrl       *ts_pinctrl;		/* touch pin control state holder */
	struct pinctrl_state *pinctrl_state_active;	/* Active pin state*/
	struct pinctrl_state *pinctrl_state_suspend;	/* Suspend pin state*/
	struct pinctrl_state *pinctrl_state_release;	/* Release pin state*/

	spinlock_t fts_int;	/* Spinlock to protect interrupt toggling */
	bool irq_enabled;	/* Interrupt state */

	struct mutex io_mutex;	/* Protect access to the I/O */
	struct mutex bus_mutex;	/* Protect access to the bus */
	unsigned int bus_refmask; /* References to the bus */

	int resume_bit;	/* Indicate if screen off/on */
	int fwupdate_stat;	/* Result of a fw update */
	int reflash_fw;	/* Attempt to reflash fw */
	int autotune_stat;	/* Attempt to autotune */

	struct fts_disp_extinfo extinfo;	/* Display extended info */

	struct drm_bridge panel_bridge;
	struct drm_connector *connector;
	bool is_panel_lp_mode;
#ifdef DYNAMIC_REFRESH_RATE
	int display_refresh_rate;	/* Display rate in Hz */
#endif
	bool sensor_sleep;		/* True if suspend called */
	struct wakeup_source *wakesrc;	/* Wake Lock struct */

	/* input lock */
	struct mutex input_report_mutex;	/* Mutex for input report */

	/* switches for features */
	int gesture_enabled;	/* Gesture during suspend */
	int glove_enabled;	/* Glove mode */
	int charger_enabled;	/* Charger mode */
	int stylus_enabled;	/* Stylus mode */
	int cover_enabled;	/* Cover mode */
	int grip_enabled;	/* Grip mode */
#if IS_ENABLED(CONFIG_TOUCHSCREEN_HEATMAP)
	int heatmap_mode;	/* heatmap mode*/
	bool v4l2_mutual_strength_updated;
#endif
	/* Stop changing motion filter and keep fw design */
	bool use_default_mf;
	/* Motion filter finite state machine (FSM) state */
	motion_filter_state_t mf_state;
	/* Time of initial single-finger touch down. This timestamp is used to
	 * compute the duration a single finger is touched before it is lifted.
	 */
	ktime_t mf_downtime;

#if IS_ENABLED(CONFIG_TOUCHSCREEN_TBN)
	u32 tbn_register_mask;
#endif

	/* Allow only one thread to execute diag command code*/
	struct mutex diag_cmd_lock;
	/* Allow one process to open procfs node */
	bool diag_node_open;

	/* Touch simulation details */
	struct fts_touchsim touchsim;

	struct proc_dir_entry *fts_dir;

	/* Select the output type of the scriptless protocol
	 * (binary = 1  or hex string = 0) */
	u8 bin_output;

	/*  store the amount of data to print into the shell */
	int limit;
	/* store the chuk of data that should be printed in this iteration */
	int chunk;
	/* store the amount of data already printed in the shell */
	int printed;

	/* store the information of the Scriptless message received */
	Message mess;

	SysInfo systemInfo;

	/* The tests to perform during the Mass Production Test */
	TestToDo tests;

	/* Variable which contains the limit file during test */
	LimitFile limit_file;

	/* Private variable which implement the Error List */
	ErrorList errors;

	bool system_reseted_up;	/* flag checked during resume to understand
				 * if there was a system reset
				 * and restore the proper state */
	bool system_reseted_down; /* flag checked during suspend to understand
				   * if there was a system reset
				   *  and restore the proper state */

	u8 scanning_frequency;

	/* buffer used to store the command sent from the
	 * MP device file node */
	u32 typeOfCommand[CMD_STR_LEN];

	/* number of parameter passed through the MP device file node */
	int numberParameters;

#ifdef USE_ONE_FILE_NODE
	int feature_feasibility = ERROR_OP_NOT_ALLOW;
#endif

#ifdef PHONE_KEY
	/* store the last update of the key mask published by the IC */
	u8 key_mask;
#endif
	/* pointer to an array of bytes used to store the result of the
	 * function executed */
	u8 *driver_test_buff;
	u8 *stm_fts_cmd_buff;
	loff_t stm_fts_cmd_buff_len;

	/* buffer used to store the message info received */
	char buf_chunk[CHUNK_PROC];

	/* Preallocated i/o read buffer */
	u8 io_read_buf[READ_CHUNK + DUMMY_FIFO];
	/* Preallocated i/o write buffer */
	u8 io_write_buf[WRITE_CHUNK + BITS_64 + DUMMY_FIFO];
	/* Preallocated i/o extra write buffer */
	u8 io_extra_write_buf[WRITE_CHUNK + BITS_64 + DUMMY_FIFO];

};

/* DSI display function used to read panel extinfo */
int dsi_panel_read_vendor_extinfo(struct drm_panel *panel, char *buffer,
				  size_t len);

int fts_chip_powercycle(struct fts_ts_info *info);
extern int input_register_notifier_client(struct notifier_block *nb);
extern int input_unregister_notifier_client(struct notifier_block *nb);

/* export declaration of functions in fts_proc.c */
extern int fts_proc_init(struct fts_ts_info *info);
extern int fts_proc_remove(struct fts_ts_info *info);

/* Bus reference tracking */
int fts_set_bus_ref(struct fts_ts_info *info, u16 ref, bool enable);

#endif
