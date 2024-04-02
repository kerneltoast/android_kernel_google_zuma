/*
  *
  **************************************************************************
  **                        STMicroelectronics				**
  **************************************************************************
  **                        marco.cali@st.com				**
  **************************************************************************
  *                                                                        *
  *			FTS API for MP test				 **
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file ftsTest.h
  * \brief Contains all the definitions and structs related to the Mass
  *Production Test
  */

#ifndef FTS_TEST_H
#define FTS_TEST_H

#include "ftsSoftware.h"
#include "ftsFrame.h"

#ifndef LIMITS_H_FILE
/* /< Name of the Production Test Limit File */
#define LIMITS_FILE			"stm_fts_production_limits.csv"
#else
#define LIMITS_FILE			"NULL"
#endif

#define WAIT_FOR_FRESH_FRAMES		200	/* /< Time in ms to wait after
						 * start to sensing before
						 * reading a frame */
#define WAIT_AFTER_SENSEOFF		50	/* /< Time in ms to wait after
						 * stop sensing and before
						 * reading a frame from
						 * memory */

#define NO_INIT				0	/* /< No Initialization required
						 *  during the MP */

#define RETRY_INIT_BOOT			3	/* /< number of retry of the
						 * init process at boot */

#define SENS_TEST_NUM_FRAMES		100 /* /< number of frames to read */
#define SENS_TEST_PERC_TARGET_PRECAL	20	/* /< +-% of target value within
						 * all the node of the frames
						 * should be contained */
#define SENS_TEST_PERC_TARGET_POSTCAL	4	/* /< +-% of target value within
						 * should be contained the
						 * avarege of the nodes and the
						 * deltas for each node */

/** @defgroup mp_test Mass Production Test
  * Mass production test API.
  * Mass Production Test (MP) should be executed at least one time in the life
  * of every device \n
  * It used to verify that tit is not present any hardware damage and
  * initialize some value of the chip in order to guarantee the working
  * performance \n
  * The MP test is made up by 3 steps:
  * - ITO test = production_test_ito() \n
  * - Initialization = production_test_initialization() \n
  * - Data Test = production_test_data(),
  * it is possible to select which items test thanks to the TestToDo struct\n
  * To execute the Data Test it is mandatory load some thresholds that
  * are stored in the Limit File.
  * @{
  */

/** @defgroup limit_file Limit File
  * @ingroup mp_test
  * Production Test Limit File is a csv which contains thresholds of the data to
  * test.
  * This file can be loaded from the file system or stored as a header file
  * according to the LIMITS_H_FILE define \n
  * For each selectable test item there can be one or more associated labels
  * which store the corresponding thresholds \n
  * @{
  */
/* LABELS PRODUCTION TEST LIMITS FILE */
/** @defgroup test_labels Test Items Labels
  * @ingroup limit_file
  * Labels present in the Limit File and associated to the test items of
  * TestToDo
  * @{
  */
#define MS_RAW_MIN_MAX			"MS_RAW_DATA_MIN_MAX"
#define MS_RAW_EACH_NODE_MIN		"MS_RAW_DATA_EACH_MIN"
#define MS_RAW_EACH_NODE_MAX		"MS_RAW_DATA_EACH_MAX"
#define MS_RAW_GAP			"MS_RAW_DATA_GAP"
#define MS_RAW_ADJH			"MS_RAW_DATA_ADJ_HORIZONTAL"
#define MS_RAW_ADJV			"MS_RAW_DATA_ADJ_VERTICAL"
#define MS_RAW_ITO_ADJH			"MS_RAW_ITO_DATA_ADJ_HORIZONTAL"
#define MS_RAW_ITO_ADJV			"MS_RAW_ITO_DATA_ADJ_VERTICAL"
#define MS_RAW_ITO_MIN_MAX		"MS_RAW_ITO_MIN_MAX"
#define MS_RAW_ITO_EACH_NODE_MIN	"MS_RAW_ITO_DATA_MIN"
#define MS_RAW_ITO_EACH_NODE_MAX	"MS_RAW_ITO_DATA_MAX"
#define MS_RAW_LP_MIN_MAX		"MS_RAW_LOWPOWER_DATA_MIN_MAX"
#define MS_RAW_LP_EACH_NODE_MIN		"MS_RAW_LOWPOWER_DATA_EACH_MIN"
#define MS_RAW_LP_EACH_NODE_MAX		"MS_RAW_LOWPOWER_DATA_EACH_MAX"
#define MS_RAW_LP_GAP			"MS_RAW_LOWPOWER_DATA_GAP"
#define MS_RAW_LP_ADJH			"MS_RAW_LOWPOWER_DATA_ADJ_HORIZONTAL"
#define MS_RAW_LP_ADJV			"MS_RAW_LOWPOWER_DATA_ADJ_VERTICAL"
#define MS_RAW_ADJH_GAP			"MS_RAW_DATA_ADJ_HORIZONTAL_P2P"
#define MS_RAW_ADJV_GAP			"MS_RAW_DATA_ADJ_VERTICAL_P2P"
#define MS_RAW_ADJ_PEAK			"MS_RAW_DATA_ADJ_PEAK"
#define MS_CX1_MIN_MAX			"MS_TOUCH_ACTIVE_CX1_MIN_MAX"
#define MS_CX2_MAP_MIN			"MS_TOUCH_ACTIVE_CX2_MIN"
#define MS_CX2_MAP_MAX			"MS_TOUCH_ACTIVE_CX2_MAX"
#define MS_CX2_ADJH_MAP_MAX		"MS_TOUCH_ACTIVE_CX2_ADJ_HORIZONTAL"
#define MS_CX2_ADJV_MAP_MAX		"MS_TOUCH_ACTIVE_CX2_ADJ_VERTICAL"
#define MS_TOTAL_CX_MAP_MIN		"MS_TOUCH_ACTIVE_TOTAL_CX_MIN"
#define MS_TOTAL_CX_MAP_MAX		"MS_TOUCH_ACTIVE_TOTAL_CX_MAX"
#define MS_TOTAL_CX_ADJH_MAP_MAX	"MS_TOUCH_ACTIVE_TOTAL_CX_ADJ_HORIZONTAL"
#define MS_TOTAL_CX_ADJV_MAP_MAX	"MS_TOUCH_ACTIVE_TOTAL_CX_ADJ_VERTICAL"
#define MS_CX1_LP_MIN_MAX		"MS_TOUCH_LOWPOWER_CX1_MIN_MAX"
#define MS_CX2_LP_MAP_MIN		"MS_TOUCH_LOWPOWER_CX2_MIN"
#define MS_CX2_LP_MAP_MAX		"MS_TOUCH_LOWPOWER_CX2_MAX"
#define MS_CX2_ADJH_LP_MAP_MAX		"MS_TOUCH_LOWPOWER_CX2_ADJ_HORIZONTAL"
#define MS_CX2_ADJV_LP_MAP_MAX		"MS_TOUCH_LOWPOWER_CX2_ADJ_VERTICAL"
#define MS_TOTAL_CX_LP_MAP_MIN		"MS_TOUCH_LOWPOWER_TOTAL_CX_MIN"
#define MS_TOTAL_CX_LP_MAP_MAX		"MS_TOUCH_LOWPOWER_TOTAL_CX_MAX"
#define MS_TOTAL_CX_ADJH_LP_MAP_MAX "MS_TOUCH_LOWPOWER_TOTAL_CX_ADJ_HORIZONTAL"
#define MS_TOTAL_CX_ADJV_LP_MAP_MAX "MS_TOUCH_LOWPOWER_TOTAL_CX_ADJ_VERTICAL"
#define SS_RAW_FORCE_MIN_MAX		"SS_RAW_DATA_FORCE_MIN_MAX"
#define SS_RAW_FORCE_EACH_NODE_MIN	"SS_RAW_DATA_FORCE_EACH_MIN"
#define SS_RAW_FORCE_EACH_NODE_MAX	"SS_RAW_DATA_FORCE_EACH_MAX"
#define SS_RAW_SENSE_MIN_MAX		"SS_RAW_DATA_SENSE_MIN_MAX"
#define SS_RAW_SENSE_EACH_NODE_MIN	"SS_RAW_DATA_SENSE_EACH_MIN"
#define SS_RAW_SENSE_EACH_NODE_MAX	"SS_RAW_DATA_SENSE_EACH_MAX"
#define SS_RAW_FORCE_GAP		"SS_RAW_DATA_FORCE_GAP"
#define SS_RAW_SENSE_GAP		"SS_RAW_DATA_SENSE_GAP"
#define SS_RAW_LP_FORCE_MIN_MAX		"SS_RAW_LOWPOWER_DATA_FORCE_MIN_MAX"
#define SS_RAW_LP_SENSE_MIN_MAX		"SS_RAW_LOWPOWER_DATA_SENSE_MIN_MAX"
#define SS_RAW_LP_FORCE_EACH_NODE_MIN	"SS_RAW_LOWPOWER_DATA_FORCE_EACH_MIN"
#define SS_RAW_LP_FORCE_EACH_NODE_MAX	"SS_RAW_LOWPOWER_DATA_FORCE_EACH_MAX"
#define SS_RAW_LP_SENSE_MIN_MAX		"SS_RAW_LOWPOWER_DATA_SENSE_MIN_MAX"
#define SS_RAW_LP_SENSE_EACH_NODE_MIN	"SS_RAW_LOWPOWER_DATA_SENSE_EACH_MIN"
#define SS_RAW_LP_SENSE_EACH_NODE_MAX	"SS_RAW_LOWPOWER_DATA_SENSE_EACH_MAX"
#define SS_RAW_LP_FORCE_GAP		"SS_RAW_LOWPOWER_DATA_FORCE_GAP"
#define SS_RAW_LP_SENSE_GAP		"SS_RAW_LOWPOWER_DATA_SENSE_GAP"
#define SS_IX1_FORCE_MIN_MAX		"SS_TOUCH_ACTIVE_IX1_FORCE_MIN_MAX"
#define SS_IX1_SENSE_MIN_MAX		"SS_TOUCH_ACTIVE_IX1_SENSE_MIN_MAX"
#define SS_CX1_FORCE_MIN_MAX		"SS_TOUCH_ACTIVE_CX1_FORCE_MIN_MAX"
#define SS_CX1_SENSE_MIN_MAX		"SS_TOUCH_ACTIVE_CX1_SENSE_MIN_MAX"
#define SS_IX2_FORCE_MAP_MIN		"SS_TOUCH_ACTIVE_IX2_FORCE_MIN"
#define SS_IX2_FORCE_MAP_MAX		"SS_TOUCH_ACTIVE_IX2_FORCE_MAX"
#define SS_IX2_SENSE_MAP_MIN		"SS_TOUCH_ACTIVE_IX2_SENSE_MIN"
#define SS_IX2_SENSE_MAP_MAX		"SS_TOUCH_ACTIVE_IX2_SENSE_MAX"
#define SS_IX2_FORCE_ADJV_MAP_MAX	"SS_TOUCH_ACTIVE_IX2_ADJ_VERTICAL"
#define SS_IX2_SENSE_ADJH_MAP_MAX	"SS_TOUCH_ACTIVE_IX2_ADJ_HORIZONTAL"
#define SS_CX2_FORCE_MAP_MIN		"SS_TOUCH_ACTIVE_CX2_FORCE_MIN"
#define SS_CX2_FORCE_MAP_MAX		"SS_TOUCH_ACTIVE_CX2_FORCE_MAX"
#define SS_CX2_SENSE_MAP_MIN		"SS_TOUCH_ACTIVE_CX2_SENSE_MIN"
#define SS_CX2_SENSE_MAP_MAX		"SS_TOUCH_ACTIVE_CX2_SENSE_MAX"
#define SS_CX2_FORCE_ADJV_MAP_MAX	"SS_TOUCH_ACTIVE_CX2_ADJ_VERTICAL"
#define SS_CX2_SENSE_ADJH_MAP_MAX	"SS_TOUCH_ACTIVE_CX2_ADJ_HORIZONTAL"

/* TOTAL SS */
#define SS_TOTAL_IX_FORCE_MAP_MIN	"SS_TOUCH_ACTIVE_TOTAL_IX_FORCE_MIN"
#define SS_TOTAL_IX_FORCE_MAP_MAX	"SS_TOUCH_ACTIVE_TOTAL_IX_FORCE_MAX"
#define SS_TOTAL_IX_SENSE_MAP_MIN	"SS_TOUCH_ACTIVE_TOTAL_IX_SENSE_MIN"
#define SS_TOTAL_IX_SENSE_MAP_MAX	"SS_TOUCH_ACTIVE_TOTAL_IX_SENSE_MAX"
#define SS_TOTAL_IX_FORCE_ADJV_MAP_MAX	"SS_TOUCH_ACTIVE_TOTAL_IX_ADJ_VERTICAL"
#define SS_TOTAL_IX_SENSE_ADJH_MAP_MAX \
	"SS_TOUCH_ACTIVE_TOTAL_IX_ADJ_HORIZONTAL"
#define SS_TOTAL_CX_FORCE_MAP_MIN	"SS_TOUCH_ACTIVE_TOTAL_CX_FORCE_MIN"
#define SS_TOTAL_CX_FORCE_MAP_MAX	"SS_TOUCH_ACTIVE_TOTAL_CX_FORCE_MAX"
#define SS_TOTAL_CX_SENSE_MAP_MIN	"SS_TOUCH_ACTIVE_TOTAL_CX_SENSE_MIN"
#define SS_TOTAL_CX_SENSE_MAP_MAX	"SS_TOUCH_ACTIVE_TOTAL_CX_SENSE_MAX"
#define SS_TOTAL_CX_FORCE_ADJV_MAP_MAX	"SS_TOUCH_ACTIVE_TOTAL_CX_ADJ_VERTICAL"
#define SS_TOTAL_CX_SENSE_ADJH_MAP_MAX \
	"SS_TOUCH_ACTIVE_TOTAL_CX_ADJ_HORIZONTAL"

/* Idle (LP)  version*/
#define SS_IX1_LP_FORCE_MIN_MAX		"SS_TOUCH_IDLE_IX1_FORCE_MIN_MAX"
#define SS_IX1_LP_SENSE_MIN_MAX		"SS_TOUCH_IDLE_IX1_SENSE_MIN_MAX"
#define SS_CX1_LP_FORCE_MIN_MAX		"SS_TOUCH_IDLE_CX1_FORCE_MIN_MAX"
#define SS_CX1_LP_SENSE_MIN_MAX		"SS_TOUCH_IDLE_CX1_SENSE_MIN_MAX"
#define SS_IX2_LP_FORCE_MAP_MIN		"SS_TOUCH_IDLE_IX2_FORCE_MIN"
#define SS_IX2_LP_FORCE_MAP_MAX		"SS_TOUCH_IDLE_IX2_FORCE_MAX"
#define SS_IX2_LP_SENSE_MAP_MIN		"SS_TOUCH_IDLE_IX2_SENSE_MIN"
#define SS_IX2_LP_SENSE_MAP_MAX		"SS_TOUCH_IDLE_IX2_SENSE_MAX"
#define SS_IX2_LP_FORCE_ADJV_MAP_MAX	"SS_TOUCH_IDLE_IX2_ADJ_VERTICAL"
#define SS_IX2_LP_SENSE_ADJH_MAP_MAX	"SS_TOUCH_IDLE_IX2_ADJ_HORIZONTAL"
#define SS_CX2_LP_FORCE_MAP_MIN		"SS_TOUCH_IDLE_CX2_FORCE_MIN"
#define SS_CX2_LP_FORCE_MAP_MAX		"SS_TOUCH_IDLE_CX2_FORCE_MAX"
#define SS_CX2_LP_SENSE_MAP_MIN		"SS_TOUCH_IDLE_CX2_SENSE_MIN"
#define SS_CX2_LP_SENSE_MAP_MAX		"SS_TOUCH_IDLE_CX2_SENSE_MAX"
#define SS_CX2_LP_FORCE_ADJV_MAP_MAX	"SS_TOUCH_IDLE_CX2_ADJ_VERTICAL"
#define SS_CX2_LP_SENSE_ADJH_MAP_MAX	"SS_TOUCH_IDLE_CX2_ADJ_HORIZONTAL"


/* TOTAL SS */
#define SS_TOTAL_IX_LP_FORCE_MAP_MIN	"SS_TOUCH_IDLE_TOTAL_IX_FORCE_MIN"
#define SS_TOTAL_IX_LP_FORCE_MAP_MAX	"SS_TOUCH_IDLE_TOTAL_IX_FORCE_MAX"
#define SS_TOTAL_IX_LP_SENSE_MAP_MIN	"SS_TOUCH_IDLE_TOTAL_IX_SENSE_MIN"
#define SS_TOTAL_IX_LP_SENSE_MAP_MAX	"SS_TOUCH_IDLE_TOTAL_IX_SENSE_MAX"
#define SS_TOTAL_IX_LP_FORCE_ADJV_MAP_MAX \
	"SS_TOUCH_IDLE_TOTAL_IX_ADJ_VERTICAL"
#define SS_TOTAL_IX_LP_SENSE_ADJH_MAP_MAX \
	"SS_TOUCH_IDLE_TOTAL_IX_ADJ_HORIZONTAL"
#define SS_TOTAL_CX_LP_FORCE_MAP_MIN	"SS_TOUCH_IDLE_TOTAL_CX_FORCE_MIN"
#define SS_TOTAL_CX_LP_FORCE_MAP_MAX	"SS_TOUCH_IDLE_TOTAL_CX_FORCE_MAX"
#define SS_TOTAL_CX_LP_SENSE_MAP_MIN	"SS_TOUCH_IDLE_TOTAL_CX_SENSE_MIN"
#define SS_TOTAL_CX_LP_SENSE_MAP_MAX	"SS_TOUCH_IDLE_TOTAL_CX_SENSE_MAX"
#define SS_TOTAL_CX_LP_FORCE_ADJV_MAP_MAX \
	"SS_TOUCH_IDLE_TOTAL_CX_ADJ_VERTICAL"
#define SS_TOTAL_CX_LP_SENSE_ADJH_MAP_MAX \
	"SS_TOUCH_IDLE_TOTAL_CX_ADJ_HORIZONTAL"

/* KEYS */
#define MS_KEY_RAW_MIN_MAX		"MS_KEY_RAW_DATA_MIN_MAX"
#define MS_KEY_CX1_MIN_MAX		"MS_KEY_CX1_MIN_MAX"
#define MS_KEY_CX2_MAP_MIN		"MS_KEY_CX2_MIN"
#define MS_KEY_CX2_MAP_MAX		"MS_KEY_CX2_MAX"
#define MS_KEY_TOTAL_CX_MAP_MIN		"MS_KEY_TOTAL_CX_MIN"
#define MS_KEY_TOTAL_CX_MAP_MAX		"MS_KEY_TOTAL_CX_MAX"

/* CONSTANT TOTAL IX */
#define SS_IX1_FORCE_W			"IX1_FORCE_W"
#define SS_IX2_FORCE_W			"IX2_FORCE_W"
#define SS_IX1_SENSE_W			"IX1_SENSE_W"
#define SS_IX2_SENSE_W			"IX2_SENSE_W"


int initTestToDo(struct fts_ts_info *info);
int computeAdjHoriz(i8 *data, int row, int column, u8 **result);
int computeAdjHorizTotal(short *data, int row, int column, u16 **result);
int computeAdjVert(i8 *data, int row, int column, u8 **result);
int computeAdjVertTotal(short *data, int row, int column, u16 **result);
int computeAdjHorizFromU(u8 *data, int row, int column, u8 **result);
int computeAdjHorizTotalFromU(u16 *data, int row, int column, u16 **result);
int computeAdjVertFromU(u8 *data, int row, int column, u8 **result);
int computeAdjVertTotalFromU(u16 *data, int row, int column, u16 **result);
int checkLimitsMinMax(short *data, int row, int column, int min, int max);
int checkLimitsMap(i8 *data, int row, int column, int *min, int *max);
int checkLimitsMapTotal(short *data, int row, int column, int *min, int *max);
int checkLimitsMapFromU(u8 *data, int row, int column, int *min, int *max);
int checkLimitsMapTotalFromU(u16 *data, int row, int column, int *min,
			     int *max);
int checkLimitsMapAdj(u8 *data, int row, int column, int *max);
int checkLimitsMapAdjTotal(u16 *data, int row, int column, int *max);
int checkLimitsGap(short *data, int row, int column, int threshold);
int checkLimitsGapOffsets(short *data, int row, int column, int threshold,
		int row_start, int column_start, int row_end, int column_end);

/**  @defgroup mp_api MP API
  * @ingroup mp_test
  * Functions to execute the MP test.
  * The parameters of these functions allow to customize their behavior
  * in order to satisfy different scenarios
  * @{
  */
int production_test_ito(struct fts_ts_info *info, const char *path_limits,
			MutualSenseFrame *frame, u16 *max_val);
int production_test_initialization(struct fts_ts_info *info, u8 type);
int production_test_main(struct fts_ts_info *info, const char *pathThresholds,
			 int stop_on_fail, int saveInit, u8 mpflag);
int production_test_ms_raw(struct fts_ts_info *info, const char *path_limits,
			   int stop_on_fail);
int production_test_ms_raw_lp(struct fts_ts_info *info,
			      const char *path_limits, int stop_on_fail);
int production_test_ms_cx(struct fts_ts_info *info, const char *path_limits,
			  int stop_on_fail);
int production_test_ms_cx_lp(struct fts_ts_info *info, const char *path_limits,
			     int stop_on_fail);
int production_test_ss_raw(struct fts_ts_info *info, const char *path_limits,
			   int stop_on_fail);
int production_test_ss_raw_lp(struct fts_ts_info *info,
			      const char *path_limits, int stop_on_fail);
int production_test_ss_ix_cx(struct fts_ts_info *info, const char *path_limits,
			     int stop_on_fail);
int production_test_ss_ix_cx_lp(struct fts_ts_info *info,
				const char *path_limits, int stop_on_fail);
int production_test_data(struct fts_ts_info *info, const char *path_limits,
			 int stop_on_fail);
int production_test_ms_key_cx(struct fts_ts_info *info,
			      const char *path_limits, int stop_on_fail);
int production_test_ms_key_raw(struct fts_ts_info *info,
			       const char *path_limits);
/** @}*/

/**
  * @addtogroup limit_file
  * @{
  */
int parseProductionTestLimits(struct fts_ts_info *info, const char *path,
			      LimitFile *file, char *label, int **data,
			      int *row, int *column);
int readLine(char *data, char *line, int size, int *n);
int getLimitsFile(struct fts_ts_info *info, const char *path, LimitFile *file);
int freeLimitsFile(LimitFile *file);
int freeCurrentLimitsFile(struct fts_ts_info *info);
/**@}*/

int tp_sensitivity_test_pre_cal_ms(struct fts_ts_info *info,
				   MutualSenseFrame *finalFrame, short target,
				   int percentage);
int tp_sensitivity_test_pre_cal_ss(struct fts_ts_info *info,
				   SelfSenseFrame *finalFrame, short target,
				   int percentage);
int tp_sensitivity_compute_gains(struct fts_ts_info *info,
				  MutualSenseFrame *frame, short target,
				 int saveGain);
int tp_sensitivity_test_post_cal_ms(struct fts_ts_info *info,
				    MutualSenseFrame *finalFrame,
				    MutualSenseFrame *deltas, short target,
				    int percentage, int *mean_normal,
				    int *mean_edge);
int tp_sensitivity_set_scan_mode(struct fts_ts_info *info, u8 scan, int enableGains);
int tp_sensitivity_mode(struct fts_ts_info *info, u8 enter, int saveGain);
int tp_sensitivity_test_std_ms(struct fts_ts_info *info, int numFrames,
			       MutualSenseFrame *std);
#endif
