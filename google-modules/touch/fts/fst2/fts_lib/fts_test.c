/*
  *
  **************************************************************************
  **                        STMicroelectronics				  **
  **************************************************************************
  *                                                                        *
  *			   FTS API for MP test				   *
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file ftsTest.c
  * \brief Contains all the functions related to the Mass Production Test
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
#include <linux/firmware.h>

#include "fts_test.h"
#include "fts_error.h"

#ifdef LIMITS_H_FILE
#include "../fts_limits.h"
#endif

extern struct sys_info system_info;
struct test_to_do tests;/* /< global variable that specify the tests to
			* perform during the Mass Production Test */
static struct limit_file limit_file;/* /< variable which contains the limit file
				 * during test */

/**
  * Initialize the testToDo variable with the default tests to perform during
  * the Mass Production Test
  * @return OK
  */
int init_test_to_do(void)
{
	/*** Initialize Limit File ***/
	limit_file.size = 0;
	limit_file.data = NULL;
	strlcpy(limit_file.name, " ", MAX_LIMIT_FILE_NAME);

	tests.mutual_ito_raw = 1;
	tests.mutual_ito_raw_adj = 1;
	tests.mutual_raw = 1;
	tests.mutual_raw_lp = 1;
	tests.self_force_raw = 1;
	tests.self_force_raw_lp = 1;
	tests.self_sense_raw = 1;
	tests.self_sense_raw_lp = 1;
	tests.mutual_cx_lp = 1;
	tests.mutual_cx_lp_adj = 1;
	tests.self_force_ix = 1;
	tests.self_force_ix_lp = 1;
	tests.self_sense_ix = 1;
	tests.self_sense_ix_lp = 1;
	return OK;
}

/**
  * Compute the Horizontal adjacent matrix of short values doing the abs of
  * the difference between the column i with the i-1 one.
  * Both the original data matrix and the adj matrix are disposed as 1 dimension
  *  array one row after the other \n
  * The resulting matrix has one column less than the starting original one \n
  * @param data pointer to the array of signed bytes containing the original
  * data
  * @param row number of rows of the original data
  * @param column number of columns of the original data
  * @param result pointer of a pointer to an array of unsigned bytes which
  * will contain the adj matrix
  * @return OK if success or an error code which specify the type of error
  */
int compute_adj_horiz_total(short *data, int row, int column, u16 **result)
{
	int i, j;
	int size = row * (column - 1);

	if (column < 2) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	*result = (u16 *)kmalloc(size * sizeof(u16), GFP_KERNEL);
	if (*result == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			 ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	for (i = 0; i < row; i++)
		for (j = 1; j < column; j++)
			*(*result + (i * (column - 1) + (j - 1))) =
			abs(data[i * column + j] - data[i * column + (j - 1)]);

	return OK;
}

/**
  * Compute the Vertical adjacent matrix of short values doing the abs of
  * the difference between the row i with the i-1 one.
  * Both the original data matrix and the adj matrix are disposed as 1 dimension
  * array one row after the other. \n
  * The resulting matrix has one column less than the starting original one \n
  * @param data pointer to the array of signed bytes containing the original
  * data
  * @param row number of rows of the original data
  * @param column number of columns of the original data
  * @param result pointer of a pointer to an array of unsigned bytes which will
  * contain the adj matrix
  * @return OK if success or an error code which specify the type of error
  */
int compute_adj_vert_total(short *data, int row, int column, u16 **result)
{
	int i, j;
	int size = (row - 1) * (column);

	if (row < 2) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	*result = (u16 *)kmalloc(size * sizeof(u16), GFP_KERNEL);
	if (*result == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			 ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	for (i = 1; i < row; i++)
		for (j = 0; j < column; j++)
			*(*result + ((i - 1) * column + j)) =
			abs(data[i * column + j] - data[(i - 1) * column + j]);

	return OK;
}

/**
  * Check that each value of a matrix of u16 doesn't exceed a specific Max value
  * set for each node (max value is included in the interval).
  * The matrixes of data and max values are stored as 1 dimension arrays one row
  * after the other.
  * @param data pointer to the array of short containing the data to check
  * @param row number of rows of data
  * @param column number of columns of data
  * @param max pointer to a matrix which specify the Maximum value allowed for
  * each node
  * @return the number of elements that overcome the specified interval (0 = OK)
  */
int check_limits_map_adj_total(u16 *data, int row, int column, int *max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * column + j] > max[i * column + j]) {
				log_info(1,
				"%s: Node[%d,%d] = %d exceed limit > %d\n",
				__func__, i, j, data[i * column + j], max[i *
					     column + j]);
				count++;
			}
		}
	}
	return count;
}

/**
  * Check that each value of a matrix of short doesn't exceed a specific min and
  *  Max value  set for each node (these values are included in the interval).
  * The matrixes of data, min and max values are stored as 1 dimension arrays
  * one row after the other.
  * @param data pointer to the array of short containing the data to check
  * @param row number of rows of data
  * @param column number of columns of data
  * @param min pointer to a matrix which specify the minimum value allowed for
  * each node
  * @param max pointer to a matrix which specify the Maximum value allowed for
  * each node
  * @return the number of elements that overcome the specified interval (0 = OK)
  */
int check_limits_map_total(short *data, int row, int column,
				int *min, int *max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * column + j] < min[i * column + j] ||
				data[i * column + j] > max[i * column + j]) {
				log_info(1,
					"%s: Node[%d,%d] = %d exceed limit [%d, %d]\n",
					__func__, i, j, data[i * column + j],
					min[i *
					column
					+ j], max[i * column + j]);
				count++;
			}
		}
	}

	return count;	/* if count is 0 = OK, test completed successfully */
}

/**
  * Print in the kernel log a label followed by a matrix of i8 row x columns and
  * free its memory
  * @param label pointer to the string to print before the actual matrix
  * @param matrix reference to the matrix of u8 which contain the actual data
  * @param row number of rows on which the matrix should be print
  * @param column number of columns for each row
  */
void print_frame_i8(char *label, i8 **matrix, int row, int column)
{
	int i, j;
	int buff_len, index;
	char *buff;

	pr_info("%s\n", label);

	if (matrix == NULL)
		return;

	buff_len = (4 + 1) * column + 1; /* -128 str len: 4 */
	buff = kzalloc(buff_len, GFP_KERNEL);
	if (buff == NULL) {
		pr_err("%s: fail to allocate buffer\n", __func__);
		return;
	}

	for (i = 0; i < row; i++) {
		if (!matrix[i])
			break;
		index = 0;
		for (j = 0; j < column; j++)
			index += scnprintf(buff + index, buff_len - index,
					"%d ", matrix[i][j]);
		pr_info("%s\n", buff);
		kfree(matrix[i]);
	}
	kfree(matrix);
	kfree(buff);
}

/**
  * Print in the kernel log a label followed by a matrix of short row x columns
  *and free its memory
  * @param label pointer to the string to print before the actual matrix
  * @param matrix reference to the matrix of short which contain the actual data
  * @param row number of rows on which the matrix should be print
  * @param column number of columns for each row
  */
void print_frame_short(char *label, short **matrix, int row, int column)
{
	int i, j;
	int buff_len, index;
	char *buff;

	pr_info("%s\n", label);

	if (matrix == NULL)
		return;

	buff_len = (6 + 1) * column + 1; /* -32768 str len: 6 */
	buff = kzalloc(buff_len, GFP_KERNEL);
	if (buff == NULL) {
		pr_err("%s: fail to allocate buffer\n", __func__);
		return;
	}

	for (i = 0; i < row; i++) {
		if (!matrix[i])
			break;
		index = 0;
		for (j = 0; j < column; j++)
			index += scnprintf(buff + index, buff_len - index,
					"%d ", matrix[i][j]);
		pr_info("%s\n", buff);
		kfree(matrix[i]);
	}
	kfree(matrix);
	kfree(buff);
}

/**
  * Print in the kernel log a label followed by a matrix of u16 row x columns
  * and free its memory
  * @param label pointer to the string to print before the actual matrix
  * @param matrix reference to the matrix of u16 which contain the actual data
  * @param row number of rows on which the matrix should be print
  * @param column number of columns for each row
  */
void print_frame_u16(char *label, u16 **matrix, int row, int column)
{
	int i, j;
	int buff_len, index;
	char *buff;

	pr_info("%s\n", label);

	if (matrix == NULL)
		return;

	buff_len = (5 + 1) * column + 1; /* 65535 str len: 5 */
	buff = kzalloc(buff_len, GFP_KERNEL);
	if (buff == NULL) {
		pr_err("%s: fail to allocate buffer\n", __func__);
		return;
	}

	for (i = 0; i < row; i++) {
		if (!matrix[i])
			break;
		index = 0;
		for (j = 0; j < column; j++)
			index += scnprintf(buff + index, buff_len - index,
					"%d ", matrix[i][j]);
		pr_info("%s\n", buff);
		kfree(matrix[i]);
	}
	kfree(matrix);
	kfree(buff);
}

/**
  * Transform an array of i8 in a matrix of i8 with a defined number of
  * columns and the resulting number of rows
  * @param data array of bytes to convert
  * @param size size of data
  * @param columns number of columns that the resulting matrix should have.
  * @return a reference to a matrix of short where for each row there are
  * columns elements
  * @warning If size = 0 it will be allocated a matrix 1*1 wich still should
  * be free
  */
i8 **array_1d_to_2d_i8(i8 *data, int size, int columns)
{
	int i;
	i8 **matrix = NULL;

	if (size == 0) {
		matrix = (i8 **)kmalloc(1 *
					   sizeof(i8 *), GFP_KERNEL);
		matrix[0] = (i8 *)kmalloc(0 *
					   sizeof(i8), GFP_KERNEL);
	} else {
		matrix = (i8 **)kmalloc(((int)(size / columns)) * sizeof(i8 *),
					     GFP_KERNEL);

		if (matrix != NULL) {
			for (i = 0; i < (int)(size / columns); i++)
				matrix[i] = (i8 *)kmalloc(columns * sizeof(i8),
							  GFP_KERNEL);

			for (i = 0; i < size; i++)
				matrix[i / columns][i % columns] = data[i];
		}
	}

	return matrix;
}

/**
  * Transform an array of short in a matrix of short with a defined number of
  * columns and the resulting number of rows
  * @param data array of bytes to convert
  * @param size size of data
  * @param columns number of columns that the resulting matrix should have.
  * @return a reference to a matrix of short where for each row there are
  * columns elements
  * @warning If size = 0 it will be allocated a matrix 1*1 wich still should
  * be free
  */
short **array_1d_to_2d_short(short *data, int size, int columns)
{
	int i;
	short **matrix = NULL;

	if (size == 0) {
		matrix = (short **)kmalloc(1 *
			sizeof(short *), GFP_KERNEL);
		matrix[0] = (short *)kmalloc(0 *
			sizeof(short), GFP_KERNEL);
	} else {
		matrix = (short **)kmalloc(((int)(size / columns)) *
			sizeof(short *), GFP_KERNEL);

		if (matrix != NULL) {
			for (i = 0; i < (int)(size / columns); i++)
				matrix[i] = (short *)kmalloc(columns *
					sizeof(short), GFP_KERNEL);

			for (i = 0; i < size; i++)
				matrix[i / columns][i % columns] = data[i];
		}
	}

	return matrix;
}
/**
  * Transform an array of u16 in a matrix of u16 with a defined number of
  * columns and the resulting number of rows
  * @param data array of bytes to convert
  * @param size size of data
  * @param columns number of columns that the resulting matrix should have.
  * @return a reference to a matrix of short where for each row there are
  * columns elements
  * @warning If size = 0 it will be allocated a matrix 1*1 wich still should
  * be free
  */
u16 **array_1d_to_2d_u16(u16 *data, int size, int columns)
{
	int i;
	u16 **matrix = NULL;


	if (size == 0) {
		matrix = (u16 **)kmalloc(1 *
			sizeof(u16 *), GFP_KERNEL);
		matrix[0] = (u16 *)kmalloc(0 *
			sizeof(u16), GFP_KERNEL);
	} else {
		matrix = (u16 **)kmalloc(((int)(size / columns)) *
			sizeof(u16 *), GFP_KERNEL);

		if (matrix != NULL) {
			for (i = 0; i < (int)(size / columns); i++)
				matrix[i] = (u16 *)kmalloc(columns *
					sizeof(u16), GFP_KERNEL);

			for (i = 0; i < size; i++)
				matrix[i / columns][i % columns] = data[i];
		}
	}

	return matrix;
}

/**
  * Retrieve the actual Test Limit data from the system (bin file or header
  *file)
  * @param path name of Production Test Limit file to load or "NULL" if the
  *limits data should be loaded by a .h file
  * @param file pointer to the LimitFile struct which will contains the limits
  *data
  * @return OK if success or an error code which specify the type of error
  *encountered
  */
int get_limits_file(char *path, struct limit_file *file)
{
	const struct firmware *fw = NULL;
	struct device *dev = NULL;
	int fd = -1;

	log_info(1, "%s: Get Limits File starting... %s\n",
			__func__, path);

	if (file->data != NULL) {/* to avoid memory leak on consecutive call of
				 * the function with the same pointer */
		log_info(0,
			"%s Pointer to Limits Data already contains something...freeing its content!\n",
			__func__);
		kfree(file->data);
		file->data = NULL;
		file->size = 0;
	}

	strlcpy(file->name, path, MAX_LIMIT_FILE_NAME);
	if (strncmp(path, "NULL", 4) == 0) {
#ifdef LIMITS_H_FILE
		log_info(1, "%s Loading Limits File from .h!\n", __func__);
		file->size = LIMITS_SIZE_NAME;
		file->data = (char *)kmalloc((file->size) * sizeof(char),
			GFP_KERNEL);
		if (file->data != NULL) {
			memcpy(file->data, (char *)(LIMITS_ARRAY_NAME),
				file->size);
			return OK;
		} else {
			log_info(1,
				"%s: Error while allocating data... ERROR %08X\n",
				__func__,
				path, ERROR_ALLOC);
			return ERROR_ALLOC;
		}
#else
		log_info(1, "%s: limit file path NULL... ERROR %08X\n",
			__func__, ERROR_FILE_NOT_FOUND);
		return ERROR_FILE_NOT_FOUND;
#endif
	} else {
		dev = get_dev();
		if (dev != NULL) {
			log_info(1, "%s: Loading Limits File from .csv!\n",
				__func__);
			fd = request_firmware(&fw, path, dev);
			if (fd == 0) {
				log_info(1, "%s: Start to copy %s...\n",
					__func__, path);
				file->size = fw->size;
				file->data = (char *)kmalloc((file->size) *
					sizeof(char),
					GFP_KERNEL);
				if (file->data != NULL) {
					memcpy(file->data, (char *)fw->data,
						file->size);
					log_info(0,
						"%s: Limit file Size = %d\n",
						__func__,
						file->size);
					release_firmware(fw);
					return OK;
				}
				log_info(1,
					"%s: Error while allocating data... ERROR %08X\n",
					__func__, ERROR_ALLOC);
				release_firmware(fw);
				return ERROR_ALLOC;
			}
			log_info(1,
				"%s: Request the file %s failed... ERROR %08X\n",
				__func__, path, ERROR_FILE_NOT_FOUND);
			return ERROR_FILE_NOT_FOUND;
		}
		log_info(1,
			"%s: Error while getting the device ERROR %08X\n",
			__func__,
			ERROR_FILE_READ);
		return ERROR_FILE_READ;
	}
}

/**
  * Reset and release the memory which store a Production Limit File previously
  *loaded
  * @param file pointer to the LimitFile struct to free
  * @return OK if success or an error code which specify the type of error
  *encountered
  */
int free_limits_file(struct limit_file *file)
{
	log_info(0, "%s: Freeing Limit File ...\n", __func__);
	if (file != NULL) {
		if (file->data != NULL) {
			kfree(file->data);
			file->data = NULL;
		} else
			log_info(0, "%s: Limit File was already freed!\n",
			 __func__);
		file->size = 0;
		strlcpy(file->name, " ", MAX_LIMIT_FILE_NAME);
		return OK;
	}
	log_info(1, "%s: Passed a NULL argument! ERROR %08X\n",
	__func__, ERROR_OP_NOT_ALLOW);
	return ERROR_OP_NOT_ALLOW;
}

/**
  * Reset and release the memory which store the current Limit File previously
  *loaded
  * @return OK if success or an error code which specify the type of error
  *encountered
  */
int free_current_limits_file(void)
{
	return free_limits_file(&limit_file);
}

/**
  * Parse the raw data read from a Production test limit file in order to find
  *the specified information
  * If no limits file data are passed, the function loads and stores the limit
  *file from the system
  * @param path name of Production Test Limit file to load or "NULL" if the
  *limits data should be loaded by a .h file
  * @param file pointer to LimitFile struct that should be parsed or NULL if the
  *limit file in the system should be loaded and then parsed
  * @param label string which identify a particular set of data in the file that
  *want to be loaded
  * @param data pointer to the pointer which will contains the specified limits
  *data as 1 dimension matrix with data arranged row after row
  * @param row pointer to a int variable which will contain the number of row of
  *data
  * @param column pointer to a int variable which will contain the number of
  *column of data
  * @return OK if success or an error code which specify the type of error
  */
int parse_production_test_limits(char *path, struct limit_file *file,
				char *label, int **data, int *row, int *column)
{
	int find = 0;
	char *token = NULL;
	int i = 0;
	int j = 0;
	int z = 0;


	char *line2 = NULL;
	char line[800];
	char *buf = NULL;
	int n, size, pointer = 0, ret = OK;
	char *data_file = NULL;


	if (file == NULL || strcmp(path, file->name) != 0 || file->size == 0) {
		log_info(1,
			"%s: No limit File data passed...try to get them from the system!\n",
			__func__);
		ret = get_limits_file(LIMITS_FILE, &limit_file);
		if (ret < OK) {
			log_info(1,
				"%s: ERROR %08X\n",
				__func__,
				ERROR_FILE_NOT_FOUND);
			return ERROR_FILE_NOT_FOUND;
		}
		size = limit_file.size;
		data_file = limit_file.data;
	} else {
		log_info(1, "%s: Limit File data passed as arguments!\n",
		__func__);
		size = file->size;
		data_file = file->data;
	}

	log_info(1, "%s: The size of the limits file is %d bytes...\n",
		__func__, size);

	while (find == 0) {
		if (read_line(&data_file[pointer], line, size - pointer, &n) <
			0) {
			find = -1;
			break;
		}
		pointer += n;
		if (line[0] == '*') {
			line2 = kstrdup(line, GFP_KERNEL);
			if (line2 == NULL) {
				log_info(1,
					"%s: kstrdup ERROR %08X\n",
					__func__, ERROR_ALLOC);
				ret = ERROR_ALLOC;
				goto END;
			}
			buf = line2;
			line2 += 1;
			token = strsep(&line2, ",");
			if (strcmp(token, label) == 0) {
				find = 1;
				token = strsep(&line2, ",");
				if (token != NULL) {
					if (sscanf(token, "%d", row) == 1)
						log_info(0, "%s Row = %d\n",
							__func__, *row);
					else {
						log_info(0, "%s: ERROR while reading the row value!ERROR %08X\n",
						__func__, ERROR_FILE_PARSE);
						ret = ERROR_FILE_PARSE;
						goto END;
					}

				} else {
					log_info(1,
						"%s: ERROR %08X\n",
						__func__, ERROR_FILE_PARSE);
					ret = ERROR_FILE_PARSE;
					goto END;
				}
				token = strsep(&line2, ",");
				if (token != NULL) {
					if (sscanf(token, "%d", column) == 1)
						log_info(0, "%s Column = %d\n",
							__func__, *column);
					else {
						log_info(0, "%s: ERROR while reading the column value!ERROR %08X\n",
						__func__, ERROR_FILE_PARSE);
						ret = ERROR_FILE_PARSE;
						goto END;
					}

				} else {
					log_info(1,
						"%s: ERROR %08X\n",
						__func__, ERROR_FILE_PARSE);
					ret = ERROR_FILE_PARSE;
					goto END;
				}

				kfree(buf);
				buf = NULL;
				*data = (int *)kmalloc(((*row) * (*column)) *
					sizeof(int), GFP_KERNEL);
				j = 0;
				if (*data == NULL) {
					log_info(1,
						"%s: ERROR %08X\n",
						__func__, ERROR_ALLOC);
					ret = ERROR_ALLOC;
					goto END;
				}

				for (i = 0; i < *row; i++) {
					if (read_line(&data_file[pointer], line,
						size - pointer, &n) < 0) {
						log_info(1,
							"%s: ERROR %08X\n",
							__func__,
							ERROR_FILE_READ);
						ret = ERROR_FILE_READ;
						goto END;
					}
					pointer += n;
					line2 = kstrdup(line, GFP_KERNEL);
					if (line2 == NULL) {
						log_info(1,
							"%s: kstrdup ERROR %08X\n",
							__func__, ERROR_ALLOC);
						ret = ERROR_ALLOC;
						goto END;
					}
					buf = line2;
					token = strsep(&line2, ",");
					for (z = 0; (z < *column) &&
						(token != NULL); z++) {
						if (sscanf(token, "%d",
							((*data) + j)) == 1) {
							j++;
							token =
							strsep(&line2, ",");
						}
					}
					kfree(buf);
					buf = NULL;
				}
				if (j == ((*row) * (*column))) {
					log_info(1, "%s: READ DONE!\n",
						__func__);
					ret = OK;
					goto END;
				}
				log_info(1,
					"%s: ERROR %08X\n",
					__func__, ERROR_FILE_PARSE);
				ret = ERROR_FILE_PARSE;
				goto END;
			}
			kfree(buf);
			buf = NULL;
		}
	}
	log_info(1, "%s: Test Label not found ERROR: %08X\n", __func__,
		ERROR_LABEL_NOT_FOUND);
	ret = ERROR_LABEL_NOT_FOUND;
END:
	if (buf != NULL)
		kfree(buf);
	return ret;
}

/**
  * Read one line of a text file passed as array of byte and terminate it with a
  *termination character '\0'
  * @param data text file as array of bytes
  * @param line pointer to an array of char that will contain the line read
  * @param size size of data
  * @param n pointer to a int variable which will contain the number of
  *characters of the line
  * @return OK if success or an error code which specify the type of error
  */
int read_line(char *data, char *line, int size, int *n)
{
	int i = 0;

	if (size < 1)
		return ERROR_OP_NOT_ALLOW;

	while (data[i] != '\n' && i < size) {
		line[i] = data[i];
		i++;
	}
	*n = i + 1;
	line[i] = '\0';

	return OK;
}

/**
  * Perform an ITO test setting all the possible options
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ito(char *path_limits, struct test_to_do *tests)
{
	int res = OK;
	u8 sett[2] = { 0xFF, 0x07 };
	int i = 0;
	u8 data[8] = { 0 };
	int event_to_search =  0x00;
	struct mutual_sense_frame ms_raw_frame;
	int trows, tcolumns;
	int *thresholds = NULL;
	u16 *adj = NULL;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;

	ms_raw_frame.node_data = NULL;

	log_info(1, "%s: ITO Production test is starting...\n", __func__);
	if (tests->mutual_ito_raw || tests->mutual_ito_raw_adj) {
		res = fts_system_reset(1);
		if (res < OK) {
			res |= ERROR_PROD_TEST_ITO;
			log_info(1, "%s: ERROR %08X\n", __func__,
				res);
			goto goto_error;
		}
		res = fts_write_fw_reg(ITO_TRIGGER_ADDR, sett, 2);
		if (res < OK) {
			res |= ERROR_PROD_TEST_ITO;
			log_info(1, "%s ERROR %08X\n", __func__, res);
			goto goto_error;
		}

		for (i = 0; i < TIMEOUT_FW_REG_STATUS; i++) {
			res = fts_read_fw_reg(ITO_TRIGGER_ADDR, data, 2);
			if (res < OK) {
				res |= ERROR_PROD_TEST_ITO;
				log_info(1, "%s: ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}
			res = (data[0] & 0xFF) || (data[1] & 0x07);
			log_info(1, "%s: Status = %d\n", __func__, res);
			if (!res) {
				log_info(1, "%s: ITO Command finished..\n",
				__func__);
				break;

			}
			msleep(TIMEOUT_RESOLUTION);
		}
		res = poll_for_event(&event_to_search, 1, data, 8);
		if (res < OK) {
			res |= ERROR_PROD_TEST_ITO;
			log_info(1, "%s: ITO failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		log_info(1, "%s: ITO Command = OK!\n", __func__);
		log_info(1, "%s: Collecting MS Raw data...\n",
					__func__);
		res = get_ms_frame(MS_RAW, &ms_raw_frame);
		if (res < OK) {
			res |= ERROR_PROD_TEST_ITO;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		print_frame_short("MS Raw ITO frame =",
			array_1d_to_2d_short(
				ms_raw_frame.node_data,
				ms_raw_frame.
				node_data_size,
				ms_raw_frame.header.
				sense_node),
			ms_raw_frame.header.force_node,
			ms_raw_frame.header.sense_node);
		if (tests->mutual_ito_raw_adj) {
			log_info(1, "%s: MS RAW ITO ADJ TEST:\n", __func__);

			log_info(1, "%s: MS RAW ITO ADJ HORIZONTAL TEST:\n",
				__func__);
			res = compute_adj_horiz_total(ms_raw_frame.node_data,
				ms_raw_frame.header.force_node,
				ms_raw_frame.header.sense_node,
				&adj);
			if (res < OK) {
				res |= ERROR_PROD_TEST_ITO;
				log_info(1,
					"%s: compute adj Horizontal failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file,
				MS_RAW_ITO_ADJH, &thresholds,
				&trows, &tcolumns);
			if (res < OK ||
			(trows != ms_raw_frame.header.force_node ||
			tcolumns != ms_raw_frame.header.sense_node - 1)) {
				res |= ERROR_PROD_TEST_ITO;
				log_info(1,
					"%s: MS_RAW_ITO_ADJH limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}


			res = check_limits_map_adj_total(adj,
				ms_raw_frame.header.force_node,
				ms_raw_frame.header.sense_node - 1,
				thresholds);
			if (res != OK) {
				log_info(1,
					"%s: check limit adj horiz MS RAW ITO ADJH failed...ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: MS RAW ITO ADJ HORIZONTAL TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_ITO |
					 ERROR_PROD_TEST_CHECK_FAIL);
				goto goto_error;
			} else
				log_info(1,
					"%s: MS RAW ITO ADJ HORIZONTAL TEST:.................OK\n",
					__func__);

			kfree(thresholds);
			thresholds = NULL;

			kfree(adj);
			adj = NULL;

			log_info(1, "%s: MS RAW ITO ADJ VERTICAL TEST:\n",
				__func__);
			res = compute_adj_vert_total(ms_raw_frame.node_data,
						ms_raw_frame.header.force_node,
						ms_raw_frame.header.sense_node,
						&adj);
			if (res < OK) {
				res |= ERROR_PROD_TEST_ITO;
				log_info(1,
					"%s: compute adj vert failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file, MS_RAW_ITO_ADJV, &thresholds,
				&trows, &tcolumns);
			if (res < OK ||
			(trows != ms_raw_frame.header.force_node - 1 ||
				tcolumns != ms_raw_frame.header.sense_node)) {
				res |= ERROR_PROD_TEST_ITO;
				log_info(1,
					"%s: MS_RAW_ITO_ADJV limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}


			res = check_limits_map_adj_total(adj,
				ms_raw_frame.header.force_node -
				1, ms_raw_frame.header.sense_node,
				thresholds);
			if (res != OK) {
				log_info(1,
					"%s: check limits adj MS RAW ITO ADJV failed...ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: MS RAW ITO ADJ VERTICAL TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_ITO |
					 ERROR_PROD_TEST_CHECK_FAIL);
				goto goto_error;
			} else
				log_info(1,
					"%s: MS RAW ITO ADJ VERTICAL TEST:.................OK\n",
					__func__);

			kfree(thresholds);
			thresholds = NULL;

			kfree(adj);
			adj = NULL;
		} else
			log_info(1, "%s: MS RAW ITO ADJ TEST SKIPPED:\n",
				__func__);

		if (tests->mutual_ito_raw) {
			log_info(1, "%s: MS RAW ITO MIN MAX TEST:\n", __func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, MS_RAW_ITO_EACH_NODE_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows !=
				ms_raw_frame.header.force_node ||
				tcolumns != ms_raw_frame.header.sense_node)) {
				res |= ERROR_PROD_TEST_ITO;
				log_info(1,
					"%s: MS_RAW_ITO_EACH_NODE_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}
			res = parse_production_test_limits(path_limits,
				&limit_file, MS_RAW_ITO_EACH_NODE_MAX,
				&thresholds_max, &trows, &tcolumns);
			if (res < OK || (trows !=
				ms_raw_frame.header.force_node ||
				tcolumns != ms_raw_frame.header.sense_node)) {
				res |= ERROR_PROD_TEST_ITO;
				log_info(1,
					"%s: MS_RAW__ITO_EACH_NODE_MAX limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}
			res = check_limits_map_total(ms_raw_frame.node_data,
				ms_raw_frame.header.force_node,
				ms_raw_frame.header.sense_node, thresholds_min,
				thresholds_max);
			if (res != OK) {
				log_info(1,
					"%s: check limits min max each node data failed...ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: MS RAW ITO MAP MIN MAX TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_ITO |
					 ERROR_PROD_TEST_CHECK_FAIL);
			} else {
				log_info(1, "%s: MS RAW ITO MAP MIN MAX TEST:.................OK\n",
				__func__);
			}
			if (thresholds_min != NULL) {
				kfree(thresholds_min);
				thresholds_min = NULL;
			}
			if (thresholds_max != NULL) {
				kfree(thresholds_max);
				thresholds_max = NULL;
			}
		} else
			log_info(1, "%s: MS RAW ITO MIN MAX TEST SKIPPED..\n",
			__func__);
	} else
		log_info(1, "%s: MS RAW ITO TEST SKIPPED..\n", __func__);

goto_error:
	if (thresholds != NULL)
		kfree(thresholds);
	if (adj != NULL)
		kfree(adj);
	if (ms_raw_frame.node_data != NULL)
		kfree(ms_raw_frame.node_data);
	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	free_limits_file(&limit_file);
	res |= fts_system_reset(1);
	if (res < OK) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			ERROR_PROD_TEST_ITO);
		res = (res | ERROR_PROD_TEST_ITO);
	}
	return res;
}

/**
  * Perform all the test selected in a TestTodo variable related to MS raw data
  * (touch, keys etc..)
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ms_raw(char *path_limits, struct test_to_do *tests)
{
	int res = OK;
	struct mutual_sense_frame ms_raw_frame;
	int trows, tcolumns;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;
	uint8_t data = SCAN_MODE_LOCK_ACTIVE;

	ms_raw_frame.node_data = NULL;

	log_info(1, "%s: MS RAW DATA TEST STARTING...\n", __func__);
	if (tests->mutual_raw) {
		data = SCAN_MODE_LOCK_ACTIVE;
		res = fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);
		data = SCAN_MODE_HIBERNATE;
		res |= fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);

		log_info(1, "%s: Collecting MS Raw data...\n", __func__);
		res |= get_ms_frame(MS_RAW, &ms_raw_frame);
		if (res < OK) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		print_frame_short("MS Raw frame =",
			array_1d_to_2d_short(
				ms_raw_frame.node_data,
				ms_raw_frame.
				node_data_size,
				ms_raw_frame.header.
				sense_node),
			ms_raw_frame.header.force_node,
			ms_raw_frame.header.sense_node);

		log_info(1, "%s: MS RAW MIN MAX TEST:\n", __func__);
		res = parse_production_test_limits(path_limits,
			&limit_file, MS_RAW_EACH_NODE_MIN,
			&thresholds_min, &trows, &tcolumns);
		if (res < OK || (trows !=
			ms_raw_frame.header.force_node ||
			tcolumns != ms_raw_frame.header.sense_node)) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1,
				"%s: MS_RAW_EACH_NODE_MIN limit parse failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		res = parse_production_test_limits(path_limits,
			&limit_file, MS_RAW_EACH_NODE_MAX,
			&thresholds_max, &trows, &tcolumns);
		if (res < OK || (trows !=
			ms_raw_frame.header.force_node ||
			tcolumns != ms_raw_frame.header.sense_node)) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1,
				"%s: MS_RAW_EACH_NODE_MAX limit parse failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		res = check_limits_map_total(ms_raw_frame.node_data,
			ms_raw_frame.header.force_node,
			ms_raw_frame.header.sense_node, thresholds_min,
			thresholds_max);
		if (res != OK) {
			log_info(1,
				"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
				__func__, res);
			log_info(1,
				"%s: MS RAW MIN MAX TEST:.................FAIL\n\n",
				__func__);
			res = (ERROR_PROD_TEST_RAW |
				 ERROR_PROD_TEST_CHECK_FAIL);
		} else {
			log_info(1, "%s: MS RAW MIN MAX TEST:.................OK\n",
				__func__);
		}
		if (thresholds_min != NULL) {
			kfree(thresholds_min);
			thresholds_min = NULL;
		}
		if (thresholds_max != NULL) {
			kfree(thresholds_max);
			thresholds_max = NULL;
		}
	} else
		log_info(1, "%s: MS RAW DATA TEST SKIPPED...\n", __func__);

goto_error:
	if (ms_raw_frame.node_data != NULL)
		kfree(ms_raw_frame.node_data);
	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	free_limits_file(&limit_file);
	return res;

}

/**
  * Perform all the test selected in a TestTodo variable related to MS low power
  * raw data
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ms_raw_lp(char *path_limits, struct test_to_do *tests)
{
	int res = OK;
	struct mutual_sense_frame ms_raw_frame;
	uint8_t data = SCAN_MODE_LOCK_LP_ACTIVE;
	int trows, tcolumns;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;

	ms_raw_frame.node_data = NULL;

	log_info(1, "%s: MS LP RAW TEST STARTING..\n", __func__);
	if (tests->mutual_raw_lp) {
		res = fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);
		data = SCAN_MODE_HIBERNATE;
		res |= fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);

		log_info(1, "%s: Collecting MS LP Raw data...\n", __func__);
		res |= get_ms_frame(MS_RAW, &ms_raw_frame);
		if (res < OK) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		print_frame_short("MS LP Raw frame =",
			array_1d_to_2d_short(
				ms_raw_frame.node_data,
				ms_raw_frame.
				node_data_size,
				ms_raw_frame.header.
				sense_node),
			ms_raw_frame.header.force_node,
			ms_raw_frame.header.sense_node);

		log_info(1, "%s: MS LP RAW MIN MAX TEST:\n", __func__);
		res = parse_production_test_limits(path_limits,
			&limit_file, MS_RAW_LP_EACH_NODE_MIN,
			&thresholds_min, &trows, &tcolumns);
		if (res < OK || (trows !=
			ms_raw_frame.header.force_node ||
			tcolumns != ms_raw_frame.header.sense_node)) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1,
				"%s: MS_RAW_LP_EACH_NODE_MIN limit parse failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		res = parse_production_test_limits(path_limits,
			&limit_file, MS_RAW_LP_EACH_NODE_MAX,
			&thresholds_max, &trows, &tcolumns);
		if (res < OK || (trows !=
			ms_raw_frame.header.force_node ||
			tcolumns != ms_raw_frame.header.sense_node)) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1,
				"%s: MS_RAW_LP_EACH_NODE_MAX limit parse failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		res = check_limits_map_total(ms_raw_frame.node_data,
			ms_raw_frame.header.force_node,
			ms_raw_frame.header.sense_node, thresholds_min,
			thresholds_max);
		if (res != OK) {
			log_info(1,
				"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
				__func__, res);
			log_info(1,
				"%s: MS LP RAW MIN MAX TEST:.................FAIL\n\n",
				__func__);
			res = (ERROR_PROD_TEST_RAW |
				 ERROR_PROD_TEST_CHECK_FAIL);
		} else {
			log_info(1, "%s: MS LP RAW MIN MAX TEST:.................OK\n",
				__func__);
		}
		if (thresholds_min != NULL) {
			kfree(thresholds_min);
			thresholds_min = NULL;
		}
		if (thresholds_max != NULL) {
			kfree(thresholds_max);
			thresholds_max = NULL;
		}
	} else {
		log_info(1, "%s: MS LP RAW MIN MAX TEST SKIPPED...\n",
			__func__);
	}

goto_error:
	if (ms_raw_frame.node_data != NULL)
		kfree(ms_raw_frame.node_data);
	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	free_limits_file(&limit_file);
	return res;
}

/**
  * Perform all the test selected in a TestTodo variable related to SS raw data
  * (touch, keys etc..)
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ss_raw(char *path_limits, struct test_to_do *tests)
{
	int res = OK;
	struct self_sense_frame ss_raw_frame;
	uint8_t data = SCAN_MODE_LOCK_ACTIVE;
	int trows, tcolumns;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;

	ss_raw_frame.force_data = NULL;
	ss_raw_frame.sense_data = NULL;
	log_info(1, "%s: SS RAW DATA TEST STARTING...\n", __func__);
	if (tests->self_force_raw  || tests->self_sense_raw) {
		res = fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);
		data = SCAN_MODE_HIBERNATE;
		res |= fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);

		log_info(1, "%s: Collecting SS Raw data...\n", __func__);
		res |= get_ss_frame(SS_RAW, &ss_raw_frame);
		if (res < OK) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		if (tests->self_force_raw) {
			print_frame_short("SS Raw Force frame =",
				array_1d_to_2d_short(
					ss_raw_frame.force_data,
					ss_raw_frame.header.force_node,
					1),
				ss_raw_frame.header.force_node,
				1);

			log_info(1, "%s: SS RAW FORCE MIN MAX TEST:\n",
				__func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, SS_RAW_FORCE_EACH_NODE_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows !=
				ss_raw_frame.header.force_node ||
				tcolumns != 1)) {
				res |= ERROR_PROD_TEST_RAW;
				log_info(1,
					"%s: SS_RAW_FORCE_EACH_NODE_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file, SS_RAW_FORCE_EACH_NODE_MAX,
				&thresholds_max, &trows, &tcolumns);
			if (res < OK || (trows !=
				ss_raw_frame.header.force_node ||
				tcolumns != 1)) {
				res |= ERROR_PROD_TEST_RAW;
				log_info(1,
					"%s: SS_RAW_FORCE_EACH_NODE_MAX limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = check_limits_map_total(ss_raw_frame.force_data,
				ss_raw_frame.header.force_node, 1,
				thresholds_min,
				thresholds_max);
			if (res != OK) {
				log_info(1,
					"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: SS RAW FORCE MIN MAX TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_RAW |
					 ERROR_PROD_TEST_CHECK_FAIL);
			} else {
				log_info(1, "%s: SS RAW FORCE MIN MAX TEST:.................OK\n",
				__func__);
			}

			if (thresholds_min != NULL) {
				kfree(thresholds_min);
				thresholds_min = NULL;
			}
			if (thresholds_max != NULL) {
				kfree(thresholds_max);
				thresholds_max = NULL;
			}
		} else
			log_info(1, "%s: SS RAW FORCE TEST SKIPPED..\n",
			__func__);

		if (tests->self_sense_raw) {
			print_frame_short("SS Raw Sense frame =",
				array_1d_to_2d_short(
					ss_raw_frame.sense_data,
					ss_raw_frame.header.sense_node,
					ss_raw_frame.header.sense_node),
				1,
				ss_raw_frame.header.sense_node);

			log_info(1, "%s: SS RAW SENSE MIN MAX TEST:\n",
				__func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, SS_RAW_SENSE_EACH_NODE_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows != 1 ||
				tcolumns != ss_raw_frame.header.sense_node)) {
				res |= ERROR_PROD_TEST_RAW;
				log_info(1,
					"%s: SS_RAW_SENSE_EACH_NODE_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file, SS_RAW_SENSE_EACH_NODE_MAX,
				&thresholds_max, &trows, &tcolumns);
			if (res < OK || (trows != 1 ||
				tcolumns != ss_raw_frame.header.sense_node)) {
				res |= ERROR_PROD_TEST_RAW;
				log_info(1,
					"%s: SS_RAW_SENSE_EACH_NODE_MAX limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = check_limits_map_total(ss_raw_frame.sense_data,
						1,
						ss_raw_frame.header.sense_node,
						thresholds_min, thresholds_max);
			if (res != OK) {
				log_info(1,
					"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: SS RAW SENSE MIN MAX TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_RAW |
					 ERROR_PROD_TEST_CHECK_FAIL);
			} else
				log_info(1, "%s: SS RAW SENSE MIN MAX TEST:.................OK\n",
				__func__);
		} else
			log_info(1, "%s: SS RAW SENSE TEST SKIPPED..\n",
			__func__);
	} else
		log_info(1, "%s SS RAW TEST SKIPPED...\n", __func__);

goto_error:
	if (ss_raw_frame.force_data != NULL)
		kfree(ss_raw_frame.force_data);
	if (ss_raw_frame.sense_data != NULL)
		kfree(ss_raw_frame.sense_data);

	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	free_limits_file(&limit_file);
	return res;
}

/**
  * Perform all the test selected in a TestTodo variable related to SS raw data
  * low power
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ss_raw_lp(char *path_limits, struct test_to_do *tests)
{
	int res = OK;
	struct self_sense_frame ss_raw_frame;
	uint8_t data = SCAN_MODE_LOCK_LP_DETECT;
	int trows, tcolumns;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;

	ss_raw_frame.force_data = NULL;
	ss_raw_frame.sense_data = NULL;
	log_info(1, "%s: SS RAW LP DATA TEST STARTING...\n", __func__);

	if (tests->self_force_raw_lp || tests->self_sense_raw_lp)	{
		res = fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);
		data = SCAN_MODE_HIBERNATE;
		res |= fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		msleep(WAIT_FOR_FRESH_FRAMES);

		log_info(1, "%s: Collecting SS LP Raw data...\n", __func__);
		res |= get_ss_frame(SS_DETECT_RAW, &ss_raw_frame);
		if (res < OK) {
			res |= ERROR_PROD_TEST_RAW;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		if (tests->self_force_raw_lp) {
			if (ss_raw_frame.header.force_node > 0) {
				print_frame_short("SS LP Raw Force frame =",
					array_1d_to_2d_short(
						ss_raw_frame.force_data,
						ss_raw_frame.header.force_node,
						1),
					ss_raw_frame.header.force_node,
					1);

				log_info(1, "%s: SS LP RAW FORCE MIN MAX TEST:\n",
					__func__);
				res = parse_production_test_limits(path_limits,
					&limit_file,
					SS_RAW_LP_FORCE_EACH_NODE_MIN,
					&thresholds_min, &trows, &tcolumns);
				if (res < OK || (trows !=
					ss_raw_frame.header.force_node ||
					tcolumns != 1)) {
					res |= ERROR_PROD_TEST_RAW;
					log_info(1,
						"%s: SS_RAW_LP_FORCE_EACH_NODE_MIN limit parse failed... ERROR %08X\n",
						__func__,
						res);
					goto goto_error;
				}

				res = parse_production_test_limits(path_limits,
					&limit_file,
					SS_RAW_LP_FORCE_EACH_NODE_MAX,
					&thresholds_max, &trows, &tcolumns);
				if (res < OK || (trows !=
					ss_raw_frame.header.force_node ||
					tcolumns != 1)) {
					res |= ERROR_PROD_TEST_RAW;
					log_info(1,
						"%s: SS_RAW_LP_FORCE_EACH_NODE_MAX limit parse failed... ERROR %08X\n",
						__func__,
						res);
					goto goto_error;
				}

				res = check_limits_map_total(ss_raw_frame.
					force_data,
					ss_raw_frame.header.force_node, 1,
					thresholds_min,
					thresholds_max);
				if (res != OK) {
					log_info(1,
						"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
						__func__, res);
					log_info(1,
						"%s: SS LP RAW FORCE MIN MAX TEST:.................FAIL\n\n",
						__func__);
					res = (ERROR_PROD_TEST_RAW |
						 ERROR_PROD_TEST_CHECK_FAIL);
				} else {
					log_info(1, "%s: SS LP RAW FORCE MIN MAX TEST:.................OK\n",
					__func__);
				}

				if (thresholds_min != NULL) {
					kfree(thresholds_min);
					thresholds_min = NULL;
				}
				if (thresholds_max != NULL) {
					kfree(thresholds_max);
					thresholds_max = NULL;
				}
			} else
				log_info(1, "%s: SS LP RAW FORCE MIN MAX TEST:SS LP FORCE NOT AVAILABLE\n",
					__func__);
		} else
			log_info(1, "%s: SS LP RAW FORCE TEST SKIPPED\n",
				__func__);

		if (tests->self_sense_raw_lp) {
			if (ss_raw_frame.header.sense_node > 0) {
				print_frame_short("SS LP Raw Sense frame =",
					array_1d_to_2d_short(
						ss_raw_frame.sense_data,
						ss_raw_frame.header.sense_node,
						ss_raw_frame.header.sense_node),
					1,
					ss_raw_frame.header.sense_node);

				log_info(1, "%s: SS LP RAW SENSE MIN MAX TEST:\n",
					__func__);
				res = parse_production_test_limits(path_limits,
					&limit_file,
					SS_RAW_LP_SENSE_EACH_NODE_MIN,
					&thresholds_min, &trows, &tcolumns);
				if (res < OK || (trows != 1 ||
				tcolumns != ss_raw_frame.header.sense_node)) {
					res |= ERROR_PROD_TEST_RAW;
					log_info(1,
					"%s: SS_RAW_LP_SENSE_EACH_NODE_MIN limit parse failed...ERROR %08X\n",
					__func__, res);
					goto goto_error;
				}

				res = parse_production_test_limits(path_limits,
					&limit_file,
					SS_RAW_LP_SENSE_EACH_NODE_MAX,
					&thresholds_max, &trows, &tcolumns);
				if (res < OK || (trows != 1 ||
				tcolumns != ss_raw_frame.header.sense_node)) {
					res |= ERROR_PROD_TEST_RAW;
					log_info(1,
					"%s: SS_RAW_LP_SENSE_EACH_NODE_MAX limit parse failed...ERROR %08X\n",
					__func__, res);
					goto goto_error;
				}

				res = check_limits_map_total(ss_raw_frame.
					sense_data, 1,
					ss_raw_frame.header.sense_node,
					thresholds_min, thresholds_max);
				if (res != OK) {
					log_info(1,
						"%s: check_limits_map_total failed...ERROR COUNT = %d\n",
						__func__, res);
					log_info(1,
						"%s: SS LP RAW SENSE MIN MAX TEST:.................FAIL\n\n",
						__func__);
					res = (ERROR_PROD_TEST_RAW |
						 ERROR_PROD_TEST_CHECK_FAIL);
				} else {
					log_info(1, "%s: SS LP RAW SENSE MIN MAX TEST:.................OK\n",
					__func__);
				}
			} else
				log_info(1, "%s: SS LP RAW SENSE MIN MAX TEST: SS LP SENSE NOT AVAILABLE\n",
				__func__);
			}
		else
			log_info(1, "%s: SS LP RAW SENSE TEST SKIPPED\n",
				__func__);
	} else
		log_info(1, "%s: SS LP RAW TEST SKIPPED...\n", __func__);

goto_error:
	if (ss_raw_frame.force_data != NULL)
		kfree(ss_raw_frame.force_data);
	if (ss_raw_frame.sense_data != NULL)
		kfree(ss_raw_frame.sense_data);

	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	free_limits_file(&limit_file);
	return res;
}

/**
  * Perform all the tests selected in a TestTodo variable related to MS Total CX LowPower
  * Init data (touch, keys etc..)
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param stop_on_fail if 1, the test flow stops at the first data check
  * failure
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ms_cx_lp(char *path_limits, int stop_on_fail,
				    struct test_to_do *tests)
{
	int res = OK;
	struct mutual_total_cx_data ms_cx_data;
	int trows, tcolumns;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;
	u16 *adj = NULL;
	int *thresholds = NULL;

	ms_cx_data.node_data = NULL;

	log_info(1, "%s: MS TOTAL CX LP DATA TEST STARTING...\n", __func__);
	if (tests->mutual_cx_lp || tests->mutual_cx_lp_adj) {
		log_info(1, "%s: Collecting MS CX LP data...\n", __func__);
		res = get_mutual_total_cx_data(HDM_REQ_TOT_CX_MS_TOUCH,
			 &ms_cx_data);
		if (res < OK) {
			res |= ERROR_PROD_TEST_CX;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		print_frame_short("MS TOTAL CX data =",
			array_1d_to_2d_short(
				ms_cx_data.node_data,
				ms_cx_data.
				node_data_size,
				ms_cx_data.header.
				sense_node),
			ms_cx_data.header.force_node,
			ms_cx_data.header.sense_node);

		if (tests->mutual_cx_lp) {
			log_info(1, "%s: MS TOTAL CX LP DATA MIN MAX TEST:\n",
				 __func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, MS_TOTAL_CX_LP_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows !=
				ms_cx_data.header.force_node ||
				tcolumns != ms_cx_data.header.sense_node)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: MS_TOTAL_CX_LP_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file, MS_TOTAL_CX_LP_MAX,
				&thresholds_max, &trows, &tcolumns);
			if (res < OK || (trows !=
				ms_cx_data.header.force_node ||
				tcolumns != ms_cx_data.header.sense_node)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: MS_TOTAL_CX_LP_MAX limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = check_limits_map_total(ms_cx_data.node_data,
				ms_cx_data.header.force_node,
				ms_cx_data.header.sense_node, thresholds_min,
				thresholds_max);
			if (res != OK) {
				log_info(1,
					"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: MS TOTAL CX LP MIN MAX TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_CX |
					 ERROR_PROD_TEST_CHECK_FAIL);
				if (stop_on_fail == 1)
					goto goto_error;
			} else {
				log_info(1, "%s: MS TOTAL CX LP MIN MAX TEST:.................OK\n",
					__func__);
			}
			if (thresholds_min != NULL) {
				kfree(thresholds_min);
				thresholds_min = NULL;
			}
			if (thresholds_max != NULL) {
				kfree(thresholds_max);
				thresholds_max = NULL;
			}
		} else
			log_info(1, "%s: MS TOTAL CX LP DATA MIN MAX TEST SKIPPED...\n",
				 __func__);
		if (tests->mutual_cx_lp_adj) {
			log_info(1, "%s: MS TOTAL CX LP DATA ADJACENT HORIZONTAL TEST:\n",
				 __func__);
			res = compute_adj_horiz_total(ms_cx_data.node_data,
				ms_cx_data.header.force_node,
				ms_cx_data.header.sense_node,
				&adj);
			if (res < OK) {
				log_info(1,
					"%s: compute adj Horizontal failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file,
				MS_TOTAL_CX_LP_ADJH, &thresholds,
				&trows, &tcolumns);
			if (res < OK ||
			(trows != ms_cx_data.header.force_node ||
			tcolumns != ms_cx_data.header.sense_node - 1)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: MS_TOTAL_CX_LP_ADJH limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}


			res = check_limits_map_adj_total(adj,
				ms_cx_data.header.force_node,
				ms_cx_data.header.sense_node - 1,
				thresholds);
			if (res != OK) {
				log_info(1,
					"%s: check limit adj horiz MS_TOTAL_CX_LP_ADJH failed...ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: MS TOTAL CX LP ADJ HORIZONTAL TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_CX |
					 ERROR_PROD_TEST_CHECK_FAIL);
				if (stop_on_fail == 1)
					goto goto_error;
			} else
				log_info(1,
					"%s: MS TOTAL CX LP ADJ HORIZONTAL TEST:.................OK\n",
					__func__);

			kfree(thresholds);
			thresholds = NULL;

			kfree(adj);
			adj = NULL;

			log_info(1, "%s: MS TOTAL CX LP ADJ VERTICAL TEST:\n",
				__func__);
			res = compute_adj_vert_total(ms_cx_data.node_data,
						ms_cx_data.header.force_node,
						ms_cx_data.header.sense_node,
						&adj);
			if (res < OK) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: compute adj vert failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file, MS_TOTAL_CX_LP_ADJV, &thresholds,
				&trows, &tcolumns);
			if (res < OK ||
			(trows != ms_cx_data.header.force_node - 1 ||
				tcolumns != ms_cx_data.header.sense_node)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: MS_TOTAL_CX_LP_ADJV limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}


			res = check_limits_map_adj_total(adj,
				ms_cx_data.header.force_node -
				1, ms_cx_data.header.sense_node,
				thresholds);
			if (res != OK) {
				log_info(1,
					"%s: check limits adj MS_TOTAL_CX_LP_ADJV failed...ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: MS TOTAL CX LP ADJ VERTICAL TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_CX |
					ERROR_PROD_TEST_CHECK_FAIL);
				if (stop_on_fail == 1)
					goto goto_error;
			} else
				log_info(1,
					"%s: MS TOTAL CX LP ADJ VERTICAL TEST:.................OK\n",
					__func__);

			kfree(thresholds);
			thresholds = NULL;

			kfree(adj);
			adj = NULL;

		}
	} else
		log_info(1, "%s: MS TOTAL CX LP TEST SKIPPED...\n", __func__);

goto_error:
	if (ms_cx_data.node_data != NULL)
		kfree(ms_cx_data.node_data);
	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	if (thresholds != NULL) {
		kfree(thresholds);
		thresholds = NULL;
	}
	if (adj != NULL) {
		kfree(adj);
		adj = NULL;
	}
	free_limits_file(&limit_file);
	return res;

}

/**
  * Perform all the tests selected in a TestTodo variable related to SS Init
  * data (touch, keys etc..)
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ss_ix(char *path_limits, struct test_to_do *tests)
{
	int res = OK;
	struct self_total_cx_data ss_cx_data;
	int trows, tcolumns;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;

	ss_cx_data.ix_tx = NULL;
	ss_cx_data.ix_rx = NULL;

	log_info(1, "%s: SS TOTAL IX DATA TEST STARTING...\n", __func__);
	if (tests->self_force_ix || tests->self_sense_ix) {
		log_info(1, "%s: Collecting SS IX data...\n", __func__);
		res |= get_self_total_cx_data(HDM_REQ_TOT_IX_SS_TOUCH,
						 &ss_cx_data);
		if (res < OK) {
			res |= ERROR_PROD_TEST_CX;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

		print_frame_u16("SS TOTAL FORCE DATA =",
			array_1d_to_2d_u16(ss_cx_data.ix_tx,
			ss_cx_data.header.force_node,
			1),
			ss_cx_data.header.force_node,
			1);

		print_frame_u16("SS TOTAL SENSE DATA =",
			array_1d_to_2d_u16(ss_cx_data.ix_rx,
		  ss_cx_data.header.sense_node,
		  ss_cx_data.header.sense_node),
		  1,
		  ss_cx_data.header.sense_node);

		log_info(1, "%s: SS TOTAL IX DATA MIN MAX TEST:\n", __func__);
		if (tests->self_force_ix) {
			log_info(1, "%s: SS TOTAL FORCE IX DATA MIN MAX TEST:\n",
				 __func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, SS_FORCE_TOTAL_IX_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows !=
				ss_cx_data.header.force_node ||
				tcolumns != 1)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: SS_FORCE_TOTAL_IX_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = parse_production_test_limits(path_limits,
				&limit_file, SS_FORCE_TOTAL_IX_MAX,
				&thresholds_max, &trows, &tcolumns);
			if (res < OK || (trows !=
				ss_cx_data.header.force_node ||
				tcolumns != 1)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: SS_FORCE_TOTAL_IX_MAX limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
			}

			res = check_limits_map_total(ss_cx_data.ix_tx,
			ss_cx_data.header.force_node,
			1, thresholds_min,
			thresholds_max);
			if (res != OK) {
				log_info(1,
					"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
					__func__, res);
				log_info(1,
					"%s: SS TOTAL FORCE IX DATA MAP MIN MAX TEST:.................FAIL\n\n",
					__func__);
				res = (ERROR_PROD_TEST_CX |
					ERROR_PROD_TEST_CHECK_FAIL);
			} else {
				log_info(1, "%s: SS TOTAL FORCE IX DATA MAP MIN MAX TEST:.................OK\n",
					__func__);
			}
		} else
			log_info(1, "%s: SS TOTAL FORCE IX DATA MIN MAX TEST SKIPPED\n",
			 __func__);

		if (tests->self_sense_ix) {
			log_info(1, "%s: SS TOTAL SENSE IX DATA MIN MAX TEST:\n",
				 __func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, SS_SENSE_TOTAL_IX_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows !=
				1 ||
				tcolumns != ss_cx_data.header.sense_node)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: SS_SENSE_TOTAL_IX_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
		}

		res = parse_production_test_limits(path_limits,
			&limit_file, SS_SENSE_TOTAL_IX_MAX,
			&thresholds_max, &trows, &tcolumns);
		if (res < OK || (trows !=
			1 ||
			tcolumns != ss_cx_data.header.sense_node)) {
			res |= ERROR_PROD_TEST_CX;
			log_info(1,
				"%s: SS_SENSE_TOTAL_IX_MIN limit parse failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

			res = check_limits_map_total(ss_cx_data.ix_rx,
			1,
			ss_cx_data.header.sense_node, thresholds_min,
			thresholds_max);
		if (res != OK) {
			log_info(1,
				"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
				__func__, res);
			log_info(1,
				"%s: SS TOTAL SENSE IX DATA MAP MIN MAX TEST:.................FAIL\n\n",
				__func__);
			res = (ERROR_PROD_TEST_CX | ERROR_PROD_TEST_CHECK_FAIL);
		} else {
			log_info(1, "%s: SS TOTAL SENSE IX DATA MAP MIN MAX TEST:.................OK\n",
				__func__);
		}
		} else
			log_info(1, "%s: SS TOTAL SENSE IX DATA MAP MIN MAX TEST SKIPPED\n",
				 __func__);
		if (thresholds_min != NULL) {
			kfree(thresholds_min);
			thresholds_min = NULL;
		}
		if (thresholds_max != NULL) {
			kfree(thresholds_max);
			thresholds_max = NULL;
		}
	} else
		log_info(1, "%s: MS TOTAL CX TEST SKIPPED...\n",
				 __func__);

goto_error:
	if (ss_cx_data.ix_rx != NULL) {
		kfree(ss_cx_data.ix_rx);
		ss_cx_data.ix_rx = NULL;
	}
	if (ss_cx_data.ix_tx != NULL) {
		kfree(ss_cx_data.ix_tx);
		ss_cx_data.ix_tx = NULL;
	}
	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	free_limits_file(&limit_file);
	return res;

}

/**
  * Perform all the tests selected in a TestTodo variable related to SS Init
  * data for LP mode (touch, keys etc..)
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_ss_ix_lp(char *path_limits, struct test_to_do *tests)
{
	int res = OK;
	struct self_total_cx_data ss_cx_data;
	int trows, tcolumns;
	int *thresholds_min = NULL;
	int *thresholds_max = NULL;

	ss_cx_data.ix_tx = NULL;
	ss_cx_data.ix_rx = NULL;


	log_info(1, "%s: SS TOTAL IX LP DATA TEST STARTING...\n", __func__);
	if (tests->self_force_ix_lp  || tests->self_sense_ix_lp) {
		log_info(1, "%s: Collecting SS IX LP data...\n", __func__);
		res |= get_self_total_cx_data(HDM_REQ_TOT_IX_SS_TOUCH_IDLE,
						 &ss_cx_data);
		if (res < OK) {
			res |= ERROR_PROD_TEST_CX;
			log_info(1, "%s: failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}
		print_frame_u16("SS TOTAL FORCE LP DATA =",
			array_1d_to_2d_u16(ss_cx_data.ix_tx,
				ss_cx_data.header.force_node,
				1),
			ss_cx_data.header.force_node,
			1);

		print_frame_u16("SS TOTAL SENSE LP DATA =",
			array_1d_to_2d_u16(ss_cx_data.ix_rx,
				ss_cx_data.header.sense_node,
				ss_cx_data.header.sense_node),
			1,
			ss_cx_data.header.sense_node);

		log_info(1, "%s: SS TOTAL IX LP DATA MIN MAX TEST:\n",
			 __func__);

		if (tests->self_force_ix_lp) {
			log_info(1, "%s: SS TOTAL FORCE IX LP DATA MIN MAX TEST:\n",
				 __func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, SS_FORCE_TOTAL_IX_LP_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows !=
				ss_cx_data.header.force_node ||
				tcolumns != 1)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: SS_FORCE_TOTAL_IX_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
		}

		res = parse_production_test_limits(path_limits,
			&limit_file, SS_FORCE_TOTAL_IX_LP_MAX,
			&thresholds_max, &trows, &tcolumns);
		if (res < OK || (trows !=
			ss_cx_data.header.force_node ||
			tcolumns != 1)) {
			res |= ERROR_PROD_TEST_CX;
			log_info(1,
				"%s: SS_FORCE_TOTAL_IX_MAX limit parse failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

			res = check_limits_map_total(ss_cx_data.ix_tx,
			ss_cx_data.header.force_node,
			1, thresholds_min,
			thresholds_max);
		if (res != OK) {
			log_info(1,
				"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
				__func__, res);
			log_info(1,
				"%s: SS TOTAL FORCE IX LP DATA MAP MIN MAX TEST:.................FAIL\n\n",
				__func__);
			res = (ERROR_PROD_TEST_CX | ERROR_PROD_TEST_CHECK_FAIL);
		} else {
			log_info(1, "%s: SS TOTAL FORCE IX LP DATA MAP MIN MAX TEST:.................OK\n",
				__func__);
		}
		}	else
			log_info(1, "%s: SS TOTAL FORCE IX LP DATA MIN MAX TEST SKIPPED\n",
				 __func__);

		if (tests->self_sense_ix_lp) {
			log_info(1, "%s: SS TOTAL SENSE IX LP DATA MIN MAX TEST:\n",
				 __func__);
			res = parse_production_test_limits(path_limits,
				&limit_file, SS_SENSE_TOTAL_IX_LP_MIN,
				&thresholds_min, &trows, &tcolumns);
			if (res < OK || (trows !=
				1 ||
				tcolumns != ss_cx_data.header.sense_node)) {
				res |= ERROR_PROD_TEST_CX;
				log_info(1,
					"%s: SS_SENSE_TOTAL_IX_MIN limit parse failed... ERROR %08X\n",
					__func__, res);
				goto goto_error;
		}

		res = parse_production_test_limits(path_limits,
			&limit_file, SS_SENSE_TOTAL_IX_LP_MAX,
			&thresholds_max, &trows, &tcolumns);
		if (res < OK || (trows !=
			1 ||
			tcolumns != ss_cx_data.header.sense_node)) {
			res |= ERROR_PROD_TEST_CX;
			log_info(1,
				"%s: SS_SENSE_TOTAL_IX_MIN limit parse failed... ERROR %08X\n",
				__func__, res);
			goto goto_error;
		}

			res = check_limits_map_total(ss_cx_data.ix_rx,
			1,
			ss_cx_data.header.sense_node, thresholds_min,
			thresholds_max);
		if (res != OK) {
			log_info(1,
				"%s: check_limits_map_total failed... ERROR COUNT = %d\n",
				__func__, res);
			log_info(1,
				"%s: SS TOTAL SENSE IX LP DATA MAP MIN MAX TEST:.................FAIL\n\n",
				__func__);
			res = (ERROR_PROD_TEST_CX | ERROR_PROD_TEST_CHECK_FAIL);
		} else {
			log_info(1, "%s: SS TOTAL SENSE IX LP DATA MAP MIN MAX TEST:.................OK\n",
				__func__);
		}
		} else
			log_info(1, "%s: SS TOTAL SENSE IX LP DATA MIN MAX TEST SKIPPED\n",
				 __func__);
		if (thresholds_min != NULL) {
			kfree(thresholds_min);
			thresholds_min = NULL;
		}
		if (thresholds_max != NULL) {
			kfree(thresholds_max);
			thresholds_max = NULL;
		}
	} else
			log_info(1, "%s: SS TOTAL IX LP TEST SKIPPED...\n",
				 __func__);

goto_error:
	if (ss_cx_data.ix_rx != NULL) {
		kfree(ss_cx_data.ix_rx);
		ss_cx_data.ix_rx = NULL;
	}
	if (ss_cx_data.ix_tx != NULL) {
		kfree(ss_cx_data.ix_tx);
		ss_cx_data.ix_tx = NULL;
	}
	if (thresholds_min != NULL) {
		kfree(thresholds_min);
		thresholds_min = NULL;
	}
	if (thresholds_max != NULL) {
		kfree(thresholds_max);
		thresholds_max = NULL;
	}
	free_limits_file(&limit_file);
	return res;

}


/**
  * Perform a FULL (ITO + INIT + DATA CHECK) Mass Production Test of the IC
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param stop_on_fail if 1, the test flow stops at the first data check
  * failure, except ITO test which will always stop for error
  * @param do_init flag variable to decide on full panel initialisation
  * the Initialization of the IC is executed otherwise it is skipped
  * @param tests pointer to a test_to_do variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
int fts_production_test_main(char *path_limits, int stop_on_fail,
				struct test_to_do *tests, int do_init)
{
	int res = OK;

	log_info(1, "%s: MAIN production test is starting...\n", __func__);
	log_info(1, "%s: [1]ITO TEST...\n", __func__);
	res = fts_production_test_ito(path_limits, tests);
	if (res != OK) {
		log_info(1, "%s: ITO TEST FAIL\n", __func__);
		goto goto_error;
	}
	if (do_init) {
		log_info(1, "%s: Do Initialization...\n", __func__);
		res = fts_fw_request(PI_ADDR, 1, 1, TIMEOUT_FPI);
		if (res < OK) {
			log_info(1, "%s: Error performing autotune.. %08X\n",
				__func__, res);
			res |= ERROR_INIT;
			if (stop_on_fail)
				goto goto_error;
		}
		log_info(1, "%s: Initialization done...\n", __func__);
	}
	log_info(1, "%s: [2]MUTUAL RAW TEST...\n", __func__);
	res = fts_production_test_ms_raw(path_limits, tests);
	if (res != OK) {
		log_info(1, "%s: MUTUAL RAW TEST FAIL\n", __func__);
		if (stop_on_fail)
			goto goto_error;
	}
	log_info(1, "%s: [3]LOW POWER MUTUAL RAW Test......\n", __func__);
	res = fts_production_test_ms_raw_lp(path_limits, tests);
	if (res != OK) {
		log_info(1, "%s: LOW POWER MUTUAL RAW TEST FAIL\n", __func__);
		if (stop_on_fail)
			goto goto_error;
	}
	log_info(1, "%s: [4]SELF RAW TEST...\n", __func__);
	res = fts_production_test_ss_raw(path_limits, tests);
	if (res != OK) {
		log_info(1, "%s: SELF RAW TEST FAIL\n", __func__);
		if (stop_on_fail)
			goto goto_error;
	}
	log_info(1, "%s: [5]LOW POWER SELF RAW TEST......\n", __func__);
	res = fts_production_test_ss_raw_lp(path_limits, tests);
	if (res != OK) {
		log_info(1, "%s: LOW POWER SELF RAW TEST FAIL\n", __func__);
		if (stop_on_fail)
			goto goto_error;
	}
	log_info(1, "%s: [6]MUTUAL CX LOW POWER TEST......\n", __func__);
	res = fts_production_test_ms_cx_lp(path_limits, stop_on_fail, tests);
	if (res != OK) {
		log_info(1, "%s: MUTUAL CX LOW POWER TEST FAIL\n", __func__);
		if (stop_on_fail)
			goto goto_error;
	}
	log_info(1, "%s: [7]SELF IX TEST......\n", __func__);
	res = fts_production_test_ss_ix(path_limits, tests);
	if (res != OK) {
		log_info(1, "%s: SELF IX TEST FAIL\n", __func__);
		if (stop_on_fail)
			goto goto_error;
	}
	log_info(1, "%s: [8]SELF IX DETECT TEST......\n", __func__);
	res = fts_production_test_ss_ix_lp(path_limits, tests);
	if (res != OK) {
		log_info(1, "%s: SELF IX DETECT TEST FAIL\n", __func__);
		if (stop_on_fail)
			goto goto_error;
	}
goto_error:
	if (res != OK) {
		log_info(1, "%s: MAIN production test FAIL\n\n", __func__);
		return res;
	}
	log_info(1, "%s: MAIN production test OK\n\n", __func__);
	return res;

}
