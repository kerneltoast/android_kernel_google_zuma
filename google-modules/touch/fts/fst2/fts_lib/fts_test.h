/*
  *
  **************************************************************************
  **                        STMicroelectronics				**
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

#include "fts_io.h"

#define MAX_LIMIT_FILE_NAME 100/* /< max number of chars of the limit file name
				 * */

/**
  * Struct which store the data coming from a Production Limit File
  */
struct limit_file {
	char *data;
	int size;
	char name[MAX_LIMIT_FILE_NAME];
};

/*#define LIMITS_H_FILE*/

#ifndef LIMITS_H_FILE
#define LIMITS_FILE	"stm_fts_production_limits.csv"
/* /< Name of the Production Test Limit File */
#else
#define LIMITS_FILE	"NULL"
#endif

#ifdef LIMITS_H_FILE
	#define LIMITS_SIZE_NAME	myArray2_size	/* /< name of the
							 * variable in the
							 * limits header file
							 * which specified the
							 * dimension of the
							 * limits data array */
	#define LIMITS_ARRAY_NAME	myArray2	/* /< name of the
							 * variable in the
							 * limits header file
							 * which specified the
							 * limits data array */
#endif


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

/**
  * Struct used to specify which test perform during the Mass Production Test.
  * For each test item selected in this structure, there should be one or
  * more labels associated in the Limit file from where load the thresholds
  */

struct test_to_do {
	int mutual_ito_raw;	/* /<check ITO Raw data in a node is
				* within the min and max value provided
				* for that node*/
	int mutual_ito_raw_adj;	/* /<check ITO RawH/V data in a node is
				* within the min and max value provided
				* for that node*/
	int mutual_raw;		/* /<check Raw data in a node is within the
				* min and max value provided for that node*/
	int mutual_raw_lp;	/* /<check LP Raw data in a node is within the
				* min and max value provided for that node*/
	int self_force_raw;	/* /<check Self Force Raw in a node is within
				* the min and max value provided for that node*/
	int self_force_raw_lp;	/* /<check Self Force RawLP in a node is within
				* the min and max value provided for that node*/
	int self_sense_raw;	/* /<check Self Sense Raw in a node is within
				* the min and max value provided for that node*/
	int self_sense_raw_lp;	/* /<check Self Sense RawLp in a node is within
				* the min and max value provided for that node*/
	int mutual_cx_lp;	/* /<check Mutual Total CX in a node is within
				* the min and max value provided for that node*/
	int mutual_cx_lp_adj;	/* /<check Mutual Total CX Hor/Ver in a node is
				* within the min and max value provided for that
				* node*/
	int self_force_ix;	/* /<check self Total IX force in a node is
				* within the min and max value provided for
				* that node*/
	int self_force_ix_lp;	/* /<check self Total IX force LP in a node is
				* within the min and max value provided for that
				* node*/
	int self_sense_ix;	/* /<check self Total IX sense in a node is
				* within the min and max value provided for
				* that node*/
	int self_sense_ix_lp;	/* /<check self Total IX sense LP in a node is
				* within the min and max value provided for
				* that node*/
};


#define WAIT_FOR_FRESH_FRAMES		200
#define WAIT_AFTER_SENSEOFF		50
#define NO_INIT				0
#define RETRY_INIT_BOOT			3

#define MS_RAW_ITO_ADJH			"MS_ITO_RAW_ADJ_HOR"
#define MS_RAW_ITO_ADJV			"MS_ITO_RAW_ADJ_VER"
#define MS_RAW_ITO_EACH_NODE_MIN	"MS_ITO_RAW_MIN"
#define MS_RAW_ITO_EACH_NODE_MAX	"MS_ITO_RAW_MAX"
#define MS_RAW_EACH_NODE_MIN		"MS_RAW_MIN"
#define MS_RAW_EACH_NODE_MAX		"MS_RAW_MAX"
#define MS_RAW_LP_EACH_NODE_MIN		"MS_LP_RAW_MIN"
#define MS_RAW_LP_EACH_NODE_MAX		"MS_LP_RAW_MAX"
#define SS_RAW_FORCE_EACH_NODE_MIN	"SS_RAW_FORCE_MIN"
#define SS_RAW_FORCE_EACH_NODE_MAX	"SS_RAW_FORCE_MAX"
#define SS_RAW_SENSE_EACH_NODE_MIN	"SS_RAW_SENSE_MIN"
#define SS_RAW_SENSE_EACH_NODE_MAX	"SS_RAW_SENSE_MAX"
#define SS_RAW_LP_FORCE_EACH_NODE_MIN	"SS_LP_RAW_FORCE_MIN"
#define SS_RAW_LP_FORCE_EACH_NODE_MAX	"SS_LP_RAW_FORCE_MAX"
#define SS_RAW_LP_SENSE_EACH_NODE_MIN	"SS_LP_RAW_SENSE_MIN"
#define SS_RAW_LP_SENSE_EACH_NODE_MAX	"SS_LP_RAW_SENSE_MIN"
#define MS_TOTAL_CX_LP_MIN				"MS_LP_TOTAL_CX_MIN"
#define MS_TOTAL_CX_LP_MAX				"MS_LP_TOTAL_CX_MAX"
#define MS_TOTAL_CX_LP_ADJH				"MS_LP_TOTAL_CX_ADJ_HOR"
#define MS_TOTAL_CX_LP_ADJV				"MS_LP_TOTAL_CX_ADJ_VER"
#define SS_FORCE_TOTAL_IX_MIN			"SS_TOTAL_IX_FORCE_MIN"
#define SS_FORCE_TOTAL_IX_MAX			"SS_TOTAL_IX_FORCE_MAX"
#define SS_FORCE_TOTAL_IX_LP_MIN		"SS_LP_TOTAL_IX_FORCE_MIN"
#define SS_FORCE_TOTAL_IX_LP_MAX		"SS_LP_TOTAL_IX_FORCE_MAX"
#define SS_SENSE_TOTAL_IX_MIN			"SS_TOTAL_IX_SENSE_MIN"
#define SS_SENSE_TOTAL_IX_MAX			"SS_TOTAL_IX_SENSE_MAX"
#define SS_SENSE_TOTAL_IX_LP_MIN		"SS_LP_TOTAL_IX_SENSE_MIN"
#define SS_SENSE_TOTAL_IX_LP_MAX		"SS_LP_TOTAL_IX_SENSE_MAX"





int init_test_to_do(void);
int parse_production_test_limits(char *path, struct limit_file *file,
			char *label, int **data, int *row, int *column);
int read_line(char *data, char *line, int size, int *n);
int get_limits_file(char *path, struct limit_file *file);
int free_limits_file(struct limit_file *file);
int free_current_limits_file(void);
void print_frame_i8(char *label, i8 **matrix, int row, int column);
void print_frame_short(char *label, short **matrix, int row, int column);
void print_frame_u16(char *label, u16 **matrix, int row, int column);
short **array_1d_to_2d_short(short *data, int size, int columns);
i8 **array_1d_to_2d_i8(i8 *data, int size, int columns);
u16 **array_1d_to_2d_u16(u16 *data, int size, int columns);
int compute_adj_horiz_total(short *data, int row, int column, u16 **result);
int compute_adj_vert_total(short *data, int row, int column, u16 **result);
int check_limits_map_adj_total(u16 *data, int row, int column, int *max);
int check_limits_map_total(short *data, int row, int column,
				int *min, int *max);

/**@}*/

/**@}*/

/**  @defgroup mp_api MP API
  * @ingroup mp_test
  * Functions to execute the MP test.
  * The parameters of these functions allow to customize their behavior
  * in order to satisfy different scenarios
  * @{
  */
int fts_production_test_ito(char *path_limits, struct test_to_do *tests);
int fts_production_test_ms_raw(char *path_limits, struct test_to_do *tests);
int fts_production_test_ms_raw_lp(char *path_limits, struct test_to_do *tests);
int fts_production_test_ss_raw(char *path_limits, struct test_to_do *tests);
int fts_production_test_ss_raw_lp(char *path_limits, struct test_to_do *tests);
int fts_production_test_ms_cx_lp(char *path_limits, int stop_on_fail,
					struct test_to_do *tests);
int fts_production_test_ss_ix(char *path_limits, struct test_to_do *tests);
int fts_production_test_ss_ix_lp(char *path_limits, struct test_to_do *tests);
int fts_production_test_main(char *path_limits, int stop_on_fail,
					struct test_to_do *tests, int do_init);
/** @}*/


#endif
