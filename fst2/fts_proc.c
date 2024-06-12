/*
  *
  **************************************************************************
  **                        STMicroelectronics				  **
  **************************************************************************
  *                                                                        *
  *                     Utilities published in /proc/fts		   *
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file fts_proc.c
  * \brief contains the function and variables needed to publish a file node in
  * the file system which allow to communicate with the IC from userspace
  */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include "fts_lib/fts_io.h"
#include "fts_lib/fts_flash.h"
#include "fts_lib/fts_test.h"
#include "fts_lib/fts_error.h"
#include "fts.h"

#define DRIVER_TEST_FILE_NODE		"driver_test" /* /< name of file node
						 * published */
#define CHUNK_PROC				1024
/* /< Max chunk of a printed on the sequential file in each iteration */

/** @defgroup proc_file_code	 Proc File Node
  * The /proc/fts/driver_test file node provide expose the most important API
  * implemented into the driver to execute any possible operation into the IC \n
  * Thanks to a series of Operation Codes, each of them, with a different set of
  * parameter, it is possible to select a function to execute\n
  * The result of the function is usually returned into the shell as an ASCII
  * hex string where each byte is encoded in two chars.\n
  * @{
  */

#define CMD_DRIVER_VERSION			0x00
/*!< usage: echo 00 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_WRITE				0x01
/*!< usage: to do system reset: echo 01 FA 20 00 00 24 80 >
/proc/fts/driver_test; cat /proc/fts/driver_test (raw write, no chunking) */
#define CMD_WRITEREAD				0x02
/*!< usage: to read chip ID: echo 02 FA 20 00 00 00 00 08 00
> /proc/fts/driver_test; cat /proc/fts/driver_test
(raw writeread, no chunking)*/
#define CMD_WRITEU8UX				0x03
/*!< usage: to do system reset: echo 03 FA 04 20 00 00 24 80
> /proc/fts/driver_test; cat /proc/fts/driver_test (supports chunking)*/
#define CMD_WRITEREADU8UX			0x04
/*!< usage: to read chip id: echo 04 FA 04 20 00 00 00 00 08 00 >
 /proc/fts/driver_test; cat /proc/fts/driver_test (supports chunking)*/
#define CMD_READSYSINFO				0x05
/*!<  usage: echo 05 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_SYSTEMRESET				0x06
/*!<  usage: echo 06 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_CONFIGURESPI4			0x07
/*!<  usage: echo 07 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_GETFWFILE				0x08
/*!<  Not implemented */
#define CMD_FWUPDATE				0x09
/*!<  usage:echo 09 00 00 00 00 > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> last 4 parameters are force
updated flags for code, reg, ms, ss respectively */
#define CMD_INTERRUPT				0x0A
/*!<  usage: echo 0A 00/01 > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> 00 to disblae interrupt, 01 to enable interrupt */
#define CMD_FWREGWRITE				0x0B
/*!<  usage: echo 0B 00 00 00 08 > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> first two bytes are FW address,
followed by data to be written to that address */
#define CMD_FWREGREAD				0x0C
/*!<  usage: echo 0C 00 00 00 08 > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> first two bytes are FW address,
followed by number of bytes to be read from that address */
#define CMD_READMSFRAME				0x0D
/*!<  usage: echo 0D ms_frame_type > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> MS_RAW = 0, MS_FILTER = 1,
MS_STRENGTH = 2, MS_BASELINE = 3, */
#define CMD_READSSFRAME				0x0E
/*!<  usage: echo 0E ss_frame_type > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> SS_RAW = 0, SS_FILTER = 1,
SS_STRENGTH = 2, SS_BASELINE = 3, SS_DETECT_RAW = 4,
SS_DETECT_FILTER = 5,	SS_DETECT_STRENGTH = 6, SS_DETECT_BASELINE = 7,*/
#define CMD_READSYNCFRAME			0x0F
/*!<  usage: echo 0F sync_frame_type > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> frame type  to be referred from FW API */
#define CMD_READMSCXDATA			0x10
/*!<  usage: echo 10 ms_cx_type > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> cx type to be referred from FW API */
#define CMD_READSSCXDATA			0x11
/*!<  usage: echo 11 ss_cx_type > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> cx type to be referred from FW API */
#define CMD_MAINTEST				0x12
/*!<  usage: echo 12 00/01  00/01> /proc/fts/driver_test; cat
 /proc/fts/driver_test , to enable/disable full panel init and  enable/disable
 test flow stop at first error*/
#define CMD_ITOTEST				0x13
/*!<  usage: echo 13 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_MUTUALRAWTEST			0x14
/*!<  usage: echo 14 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_MUTUALRAWLPTEST		0x15
/*!<  usage: echo 15 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_SELFRAWTEST			0x16
/*!<  usage: echo 16 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_SELFRAWLPTEST		0x17
/*!<  usage: echo 17 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_READFIFOEVENT			0x18
/*!<  usage: echo 18 > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_READSYSERRORS			0x19
/*!<  usage: echo 19 > /proc/fts/driver_test; cat /proc/fts/driver_test
==> read the system errors */
#define CMD_REQUESTHDM				0x1A
/*!<  usage: echo 1A hdm type > /proc/fts/driver_test; cat /proc/fts/driver_test
==> hdm type is the type of the host data memory to be loaded */
#define CMD_MUTUALTOTCXLPTEST		0x1B
/*!<usage: echo 1B 00/01> /proc/fts/driver_test; cat /proc/fts/driver_test
enable/disable test flow stop at first error*/
#define CMD_SELFTOTIXTEST			0x1C
/*!<usage: echo 1C > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_SELFTOTIXDETECTTEST		0x1D
/*!< usage: echo 1D > /proc/fts/driver_test; cat /proc/fts/driver_test */
#define CMD_READMSTOTALCXLPDATA			0x1E
/*!< usage: echo 1E ms_total_cx_type > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> cx type to be referred from FW API */
#define CMD_READSSTOTALIXDATA			0x1F
/*!<usage: echo 1F ss_total_ix_type > /proc/fts/driver_test;
cat /proc/fts/driver_test ==> ix type to be referred from FW API */
#define CMD_FWWRITEAUTOCLEAR			0x20
/*!<usage: echo 20 00 22 01 0F 00> /proc/fts/driver_test;
cat /proc/fts/driver_test ==>  this api shall write 1 and will
be auto cleared with a time out when command is completed ,
< cmdid , 2bytes fw reg address , 1byte bitset position, 2bytes timeout in ms */
/**@}*/




static int limit;	/* /< store the amount of data to print into the shell*/
static int chunk;	/* /< store the chuk of data that should be printed in
			 * this iteration */
static int printed;	/* /< store the amount of data already printed in the
			 * shell */
static struct proc_dir_entry *fts_dir;	/* /< reference to the directory
						 * fts under /proc */
static u8 *test_print_buff;	/* /< pointer to an array of bytes used
					 * to store the result of the function
					 * executed */
char buf_chunk[CHUNK_PROC] = { 0 };	/* /< buffer used to store the message
					 * info received */
extern struct test_to_do tests;


/************************ SEQUENTIAL FILE UTILITIES **************************/
/**
  * This function is called at the beginning of the stream to a sequential file
  * or every time into the sequential were already written PAGE_SIZE bytes and
  * the stream need to restart
  * @param s pointer to the sequential file on which print the data
  * @param pos pointer to the offset where write the data
  * @return NULL if there is no data to print or the pointer to the beginning of
  * the data that need to be printed
  */
static void *fts_seq_start(struct seq_file *s, loff_t *pos)
{
	log_info(0,
		"%s: Entering start(), pos = %ld limit = %d printed = %d\n",
		__func__, *pos, limit, printed);

	if (test_print_buff == NULL && *pos == 0) {
		log_info(1, "%s: No data to print!\n", __func__);
		test_print_buff = (u8 *)kmalloc(13 * sizeof(u8), GFP_KERNEL);
		snprintf(test_print_buff, 14, "{ %08X }\n", ERROR_OP_NOT_ALLOW);
		limit = strlen(test_print_buff);
	} else {
		if (*pos != 0)
			*pos += chunk - 1;

		if (*pos >= limit)
			return NULL;
	}
	chunk = CHUNK_PROC;
	if (limit - *pos < CHUNK_PROC)
		chunk = limit - *pos;
	memset(buf_chunk, 0, CHUNK_PROC);
	memcpy(buf_chunk, &test_print_buff[(int)*pos], chunk);

	return buf_chunk;
}

/**
  * This function actually print a chunk amount of data in the sequential file
  * @param s pointer to the sequential file where to print the data
  * @param v pointer to the data to print
  * @return 0
  */
static int fts_seq_show(struct seq_file *s, void *v)
{
	log_info(0, "%s: In show()\n", __func__);
	seq_write(s, (u8 *)v, chunk);
	printed += chunk;
	return 0;
}

/**
  * This function update the pointer and the counters to the next data to be
  * printed
  * @param s pointer to the sequential file where to print the data
  * @param v pointer to the data to print
  * @param pos pointer to the offset where write the next data
  * @return NULL if there is no data to print or the pointer to the beginning of
  * the next data that need to be printed
  */
static void *fts_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	log_info(0,
		"%s: Entering next(), pos = %ld limit = %d printed = %d\n",
		__func__, *pos, limit, printed);
	(*pos) += chunk;/* increase my position counter */
	chunk = CHUNK_PROC;
	if (*pos >= limit)
		return NULL;
	else if (limit - *pos < CHUNK_PROC)
		chunk = limit - *pos;
	memset(buf_chunk, 0, CHUNK_PROC);
	memcpy(buf_chunk, &test_print_buff[(int)*pos], chunk);
	return buf_chunk;
}

/**
  * This function is called when there are no more data to print  the stream
  *need to be terminated or when PAGE_SIZE data were already written into the
  *sequential file
  * @param s pointer to the sequential file where to print the data
  * @param v pointer returned by fts_seq_next
  */
static void fts_seq_stop(struct seq_file *s, void *v)
{
	log_info(0, "%s: In stop()\n", __func__);
	if (v) {
		log_info(1, "%s: v is %X.\n", __func__, v);
	} else {
		limit = 0;
		chunk = 0;
		printed = 0;
		if (test_print_buff != NULL) {
			kfree(test_print_buff);
			test_print_buff = NULL;
		}
	}
}

/**
  * Struct where define and specify the functions which implements the flow for
  *writing on a sequential file
  */
static const struct seq_operations fts_seq_ops = {
	.start = fts_seq_start,
	.next = fts_seq_next,
	.stop = fts_seq_stop,
	.show = fts_seq_show
};

/**
  * This function open a sequential file
  * @param inode Inode in the file system that was called and triggered this
  * function
  * @param file file associated to the file node
  * @return error code, 0 if success
  */
static int fts_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &fts_seq_ops);
};

/*****************************************************************************/

/**************************** DRIVER TEST ************************************/

/** @addtogroup proc_file_code
  * @{
  */

/**
  * Receive the OP code and the inputs from shell when the file node is called,
  * parse it and then execute the corresponding function
  * echo cmd+parameters > /proc/fts/driver_test to execute the select command
  * cat /proc/fts/driver_test			to obtain the result into the
  * shell \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * the answer content and format strictly depend on the cmd executed. In
  * general can be: an HEX string or a byte array (e.g in case of 0xF- commands)
  * \n
  * } = end byte \n
  */
static ssize_t fts_seq_write(struct file *file, const char __user *buf,
			size_t count, loff_t *pos)
{
	int res = OK;
	char *pbuf = NULL;
	char *p = NULL;
	u32 *func_to_test = NULL;
	u8 *cmd = NULL;
	int number_param = 0;
	u8 *read_buf = NULL;
	u16 to_read = 0;
	int index = 0;
	int size = 0;
	int dummy = 0;
	int i = 0;
	u64 address = 0;
	struct mutual_sense_frame mutual_frame;
	struct self_sense_frame self_frame;
	struct mutual_sense_cx_data mutual_cx;
	struct self_sense_cx_data self_cx;
	struct force_update_flag force_burn_flag;
	struct mutual_total_cx_data mutual_total_cx;
	struct self_total_cx_data self_total_cx;

	pbuf = (u8 *)kmalloc(count * sizeof(u8), GFP_KERNEL);
	if (pbuf == NULL) {
		log_info(1, "%s: Error allocating memory\n", __func__);
		res = ERROR_ALLOC;
		goto goto_end;
	}

	cmd = (u8 *)kmalloc(count * sizeof(u8), GFP_KERNEL);
	if (cmd == NULL) {
		log_info(1, "%s: Error allocating memory\n", __func__);
		res = ERROR_ALLOC;
		goto goto_end;
	}
	func_to_test = (u32 *)kmalloc(((count + 1) / 3) * sizeof(u32),
			GFP_KERNEL);
	if (func_to_test == NULL) {
		log_info(1, "%s: Error allocating memory\n", __func__);
		res = ERROR_ALLOC;
		goto goto_end;
	}
	if (access_ok(buf, count) < OK ||
		copy_from_user(pbuf, buf, count) != 0) {
		res = ERROR_ALLOC;
		goto goto_end;
	}
	p = pbuf;
	if (((count + 1) / 3) >= 1) {
		if (sscanf(p, "%02X ", &func_to_test[0]) == 1) {
			p += 3;
			cmd[0] = (u8)func_to_test[0];
			number_param = 1;
		}
	} else {
		res = ERROR_OP_NOT_ALLOW;
		goto goto_end;
	}

	log_info(1,
		"%s: func_to_test[0] = %02X cmd[0]= %02X Number of Parameters = %d\n",
		__func__, func_to_test[0], cmd[0], number_param);

	for (; number_param < (count + 1) / 3; number_param++) {
		if (sscanf(p, "%02X ",
			&func_to_test[number_param]) == 1) {
			p += 3;
			cmd[number_param] =
				(u8)func_to_test[number_param];
			log_info(1,
				"%s: func_to_test[%d] = %02X cmd[%d]= %02X\n",
				__func__, number_param,
				func_to_test[number_param],
				number_param, cmd[number_param]);
		}
	}

	log_info(1, "%s: Number of Parameters = %d\n", __func__, number_param);
	if (number_param >= 1) {
		switch (func_to_test[0]) {
		case CMD_DRIVER_VERSION:
			to_read = sizeof(u32);
			read_buf = (u8 *)kmalloc(to_read *
				sizeof(u8), GFP_KERNEL);
			if (read_buf == NULL) {
				log_info(1, "%s: Error allocating memory\n",
					__func__);
				to_read = 0;
				res = ERROR_ALLOC;
				break;
			}
			u32_to_u8_be(FTS_TS_DRV_VER, read_buf);
			size += (to_read * sizeof(u8));
			break;

		case CMD_WRITE:
			if (number_param >= 4)
				res = fts_write(&cmd[1], number_param - 1);
			else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_WRITEREAD:
			if (number_param >= 6) {
				dummy = cmd[number_param - 1];
				u8_to_u16_be(&cmd[number_param - 3], &to_read);
				log_info(1, "%s: Number of bytes to read = %d\n",
					__func__, to_read + dummy);
				read_buf = (u8 *)kmalloc((to_read + dummy) *
						sizeof(u8), GFP_KERNEL);
				if (read_buf == NULL) {
					log_info(1, "%s: Error allocating memory\n",
						__func__);
					to_read = 0;
					res = ERROR_ALLOC;
					break;
				}
				res = fts_write_read(&cmd[1], number_param - 4,
						read_buf, to_read + dummy);
				if (res >= OK)
					size += (to_read * sizeof(u8));
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_WRITEU8UX:
			if (number_param >= 5) {
				if (cmd[2] <= sizeof(u64)) {
					u8_to_u64_be(&cmd[3], &address, cmd[2]);
					log_info(1, "%s: address = %016llX %ld\n",
						__func__, address,
						(long int)address);
					res = fts_write_u8ux(cmd[1], cmd[2],
						address, &cmd[3 + cmd[2]],
						(number_param - cmd[2] - 3));
				} else {
					log_info(1, "%s Wrong address size!\n",
						__func__);
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_WRITEREADU8UX:
			if (number_param >= 7) {
				dummy = cmd[number_param - 1];
				u8_to_u16_be(&cmd[number_param - 3], &to_read);
				log_info(1,
					"%s: Number of bytes to read = %d\n",
						__func__, to_read);
				if (cmd[2] <= sizeof(u64)) {
					u8_to_u64_be(&cmd[3], &address, cmd[2]);
					log_info(1, "%s: address = %016llX %ld\n",
							__func__, address,
							(long int)address);
					read_buf = (u8 *)kmalloc(to_read *
						sizeof(u8), GFP_KERNEL);
					if (read_buf == NULL) {
						log_info(1, "%s: Error allocating memory\n",
						__func__);
						to_read = 0;
						res = ERROR_ALLOC;
						break;
					}
					res = fts_write_read_u8ux(cmd[1],
						cmd[2], address, read_buf,
						to_read, dummy);
					if (res >= OK)
						size += (to_read * sizeof(u8));
				} else {
					log_info(1, "%s Wrong address size!\n",
					__func__);
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READSYSINFO:
			if (number_param == 1)
				res = read_sys_info();
			else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_SYSTEMRESET:
			if (number_param == 1)
				res = fts_system_reset(1);
			else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_CONFIGURESPI4:
			if (number_param == 1)
				res = configure_spi4();
			else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_GETFWFILE:
			break;
		case CMD_FWUPDATE:
			/*need to pass force_update_code [force_update_reg]
			[forceupdate_ms] [forceupdate_ss]*/
			if (number_param >= 2) {
				force_burn_flag.code_update = 0;
				force_burn_flag.panel_init = 0;
				for (; i < FLASH_MAX_SECTIONS; i++)
					force_burn_flag.section_update[i] = 0;
				force_burn_flag.code_update = cmd[1];
				if (number_param > 2) {
					for (i = 0; i < (number_param - 2); i++)
						force_burn_flag.
						section_update[i] = cmd[2 + i];
				}
				res = flash_update(&force_burn_flag);
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_INTERRUPT:
			if (number_param == 2) {
				if (cmd[1] == 1)
					res = fts_enable_interrupt();
				else
					res = fts_disable_interrupt();
			} else {
				log_info(1, "%s: wrong number of parameters\n",
				__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_FWREGWRITE:
			if (number_param >= 4)
				res = fts_write_fw_reg(
						(u16)(((cmd[1] & 0x00FF) << 8)+
						(cmd[2] & 0x00FF)), &cmd[3],
						(number_param - 3));
			else {
				log_info(1, "%s: wrong number of parameters\n",
				__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_FWREGREAD:
			if (number_param >= 5) {
				u8_to_u16_be(&cmd[3], &to_read);
				read_buf = (u8 *)kmalloc(to_read * sizeof(u8),
						GFP_KERNEL);
				if (read_buf == NULL) {
					log_info(1, "%s: Error allocating memory\n",
						__func__);
					to_read = 0;
					res = ERROR_ALLOC;
					break;
				}
				res = fts_read_fw_reg(
						(u16)(((cmd[1] & 0x00FF) << 8)+
						(cmd[2] & 0x00FF)),
						read_buf, to_read);
				if (res >= OK)
					size += (to_read * sizeof(u8));
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READMSFRAME:
			if (number_param == 2) {
				res = get_ms_frame(cmd[1], &mutual_frame);
				if (res < OK)
					log_info(1,
					"%s: Error while reading mutual frame..ERROR: %08X\n",
					__func__, res);
				else {
					res = OK;
					print_frame_short("Mutual frame =",
					  array_1d_to_2d_short(
					  mutual_frame.node_data,
					  mutual_frame.node_data_size,
					  mutual_frame.header.sense_node),
					  mutual_frame.header.force_node,
					  mutual_frame.header.sense_node);
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
				__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READSSFRAME:
			if (number_param == 2) {
				res = get_ss_frame(cmd[1], &self_frame);
				if (res < OK)
					log_info(1,
					"%s: Error while reading self frame.. ERROR: %08X\n",
					__func__, res);
				else {
					res = OK;
					print_frame_short("Self force frame =",
					  array_1d_to_2d_short(
					  self_frame.force_data,
					  self_frame.header.force_node, 1),
					  self_frame.header.force_node, 1);
					print_frame_short("Self sense frame =",
					  array_1d_to_2d_short(
					  self_frame.sense_data,
					  self_frame.header.sense_node,
					  self_frame.header.sense_node),
					  1,
					  self_frame.header.sense_node);
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READSYNCFRAME:
			if (number_param == 2) {
				res = get_sync_frame(cmd[1], &mutual_frame,
							&self_frame);
				if (res < OK)
					log_info(1,
					"%s: Error while reading self frame..ERROR: %08X\n",
					__func__, res);
				else {
					res = OK;
					print_frame_short("Mutual frame =",
					  array_1d_to_2d_short(
					  mutual_frame.node_data,
					  mutual_frame.node_data_size,
					  mutual_frame.header.sense_node),
					  mutual_frame.header.force_node,
					  mutual_frame.header.sense_node);
					print_frame_short("Self force frame =",
					  array_1d_to_2d_short(
					  self_frame.force_data,
					  self_frame.header.force_node,
					  1),
					  self_frame.header.force_node,
					  1);
					print_frame_short("Self sense frame =",
					  array_1d_to_2d_short(
					  self_frame.sense_data,
					  self_frame.header.sense_node,
					  self_frame.header.sense_node),
					  1,
					  self_frame.header.sense_node);
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
				__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READMSCXDATA:
			if (number_param == 2) {
				res = get_mutual_cx_data(cmd[1], &mutual_cx);
				if (res < OK)
					log_info(1,
					"%s: Error while reading mutual cx data.. ERROR: %08X\n",
					__func__, res);
				else {
					res = OK;
					print_frame_i8("Mutual CX2 data =",
					array_1d_to_2d_i8(mutual_cx.node_data,
					  mutual_cx.node_data_size,
					  mutual_cx.header.sense_node),
					  mutual_cx.header.force_node,
					  mutual_cx.header.sense_node);
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READSSCXDATA:
			if (number_param == 2) {
				res = get_self_cx_data(cmd[1], &self_cx);
				if (res < OK)
					log_info(1,
					"%s: Error while reading self cx data.. ERROR: %08X\n",
					__func__, res);
				else {
					res = OK;
					print_frame_i8("Self ix2_tx data =",
					array_1d_to_2d_i8(self_cx.ix2_tx,
					self_cx.header.force_node,
					self_cx.header.force_node),
					1,
					self_cx.header.force_node);
					print_frame_i8("Self cx2_tx data =",
					  array_1d_to_2d_i8(self_cx.cx2_tx,
					  self_cx.header.force_node,
					  self_cx.header.force_node),
					  1,
					  self_cx.header.force_node);
					print_frame_i8("Self ix2_rx data =",
					  array_1d_to_2d_i8(self_cx.ix2_rx,
					  self_cx.header.sense_node,
					  self_cx.header.sense_node),
					  1,
					  self_cx.header.sense_node);
					print_frame_i8("Self cx2_rx data =",
					  array_1d_to_2d_i8(self_cx.cx2_rx,
					  self_cx.header.sense_node,
					  self_cx.header.sense_node),
					  1,
					  self_cx.header.sense_node);
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
						__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_MAINTEST:
			if (number_param == 3) {
				res = fts_production_test_main(LIMITS_FILE,
						 cmd[2], &tests, cmd[1]);
				if (res < OK)
					log_info(1,
					"%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1,
				"%s: wrong number of parameters\n", __func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_ITOTEST:
			if (number_param == 1) {
				res = fts_production_test_ito(LIMITS_FILE,
								&tests);
				if (res < OK)
					log_info(1,
					"%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1,
				"%s: wrong number of parameters\n", __func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_MUTUALRAWTEST:
			if (number_param == 1) {
				res = fts_production_test_ms_raw(LIMITS_FILE,
								&tests);
				if (res < OK)
					log_info(1, "%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_MUTUALRAWLPTEST:
			if (number_param == 1) {
				res = fts_production_test_ms_raw_lp(LIMITS_FILE,
								&tests);
				if (res < OK)
					log_info(1,
					"%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1,
				"%s: wrong number of parameters\n", __func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_SELFRAWTEST:
			if (number_param == 1) {
				res = fts_production_test_ss_raw(LIMITS_FILE,
								&tests);
				if (res < OK)
					log_info(1,
					"%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_SELFRAWLPTEST:
			if (number_param == 1) {
				res = fts_production_test_ss_raw_lp(LIMITS_FILE,
									&tests);
				if (res < OK)
					log_info(1,
					"%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1,
				"%s: wrong number of parameters\n", __func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READSYSERRORS:
			if (number_param == 1)
				res = fts_read_sys_errors();
			else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_REQUESTHDM:
			if (number_param == 2)
				res = fts_request_hdm(cmd[1]);
			else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READFIFOEVENT:
			if (number_param == 1) {
				to_read = 8;
				dummy = 0;
				read_buf = (u8 *)kmalloc(to_read * sizeof(u8),
					GFP_KERNEL);
				if (read_buf == NULL) {
					log_info(1,
					"%s: Error allocating memory\n",
					__func__);
					to_read = 0;
					res = ERROR_ALLOC;
					break;
				}
				res = fts_read_fw_reg(FIFO_READ_ADDR,
							read_buf, to_read);
				if (res >= OK)
					size += (to_read * sizeof(u8));
			} else {
				log_info(1,
				"%s: wrong number of parameters\n", __func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_MUTUALTOTCXLPTEST:
			if (number_param == 2) {
				res = fts_production_test_ms_cx_lp(LIMITS_FILE,
								cmd[1], &tests);
				if (res < OK)
					log_info(1, "%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_SELFTOTIXTEST:
			if (number_param == 1) {
				res = fts_production_test_ss_ix(LIMITS_FILE,
								&tests);
				if (res < OK)
					log_info(1, "%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_SELFTOTIXDETECTTEST:
			if (number_param == 1) {
				res = fts_production_test_ss_ix_lp(LIMITS_FILE,
								&tests);
				if (res < OK)
					log_info(1, "%s: Error running production tests: %08X\n",
					__func__, res);
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READMSTOTALCXLPDATA:
			if (number_param == 2) {
				res = get_mutual_total_cx_data(cmd[1],
					&mutual_total_cx);
				if (res < OK)
					log_info(1,
					"%s: Error while reading mutual total cx data.. ERROR: %08X\n",
					__func__, res);
				else {
					res = OK;
					print_frame_short(
					"Mutual Total CX data =",
					array_1d_to_2d_short(
					  mutual_total_cx.node_data,
					  mutual_total_cx.node_data_size,
					  mutual_total_cx.header.sense_node),
					  mutual_total_cx.header.force_node,
					  mutual_total_cx.header.sense_node);
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_READSSTOTALIXDATA:
			if (number_param == 2) {
				res = get_self_total_cx_data(cmd[1],
&self_total_cx);
				if (res < OK)
					log_info(1,
					"%s: Error while reading self total ix data.. ERROR: %08X\n",
					__func__, res);
				else {
					res = OK;
					print_frame_u16("Self ix_tx data =",
					array_1d_to_2d_u16(self_total_cx.ix_tx,
					self_total_cx.header.force_node,
					1),
					self_total_cx.header.force_node,
					1);
					print_frame_u16("Self ix_rx data =",
					array_1d_to_2d_u16(self_total_cx.ix_rx,
					  self_total_cx.header.sense_node,
					  self_total_cx.header.sense_node),
					  1,
					  self_total_cx.header.sense_node);
				}
			} else {
				log_info(1, "%s: wrong number of parameters\n",
						__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		case CMD_FWWRITEAUTOCLEAR:
			if (number_param == 6)
				res = fts_fw_request(
						(u16)(((cmd[1] & 0x00FF) << 8)+
						(cmd[2] & 0x00FF)), cmd[3], 1,
						(int)((((cmd[4] & 0x00FF) << 8)+
						(cmd[5] & 0x00FF))/10));
			/*(actual time out in API is x10(multiple) of input)*/
			else {
				log_info(1, "%s: wrong number of parameters\n",
					__func__);
				res = ERROR_OP_NOT_ALLOW;
			}
			break;
		default:
			log_info(1, "%s: COMMAND ID NOT VALID!!!\n", __func__);
			res = ERROR_OP_NOT_ALLOW;
			break;
		}
	}

 goto_end:
	size += (sizeof(u32)); /*for error code of 4 bytes*/
	size *= 2; /*for each chars*/
	size += 5; /*for "{", " ", " ", "}", "\n" */
	test_print_buff = (u8 *)kmalloc(size * sizeof(u8), GFP_KERNEL);
	if (test_print_buff == NULL) {
		log_info(1,
		"%s: Error allocating memory for io buff\n", __func__);
		res = ERROR_ALLOC;
	}
	snprintf(&test_print_buff[index], 3, "{ ");
	index += 2;
	snprintf(&test_print_buff[index], 9, "%08X", res);
	index += 8;
	if (res >= OK) {
		switch (func_to_test[0]) {
		case CMD_DRIVER_VERSION:
		case CMD_WRITEREAD:
		case CMD_FWREGREAD:
		case CMD_READFIFOEVENT:
			if (dummy == 1)
				i = 1;
			else
				i = 0;
			for (; i < (to_read + dummy); i++) {
				snprintf(&test_print_buff[index],
				3, "%02X", read_buf[i]);
				index += 2;
			}
		break;
		}
	}
	snprintf(&test_print_buff[index], 4, " }\n");
	limit = size;
	printed = 0;

	number_param = 0;
	if (read_buf != NULL) {
		kfree(read_buf);
		read_buf = NULL;
	}

	if (pbuf != NULL) {
		kfree(pbuf);
		pbuf = NULL;
	}
	if (cmd != NULL) {
		kfree(cmd);
		cmd = NULL;
	}

	if (func_to_test != NULL) {
		kfree(func_to_test);
		func_to_test = NULL;
	}
	return count;
}

/** @}*/

/**
  * file_operations struct which define the functions for the canonical
  *operation on a device file node (open. read, write etc.)
  */
static const struct proc_ops fts_driver_test_ops = {
	.proc_open = fts_open,
	.proc_read = seq_read,
	.proc_write = fts_seq_write,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release
};

/**
  * This function is called in the probe to initialize and create the directory
  * proc/fts and the driver test file node DRIVER_TEST_FILE_NODE into the /proc
  * file system
  * @return OK if success or an error code which specify the type of error
  */
int fts_proc_init(void)
{
	struct proc_dir_entry *entry;
	int retval = 0;

	fts_dir = proc_mkdir_data("fts", 0777, NULL, NULL);
	if (fts_dir == NULL) {	/* directory creation failed */
		retval = -ENOMEM;
		goto out;
	}

	entry = proc_create(DRIVER_TEST_FILE_NODE, 0777, fts_dir,
		&fts_driver_test_ops);

	if (entry)
		log_info(1, "%s: proc entry CREATED!\n", __func__);
	else {
		log_info(1, "%s: error creating proc entry!\n",	__func__);
		retval = -ENOMEM;
		goto bad_file;
	}
	return OK;
bad_file:
	remove_proc_entry("fts", NULL);
out:
	return retval;
}

/**
  * Delete and Clean from the file system, all the references to the driver test
  * file node
  * @return OK
  */
int fts_proc_remove(void)
{
	remove_proc_entry(DRIVER_TEST_FILE_NODE, fts_dir);
	remove_proc_entry("fts", NULL);
	return OK;
}
