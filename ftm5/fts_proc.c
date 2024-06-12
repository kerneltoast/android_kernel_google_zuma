/*
  *
  **************************************************************************
  **                        STMicroelectronics				 **
  **************************************************************************
  **                        marco.cali@st.com				*
  **************************************************************************
  *                                                                        *
  *                     Utilities published in /proc/fts		*
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

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include "fts.h"
#include "fts_lib/ftsCompensation.h"
#include "fts_lib/ftsCore.h"
#include "fts_lib/ftsIO.h"
#include "fts_lib/ftsError.h"
#include "fts_lib/ftsFrame.h"
#include "fts_lib/ftsFlash.h"
#include "fts_lib/ftsTest.h"
#include "fts_lib/ftsTime.h"
#include "fts_lib/ftsTool.h"


#define DRIVER_TEST_FILE_NODE	"driver_test"	/* /< name of file node
						 * published */
#define DIAGNOSTIC_NUM_FRAME	10	/* /< number of frames reading
					 * iterations during the diagnostic
					 * test */



/** @defgroup proc_file_code	 Proc File Node
  * @ingroup file_nodes
  * The /proc/fts/driver_test file node provide expose the most important API
  * implemented into the driver to execute any possible operation into the IC \n
  * Thanks to a series of Operation Codes, each of them, with a different set of
  * parameter, it is possible to select a function to execute\n
  * The result of the function is usually returned into the shell as an ASCII
  * hex string where each byte is encoded in two chars.\n
  * @{
  */

/* Bus operations */
#define CMD_READ				0x00	/* /< I2C/SPI read: need
							 * to pass: byteToRead1
							 * byteToRead0
							 * (optional) dummyByte
							 * */
#define CMD_WRITE				0x01	/* /< I2C/SPI write:
							 * need to pass: cmd[0]
							 *  cmd[1] …
							 * cmd[cmdLength-1] */
#define CMD_WRITEREAD				0x02	/* /< I2C/SPI writeRead:
							 * need to pass: cmd[0]
							 *  cmd[1] …
							 * cmd[cmdLength-1]
							 * byteToRead1
							 * byteToRead0 dummyByte
							 * */
#define CMD_WRITETHENWRITEREAD			0x03	/* /< I2C/SPI write then
							 * writeRead: need to
							 * pass: cmdSize1
							 * cmdSize2 cmd1[0]
							 * cmd1[1] …
							 * cmd1[cmdSize1-1]
							 * cmd2[0] cmd2[1] …
							 * cmd2[cmdSize2-1]
							 *  byteToRead1
							 * byteToRead0 */
#define CMD_WRITEU8UX				0x04
						/* /< I2C/SPI
						 * writeU8UX:
						 * need to pass: cmd
						 * addrSize addr[0] …
						 * addr[addrSize-1]
						 * data[0] data[1] … */
#define CMD_WRITEREADU8UX			0x05	/* /< I2C/SPI
							 * writeReadU8UX: need
							 * to pass: cmd addrSize
							 * addr[0] …
							 * addr[addrSize-1]
							 * byteToRead1
							 * byteToRead0
							 * hasDummyByte */
#define CMD_WRITEU8UXTHENWRITEU8UX		0x06
						/* /< I2C/SPI writeU8UX
						 * then writeU8UX: need
						 * to pass: cmd1
						 * addrSize1 cmd2
						 * addrSize2 addr[0] …
						 * addr[addrSize1+
						 *      addrSize2-1]
						 * data[0] data[1] … */
#define CMD_WRITEU8UXTHENWRITEREADU8UX		0x07	/* /< I2C/SPI writeU8UX
							 *  then writeReadU8UX:
							 * need to pass: cmd1
							 * addrSize1 cmd2
							 * addrSize2 addr[0] …
							 * addr[addrSize1+
							 *      addrSize2-1]
							 *  byteToRead1
							 * byteToRead0
							 * hasDummybyte */
#define CMD_GETLIMITSFILE			0x08	/* /< Get the Production
							 * Limits File and print
							 * its content into the
							 * shell: need to pass:
							 * path(optional)
							 * otherwise select the
							 * approach chosen at
							 * compile time */
#define CMD_GETFWFILE				0x09	/* /< Get the FW file
							 * and print its content
							 * into the shell: need
							 * to pass: path
							 * (optional) otherwise
							 * select the approach
							 * chosen at compile
							 * time */
#define CMD_VERSION				0x0A	/* /< Get the driver
							 * version and other
							 * driver setting info
							 * */
#define CMD_READCONFIG				0x0B	/* /< Read The config
							 * memory, need to pass:
							 * addr[0] addr[1]
							 * byteToRead0
							 * byteToRead1 */


/* GUI utils byte ver */
#define CMD_READ_BYTE				0xF0	/* /< Byte output
							 * version of I2C/SPI
							 * read @see CMD_READ */
#define CMD_WRITE_BYTE				0xF1	/* /< Byte output
							 * version of I2C/SPI
							 * write @see CMD_WRITE
							 * */
#define CMD_WRITEREAD_BYTE			0xF2	/* /< Byte output
							 * version of I2C/SPI
							 * writeRead @see
							 * CMD_WRITEREAD */
#define CMD_WRITETHENWRITEREAD_BYTE		0xF3
						/* /< Byte output
						 * version of I2C/SPI
						 * write then writeRead
						 * @see
						 * CMD_WRITETHENWRITEREAD
						 * */
#define CMD_WRITEU8UX_BYTE			0xF4	/* /< Byte output
							 * version of I2C/SPI
							 * writeU8UX @see
							 * CMD_WRITEU8UX */
#define CMD_WRITEREADU8UX_BYTE			0xF5	/* /< Byte output
							 * version of I2C/SPI
							 * writeReadU8UX @see
							 * CMD_WRITEREADU8UX */
#define CMD_WRITEU8UXTHENWRITEU8UX_BYTE		0xF6
						/* /< Byte output
						 * version of I2C/SPI
						 * writeU8UX then
						 * writeU8UX @see
						 * CMD_WRITEU8UXTHENWRITEU8UX
						 * */
#define CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE	0xF7
						/* /< Byte output
						* version of I2C/SPI
						* writeU8UX  then
						* writeReadU8UX @see
						* CMD_WRITEU8UXTHENWRITEREADU8UX
						* */
#define CMD_GETLIMITSFILE_BYTE			0xF8	/* /< Byte output
							 * version of Production
							 * Limits File @see
							 * CMD_GETLIMITSFILE */
#define CMD_GETFWFILE_BYTE			0xF9	/* /< Byte output
							 * version of FW file
							 * need to pass: @see
							 * CMD_GETFWFILE */
#define CMD_VERSION_BYTE			0xFA	/* /< Byte output
							 * version of driver
							 * version and setting
							 * @see CMD_VERSION */
#define CMD_CHANGE_OUTPUT_MODE			0xFF	/* /< Select the output
							 * mode of the
							 * scriptless protocol,
							 * need to pass:
							 * bin_output = 1 data
							 * returned as binary,
							 * bin_output =0 data
							 * returned as hex
							 * string */

/* Core/Tools */
#define CMD_POLLFOREVENT			0x11	/* /< Poll the FIFO for
							 * an event: need to
							 * pass: eventLength
							 * event[0] event[1] …
							 * event[eventLength-1]
							 * timeToWait1
							 * timeToWait0 */
#define CMD_SYSTEMRESET				0x12	/* /< System Reset */
#define CMD_CLEANUP				0x13	/* /< Perform a system
							 * reset and optionally
							 * re-enable the
							 * scanning, need to
							 * pass: enableTouch */
#define CMD_POWERCYCLE				0x14	/* /< Execute a power
							 * cycle toggling the
							 * regulators */
#define CMD_READSYSINFO				0x15	/* /< Read the System
							 * Info information from
							 * the framebuffer, need
							 * to pass: doRequest */
#define CMD_FWWRITE				0x16
						/* /< Write a FW
						 * command: need to
						 * pass: cmd[0]  cmd[1]
						 * … cmd[cmdLength-1] */
#define CMD_INTERRUPT				0x17	/* /< Allow to enable or
							 * disable the
							 * interrupts, need to
							 * pass: enable (if 1
							 * will enable the
							 * interrupt) */
#define CMD_SETSCANMODE				0x18	/* /< set Scan Mode
							 * need to pass:
							 * scanType option
							 */
#define CMD_SAVEMPFLAG				0x19	/* /< save manually a
							 * value in the MP flag
							 * need to pass: mpflag
							 */

/* Frame */
#define CMD_GETFORCELEN				0x20	/* /< Get the number of
							 * Force channels */
#define CMD_GETSENSELEN				0x21	/* /< Get the number of
							 * Sense channels */
#define CMD_GETMSFRAME				0x23	/* /< Get a MS frame:
							 * need to pass:
							 * MSFrameType */
#define CMD_GETSSFRAME				0x24	/* /< Get a SS frame:
							 * need to pass:
							 * SSFrameType */
#define CMD_GETSYNCFRAME			0x25	/* /< Get a SS frame:
							 * need to pass:
							 * SSFrameType
							 */

/* Compensation */
#define CMD_REQCOMPDATA				0x30	/* /< Request Init data:
							 * need to pass: type */
#define CMD_READCOMPDATAHEAD			0x31	/* /< Read Init data
							 * header: need to pass:
							 * type */
#define CMD_READMSCOMPDATA			0x32	/* /< Read MS Init data:
							 * need to pass: type */
#define CMD_READSSCOMPDATA			0x33	/* /< Read SS Init data:
							 * need to pass: type */
#define CMD_READGOLDENMUTUAL			0x34	/* /< Read GoldenMutual
							   raw data */
#define CMD_READTOTMSCOMPDATA			0x35	/* /< Read Tot MS Init
							 * data: need to pass:
							 * type */
#define CMD_READTOTSSCOMPDATA			0x36	/* /< Read Tot SS Init
							 * data: need to pass:
							 * type */
#define CMD_READSENSCOEFF			0x37	/* /< Read MS and SS
							 * Sensitivity
							 * Calibration
							 * Coefficients */

/* FW Update */
#define CMD_GETFWVER				0x40	/* /< Get the FW version
							 * of the IC */
#define CMD_FLASHUNLOCK				0x42	/* /< Unlock the flash
							 * */
#define CMD_READFWFILE				0x43	/* /< Try to read the FW
							 * file, need to pass:
							 * keep_cx */
#define CMD_FLASHPROCEDURE			0x44	/* /< Perform a full
							 * flashing procedure:
							 * need to pass: force
							 * keep_cx */
#define CMD_FLASHERASEUNLOCK			0x45	/* /< Unlock the erase
							 * of the flash */
#define CMD_FLASHERASEPAGE			0x46
						/* /< Erase page by page
						 * the flash, need to
						 * pass: keep_cx, if
						 * keep_cx>SKIP_PANEL_INIT
						 * Panel Init Page will
						 * be skipped, if
						 * >SKIP_PANEL_CX_INIT
						 * Cx and Panel Init
						 * Pages will be skipped
						 * otherwise if
						 * =ERASE_ALL all the
						 * pages will be deleted
						 * */

/* MP test */
#define CMD_ITOTEST				0x50	/* /< Perform an ITO
							 * test */
#define CMD_INITTEST				0x51	/* /< Perform an
							 * Initialization test:
							 * need to pass: type */
#define CMD_MSRAWTEST				0x52	/* /< Perform MS raw
							 * test: need to pass
							 * stop_on_fail */
#define CMD_MSINITDATATEST			0x53	/* /< Perform MS Init
							 * data test: need to
							 * pass stop_on_fail */
#define CMD_SSRAWTEST				0x54	/* /< Perform SS raw
							 * test: need to pass
							 * stop_on_fail */
#define CMD_SSINITDATATEST			0x55	/* /< Perform SS Init
							 * data test: need to
							 * pass stop_on_fail */
#define CMD_MAINTEST				0x56	/* /< Perform a full
							 * Mass production test:
							 * need to pass
							 * stop_on_fail saveInit
							 * */
#define CMD_FREELIMIT				0x57	/* /< Free (if any)
							 * limit file which was
							 * loaded during any
							 * test procedure */

/* Diagnostic */
#define CMD_DIAGNOSTIC				0x60	/* /< Perform a series
							 * of commands and
							 * collect severals data
							 * to detect any
							 * malfunction */

#define CMD_CHANGE_SAD				0x70	/* /< Allow to change
							 * the SAD address (for
							 * debugging) */
#define CMD_INFOBLOCK_STATUS			0x61	/* /< Check for Info
							 * block error */

/* Debug functionalities requested by Google for B1 Project */
#define CMD_TRIGGER_FORCECAL			0x80	/* /< Trigger manually
							 * forcecal for MS and
							 * SS */
#define CMD_BASELINE_ADAPTATION			0x81	/* /< Enable/Disable
							 * Baseline adaptation,
							 * need to pass: enable
							 * */
#define CMD_FREQ_HOP				0x82	/* /< Enable/Disable
							 * Frequency hopping,
							 * need to pass: enable
							 * */
#define CMD_SET_OPERATING_FREQ			0x83	/* /< Set a defined
							 * scanning frequency in
							 * Hz passed as 4 bytes
							 * in big endian, need
							 * to pass: freq3 freq2
							 * freq1 freq0 */
#define CMD_READ_SYNC_FRAME			0x84
						/* /< Read Sync Frame
						 * which contain MS and
						 * SS data, need to
						 * pass: frameType (this
						 * parameter can be
						 * LOAD_SYNC_FRAME_STRENGTH
						 * or LOAD_SYNC_FRAME_BASELINE)
						 * */


#define CMD_TP_SENS_MODE			0x90	/* /< Enter/Exit in the
							 * TP Sensitivity
							 * Calibration mode,
							 * need to pass: enter
							 * (optional)saveGain */
#define CMD_TP_SENS_SET_SCAN_MODE		0x91	/* /< Set scan mode type
							 * which should be used
							 * for the test before
							 * the stimpad is down,
							 * need to pass: type */
#define CMD_TP_SENS_PRECAL_SS			0x92	/* /< Perform Pre
							 * Calibration for SS
							 * steps when stimpad is
							 * down */
#define CMD_TP_SENS_PRECAL_MS			0x93	/* /< Perform Pre
							 * Calibration for MS
							 * steps when stimpad is
							 * down */
#define CMD_TP_SENS_POSTCAL_MS			0x94	/* /< Perform Post
							 * Calibration for MS
							 * steps when stimpad is
							 * down */
#define CMD_TP_SENS_STD				0x95	/* /< Compute the
							 * Standard deviation of
							 * a certain number of
							 * frames, need to pass:
							 * numFrames */

#define CMD_FORCE_TOUCH_ACTIVE			0xA0	/* /< Prevent the driver
							 * from transitioning
							 * the ownership of the
							 * bus to SLPI
							 */

/** @}*/

/** @defgroup scriptless Scriptless Protocol
  * @ingroup proc_file_code
  * Scriptless Protocol allows ST Software (such as FingerTip Studio etc) to
  * communicate with the IC from an user space.
  * This mode gives access to common bus operations (write, read etc) and
  * support additional functionalities. \n
  * The protocol is based on exchange of binary messages included between a
  * start and an end byte
  * @{
  */

#define MESSAGE_START_BYTE	0x7B	/* /< start byte of each message
					 * transferred in Scriptless Mode */
#define MESSAGE_END_BYTE	0x7D	/* /< end byte of each message
					 * transferred in Scriptless Mode */
#define MESSAGE_MIN_HEADER_SIZE 8	/* /< minimun number of bytes of the
					 * structure of a messages exchanged
					 * with host (include start/end byte,
					 * counter, actions, msg_size) */


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
	struct fts_ts_info *info = PDE_DATA(file_inode(s->file));

	dev_info(info->dev, "%s: Entering start(), pos = %lld limit = %d printed = %d\n",
		__func__, *pos, info->limit, info->printed);

	if (info->driver_test_buff == NULL && *pos == 0) {
		int size = 13 * sizeof(u8);

		dev_info(info->dev, "%s: No data to print!\n", __func__);
		info->driver_test_buff = (u8 *)kmalloc(size, GFP_KERNEL);
		info->limit = scnprintf(info->driver_test_buff,
				  size,
				  "{ %08X }\n", ERROR_OP_NOT_ALLOW);
		/* dev_err(info->dev, "%s: len = %d driver_test_buff = %s\n",
		 * __func__, info->limit, info->driver_test_buff); */
	} else {
		if (*pos != 0)
			*pos = info->printed;

		if (*pos >= info->limit)
			/* dev_err(info->dev, "%s: Apparently, we're done.\n", __func__); */
			return NULL;
	}

	info->chunk = CHUNK_PROC;
	if (info->limit - *pos < CHUNK_PROC)
		info->chunk = info->limit - *pos;
	/* dev_err(info->dev, "%s: In start(),
	 *	updated pos = %Ld limit = %d printed = %d chunk = %d\n",
	 *	__func__, *pos, info->limit, info->printed, info->chunk); */
	memset(info->buf_chunk, 0, CHUNK_PROC);
	memcpy(info->buf_chunk, &info->driver_test_buff[(int)*pos], info->chunk);

	return info->buf_chunk;
}

/**
  * This function actually print a chunk amount of data in the sequential file
  * @param s pointer to the sequential file where to print the data
  * @param v pointer to the data to print
  * @return 0
  */
static int fts_seq_show(struct seq_file *s, void *v)
{
	struct fts_ts_info *info = PDE_DATA(file_inode(s->file));

	/* dev_err(info->dev, "%s: In show()\n", __func__); */
	if (seq_write(s, (u8 *)v, info->chunk) == 0)
		info->printed += info->chunk;
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
	struct fts_ts_info *info = PDE_DATA(file_inode(s->file));

	/* int* val_ptr; */
	/* dev_err(info->dev, "%s: In next(), v = %X, pos = %Ld.\n", __func__,
	 * v, *pos); */
	(*pos) += info->chunk;/* increase my position counter */
	info->chunk = CHUNK_PROC;

	/* dev_err(info->dev, "%s: In next(),
	 *	updated pos = %Ld limit = %d printed = %d\n",
	 *	__func__, *pos, info->limit, info->printed); */
	if (*pos >= info->limit)	/* are we done? */
		return NULL;
	else if (info->limit - *pos < CHUNK_PROC)
		info->chunk = info->limit - *pos;


	memset(info->buf_chunk, 0, CHUNK_PROC);
	memcpy(info->buf_chunk, &info->driver_test_buff[(int)*pos], info->chunk);
	return info->buf_chunk;
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
	struct fts_ts_info *info = PDE_DATA(file_inode(s->file));

	/* dev_err(info->dev, "%s: Entering stop().\n", __func__); */

	if (v) {
		/* dev_err(info->dev, "%s: v is %X.\n", __func__, v); */
	} else {
		/* dev_err(info->dev, "%s: v is null.\n", __func__); */
		info->limit = 0;
		info->chunk = 0;
		info->printed = 0;
		if (info->driver_test_buff != NULL) {
		/* dev_err(info->dev, "%s: Freeing and clearing driver_test_buff.\n",
		 *   __func__); */
			kfree(info->driver_test_buff);
			info->driver_test_buff = NULL;
		} else {
		/* dev_err(info->dev, "%s: driver_test_buff is already null.\n",
		 *   __func__); */
		}
	}
}

/**
  * Struct where define and specify the functions which implements the flow for
  * writing on a sequential file
  */
static const struct seq_operations fts_seq_ops = {
	.start	= fts_seq_start,
	.next	= fts_seq_next,
	.stop	= fts_seq_stop,
	.show	= fts_seq_show
};

/**
  * This function open a sequential file
  * @param inode Inode in the file system that was called and triggered this
  * function
  * @param file file associated to the file node
  * @return error code, 0 if success
  */
static int fts_driver_test_open(struct inode *inode, struct file *file)
{
	int retval;
	struct fts_ts_info *info = PDE_DATA(inode);

	if (!info) {
		dev_err(info->dev, "%s: Unable to access driver data\n", __func__);
		retval = -ENODEV;
		goto exit;
	}

	if (!mutex_trylock(&info->diag_cmd_lock)) {
		dev_err(info->dev, "%s: Blocking concurrent access\n", __func__);
		retval = -EBUSY;
		goto exit;
	}

	/* Allowing only a single process to open diag procfs node */
	if (info->diag_node_open == true) {
		dev_err(info->dev, "%s: Blocking multiple open\n", __func__);
		retval = -EBUSY;
		goto unlock;
	}

	retval = seq_open(file, &fts_seq_ops);
	if(!retval) {
		info->diag_node_open = true;
	}

unlock:
	mutex_unlock(&info->diag_cmd_lock);
exit:
	return retval;
};

/**
  * This function closes a sequential file
  * @param inode Inode in the file system that was called and triggered this
  * function
  * @param file file associated to the file node
  * @return error code, 0 if success
  */
static int fts_driver_test_release(struct inode *inode, struct file *file)
{
	int retval;
	struct fts_ts_info *info = PDE_DATA(inode);

	if (info)
		mutex_lock(&info->diag_cmd_lock);
	else
		dev_err(info->dev, "%s: Unable to access driver data\n", __func__);

	retval = seq_release(inode, file);

	if (info) {
		info->diag_node_open = false;
		mutex_unlock(&info->diag_cmd_lock);
	}

	return retval;
}

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
static ssize_t fts_driver_test_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *pos)
{
	int numberParam = 0;
	struct fts_ts_info *info = PDE_DATA(file_inode(file));
	char *p = NULL;
	char *pbuf = NULL;
	char path[100] = { 0 };
	int res = -1, j, index = 0;
	u8 report = 0;
	int size = 6;
	int temp, byte_call = 0;
	u16 byteToRead = 0;
	u32 fileSize = 0;
	u8 *readData = NULL;
	u8 *cmd = NULL;	/* worst case needs count bytes */
	u32 maxNum_cmd = 0;
	u32 *funcToTest = NULL;
	u32 maxNum_funcToTest = 0;
	u64 addr = 0;
	MutualSenseFrame frameMS;
	MutualSenseFrame deltas;
	SelfSenseFrame frameSS;
	u8 error_to_search[2] = { EVT_TYPE_ERROR_OSC_TRIM,
				  EVT_TYPE_ERROR_AOFFSET_TRIM };

	DataHeader dataHead;
	MutualSenseData compData;
	SelfSenseData comData;
	TotMutualSenseData totCompData;
	TotSelfSenseData totComData;
	MutualSenseCoeff msCoeff;
	SelfSenseCoeff ssCoeff;
	GoldenMutualRawData gmRawData;
	int meanNorm = 0, meanEdge = 0;

	u64 address;
	u16 ito_max_val[2] = {0x00};
	char *label[4];

	Firmware fw;
	LimitFile lim;
	const char *limits_file = info->board->limits_name;

	if (!info) {
		dev_err(info->dev, "%s: Unable to access driver data\n", __func__);
		count =  -ENODEV;
		goto exit;
	}

	info->mess.dummy = 0;
	info->mess.action = 0;
	info->mess.msg_size = 0;

	if (count < 2) {
		res = ERROR_OP_NOT_ALLOW;
		goto ERROR;
	}

	/* alloc one more space to store '\0' at the end
	 * for "echo -n CMDs" case
	 */
	pbuf = kmalloc((count + 1) * sizeof(*pbuf), GFP_KERNEL);
	if (!pbuf) {
		res = ERROR_ALLOC;
		goto ERROR;
	}
	pbuf[count] = '\0';


	maxNum_funcToTest = (count + 1) / 3;
	funcToTest = kmalloc_array(maxNum_funcToTest, sizeof(*funcToTest),
			     GFP_KERNEL);
	if (!funcToTest) {
		res = ERROR_ALLOC;
		goto ERROR;
	}

	/*for(temp = 0; temp<count; temp++){
	  *      dev_err(info->dev, "p[%d] = %02X\n", temp, p[temp]);
	  * }*/
	if (access_ok(buf, count) < OK ||
	    copy_from_user(pbuf, buf, count) != 0) {
		res = ERROR_ALLOC;
		goto END;
	}

	maxNum_cmd = count;
	cmd = (u8 *)kmalloc_array(maxNum_cmd, sizeof(u8), GFP_KERNEL);
	if (cmd == NULL) {
		res = ERROR_ALLOC;
		dev_err(info->dev, "%s: Impossible allocate memory... ERROR %08X!\n",
			__func__, res);
		goto ERROR;
	}

	p = pbuf;
	if (count > MESSAGE_MIN_HEADER_SIZE - 1 && p[0] == MESSAGE_START_BYTE) {
		dev_info(info->dev, "Enter in Byte Mode!\n");
		byte_call = 1;
		info->mess.msg_size = (p[1] << 8) | p[2];
		info->mess.counter = (p[3] << 8) | p[4];
		info->mess.action = (p[5] << 8) | p[6];
		dev_info(info->dev, "Message received: size = %d, counter_id = %d, action = %04X\n",
			info->mess.msg_size, info->mess.counter,
			info->mess.action);
		size = MESSAGE_MIN_HEADER_SIZE + 2;	/* +2 error code */
		if (count < info->mess.msg_size || p[count - 2] !=
						   MESSAGE_END_BYTE) {
			dev_err(info->dev, "number of byte received or end byte wrong! msg_size = %d != %zu, last_byte = %02X != %02X ... ERROR %08X\n",
				info->mess.msg_size, count, p[count - 1],
				MESSAGE_END_BYTE, ERROR_OP_NOT_ALLOW);
			res = ERROR_OP_NOT_ALLOW;
			goto END;
		} else {
			numberParam = info->mess.msg_size - MESSAGE_MIN_HEADER_SIZE +
				      1;	/* +1 because put the internal
						 * op code */
			size = MESSAGE_MIN_HEADER_SIZE + 2;	/* +2 send also
								 * the first 2
								 * lsb of the
								 * error code */
			switch (info->mess.action) {
			case ACTION_READ:
				/* numberParam =
				 * info->mess.msg_size-MESSAGE_MIN_HEADER_SIZE+1; */
				cmd[0] = funcToTest[0] = CMD_READ_BYTE;
				break;

			case ACTION_WRITE:
				cmd[0] = funcToTest[0] = CMD_WRITE_BYTE;
				break;

			case ACTION_WRITE_READ:
				cmd[0] = funcToTest[0] = CMD_WRITEREAD_BYTE;
				break;

			case ACTION_GET_VERSION:
				cmd[0] = funcToTest[0] = CMD_VERSION_BYTE;
				break;

			case ACTION_WRITETHENWRITEREAD:
				cmd[0] = funcToTest[0] =
						 CMD_WRITETHENWRITEREAD_BYTE;
				break;

			case ACTION_WRITEU8UX:
				cmd[0] = funcToTest[0] = CMD_WRITEU8UX_BYTE;
				break;

			case ACTION_WRITEREADU8UX:
				cmd[0] = funcToTest[0] = CMD_WRITEREADU8UX_BYTE;
				break;

			case ACTION_WRITEU8UXTHENWRITEU8UX:
				cmd[0] = funcToTest[0] =
					 CMD_WRITEU8UXTHENWRITEU8UX_BYTE;
				break;

			case ACTION_WRITEU8XTHENWRITEREADU8UX:
				cmd[0] = funcToTest[0] =
					 CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE;
				break;

			case ACTION_GET_FW:
				cmd[0] = funcToTest[0] = CMD_GETFWFILE_BYTE;
				break;

			case ACTION_GET_LIMIT:
				cmd[0] = funcToTest[0] = CMD_GETLIMITSFILE_BYTE;
				break;

			default:
				dev_err(info->dev, "Invalid Action = %d ... ERROR %08X\n",
					info->mess.action, ERROR_OP_NOT_ALLOW);
				res = ERROR_OP_NOT_ALLOW;
				goto END;
			}

			if (numberParam - 1 != 0)
				memcpy(&cmd[1], &p[7], numberParam - 1);
			/* -1 because i need to exclude the cmd[0] */
		}
	} else {
		u8 result;
		char *token;
		char *path_token = NULL;
		size_t token_len;

		/* newline case at last char */
		if (p[count - 1] == '\n')
			p[count - 1] = '\0';

		/* Parse the input string to retrieve 2 hex-digit width
		 * cmds/args separated by one or more spaces, except for
		 * the fw/limits file name.
		 */
		while (p &&
			numberParam < min(maxNum_cmd, maxNum_funcToTest)) {

			while (isspace(*p))
				p++;

			token = strsep(&p, " ");

			if (!token || *token == '\0')
				break;

			token_len = strlen(token);

			/* break the loop to handle FW/LIMITS path */
			if (numberParam == 1 &&
				(funcToTest[0] == CMD_GETFWFILE ||
				funcToTest[0] == CMD_GETLIMITSFILE)) {
				path_token = token;
				break;
			}

			if (token_len != 2) {
				dev_err(info->dev, "bad len. len=%zu\n", token_len);
				res = ERROR_OP_NOT_ALLOW;
				goto ERROR;
			}

			if (kstrtou8(token, 16, &result)) {
				/* Conversion failed due to bad input.
				 * Discard the entire buffer.
				 */
				dev_err(info->dev, "bad input\n");
				res = ERROR_OP_NOT_ALLOW;
				goto ERROR;
			}

			/* found a valid cmd/args */
			cmd[numberParam] = funcToTest[numberParam] = result;
			dev_info(info->dev, "functionToTest[%d] = %02X cmd[%d] = %02X\n",
				numberParam, funcToTest[numberParam],
				numberParam, cmd[numberParam]);
			numberParam++;
		}

		/* FW/LIMITS path */
		if (path_token && strlen(path_token)) {
			strlcpy(path, path_token, sizeof(path));
			numberParam++;
		}

		if (numberParam == 0) {
			dev_err(info->dev, "Found invalid cmd/arg\n");
			res = ERROR_OP_NOT_ALLOW;
			goto END;
		}
	}

	fw.data = NULL;
	lim.data = NULL;

	dev_info(info->dev, "Number of Parameters = %d\n", numberParam);

	/* elaborate input */
	if (numberParam >= 1) {
		switch (funcToTest[0]) {
		case CMD_VERSION_BYTE:
			dev_info(info->dev, "%s: Get Version Byte\n", __func__);
			byteToRead = 2;
			info->mess.dummy = 0;
			readData = (u8 *)kmalloc(byteToRead * sizeof(u8),
						 GFP_KERNEL);
			size += byteToRead;
			if (readData != NULL) {
				readData[0] = (u8)(FTS_TS_DRV_VER >> 24);
				readData[1] = (u8)(FTS_TS_DRV_VER >> 16);
				dev_info(info->dev, "%s: Version = %02X%02X\n",
					 __func__, readData[0], readData[1]);
				res = OK;
			} else {
				res = ERROR_ALLOC;
				dev_err(info->dev, "%s: Impossible allocate memory... ERROR %08X\n",
					__func__, res);
			}
			break;


		case CMD_VERSION:
			byteToRead = 2 * sizeof(u32);
			info->mess.dummy = 0;
			readData = (u8 *)kmalloc(byteToRead * sizeof(u8),
						 GFP_KERNEL);
			u32ToU8_be(FTS_TS_DRV_VER, readData);
			fileSize = 0;
			/* first two bytes bitmask of features enabled in the
			 * IC, second two bytes bitmask of features enabled in
			 * the driver */

#ifdef FW_H_FILE
			fileSize |= 0x00010000;
#endif

#ifdef LIMITS_H_FILE
			fileSize |= 0x00020000;
#endif

#ifdef USE_ONE_FILE_NODE
			fileSize |= 0x00040000;
#endif

#ifdef FW_UPDATE_ON_PROBE
			fileSize |= 0x00080000;
#endif

#ifdef PRE_SAVED_METHOD
			fileSize |= 0x00100000;
#endif

#ifdef COMPUTE_INIT_METHOD
			fileSize |= 0x00200000;
#endif

#ifdef USE_GESTURE_MASK
			fileSize |= 0x00100000;
#endif

#ifdef I2C_INTERFACE
			fileSize |= 0x00200000;
#else
			if (info->client &&
			    (info->client->mode & SPI_3WIRE) == 0)
				fileSize |= 0x00400000;
#endif

#ifdef PHONE_KEY	/* it is a feature enabled in the config of the chip */
			fileSize |= 0x00000100;
#endif

#ifdef GESTURE_MODE
			fromIDtoMask(FEAT_SEL_GESTURE, (u8 *)&fileSize, 4);
#endif


#ifdef GRIP_MODE
			fromIDtoMask(FEAT_SEL_GRIP, (u8 *)&fileSize, 4);
#endif

#ifdef CHARGER_MODE
			fromIDtoMask(FEAT_SEL_CHARGER, (u8 *)&fileSize, 4);
#endif

#ifdef GLOVE_MODE
			fromIDtoMask(FEAT_SEL_GLOVE, (u8 *)&fileSize, 4);
#endif


#ifdef COVER_MODE
			fromIDtoMask(FEAT_SEL_COVER, (u8 *)&fileSize, 4);
#endif

#ifdef STYLUS_MODE
			fromIDtoMask(FEAT_SEL_STYLUS, (u8 *)&fileSize, 4);
#endif

			u32ToU8_be(fileSize, &readData[4]);
			res = OK;
			size += (byteToRead * sizeof(u8));
			break;

		case CMD_WRITEREAD:
		case CMD_WRITEREAD_BYTE:
			if (numberParam >= 5) {	/* need to pass: cmd[0]  cmd[1]
						 * … cmd[cmdLength-1]
						 * byteToRead1 byteToRead0
						 * dummyByte */
				temp = numberParam - 4;
				if (cmd[numberParam - 1] == 0)
					info->mess.dummy = 0;
				else
					info->mess.dummy = 1;

				u8ToU16_be(&cmd[numberParam - 3], &byteToRead);
				dev_info(info->dev, "bytesToRead = %d\n",
					byteToRead + info->mess.dummy);

				readData = (u8 *)kmalloc((byteToRead +
							  info->mess.dummy) *
							 sizeof(u8),
							 GFP_KERNEL);
				res = fts_writeRead_heap(info, &cmd[1], temp,
					readData, byteToRead + info->mess.dummy);
				size += (byteToRead * sizeof(u8));
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_WRITE:
		case CMD_WRITE_BYTE:
			if (numberParam >= 2) {	/* need to pass: cmd[0]  cmd[1]
						 * … cmd[cmdLength-1] */
				temp = numberParam - 1;

				res = fts_write_heap(info, &cmd[1], temp);
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READ:
		case CMD_READ_BYTE:
			if (numberParam >= 3) {	/* need to pass: byteToRead1
						 * byteToRead0 (optional)
						 * dummyByte */
				if (numberParam == 3 ||
				     (numberParam == 4 &&
				      cmd[numberParam - 1] == 0))
					info->mess.dummy = 0;
				else
					info->mess.dummy = 1;
				u8ToU16_be(&cmd[1], &byteToRead);
				readData = (u8 *)kmalloc((byteToRead +
							  info->mess.dummy) *
							 sizeof(u8),
							 GFP_KERNEL);
				res = fts_read_heap(info, readData,
						    byteToRead + info->mess.dummy);
				size += (byteToRead * sizeof(u8));
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_WRITETHENWRITEREAD:
		case CMD_WRITETHENWRITEREAD_BYTE:
			/* need to pass: cmdSize1 cmdSize2 cmd1[0] cmd1[1] …
			 * cmd1[cmdSize1-1] cmd2[0] cmd2[1] … cmd2[cmdSize2-1]
			 *  byteToRead1 byteToRead0 */
			if (numberParam >= 6) {
				u8ToU16_be(&cmd[numberParam - 2], &byteToRead);
				readData = (u8 *)kmalloc(byteToRead *
							 sizeof(u8),
							 GFP_KERNEL);
				res = fts_writeThenWriteRead_heap(info,
						&cmd[3], cmd[1],
						&cmd[3 + (int)cmd[1]], cmd[2],
						readData, byteToRead);
				size += (byteToRead * sizeof(u8));
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_WRITEU8UX:
		case CMD_WRITEU8UX_BYTE:
			/* need to pass:
			 *    cmd addrSize addr[0] … addr[addrSize-1]
			 *    data[0] data[1] … */
			if (numberParam >= 4) {
				if (cmd[2] <= sizeof(u64)) {
					u8ToU64_be(&cmd[3], &addr, cmd[2]);
					dev_info(info->dev, "addr = %llx\n", addr);
					res = fts_writeU8UX(info, cmd[1],
						cmd[2], addr,
						&cmd[3 + cmd[2]],
						(numberParam - cmd[2] - 3));
				} else {
					dev_err(info->dev, "Wrong address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;


		case CMD_WRITEREADU8UX:
		case CMD_WRITEREADU8UX_BYTE:
			/* need to pass:
			 *    cmd addrSize addr[0] … addr[addrSize-1]
			 *    byteToRead1 byteToRead0 hasDummyByte */
			if (numberParam >= 6) {
				if (cmd[2] <= sizeof(u64)) {
					u8ToU64_be(&cmd[3], &addr, cmd[2]);
					u8ToU16_be(&cmd[numberParam - 3],
						   &byteToRead);
					readData = (u8 *)kmalloc(byteToRead *
								 sizeof(u8),
								 GFP_KERNEL);
					dev_info(info->dev, "addr = %llx byteToRead = %d\n",
						addr, byteToRead);
					res = fts_writeReadU8UX(info, cmd[1],
							cmd[2], addr, readData,
							byteToRead,
							cmd[numberParam - 1]);
					size += (byteToRead * sizeof(u8));
				} else {
					dev_err(info->dev, "Wrong address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_WRITEU8UXTHENWRITEU8UX:
		case CMD_WRITEU8UXTHENWRITEU8UX_BYTE:
			/* need to pass:
			 *    cmd1 addrSize1 cmd2 addrSize2 addr[0] …
			 *    addr[addrSize1+addrSize2-1] data[0] data[1] … */
			if (numberParam >= 6) {
				if ((cmd[2] + cmd[4]) <= sizeof(u64)) {
					u8ToU64_be(&cmd[5], &addr, cmd[2] +
						   cmd[4]);

					dev_info(info->dev, "addr = %llx\n", addr);
					res = fts_writeU8UXthenWriteU8UX(info,
						cmd[1], cmd[2], cmd[3],
						cmd[4], addr,
						&cmd[5 + cmd[2] + cmd[4]],
						(numberParam - cmd[2]
							- cmd[4] - 5));
				} else {
					dev_err(info->dev, "Wrong address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_WRITEU8UXTHENWRITEREADU8UX:
		case CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE:
			/* need to pass:
			 * cmd1 addrSize1 cmd2 addrSize2 addr[0] …
			 * addr[addrSize1+addrSize2-1]  byteToRead1 byteToRead0
			 * hasDummybyte */
			if (numberParam >= 8) {
				if ((cmd[2] + cmd[4]) <= sizeof(u64)) {
					u8ToU64_be(&cmd[5], &addr, cmd[2] +
						   cmd[4]);
					dev_info(info->dev, "%s: cmd[5] = %02X, addr =  %llx\n",
						__func__, cmd[5], addr);
					u8ToU16_be(&cmd[numberParam - 3],
						   &byteToRead);
					readData = (u8 *)kmalloc(byteToRead *
								 sizeof(u8),
								 GFP_KERNEL);
					res = fts_writeU8UXthenWriteReadU8UX(
						info,
						cmd[1], cmd[2], cmd[3], cmd[4],
						addr,
						readData, byteToRead,
						cmd[numberParam - 1]);
					size += (byteToRead * sizeof(u8));
				} else {
					dev_err(info->dev, "Wrong total address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_CHANGE_OUTPUT_MODE:
			/* need to pass: bin_output */
			if (numberParam >= 2) {
				info->bin_output = cmd[1];
				dev_info(info->dev, "Setting Scriptless output mode: %d\n",
					info->bin_output);
				res = OK;
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_FWWRITE:
			if (numberParam >= 3) {	/* need to pass: cmd[0]  cmd[1]
						 * … cmd[cmdLength-1] */
				if (numberParam >= 2) {
					temp = numberParam - 1;
					res = fts_writeFwCmd_heap(info,
								&cmd[1],
								temp);
				} else {
					dev_err(info->dev, "Wrong parameters!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_INTERRUPT:
			/* need to pass: enable */
			if (numberParam >= 2) {
				if (cmd[1] == 1)
					res = fts_enableInterrupt(info, true);
				else
					res = fts_enableInterrupt(info, false);
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_SETSCANMODE:
			/* need to pass: scanMode option */
			if (numberParam >= 3)
				res = setScanMode(info, cmd[1], cmd[2]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_SAVEMPFLAG:
			/* need to pass: mpflag */
			if (numberParam == 2)
				res = saveMpFlag(info, cmd[1]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READCONFIG:
			if (numberParam == 5) {	/* need to pass: addr[0]
						 *  addr[1] byteToRead1
						 * byteToRead0 */
				byteToRead = ((funcToTest[3] << 8) |
					      funcToTest[4]);
				readData = (u8 *)kmalloc(byteToRead *
							 sizeof(u8),
							 GFP_KERNEL);
				res = readConfig(info,
						(u16)((((u8)funcToTest[1] &
								0x00FF) << 8) +
						       ((u8)funcToTest[2] &
								0x00FF)),
						 readData, byteToRead);
				size += (byteToRead * sizeof(u8));
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_POLLFOREVENT:
			if (numberParam >= 5) {	/* need to pass: eventLength
						 * event[0] event[1] …
						 * event[eventLength-1]
						 * timeTowait1 timeTowait0 */
				temp = (int)funcToTest[1];
				if (numberParam == 5 + (temp - 1) &&
					temp != 0) {
					readData = (u8 *)kmalloc(
						FIFO_EVENT_SIZE * sizeof(u8),
						GFP_KERNEL);
					res = pollForEvent(info,
						(int *)&funcToTest[2], temp,
						readData,
						((funcToTest[temp + 2] &
							0x00FF) << 8) +
						(funcToTest[temp + 3] &
							0x00FF));
					if (res >= OK)
						res = OK;	/* pollForEvent
								 * return the
								 * number of
								 * error found
								 * */
					size += (FIFO_EVENT_SIZE * sizeof(u8));
					byteToRead = FIFO_EVENT_SIZE;
				} else {
					dev_err(info->dev, "Wrong parameters!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_SYSTEMRESET:
			res = fts_system_reset(info);

			break;

		case CMD_READSYSINFO:
			if (numberParam == 2)	/* need to pass: doRequest */
				res = readSysInfo(info, funcToTest[1]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_CLEANUP:/* TOUCH ENABLE/DISABLE */
			if (numberParam == 2)	/* need to pass: enableTouch */
				res = cleanUp(info, funcToTest[1]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_GETFORCELEN:	/* read number Tx channels */
			temp = getForceLen(info);
			if (temp < OK)
				res = temp;
			else {
				size += (1 * sizeof(u8));
				res = OK;
			}
			break;

		case CMD_GETSENSELEN:	/* read number Rx channels */
			temp = getSenseLen(info);
			if (temp < OK)
				res = temp;
			else {
				size += (1 * sizeof(u8));
				res = OK;
			}
			break;


		case CMD_GETMSFRAME:
			if (numberParam == 2) {
				dev_info(info->dev, "Get 1 MS Frame\n");
				/* setScanMode(SCAN_MODE_ACTIVE, 0xFF);
				 * mdelay(WAIT_FOR_FRESH_FRAMES);
				 * setScanMode(SCAN_MODE_ACTIVE, 0x00);
				 * mdelay(WAIT_AFTER_SENSEOFF);
				 */
				/* flushFIFO(); //delete the events related to
				 * some touch (allow to call this function while
				 * touching the screen without having a flooding
				 * of the FIFO) */
				res = getMSFrame3(info, (MSFrameType)cmd[1],
						  &frameMS);
				if (res < 0)
					dev_err(info->dev, "Error while taking the MS frame... ERROR %08X\n",
						res);

				else {
					dev_info(info->dev, "The frame size is %d words\n",
						res);
					size += (res * sizeof(short) + 2);
					/* +2 to add force and sense channels
					 * set res to OK because if getMSFrame
					 * is successful
					 *	res = number of words read
					 */
					res = OK;
					print_frame_short(info, "MS frame =",
						array1dTo2d_short(
						    frameMS.node_data,
						    frameMS.node_data_size,
						    frameMS.header.sense_node),
						frameMS.header.force_node,
						frameMS.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;


		/*read self raw*/
		case CMD_GETSSFRAME:
			if (numberParam == 2) {
				/* dev_info(info->dev, "Get 1 SS Frame\n"); */
				/* flushFIFO(); //delete the events related to
				 * some touch (allow to call this function while
				 * touching the screen without having a flooding
				 * of the FIFO) */
				/* setScanMode(SCAN_MODE_ACTIVE, 0xFF);
				 * mdelay(WAIT_FOR_FRESH_FRAMES);
				 * setScanMode(SCAN_MODE_ACTIVE, 0x00);
				 * mdelay(WAIT_AFTER_SENSEOFF);
				 */
				res = getSSFrame3(info, (SSFrameType)cmd[1],
						  &frameSS);

				if (res < OK)
					dev_err(info->dev, "Error while taking the SS frame... ERROR %08X\n",
						res);

				else {
					dev_info(info->dev, "The frame size is %d words\n",
						res);
					size += (res * sizeof(short) + 2);
					/* +2 to add force and sense channels
					 * set res to OK because if getMSFrame
					 * is successful
					 *	res = number of words read
					 */
					res = OK;
					print_frame_short(info,
						  "SS force frame =",
						  array1dTo2d_short(
						    frameSS.force_data,
						    frameSS.header.force_node,
						    1),
						  frameSS.header.force_node, 1);
					print_frame_short(info,
						"SS sense frame =",
						array1dTo2d_short(
						  frameSS.sense_data,
						  frameSS.header.sense_node,
						  frameSS.header.sense_node),
						  1,
						frameSS.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_GETSYNCFRAME:
			/* need to pass: frameType (this parameter can be
			 * one of LOAD_SYNC_FRAME_X)
			 */
			if (numberParam == 2) {
				dev_info(info->dev, "Reading Sync Frame...\n");
				res = getSyncFrame(info, cmd[1], &frameMS,
						   &frameSS);
				if (res < OK)
					dev_err(info->dev, "Error while taking the Sync Frame frame... ERROR %08X\n",
						res);

				else {
					dev_info(info->dev, "The total frames size is %d words\n",
						 res);
					size += (res * sizeof(short) + 4);
					/* +4 to add force and sense channels
					 * for MS and SS.
					 * Set res to OK because if getSyncFrame
					 * is successful res = number of words
					 * read
					 */
					res = OK;

					print_frame_short(info, "MS frame =",
						array1dTo2d_short(
						    frameMS.node_data,
						    frameMS.node_data_size,
						    frameMS.header.sense_node),
						frameMS.header.force_node,
						frameMS.header.sense_node);
					print_frame_short(info,
						"SS force frame =",
						array1dTo2d_short(
						    frameSS.force_data,
						    frameSS.header.force_node,
						    1),
						frameSS.header.force_node,
						1);
					print_frame_short(info,
						"SS sense frame =",
						array1dTo2d_short(
						    frameSS.sense_data,
						    frameSS.header.sense_node,
						    frameSS.header.sense_node),
						1,
						frameSS.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_REQCOMPDATA:	/* request comp data */
			if (numberParam == 2) {
				dev_info(info->dev, "Requesting Compensation Data\n");
				res = requestHDMDownload(info, cmd[1]);

				if (res < OK)
					dev_err(info->dev, "Error requesting compensation data ERROR %08X\n",
						res);
				else
					dev_info(info->dev, "Requesting Compensation Data Finished!\n");
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READCOMPDATAHEAD:	/* read comp data header */
			if (numberParam == 2) {
				dev_info(info->dev, "Requesting Compensation Data\n");
				res = requestHDMDownload(info, cmd[1]);
				if (res < OK)
					dev_err(info->dev, "Error requesting compensation data ERROR %08X\n",
						res);
				else {
					dev_info(info->dev, "Requesting Compensation Data Finished!\n");
					res = readHDMHeader(info,
						(u8)funcToTest[1],
						&dataHead, &address);
					if (res < OK)
						dev_err(info->dev, "Read Compensation Data Header ERROR %08X\n",
							res);
					else {
						dev_info(info->dev, "Read Compensation Data Header OK!\n");
						size += (1 * sizeof(u8));
					}
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;


		case CMD_READMSCOMPDATA:/* read mutual comp data */
			if (numberParam == 2) {
				dev_info(info->dev, "Get MS Compensation Data\n");
				res = readMutualSenseCompensationData(info,
							      cmd[1],
							      &compData);

				if (res < OK)
					dev_err(info->dev, "Error reading MS compensation data ERROR %08X\n",
						res);
				else {
					dev_info(info->dev, "MS Compensation Data Reading Finished!\n");
					size = ((compData.node_data_size + 10) *
						sizeof(i8));
					if (cmd[1] == LOAD_CX_MS_TOUCH)
						label[0] = "CX2 =";
					else if(cmd[1] == LOAD_CX_MS_LOW_POWER)
						label[0] = "CX2_LP =";
					else
						label[0] = "MS Data (Cx2) =";
					print_frame_i8(info,
						label[0],
						array1dTo2d_i8(
						compData.node_data,
						compData.node_data_size,
						compData.header.sense_node),
						compData.header.force_node,
						compData.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READSSCOMPDATA:
			if (numberParam == 2) {	/* read self comp data */
				dev_info(info->dev, "Get SS Compensation Data...\n");
				res = readSelfSenseCompensationData(info,
								    cmd[1],
								    &comData);
				if (res < OK)
					dev_err(info->dev, "Error reading SS compensation data ERROR %08X\n",
						res);
				else {
					dev_info(info->dev, "SS Compensation Data Reading Finished!\n");
					size = ((comData.header.force_node +
						 comData.header.sense_node) *
						2 + 15) *
					       sizeof(i8);
					print_frame_i8(info,
						"SS Data Ix2_fm = ",
						array1dTo2d_i8(
						  comData.ix2_fm,
						  comData.header.force_node,
						  comData.header.force_node),
						1,
						comData.header.force_node);
					print_frame_i8(info,
						"SS Data Cx2_fm = ",
						array1dTo2d_i8(
						  comData.cx2_fm,
						  comData.header.force_node,
						  comData.header.force_node),
						1,
						comData.header.force_node);
					print_frame_i8(info,
						"SS Data Ix2_sn = ",
						array1dTo2d_i8(
						  comData.ix2_sn,
						  comData.header.sense_node,
						  comData.header.sense_node),
						1,
						comData.header.sense_node);
					print_frame_i8(info,
						"SS Data Cx2_sn = ",
						array1dTo2d_i8(
						  comData.cx2_sn,
						  comData.header.sense_node,
						  comData.header.sense_node),
						1,
						comData.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READGOLDENMUTUAL:
			if (numberParam != 1) {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
				break;
			}

			dev_info(info->dev, "Get Golden Mutual Raw data\n");

			res = readGoldenMutualRawData(info, &gmRawData);
			if (res < OK) {
				dev_err(info->dev, "Err reading GM data %08X\n", res);
				break;
			}

			dev_info(info->dev, "GM data reading Finished!\n");

			size = (6 + gmRawData.data_size) * sizeof(u16);

			print_frame_short(info, "Golden Mutual Data =",
					array1dTo2d_short(
						gmRawData.data,
						gmRawData.data_size,
						gmRawData.hdr.ms_s_len),
					gmRawData.hdr.ms_f_len,
					gmRawData.hdr.ms_s_len);
			break;

		case CMD_READTOTMSCOMPDATA:	/* read mutual comp data */
			if (numberParam == 2) {
				dev_info(info->dev, "Get TOT MS Compensation Data\n");
				res = readTotMutualSenseCompensationData(info,
								cmd[1],
								&totCompData);

				if (res < OK)
					dev_err(info->dev, "Error reading TOT MS compensation data ERROR %08X\n",
						res);
				else {
					dev_info(info->dev, "TOT MS Compensation Data Reading Finished!\n");
					size = (totCompData.node_data_size *
						sizeof(short) + 9);
					print_frame_short(info,
					  "MS Data (TOT Cx) =",
					  array1dTo2d_short(
					      totCompData.node_data,
					      totCompData.node_data_size,
					      totCompData.header.sense_node),
					  totCompData.header.force_node,
					  totCompData.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READTOTSSCOMPDATA:
			if (numberParam == 2) {	/* read self comp data */
				dev_info(info->dev, "Get TOT SS Compensation Data...\n");
				res = readTotSelfSenseCompensationData(info,
								cmd[1],
								&totComData);
				if (res < OK)
					dev_err(info->dev, "Error reading TOT SS compensation data ERROR %08X\n",
						res);
				else {
					dev_info(info->dev, "TOT SS Compensation Data Reading Finished!\n");
					size = ((totComData.header.force_node +
						 totComData.header.sense_node) *
						2 *
						sizeof(short) + 9);
					if (cmd[1] ==
					    LOAD_PANEL_CX_TOT_SS_TOUCH) {
					    label[0] = "SS_TOT_IX_TX = ";
					    label[1] = "SS_TOT_Cx_Tx = ";
					    label[2] = "SS_TOT_Ix_Rx = ";
					    label[3] = "SS_TOT_Cx_Rx = ";
					} else if (cmd[1] ==
					    LOAD_PANEL_CX_TOT_SS_TOUCH_IDLE) {
					    label[0] = "SS_TOT_Ix_Tx_LP = ";
					    label[1] = "SS_TOT_Cx_Tx_LP = ";
					    label[2] = "SS_TOT_Ix_Rx_LP = ";
					    label[3] = "SS_TOT_Cx_Rx_LP = ";
					} else {
					    label[0] = "SS Data TOT Ix_fm = ";
					    label[1] = "SS Data TOT Cx_fm = ";
					    label[2] = "SS Data TOT Ix_sn = ";
					    label[3] = "SS Data TOT Cx_sn = ";
					}
					print_frame_u16(info,
						label[0],
						array1dTo2d_u16(
						totComData.ix_fm,
						totComData.header.force_node,
						totComData.header.force_node),
						1,
						totComData.header.force_node);
					print_frame_short(info,
						label[1],
						array1dTo2d_short(
						totComData.cx_fm,
						totComData.header.force_node,
						totComData.header.force_node),
						1,
						totComData.header.force_node);
					print_frame_u16(info,
						label[2],
						array1dTo2d_u16(
						totComData.ix_sn,
						totComData.header.sense_node,
						totComData.header.sense_node),
						1,
						totComData.header.sense_node);
					print_frame_short(info,
						label[3],
						array1dTo2d_short(
						totComData.cx_sn,
						totComData.header.sense_node,
						totComData.header.sense_node),
						1,
						totComData.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READSENSCOEFF:
			/* read MS and SS Sensitivity Coefficients */
			dev_info(info->dev, "Get Sensitivity Calibration Coefficients...\n");
			res = readSensitivityCoefficientsData(info, &msCoeff,
							      &ssCoeff);
			if (res < OK)
				dev_err(info->dev, "Error reading Sensitivity Calibration Coefficients ERROR %08X\n",
					res);
			else {
				dev_info(info->dev, "Sensitivity Calibration Coefficients Reading Finished!\n");
				size += (((msCoeff.node_data_size) +
					  ssCoeff.header.force_node +
					  ssCoeff.header.sense_node) *
					 sizeof(u8) + 4);
				print_frame_u8(info,
					       "MS Sensitivity Coeff = ",
					       array1dTo2d_u8(msCoeff.ms_coeff,
							      msCoeff.
							      node_data_size,
							      msCoeff.header.
							      sense_node),
					       msCoeff.header.force_node,
					       msCoeff.header.sense_node);
				print_frame_u8(info,
					       "SS Sensitivity Coeff force = ",
					       array1dTo2d_u8(
						       ssCoeff.ss_force_coeff,
						       ssCoeff.header.
						       force_node, 1),
					       ssCoeff.header.force_node, 1);
				print_frame_u8(info,
					       "SS Sensitivity Coeff sense = ",
					       array1dTo2d_u8(
						       ssCoeff.ss_sense_coeff,
						       ssCoeff.header.
						       sense_node,
						       ssCoeff.header.
						       sense_node), 1,
					       ssCoeff.header.sense_node);
			}
			break;

		case CMD_GETFWVER:
			size += (EXTERNAL_RELEASE_INFO_SIZE)*sizeof(u8);
			break;

		case CMD_FLASHUNLOCK:
			res = flash_unlock(info);
			if (res < OK)
				dev_err(info->dev, "Impossible Unlock Flash ERROR %08X\n",
					res);
			else
				dev_info(info->dev, "Flash Unlock OK!\n");
			break;

		case CMD_READFWFILE:
			if (numberParam == 2) {	/* read fw file */
				dev_info(info->dev, "Reading FW File...\n");
				res = readFwFile(info, info->board->fw_name,
						 &fw, funcToTest[1]);
				if (res < OK)
					dev_err(info->dev, "Error reading FW File ERROR %08X\n",
						res);
				else
					dev_info(info->dev, "Read FW File Finished!\n");
				kfree(fw.data);
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_FLASHPROCEDURE:
			if (numberParam == 3) {	/* flashing procedure */
				dev_info(info->dev, "Starting Flashing Procedure...\n");
				res = flashProcedure(info,
						     info->board->fw_name,
						     cmd[1],
						     cmd[2]);
				if (res < OK)
					dev_err(info->dev, "Error during flash procedure ERROR %08X\n",
						res);
				else
					dev_info(info->dev, "Flash Procedure Finished!\n");
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_FLASHERASEUNLOCK:
			res = flash_erase_unlock(info);
			if (res < OK)
				dev_err(info->dev, "Error during flash erase unlock... ERROR %08X\n",
					res);
			else
				dev_info(info->dev, "Flash Erase Unlock Finished!\n");
			break;

		case CMD_FLASHERASEPAGE:
			if (numberParam == 2) {	/* need to pass: keep_cx */
				dev_info(info->dev, "Reading FW File...\n");
				res = readFwFile(info, info->board->fw_name,
						 &fw, funcToTest[1]);
				if (res < OK)
					dev_err(info->dev, "Error reading FW File ERROR"
						"%08X\n", res);
				else
					dev_info(info->dev, "Read FW File Finished!\n");
				dev_info(info->dev, "Starting Flashing Page Erase...\n");
				res = flash_erase_page_by_page(info, cmd[1],
							       &fw);
				if (res < OK)
					dev_err(info->dev, "Error during flash page erase... ERROR %08X\n",
						res);
				else
					dev_info(info->dev, "Flash Page Erase Finished!\n");
				kfree(fw.data);
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		/*ITO TEST*/
		case CMD_ITOTEST:
			frameMS.node_data = NULL;
			res = production_test_ito(info, limits_file, &frameMS,
						  ito_max_val);

			if (frameMS.node_data != NULL) {
				size += (frameMS.node_data_size *
						sizeof(short) + 2);
				report = 1;
			}
			break;

		/*Initialization*/
		case CMD_INITTEST:
			if (numberParam == 2)
				res = production_test_initialization(info,
								     cmd[1]);

			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;


		case CMD_MSRAWTEST:	/* MS Raw DATA TEST */
			if (numberParam == 2)	/* need to specify if stopOnFail
						 * */
				res = production_test_ms_raw(info, limits_file,
							     cmd[1]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_MSINITDATATEST:/* MS CX DATA TEST */
			if (numberParam == 2)	/* need stopOnFail */
				res = production_test_ms_cx(info, limits_file,
							    cmd[1]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_SSRAWTEST:	/* SS RAW DATA TEST */
			if (numberParam == 2) /* need stopOnFail */
				res = production_test_ss_raw(info, limits_file,
							     cmd[1]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_SSINITDATATEST:/* SS IX CX DATA TEST */
			if (numberParam == 2)	/* need stopOnFail */
				res = production_test_ss_ix_cx(info,
							       limits_file,
							       cmd[1]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		/*PRODUCTION TEST*/
		case CMD_MAINTEST:
			if (numberParam >= 3)	/* need to specify if stopOnFail
						 * saveInit and
						 * mpflag(optional)
						 */
				if (numberParam == 3)
					res = production_test_main(info,
							   limits_file,
							   cmd[1],
							   cmd[2],
							   MP_FLAG_OTHERS);
				else
					res = production_test_main(info,
							   limits_file,
							   cmd[1],
							   cmd[2],
							   cmd[3]);
			else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_FREELIMIT:
			res = freeCurrentLimitsFile(info);
			break;

		case CMD_POWERCYCLE:
			res = fts_chip_powercycle(info);
			break;

		case CMD_GETLIMITSFILE:
			/* need to pass: path(optional) return error code +
			 * number of byte read otherwise GUI could not now how
			 * many byte read */
			if (numberParam >= 1) {
				lim.data = NULL;
				lim.size = 0;
				if (numberParam == 1)
					res = getLimitsFile(info, limits_file,
							    &lim);
				else
					res = getLimitsFile(info, path, &lim);
				readData = lim.data;
				fileSize = lim.size;
				size += (fileSize * sizeof(u8));
				if (byte_call == 1)
					size += sizeof(u32);	/* transmit as
								 * first 4 bytes
								 * the size */
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_GETLIMITSFILE_BYTE:
			/* need to pass: byteToRead1 byteToRead0 */
			if (numberParam >= 3) {
				lim.data = NULL;
				lim.size = 0;

				u8ToU16_be(&cmd[1], &byteToRead);
				addr = ((u64)byteToRead) * 4;	/* number of
								 * words */

				res = getLimitsFile(info, limits_file, &lim);

				readData = lim.data;
				fileSize = lim.size;

				if (fileSize > addr) {
					dev_err(info->dev, "Limits dimension expected by Host is less than actual size: expected = %d, real = %d\n",
						byteToRead, fileSize);
					res = ERROR_OP_NOT_ALLOW;
				}

				size += (addr * sizeof(u8));
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_GETFWFILE:
			/* need to pass: from (optional) otherwise select the
			 * approach chosen at compile time */
			if (numberParam >= 1) {
				if (numberParam == 1)
					res = getFWdata(info,
							info->board->fw_name,
							&readData, &fileSize);
				else
					res = getFWdata(info, path, &readData,
							&fileSize);

				size += (fileSize * sizeof(u8));
				if (byte_call == 1)
					size += sizeof(u32);	/* transmit as
								 * first 4 bytes
								 * the size */
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_GETFWFILE_BYTE:
			/* need to pass: byteToRead1 byteToRead0 */
			if (numberParam == 3) {
				u8ToU16_be(&cmd[1], &byteToRead);
				addr = ((u64)byteToRead) * 4;	/* number of
								 * words */
				res = getFWdata(info, info->board->fw_name,
						&readData, &fileSize);
				if (fileSize > addr) {
					dev_err(info->dev, "FW dimension expected by Host is less than actual size: expected = %d, real = %d\n",
						byteToRead, fileSize);
					res = ERROR_OP_NOT_ALLOW;
				}

				size += (addr * sizeof(u8));	/* return always
								 * the amount
								 * requested by
								 * host, if real
								 * size is
								 * smaller, the
								 * data are
								 * padded to
								 * zero */
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		/* finish all the diagnostic command with a goto ERROR in order
		 * to skip the modification on driver_test_buff
		 * remember to set properly the limit and printed variables in
		 * order to make the seq_file logic to work */
		case CMD_DIAGNOSTIC:
			index = 0;
			size = 0;
			fileSize = 256 * 1024 * sizeof(char);
			info->driver_test_buff = (u8 *)kzalloc(fileSize, GFP_KERNEL);
			readData = (u8 *)kmalloc((ERROR_DUMP_ROW_SIZE *
						  ERROR_DUMP_COL_SIZE) *
						 sizeof(u8), GFP_KERNEL);
			if (info->driver_test_buff == NULL || readData == NULL) {
				res = ERROR_ALLOC;
				dev_err(info->dev, "Impossible allocate memory for buffers! ERROR %08X!\n",
					res);
				goto END;
			}
			j = scnprintf(&info->driver_test_buff[index],
				      fileSize - index,
				      "DIAGNOSTIC TEST:\n1) I2C Test: ");
			index += j;

			res = fts_writeReadU8UX(info, FTS_CMD_HW_REG_R,
						ADDR_SIZE_HW_REG, ADDR_DCHIP_ID,
						(u8 *)&temp, 2,
						DUMMY_HW_REG);
			if (res < OK) {
				dev_err(info->dev, "Error during I2C test: ERROR %08X!\n",
					res);
				j = scnprintf(&info->driver_test_buff[index],
					      fileSize - index, "ERROR %08X\n",
					      res);
				index += j;
				res = ERROR_OP_NOT_ALLOW;
				goto END_DIAGNOSTIC;
			}

			temp &= 0xFFFF;
			dev_info(info->dev, "Chip ID = %04X!\n", temp);
			j = scnprintf(&info->driver_test_buff[index],
				      fileSize - index,
				      "DATA = %04X, expected = %02X%02X\n",
				      temp, info->board->dchip_id[1],
				      info->board->dchip_id[0]);
			index += j;
			if (temp != ((info->board->dchip_id[1] << 8) |
				     info->board->dchip_id[0])) {
				dev_err(info->dev, "Wrong CHIP ID, Diagnostic failed!\n");
				res = ERROR_OP_NOT_ALLOW;
				goto END_DIAGNOSTIC;
			}

			j = scnprintf(&info->driver_test_buff[index],
				      fileSize - index,
				      "Present Driver Mode: %08X\n",
				      info->mode);
			index += j;

			j = scnprintf(&info->driver_test_buff[index],
				      fileSize - index,
				      "2) FW running: Sensing On...");
			index += j;
			dev_info(info->dev, "Sensing On!\n");
			readData[0] = FTS_CMD_SCAN_MODE;
			readData[1] = SCAN_MODE_ACTIVE;
			readData[2] = 0x1;
			fts_write_heap(info, readData, 3);
			res = checkEcho(info, readData, 3);
			if (res < OK) {
				dev_err(info->dev, "No Echo received.. ERROR %08X !\n",
					res);
				j = scnprintf(&info->driver_test_buff[index],
					      fileSize - index,
					      "No echo found... ERROR %08X!\n",
					      res);
				index += j;
				goto END_DIAGNOSTIC;
			} else {
				dev_info(info->dev, "Echo FOUND... OK!\n");
				j = scnprintf(&info->driver_test_buff[index],
					      fileSize - index,
					      "Echo FOUND... OK!\n");
				index += j;
			}

			dev_info(info->dev, "Reading Frames...!\n");
			j = scnprintf(&info->driver_test_buff[index],
				      fileSize - index,
				      "3) Read Frames:\n");
			index += j;
			for (temp = 0; temp < DIAGNOSTIC_NUM_FRAME; temp++) {
				dev_info(info->dev, "Iteration n. %d...\n", temp + 1);
				j = scnprintf(&info->driver_test_buff[index],
					      fileSize - index,
					      "Iteration n. %d...\n",
					      temp + 1);
				index += j;
				for (addr = 0; addr < 3; addr++) {
					switch (addr) {
					case 0:
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "MS RAW FRAME =");
						index += j;
						res |= getMSFrame3(info,
								   MS_RAW,
								   &frameMS);
						break;
					case 2:
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "MS STRENGTH FRAME =");
						index += j;
						res |= getMSFrame3(info,
								   MS_STRENGTH,
								   &frameMS);
						break;
					case 1:
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "MS BASELINE FRAME =");
						index += j;
						res |= getMSFrame3(info,
								   MS_BASELINE,
								   &frameMS);
						break;
					}
					if (res < OK) {
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "No data! ERROR %08X\n",
						    res);
						index += j;
					} else {
						for (address = 0; address <
						    frameMS.node_data_size;
						  address++) {
							if (address %
							    frameMS.header.
							      sense_node == 0) {
								j = scnprintf(
							       &info->driver_test_buff
									[index],
							       fileSize	-
									  index,
							       "\n");
								index += j;
							}
							j = scnprintf(
							    &info->driver_test_buff
								[index],
							    fileSize - index,
							    "%5d, ",
							    frameMS.
							    node_data[address]);
							index += j;
						}
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index, "\n");
						index += j;
					}
					if (frameMS.node_data != NULL)
						kfree(frameMS.node_data);
				}
				for (addr = 0; addr < 3; addr++) {
					switch (addr) {
					case 0:
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "SS RAW FRAME =\n");
						index += j;
						res |= getSSFrame3(info,
								   SS_RAW,
								   &frameSS);
						break;
					case 2:
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "SS STRENGTH FRAME =\n");
						index += j;
						res |= getSSFrame3(info,
								   SS_STRENGTH,
								   &frameSS);
						break;
					case 1:
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "SS BASELINE FRAME =\n");
						index += j;
						res |= getSSFrame3(info,
								   SS_BASELINE,
								   &frameSS);
						break;
					}
					if (res < OK) {
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "No data! ERROR %08X\n",
						    res);
						index += j;
					} else {
						int num;
						short *data;

						num = frameSS.header.force_node;
						data = frameSS.force_data;
						for (address = 0;
							address < num;
							address++) {
						    j = scnprintf(
						       &info->driver_test_buff[index],
						       fileSize - index,
						       "%d\n",
						       data[address]);
						    index += j;
						}

						num = frameSS.header.sense_node;
						data = frameSS.sense_data;
						for (address = 0;
							address < num;
							address++) {
						    j = scnprintf(
						       &info->driver_test_buff[index],
						       fileSize - index,
						       "%d, ",
						       data[address]);
						    index += j;
						}
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index, "\n");
						index += j;
					}
					if (frameSS.force_data != NULL)
						kfree(frameSS.force_data);
					if (frameSS.sense_data != NULL)
						kfree(frameSS.sense_data);
				}
			}


			dev_info(info->dev, "Reading error info...\n");
			j = scnprintf(&info->driver_test_buff[index],
				      fileSize - index,
				      "4) FW INFO DUMP: ");
			index += j;
			temp = dumpErrorInfo(info, readData,
					     ERROR_DUMP_ROW_SIZE *
					     ERROR_DUMP_COL_SIZE);
			/* OR to detect if there are failures also in the
			 * previous reading of frames and write the correct
			 * result */
			if (temp < OK) {
				dev_err(info->dev, "Error during dump: ERROR %08X!\n",
					res);
				j = scnprintf(&info->driver_test_buff[index],
					      fileSize - index, "ERROR %08X\n",
					      temp);
				index += j;
			} else {
				dev_info(info->dev, "DUMP OK!\n");
				for (temp = 0; temp < ERROR_DUMP_ROW_SIZE *
				     ERROR_DUMP_COL_SIZE; temp++) {
					if (temp % ERROR_DUMP_COL_SIZE == 0) {
						j = scnprintf(
						    &info->driver_test_buff[index],
						    fileSize - index,
						    "\n%2d - ",
						    temp / ERROR_DUMP_COL_SIZE);
						index += j;
					}
					j = scnprintf(&info->driver_test_buff[index],
						      fileSize - index, "%02X ",
						      readData[temp]);
					index += j;
				}
			}
			res |= temp;

END_DIAGNOSTIC:
			if (res < OK) {
				j = scnprintf(&info->driver_test_buff[index],
					      fileSize - index,
					      "\nRESULT = FAIL\n");
				index += j;
			} else {
				j = scnprintf(&info->driver_test_buff[index],
					      fileSize - index,
					      "\nRESULT = FINISHED\n");
				index += j;
			}
			/* the sting is already terminated with the null char by
			 * scnprintf */
			info->limit = index;
			info->printed = 0;
			goto ERROR;
			break;

#ifdef I2C_INTERFACE
		case CMD_CHANGE_SAD:
			res = changeSAD(cmd[1]);
			break;
#endif

		case CMD_TRIGGER_FORCECAL:
			cmd[0] = CAL_MS_TOUCH | CAL_SS_TOUCH;
			cmd[1] = 0x00;
			fts_enableInterrupt(info, false);
			res = writeSysCmd(info, SYS_CMD_FORCE_CAL, cmd, 2);
			res |= fts_enableInterrupt(info, true);
			if (res < OK)
				dev_err(info->dev, "can not trigger Force Cal! ERROR %08X\n",
					res);
			else
				dev_info(info->dev, "MS and SS force cal triggered!\n");
			break;

		case CMD_BASELINE_ADAPTATION:
			/* need to pass: enable */
			if (numberParam == 2) {
				if (cmd[1] == 0x01)
					dev_info(info->dev, "Enabling Baseline adaptation...\n");
				else {
					dev_info(info->dev, "Disabling Baseline adaptation...\n");
					cmd[1] = 0x00;	/* set to zero to
							 * disable baseline
							 * adaptation */
				}

				res = writeConfig(info, ADDR_CONFIG_AUTOCAL, &cmd[1],
						  1);
				if (res < OK)
					dev_err(info->dev, "Baseline adaptation operation FAILED! ERROR %08X\n",
						res);
				else
					dev_info(info->dev, "Baseline adaptation operation OK!\n");
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_FREQ_HOP:
			/* need to pass: enable */
			if (numberParam == 2) {
				dev_info(info->dev, "Reading MNM register...\n");
				res = readConfig(info, ADDR_CONFIG_MNM,
						 &cmd[2], 1);
				if (res < OK) {
					dev_err(info->dev, "Reading MNM register... ERROR %08X!\n",
						res);
					break;
				}

				if (cmd[1] == 0x01) {
					dev_info(info->dev, "Enabling Frequency Hopping... %02X => %02X\n",
						cmd[2], cmd[2] | 0x01);
					cmd[2] |= 0x01;	/* set bit 0 to enable
							 * Frequency Hopping */
				} else {
					dev_info(info->dev, "Disabling Frequency Hopping... %02X => %02X\n",
						cmd[2], cmd[2] & (~0x01));
					cmd[2] &= (~0x01);	/* reset bit 0
								 * to disable
								 * Frequency
								 * Hopping */
				}

				res = writeConfig(info, ADDR_CONFIG_MNM, &cmd[2], 1);
				if (res < OK)
					dev_err(info->dev, "Frequency Hopping operation FAILED! ERROR %08X\n",
						res);
				else
					dev_info(info->dev, "Frequency Hopping operation OK!\n");
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_READ_SYNC_FRAME:
			/* need to pass: frameType (this parameter can be
			 * LOAD_SYNC_FRAME_STRENGTH or LOAD_SYNC_FRAME_BASELINE)
			 * */
			if (numberParam == 2) {
				dev_info(info->dev, "Reading Sync Frame...\n");
				res = getSyncFrame(info, cmd[1], &frameMS,
						   &frameSS);
				if (res < OK)
					dev_err(info->dev, "Error while taking the Sync Frame frame... ERROR %08X\n",
						res);

				else {
					dev_info(info->dev, "The total frames size is %d words\n",
						res);
					size += (res * sizeof(short) + 4);
					/* +4 to add force and sense channels
					 * for MS and SS
					 * set res to OK because if getSyncFrame
					 * is successful
					 *	res = number of words read
					 */
					res = OK;

					print_frame_short(info,
						"MS frame =",
						array1dTo2d_short(
						    frameMS.node_data,
						    frameMS.node_data_size,
						    frameMS.header.sense_node),
						frameMS.header.force_node,
						frameMS.header.sense_node);
					print_frame_short(info,
						"SS force frame =",
						array1dTo2d_short(
						    frameSS.force_data,
						    frameSS.header.force_node,
						    1),
						frameSS.header.force_node, 1);
					print_frame_short(info,
						"SS sense frame =",
						array1dTo2d_short(
						    frameSS.sense_data,
						    frameSS.header.sense_node,
						    frameSS.header.sense_node),
						1,
						frameSS.header.sense_node);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_SET_OPERATING_FREQ:
			/* need to pass: freq3 freq2 freq1 freq0 */
			if (numberParam == 5) {
				res = fts_enableInterrupt(info, false);
				if (res >= OK) {
					dev_info(info->dev, "Setting Scan Freq...\n");
					u8ToU32_be(&cmd[1], &fileSize);
					/* fileSize is used just as container
					 * variable, sorry for the name! */

					res = setActiveScanFrequency(info, fileSize);
					if (res < OK)
						dev_err(info->dev, "Error while setting the scan frequency... ERROR %08X\n",
							res);
					else {
						/* setActiveScan Frequency leave
						 * the chip in reset state but
						 * with the new scan freq set */
						/* need to enable the scan mode
						 * and re-enable the interrupts
						 * */
						res |= setScanMode(info,
							SCAN_MODE_LOCKED,
							LOCKED_ACTIVE);
						/* this is a choice to force
						 * the IC to use the freq set */
						res |= fts_enableInterrupt(
								info,
								true);
						dev_info(info->dev, "Setting Scan Freq... res = %08X\n",
							res);
					}
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_TP_SENS_MODE:
			/* need to pass: enter (optional)saveGain */
			if (numberParam >= 2) {
				if (numberParam == 2)
					cmd[2] = 0;	/* by default never save
							 * the gain (used only
							 * when exit) */

				res = tp_sensitivity_mode(info, cmd[1], cmd[2]);
				if (res < OK)
					dev_err(info->dev, "Error while setting TP Sens mode... ERROR %08X\n",
						res);
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;


		case CMD_TP_SENS_SET_SCAN_MODE:
			/* need to pass: scan_type, enableGains */
			if (numberParam == 3) {
				if (cmd[1] == 0x07) { /* To match the api for
						       * C2/F2
						       */
					res = tp_sensitivity_set_scan_mode(
					    info,
					    LOCKED_SINGLE_ENDED_ONLY_MUTUAL_0,
					    cmd[2]);
					/* this force the IC to lock in a scan
					 * mode
					 */
					if (res < OK)
						dev_err(info->dev, "Error while setting TP Sens scan mode... ERROR %08X\n",
						res);
				} else {
					dev_err(info->dev, "Wrong parameter!\n");
					 res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_TP_SENS_PRECAL_SS:
			/* need to pass: target1 target0 percentage(optional) */
			if (numberParam >= 3) {
				if (numberParam > 3)
					temp = cmd[3];
				else
					temp = SENS_TEST_PERC_TARGET_PRECAL;

				dev_info(info->dev, "Setting target = %d and percentage = %d\n",
					(cmd[1] << 8 | cmd[2]), temp);

				res = tp_sensitivity_test_pre_cal_ss(info,
							     &frameSS,
							     (cmd[1] << 8 |
									cmd[2]),
							      temp);
				if (res < OK)
					dev_err(info->dev, "Error while setting the scan frequency... ERROR %08X\n",
						res);

				if ((frameSS.force_data != NULL) &&
				    (frameSS.sense_data != NULL)) {
					size += ((frameSS.header.force_node +
						  frameSS.header.sense_node) *
						 sizeof(short) + 2);
					/*make error code positive to print the
					 * frame*/
					res &= (~0x80000000);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_TP_SENS_PRECAL_MS:
			/* need to pass: target1 target0 calibrate
			 * percentage(optional) */
			if (numberParam >= 4) {
				if (numberParam > 4)
					temp = cmd[4];
				else
					temp = SENS_TEST_PERC_TARGET_PRECAL;

				dev_info(info->dev, "Setting target = %d and percentage = %d\n",
					(cmd[1] << 8 | cmd[2]), temp);

				res = tp_sensitivity_test_pre_cal_ms(info,
							     &frameMS,
							     (cmd[1] << 8 |
									cmd[2]),
							      temp);
				if (res < OK)
					dev_err(info->dev, "Error during TP Sensitivity Precal ... ERROR %08X\n",
						res);

				if (cmd[3] != 0) {
					dev_info(info->dev, "Computing gains with target = %d and saveGain = %d\n",
						(cmd[1] << 8 | cmd[2]), 0);
					temp = tp_sensitivity_compute_gains(
						info,
						&frameMS, (cmd[1] << 8 |
							   cmd[2]),
						0);
					if (temp < OK)
						dev_err(info->dev, "Error during TP Sensitivity Calibration... ERROR %08X\n",
							temp);
					res |= temp;
				}

				if (frameMS.node_data != NULL) {
					size += (frameMS.node_data_size *
						 sizeof(short) + 2);
					/*make error code positive to print the
					 * frame*/
					res &= (~0x80000000);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_TP_SENS_POSTCAL_MS:
			/* need to pass: target1 target0 executeTest
			 * percentage(optional) */
			if (numberParam >= 4) {
				if (cmd[3] != 0) {
					if (numberParam > 4)
						temp = cmd[4];
					else
						temp =
						SENS_TEST_PERC_TARGET_POSTCAL;
				} else
					temp = -1;

				dev_info(info->dev, "Setting target = %d and percentage = %d\n",
					(cmd[1] << 8 | cmd[2]), temp);

				res = tp_sensitivity_test_post_cal_ms(info,
							      &frameMS,
							      &deltas,
							      (cmd[1] << 8 |
									cmd[2]),
							      temp,
							      &meanNorm,
							      &meanEdge);
				if (res < OK)
					dev_err(info->dev, "Error during TP Sensitivity Post Cal ... ERROR %08X\n",
						res);

				/* processing for a proper printing on the shell
				 * */
				if ((frameMS.node_data != NULL) &&
				    (deltas.node_data != NULL)) {
					size += ((frameMS.node_data_size +
						  deltas.node_data_size) *
						 sizeof(short) +
						 2 + 8);/* +2 force and
							 * sense len, +8
							 * mean_normal/edge
							 * */
					/*make error code positive to print the
					 * frame*/
					res &= (~0x80000000);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;


		case CMD_TP_SENS_STD:
			/* need to pass: numFrames */
			if (numberParam >= 2) {
				res =  tp_sensitivity_test_std_ms(info, cmd[1],
								  &frameMS);
				if (res < OK)
					dev_err(info->dev, "Error during TP Sensitivity STD... ERROR %08X\n",
						res);

				/* processing for a proper printing on the shell
				 * */
				if (frameMS.node_data != NULL) {
					size += ((frameMS.node_data_size) *
						 sizeof(short) + 2);
					/* +2 force and sense len */
					/*make error code positive to print the
					 * frame*/
					res &= (~0x80000000);
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_FORCE_TOUCH_ACTIVE:
			/* Single parameter indicates force touch state */
			if (numberParam == 2 || numberParam == 3) {
				if (cmd[1] > 1) {
					dev_err(info->dev, "Parameter should be 1 or 0\n");
					res = ERROR_OP_NOT_ALLOW;
				} else {
					dev_info(info->dev, "FTS_BUS_REF_FORCE_ACTIVE: %s\n",
						cmd[1] ? "ON" : "OFF");
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
					cmd[1] ? goog_pm_wake_lock(info->gti,
						GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false) :
						goog_pm_wake_unlock(info->gti,
						GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
#endif
					res = OK;
				}
			} else {
				dev_err(info->dev, "Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_INFOBLOCK_STATUS:
			res = fts_system_reset(info);
			if (res >= OK) {
				res = pollForErrorType(info, error_to_search,
						       2);
				if (res < OK) {
					dev_err(info->dev, "No info block corruption!\n");
					res = OK;
				} else {
					dev_info(info->dev, "Info block errors found!\n");
					res = ERROR_INFO_BLOCK;
				}
			}
			break;

		default:
			dev_err(info->dev, "COMMAND ID NOT VALID!!!\n");
			res = ERROR_OP_NOT_ALLOW;
			break;
		}

		/* res2 = fts_enableInterrupt(info, true);
		 * the interrupt was disabled on purpose in this node because it
		 * can be used for testing procedure and between one step and
		 * another the interrupt wan to be kept disabled
		 * if (res2 < 0) {
		 *      dev_err(info->dev, "stm_driver_test_show: ERROR %08X\n",
		 * (res2 | ERROR_ENABLE_INTER));
		 * }*/
	} else {
		dev_err(info->dev, "NO COMMAND SPECIFIED!!! do: 'echo [cmd_code] [args] > stm_fts_cmd' before looking for result!\n");
		res = ERROR_OP_NOT_ALLOW;
	}

END:	/* here start the reporting phase, assembling the data to send in the
	 * file node */
	if (info->driver_test_buff != NULL) {
		dev_info(info->dev, "Consecutive echo on the file node, free the buffer with the previous result\n");
		kfree(info->driver_test_buff);
	}

	if (byte_call == 0) {
		/* keep for ito_max_val array */
		if (funcToTest[0] == CMD_ITOTEST)
			size += (ARRAY_SIZE(ito_max_val) * sizeof(u16));
		size *= 2;
		size += 2;	/* add \n and \0 (terminator char) */
	} else {
		if (info->bin_output != 1) {
			size *= 2; /* need to code each byte as HEX string */
			size -= 1;	/* start byte is just one, the extra
					 * byte of end byte taken by \n */
		} else
			size += 1;	/* add \n */
	}

	dev_info(info->dev, "Size = %d\n", size);
	info->driver_test_buff = (u8 *)kzalloc(size, GFP_KERNEL);
	dev_info(info->dev, "Finish to allocate memory!\n");
	if (info->driver_test_buff == NULL) {
		dev_err(info->dev, "Unable to allocate driver_test_buff! ERROR %08X\n",
			ERROR_ALLOC);
		goto ERROR;
	}

	if (byte_call == 0) {
		index = 0;
		index += scnprintf(&info->driver_test_buff[index],
				   size - index, "{ ");
		index += scnprintf(&info->driver_test_buff[index],
				   size - index, "%08X", res);
		if (res >= OK || report) {
			/*all the other cases are already fine printing only the
			 * res.*/
			switch (funcToTest[0]) {
			case CMD_VERSION:
			case CMD_READ:
			case CMD_WRITEREAD:
			case CMD_WRITETHENWRITEREAD:
			case CMD_WRITEREADU8UX:
			case CMD_WRITEU8UXTHENWRITEREADU8UX:
			case CMD_READCONFIG:
			case CMD_POLLFOREVENT:
				/* dev_err(info->dev, "Data = "); */
				if (info->mess.dummy == 1)
					j = 1;
				else
					j = 0;
				for (; j < byteToRead + info->mess.dummy; j++) {
					/* dev_err(info->dev, "%02X ", readData[j]); */
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X", readData[j]);
					/* this approach is much more faster */
				}
				/* dev_err(info->dev, "\n"); */
				break;
			case CMD_GETFWFILE:
			case CMD_GETLIMITSFILE:
				dev_info(info->dev, "Start To parse!\n");
				for (j = 0; j < fileSize; j++) {
					/* dev_err(info->dev, "%02X ", readData[j]); */
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X", readData[j]);
				}
				dev_info(info->dev, "Finish to parse!\n");
				break;
			case CMD_GETFORCELEN:
			case CMD_GETSENSELEN:
				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   (u8)temp);
				break;
			case CMD_GETMSFRAME:
			case CMD_TP_SENS_PRECAL_MS:
			case CMD_TP_SENS_POSTCAL_MS:
			case CMD_TP_SENS_STD:
			case CMD_ITOTEST:

				if (frameMS.node_data == NULL)
					break;

				if (res != OK)
					info->driver_test_buff[2] = '8';
				/* convert back error code to negative */

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameMS.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameMS.header.sense_node);

				if (funcToTest[0] == CMD_ITOTEST) {
					index += scnprintf(&info->driver_test_buff[index],
							size - index,
							"%02X%02X",
							(ito_max_val[0] & 0xFF00) >> 8,
							ito_max_val[0] & 0xFF);

					index += scnprintf(&info->driver_test_buff[index],
							size - index,
							"%02X%02X",
							(ito_max_val[1] & 0xFF00) >> 8,
							ito_max_val[1] & 0xFF);
				}

				for (j = 0; j < frameMS.node_data_size; j++) {
					index += scnprintf(
					   &info->driver_test_buff[index],
					   size - index,
					   "%02X%02X",
					   (frameMS.node_data[j] & 0xFF00) >> 8,
					   frameMS.node_data[j] & 0xFF);
				}

				kfree(frameMS.node_data);

				if (funcToTest[0] == CMD_TP_SENS_POSTCAL_MS) {
					/* print also mean and deltas */
					for (j = 0; j < deltas.node_data_size;
					     j++) {
						index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (deltas.node_data[j] &
							 0xFF00) >> 8,
						    deltas.node_data[j] &
							0xFF);
					}
					kfree(deltas.node_data);

					index += scnprintf(
						     &info->driver_test_buff[index],
						     size - index,
						     "%08X", meanNorm);

					index += scnprintf(
						     &info->driver_test_buff[index],
						     size - index,
						     "%08X", meanEdge);
				}
				break;
			case CMD_GETSSFRAME:
			case CMD_TP_SENS_PRECAL_SS:
				if (res != OK)
					info->driver_test_buff[2] = '8';
				/* convert back error code to negative */
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameSS.header.force_node);
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameSS.header.sense_node);
				/* Copying self raw data Force */
				for (j = 0; j < frameSS.header.force_node;
				     j++) {
					index += scnprintf(
					  &info->driver_test_buff[index],
					  size - index,
					  "%02X%02X",
					  (frameSS.force_data[j] & 0xFF00) >> 8,
					  frameSS.force_data[j] & 0xFF);
				}

				/* Copying self raw data Sense */
				for (j = 0; j < frameSS.header.sense_node;
				     j++) {
					index += scnprintf(
					  &info->driver_test_buff[index],
					  size - index,
					  "%02X%02X",
					  (frameSS.sense_data[j] & 0xFF00) >> 8,
					  frameSS.sense_data[j] & 0xFF);
				}

				kfree(frameSS.force_data);
				kfree(frameSS.sense_data);
				break;

			case CMD_GETSYNCFRAME:
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameMS.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameMS.header.sense_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameSS.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameSS.header.sense_node);

				/* Copying mutual data */
				for (j = 0; j < frameMS.node_data_size; j++) {
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index,
						"%02X%02X",
						(frameMS.node_data[j] &
							0xFF00) >> 8,
						frameMS.node_data[j] & 0xFF);
				}

				/* Copying self data Force */
				for (j = 0; j < frameSS.header.force_node;
				     j++) {
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index,
						"%02X%02X",
						(frameSS.force_data[j] &
							0xFF00) >> 8,
						frameSS.force_data[j] & 0xFF);
				}

				/* Copying self  data Sense */
				for (j = 0; j < frameSS.header.sense_node;
				     j++) {
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index,
						"%02X%02X",
						(frameSS.sense_data[j] &
							0xFF00) >> 8,
						frameSS.sense_data[j] & 0xFF);
				}

				kfree(frameMS.node_data);
				kfree(frameSS.force_data);
				kfree(frameSS.sense_data);
				break;

			case CMD_READMSCOMPDATA:
				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   (u8)compData.header.type);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)compData.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)compData.header.sense_node);

				/* Cpying CX1 value */
				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   compData.cx1 & 0xFF);

				/* Copying CX2 values */
				for (j = 0; j < compData.node_data_size; j++) {
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index,
						"%02X",
						compData.node_data[j] & 0xFF);
				}

				kfree(compData.node_data);
				break;

			case CMD_READSSCOMPDATA:
				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   (u8)comData.header.type);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.header.sense_node);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.f_ix1 & 0xFF);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.s_ix1 & 0xFF);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.f_cx1 & 0xFF);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.s_cx1 & 0xFF);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.f_ix0 & 0xFF);

				index += scnprintf(&info->driver_test_buff[index],
						   size - index, "%02X",
						   comData.s_ix0 & 0xFF);

				/* Copying IX2 Force */
				for (j = 0; j < comData.header.force_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X",
						    comData.ix2_fm[j] & 0xFF);
				}

				/* Copying IX2 Sense */
				for (j = 0; j < comData.header.sense_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X",
						    comData.ix2_sn[j] & 0xFF);
				}

				/* Copying CX2 Force */
				for (j = 0; j < comData.header.force_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X",
						    comData.cx2_fm[j] & 0xFF);
				}

				/* Copying CX2 Sense */
				for (j = 0; j < comData.header.sense_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X",
						    comData.cx2_sn[j] & 0xFF);
				}

				kfree(comData.ix2_fm);
				kfree(comData.ix2_sn);
				kfree(comData.cx2_fm);
				kfree(comData.cx2_sn);
				break;


			case CMD_READGOLDENMUTUAL:
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)gmRawData.hdm_hdr.type);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						gmRawData.hdr.ms_f_len);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						gmRawData.hdr.ms_s_len);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						gmRawData.hdr.ss_f_len);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						gmRawData.hdr.ss_s_len);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						gmRawData.hdr.ms_k_len);

				/* Copying Golden Mutual raw values */
				for (j = 0; j < gmRawData.data_size; j++) {
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index, "%04X",
						(u16)gmRawData.data[j]);
				}

				kfree(gmRawData.data);
				break;

			case CMD_READTOTMSCOMPDATA:
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)totCompData.header.type);

				index += scnprintf(&info->driver_test_buff[index],
					    size - index, "%02X",
					    (u8)totCompData.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
					    size - index, "%02X",
					    (u8)totCompData.header.sense_node);

				/* Copying TOT CX values */
				for (j = 0; j < totCompData.node_data_size;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (totCompData.node_data[j] &
							0xFF00) >> 8,
						    totCompData.node_data[j] &
							0xFF);
				}

				kfree(totCompData.node_data);
				break;

			case CMD_READTOTSSCOMPDATA:
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)totComData.header.type);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						totComData.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						totComData.header.sense_node);

				/* Copying TOT IX Force */
				for (j = 0; j < totComData.header.force_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (totComData.ix_fm[j] &
							0xFF00) >> 8,
						    totComData.ix_fm[j] &
							0xFF);
				}

				/* Copying TOT IX Sense */
				for (j = 0; j < totComData.header.sense_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (totComData.ix_sn[j] &
							0xFF00) >> 8,
						    totComData.ix_sn[j] &
							0xFF);
				}

				/* Copying TOT CX Force */
				for (j = 0; j < totComData.header.force_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (totComData.cx_fm[j] &
							0xFF00) >> 8,
						    totComData.cx_fm[j] &
							0xFF);
				}

				/* Copying CX2 Sense */
				for (j = 0; j < totComData.header.sense_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (totComData.cx_sn[j] &
							0xFF00) >> 8,
						    totComData.cx_sn[j] &
							0xFF);
				}

				kfree(totComData.ix_fm);
				kfree(totComData.ix_sn);
				kfree(totComData.cx_fm);
				kfree(totComData.cx_sn);
				break;

			case CMD_READSENSCOEFF:
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)msCoeff.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)msCoeff.header.sense_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)ssCoeff.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)ssCoeff.header.sense_node);

				/* Copying MS Coefficients */
				for (j = 0; j < msCoeff.node_data_size; j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X",
						    msCoeff.ms_coeff[j] & 0xFF);
				}

				/* Copying SS force Coefficients */
				for (j = 0; j < ssCoeff.header.force_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X",
						    ssCoeff.ss_force_coeff[j] &
							0xFF);
				}

				/* Copying SS sense Coefficients */
				for (j = 0; j < ssCoeff.header.sense_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X",
						    ssCoeff.ss_sense_coeff[j] &
							0xFF);
				}

				kfree(msCoeff.ms_coeff);
				kfree(ssCoeff.ss_force_coeff);
				kfree(ssCoeff.ss_sense_coeff);
				break;

			case CMD_GETFWVER:
				for (j = 0; j < EXTERNAL_RELEASE_INFO_SIZE;
				     j++) {
					index += scnprintf(
					  &info->driver_test_buff[index],
					  size - index,
					  "%02X",
					  info->systemInfo.u8_releaseInfo[j]);
				}
				break;

			case CMD_READCOMPDATAHEAD:
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						dataHead.type);
				break;

			case CMD_READ_SYNC_FRAME:
				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameMS.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameMS.header.sense_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameSS.header.force_node);

				index += scnprintf(&info->driver_test_buff[index],
						size - index, "%02X",
						(u8)frameSS.header.sense_node);

				/* Copying mutual data */
				for (j = 0; j < frameMS.node_data_size; j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (frameMS.node_data[j] &
							0xFF00) >> 8,
						    frameMS.node_data[j] &
							0xFF);
				}

				/* Copying self data Force */
				for (j = 0; j < frameSS.header.force_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (frameSS.force_data[j] &
							0xFF00) >> 8,
						    frameSS.force_data[j] &
							0xFF);
				}


				/* Copying self  data Sense */
				for (j = 0; j < frameSS.header.sense_node;
				     j++) {
					index += scnprintf(
						    &info->driver_test_buff[index],
						    size - index,
						    "%02X%02X",
						    (frameSS.sense_data[j] &
							0xFF00) >> 8,
						    frameSS.sense_data[j] &
							0xFF);
				}

				kfree(frameMS.node_data);
				kfree(frameSS.force_data);
				kfree(frameSS.sense_data);
				break;


			default:
				break;
			}
		}

		index += scnprintf(&info->driver_test_buff[index],
				   size - index, " }\n");
		info->limit = size - 1;/* avoid to print \0 in the shell */
		info->printed = 0;
	} else {
		/* start byte */
		info->driver_test_buff[index++] = MESSAGE_START_BYTE;
		if (info->bin_output == 1) {
			/* msg_size */
			info->driver_test_buff[index++] = (size & 0xFF00) >> 8;
			info->driver_test_buff[index++] = (size & 0x00FF);
			/* counter id */
			info->driver_test_buff[index++] =
				(info->mess.counter & 0xFF00) >> 8;
			info->driver_test_buff[index++] = (info->mess.counter & 0x00FF);
			/* action */
			info->driver_test_buff[index++] = (info->mess.action & 0xFF00) >> 8;
			info->driver_test_buff[index++] = (info->mess.action & 0x00FF);
			/* error */
			info->driver_test_buff[index++] = (res & 0xFF00) >> 8;
			info->driver_test_buff[index++] = (res & 0x00FF);
		} else {
			if (funcToTest[0] == CMD_GETLIMITSFILE_BYTE ||
			    funcToTest[0] == CMD_GETFWFILE_BYTE)
				index += scnprintf(&info->driver_test_buff[index],
					   size - index,
					   "%02X%02X",
					   (((fileSize + 3) / 4) & 0xFF00) >> 8,
					   ((fileSize + 3) / 4) & 0x00FF);
			else
				index += scnprintf(&info->driver_test_buff[index],
					    size - index,
					    "%02X%02X", (size & 0xFF00) >> 8,
					    size & 0xFF);

			index += scnprintf(&info->driver_test_buff[index],
					   size - index, "%04X",
					   (u16)info->mess.counter);
			index += scnprintf(&info->driver_test_buff[index],
					   size - index, "%04X",
					   (u16)info->mess.action);
			index += scnprintf(&info->driver_test_buff[index],
					   size - index,
					   "%02X%02X", (res & 0xFF00) >> 8,
					   res & 0xFF);
		}

		switch (funcToTest[0]) {
		case CMD_VERSION_BYTE:
		case CMD_READ_BYTE:
		case CMD_WRITEREAD_BYTE:
		case CMD_WRITETHENWRITEREAD_BYTE:
		case CMD_WRITEREADU8UX_BYTE:
		case CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE:
			if (info->bin_output == 1) {
				if (info->mess.dummy == 1)
					memcpy(&info->driver_test_buff[index],
					       &readData[1], byteToRead);
				else
					memcpy(&info->driver_test_buff[index],
					       readData, byteToRead);
				index += byteToRead;
			} else {
				j = info->mess.dummy;
				for (; j < byteToRead + info->mess.dummy; j++)
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index,
						"%02X",
						(u8)readData[j]);
			}
			break;

		case CMD_GETLIMITSFILE_BYTE:
		case CMD_GETFWFILE_BYTE:
			if (info->bin_output == 1) {
				/* override the msg_size with dimension in words
				 * */
				info->driver_test_buff[1] = (
					((fileSize + 3) / 4) & 0xFF00) >> 8;
				info->driver_test_buff[2] = (
					((fileSize + 3) / 4) & 0x00FF);

				if (readData != NULL)
					memcpy(&info->driver_test_buff[index],
					       readData, fileSize);
				else
					dev_err(info->dev, "readData = NULL... returning junk data!");
				index += addr;	/* in this case the byte to read
						 * are stored in addr because it
						 * is a u64 end byte need to be
						 * inserted at the end of the
						 * padded memory */
			} else {
				/* snprintf(&info->driver_test_buff[1], 3, "%02X",
				 * (((fileSize + 3) / 4)&0xFF00) >> 8); */
				/* snprintf(&info->driver_test_buff[3], 3, "%02X",
				 * ((fileSize + 3) / 4)&0x00FF); */
				for (j = 0; j < fileSize; j++)
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index,
						"%02X",
						(u8)readData[j]);
				for (; j < addr; j++)
					index += scnprintf(
						&info->driver_test_buff[index],
						size - index,
						"%02X", 0);	/* pad memory
								 * with 0x00 */
			}
			break;
		default:
			break;
		}

		index += scnprintf(&info->driver_test_buff[index],
				  size - index, "%c\n", MESSAGE_END_BYTE);
		/*for(j=0; j<size; j++){
		  *      dev_err(info->dev, "%c", info->driver_test_buff[j]);
		  * }*/
		info->limit = size;
		info->printed = 0;
	}
ERROR:
	numberParam = 0;/* need to reset the number of parameters in order to
			 * wait the next command, comment if you want to repeat
			 * the last command sent just doing a cat */

	/* dev_err(info->dev, 0,"numberParameters = %d\n", numberParam); */

	kfree(readData);
	kfree(cmd);
	kfree(funcToTest);
	kfree(pbuf);

exit:
	return count;
}

/** @}*/

/**
  * file_operations struct which define the functions for the canonical
  * operation on a device file node (open. read, write etc.)
  */
static const struct proc_ops fts_driver_test_ops = {
	.proc_open	= fts_driver_test_open,
	.proc_read	= seq_read,
	.proc_write	= fts_driver_test_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= fts_driver_test_release
};

/*****************************************************************************/

/**
  * This function is called in the probe to initialize and create the directory
  * proc/fts and the driver test file node DRIVER_TEST_FILE_NODE into the /proc
  * file system
  * @return OK if success or an error code which specify the type of error
  */
int fts_proc_init(struct fts_ts_info *info)
{
	struct proc_dir_entry *entry;

	int retval = 0;

	info->fts_dir = proc_mkdir_data(info->board->device_name, 0555,
					NULL, info);
	if (info->fts_dir == NULL) {	/* directory creation failed */
		retval = -ENOMEM;
		goto out;
	}

	entry = proc_create_data(DRIVER_TEST_FILE_NODE, 0666, info->fts_dir,
				 &fts_driver_test_ops, info);

	if (entry)
		dev_info(info->dev, "%s: proc entry CREATED!\n", __func__);
	else {
		dev_err(info->dev, "%s: error creating proc entry!\n", __func__);
		retval = -ENOMEM;
		goto badfile;
	}
	return OK;
badfile:
	remove_proc_entry("fts", NULL);
out:
	return retval;
}

/**
  * Delete and Clean from the file system, all the references to the driver test
  * file node
  * @return OK
  */
int fts_proc_remove(struct fts_ts_info *info)
{
	remove_proc_entry(DRIVER_TEST_FILE_NODE, info->fts_dir);
	remove_proc_entry("fts", NULL);
	return OK;
}
