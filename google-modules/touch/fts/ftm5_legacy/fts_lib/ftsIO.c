/*
  *
  **************************************************************************
  **                        STMicroelectronics				**
  **************************************************************************
  **                        marco.cali@st.com				**
  **************************************************************************
  *                                                                        *
  *                     I2C/SPI Communication				  *
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file ftsIO.c
  * \brief Contains all the functions which handle with the I2C/SPI
  *communication
  */


#include "ftsSoftware.h"

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/stdarg.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/of_gpio.h>

#ifdef I2C_INTERFACE
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
static u16 I2CSAD;	/* /< slave address of the IC in the i2c bus */
#else
#include <linux/spi/spidev.h>
#endif

#include "ftsCore.h"
#include "ftsError.h"
#include "ftsHardware.h"
#include "ftsIO.h"


/**
  * Initialize the static client variable of the fts_lib library in order
  * to allow any i2c/spi transaction in the driver (Must be called in the probe)
  * @param clt pointer to i2c_client or spi_device struct which identify the bus
  * slave device
  * @return OK
  */
int openChannel(void *clt)
{
#ifdef I2C_INTERFACE
	I2CSAD = ((struct i2c_client *)clt)->addr;
	dev_info(&((struct i2c_client *)clt)->dev, "openChannel: SAD: %02X\n",
		I2CSAD);
#else
	dev_info(&((struct spi_device *)clt)->dev,
		"%s: spi_master: flags = %04X !\n", __func__,
		 ((struct spi_device *)clt)->master->flags);
	dev_info(&((struct spi_device *)clt)->dev,
		"%s: spi_device: max_speed = %d chip select = %02X bits_per_words = %d mode = %04X !\n",
		__func__, ((struct spi_device *)clt)->max_speed_hz,
		((struct spi_device *)clt)->chip_select,
		((struct spi_device *)clt)->bits_per_word,
		((struct spi_device *)clt)->mode);
	dev_info(&((struct spi_device *)clt)->dev, "openChannel: completed!\n");
#endif
	return OK;
}

#ifdef I2C_INTERFACE
/**
  * Change the I2C slave address which will be used during the transaction
  * (For Debug Only)
  * @param sad new slave address id
  * @return OK
  */
int changeSAD(u8 sad)
{
	I2CSAD = sad;
	return OK;
}
#endif


/****************** New I2C API *********************/

/**
  * Perform a direct bus read
  * @param outBuf pointer of a byte array which should contain the byte read
  * from the IC
  * @param byteToRead number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
static int fts_read_internal(struct fts_ts_info *info, u8 *outBuf,
			     int byteToRead, bool dma_safe)
{
	int ret = -1;
	int retry = 0;
#ifdef I2C_INTERFACE
	struct i2c_msg I2CMsg[1];
#else
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };
#endif

	if (dma_safe == false && byteToRead > sizeof(info->io_read_buf)) {
		dev_err(info->dev, "%s: preallocated buffers are too small!\n", __func__);
		return ERROR_ALLOC;
	}

#ifdef I2C_INTERFACE
	I2CMsg[0].addr = (__u16)I2CSAD;
	I2CMsg[0].flags = (__u16)I2C_M_RD;
	I2CMsg[0].len = (__u16)byteToRead;
	if (dma_safe == false)
		I2CMsg[0].buf = (__u8 *)info->io_read_buf;
	else
		I2CMsg[0].buf = (__u8 *)outBuf;
#else
	spi_message_init(&msg);

	transfer[0].len = byteToRead;
	transfer[0].delay.value = SPI_DELAY_CS;
	transfer[0].delay.unit = SPI_DELAY_UNIT_USECS;
	transfer[0].tx_buf = NULL;
	if (dma_safe == false)
		transfer[0].rx_buf = info->io_read_buf;
	else
		transfer[0].rx_buf = outBuf;
	spi_message_add_tail(&transfer[0], &msg);
#endif

	if (info->client == NULL)
		return ERROR_BUS_O;
	while (retry < I2C_RETRY && ret < OK) {
#ifdef I2C_INTERFACE
		ret = i2c_transfer(info->client->adapter, I2CMsg, 1);
#else
		ret = spi_sync(info->client, &msg);
#endif

		retry++;
		if (ret < OK)
			mdelay(I2C_WAIT_BEFORE_RETRY);
		/* dev_err(info->dev, "fts_writeCmd: attempt %d\n", retry); */
	}
	if (ret < 0) {
		dev_err(info->dev, "%s: ERROR %08X\n", __func__, ERROR_BUS_R);
		return ERROR_BUS_R;
	}

	if (dma_safe == false)
		memcpy(outBuf, info->io_read_buf, byteToRead);

	return OK;
}


/**
  * Perform a bus write followed by a bus read without a stop condition
  * @param cmd byte array containing the command to write
  * @param cmdLength size of cmd
  * @param outBuf pointer of a byte array which should contain the bytes read
  * from the IC
  * @param byteToRead number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
static int fts_writeRead_internal(struct fts_ts_info *info, u8 *cmd,
				  int cmdLength, u8 *outBuf, int byteToRead,
				  bool dma_safe)
{
	int ret = -1;
	int retry = 0;
#ifdef I2C_INTERFACE
	struct i2c_msg I2CMsg[2];
#else
	struct spi_message msg;
	struct spi_transfer transfer[2] = { { 0 }, { 0 } };
#endif

	if (dma_safe == false && (cmdLength > sizeof(info->io_write_buf) ||
	    byteToRead > sizeof(info->io_read_buf))) {
		dev_err(info->dev, "%s: preallocated buffers are too small!\n", __func__);
		return ERROR_ALLOC;
	}

	if (dma_safe == false) {
		memcpy(info->io_write_buf, cmd, cmdLength);
		cmd = info->io_write_buf;
	}

#ifdef I2C_INTERFACE
	/* write msg */
	I2CMsg[0].addr = (__u16)I2CSAD;
	I2CMsg[0].flags = (__u16)0;
	I2CMsg[0].len = (__u16)cmdLength;
	I2CMsg[0].buf = (__u8 *)cmd;

	/* read msg */
	I2CMsg[1].addr = (__u16)I2CSAD;
	I2CMsg[1].flags = I2C_M_RD;
	I2CMsg[1].len = byteToRead;
	if (dma_safe == false)
		I2CMsg[1].buf = (__u8 *)info->io_read_buf;
	else
		I2CMsg[1].buf = (__u8 *)outBuf;
#else
	spi_message_init(&msg);

	transfer[0].len = cmdLength;
	transfer[0].tx_buf = cmd;
	transfer[0].rx_buf = NULL;
	spi_message_add_tail(&transfer[0], &msg);

	transfer[1].len = byteToRead;
	transfer[1].delay.value = SPI_DELAY_CS;
	transfer[1].delay.unit = SPI_DELAY_UNIT_USECS;
	transfer[1].tx_buf = NULL;
	if (dma_safe == false)
		transfer[1].rx_buf = info->io_read_buf;
	else
		transfer[1].rx_buf = outBuf;
	spi_message_add_tail(&transfer[1], &msg);

#endif

	if (info->client == NULL)
		return ERROR_BUS_O;

	while (retry < I2C_RETRY && ret < OK) {
#ifdef I2C_INTERFACE
		ret = i2c_transfer(info->client->adapter, I2CMsg, 2);
#else
		ret = spi_sync(info->client, &msg);
#endif

		retry++;
		if (ret < OK)
			mdelay(I2C_WAIT_BEFORE_RETRY);
	}
	if (ret < 0) {
		dev_err(info->dev, "%s: ERROR %08X\n", __func__, ERROR_BUS_WR);
		return ERROR_BUS_WR;
	}

	if (dma_safe == false)
		memcpy(outBuf, info->io_read_buf, byteToRead);

	return OK;
}


/**
  * Perform a bus write
  * @param cmd byte array containing the command to write
  * @param cmdLength size of cmd
  * @return OK if success or an error code which specify the type of error
  */
static int fts_write_internal(struct fts_ts_info *info, u8 *cmd, int cmdLength,
			      bool dma_safe)
{
	int ret = -1;
	int retry = 0;
#ifdef I2C_INTERFACE
	struct i2c_msg I2CMsg[1];
#else
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };
#endif

	if (dma_safe == false && cmdLength > sizeof(info->io_write_buf)) {
		dev_err(info->dev, "%s: preallocated buffers are too small!\n", __func__);
		return ERROR_ALLOC;
	}

	if (dma_safe == false) {
		memcpy(info->io_write_buf, cmd, cmdLength);
		cmd = info->io_write_buf;
	}

#ifdef I2C_INTERFACE
	I2CMsg[0].addr = (__u16)I2CSAD;
	I2CMsg[0].flags = (__u16)0;
	I2CMsg[0].len = (__u16)cmdLength;
	I2CMsg[0].buf = (__u8 *)cmd;
#else
	spi_message_init(&msg);

	transfer[0].len = cmdLength;
	transfer[0].delay.value = SPI_DELAY_CS;
	transfer[0].delay.unit = SPI_DELAY_UNIT_USECS;
	transfer[0].tx_buf = cmd;
	transfer[0].rx_buf = NULL;
	spi_message_add_tail(&transfer[0], &msg);
#endif


	if (info->client == NULL)
		return ERROR_BUS_O;
	while (retry < I2C_RETRY && ret < OK) {
#ifdef I2C_INTERFACE
		ret = i2c_transfer(info->client->adapter, I2CMsg, 1);
#else
		ret = spi_sync(info->client, &msg);
#endif

		retry++;
		if (ret < OK)
			mdelay(I2C_WAIT_BEFORE_RETRY);
		/* dev_err(info->dev, "fts_writeCmd: attempt %d\n", retry); */
	}
	if (ret < 0) {
		dev_err(info->dev, "%s: ERROR %08X\n", __func__, ERROR_BUS_W);
		return ERROR_BUS_W;
	}
	return OK;
}

/**
  * Perform two bus write and one bus read without any stop condition
  * In case of FTI this function is not supported and the same sequence
  * can be achieved calling fts_write followed by an fts_writeRead.
  * @param writeCmd1 byte array containing the first command to write
  * @param writeCmdLength size of writeCmd1
  * @param readCmd1 byte array containing the second command to write
  * @param readCmdLength size of readCmd1
  * @param outBuf pointer of a byte array which should contain the bytes read
  * from the IC
  * @param byteToRead number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
static int fts_writeThenWriteRead_internal(struct fts_ts_info *info,
					   u8 *writeCmd1, int writeCmdLength,
					   u8 *readCmd1, int readCmdLength,
					   u8 *outBuf, int byteToRead,
					   bool dma_safe)
{
	int ret = -1;
	int retry = 0;
#ifdef I2C_INTERFACE
	struct i2c_msg I2CMsg[3];
#else
	struct spi_message msg;
	struct spi_transfer transfer[3] = { { 0 }, { 0 }, { 0 } };
#endif

	if (dma_safe == false && (writeCmdLength > sizeof(info->io_write_buf) ||
	    readCmdLength > sizeof(info->io_extra_write_buf) ||
	    byteToRead > sizeof(info->io_read_buf))) {
		dev_err(info->dev, "%s: preallocated buffers are too small!\n", __func__);
		return ERROR_ALLOC;
	}

	if (dma_safe == false) {
		memcpy(info->io_write_buf, writeCmd1, writeCmdLength);
		writeCmd1 = info->io_write_buf;
		memcpy(info->io_extra_write_buf, readCmd1, readCmdLength);
		readCmd1 = info->io_extra_write_buf;
	}

#ifdef I2C_INTERFACE
	/* write msg */
	I2CMsg[0].addr = (__u16)I2CSAD;
	I2CMsg[0].flags = (__u16)0;
	I2CMsg[0].len = (__u16)writeCmdLength;
	I2CMsg[0].buf = (__u8 *)writeCmd1;

	/* write msg */
	I2CMsg[1].addr = (__u16)I2CSAD;
	I2CMsg[1].flags = (__u16)0;
	I2CMsg[1].len = (__u16)readCmdLength;
	I2CMsg[1].buf = (__u8 *)readCmd1;

	/* read msg */
	I2CMsg[2].addr = (__u16)I2CSAD;
	I2CMsg[2].flags = I2C_M_RD;
	I2CMsg[2].len = byteToRead;
	if (dma_safe == false)
		I2CMsg[2].buf = (__u8 *)info->io_read_buf;
	else
		I2CMsg[2].buf = (__u8 *)outBuf;
#else
	spi_message_init(&msg);

	transfer[0].len = writeCmdLength;
	transfer[0].tx_buf = writeCmd1;
	transfer[0].rx_buf = NULL;
	spi_message_add_tail(&transfer[0], &msg);

	transfer[1].len = readCmdLength;
	transfer[1].tx_buf = readCmd1;
	transfer[1].rx_buf = NULL;
	spi_message_add_tail(&transfer[1], &msg);

	transfer[2].len = byteToRead;
	transfer[2].delay.value = SPI_DELAY_CS;
	transfer[2].delay.unit = SPI_DELAY_UNIT_USECS;
	transfer[2].tx_buf = NULL;
	if (dma_safe == false)
		transfer[2].rx_buf = info->io_read_buf;
	else
		transfer[2].rx_buf = outBuf;
	spi_message_add_tail(&transfer[2], &msg);
#endif

	if (info->client == NULL)
		return ERROR_BUS_O;
	while (retry < I2C_RETRY && ret < OK) {
#ifdef I2C_INTERFACE
		ret = i2c_transfer(info->client->adapter, I2CMsg, 3);
#else
		ret = spi_sync(info->client, &msg);
#endif
		retry++;
		if (ret < OK)
			mdelay(I2C_WAIT_BEFORE_RETRY);
	}

	if (ret < 0) {
		dev_err(info->dev, "%s: ERROR %08X\n", __func__, ERROR_BUS_WR);
		return ERROR_BUS_WR;
	}

	if (dma_safe == false)
		memcpy(outBuf, info->io_read_buf, byteToRead);

	return OK;
}

/* Wrapper API for i2c read and write */
int fts_read(struct fts_ts_info *info, u8 *outBuf, int byteToRead)
{
	int ret;
	mutex_lock(&info->io_mutex);
	ret = fts_read_internal(info, outBuf, byteToRead, false);
	mutex_unlock(&info->io_mutex);
	return ret;
}

int fts_read_heap(struct fts_ts_info *info, u8 *outBuf, int byteToRead)
{
	return fts_read_internal(info, outBuf, byteToRead, true);
}

int fts_writeRead(struct fts_ts_info *info, u8 *cmd, int cmdLength, u8 *outBuf,
		  int byteToRead)
{
	int ret;
	mutex_lock(&info->io_mutex);
	ret = fts_writeRead_internal(info, cmd, cmdLength, outBuf, byteToRead,
					false);
	mutex_unlock(&info->io_mutex);
	return ret;
}

int fts_writeRead_heap(struct fts_ts_info *info, u8 *cmd, int cmdLength,
		       u8 *outBuf, int byteToRead)
{
	return fts_writeRead_internal(info, cmd, cmdLength, outBuf, byteToRead,
				      true);
}

int fts_write(struct fts_ts_info *info, u8 *cmd, int cmdLength)
{
	int ret;
	mutex_lock(&info->io_mutex);
	ret = fts_write_internal(info, cmd, cmdLength, false);
	mutex_unlock(&info->io_mutex);
	return ret;
}

int fts_write_heap(struct fts_ts_info *info, u8 *cmd, int cmdLength)
{
	return fts_write_internal(info, cmd, cmdLength, true);
}

int fts_writeFwCmd(struct fts_ts_info *info, u8 *cmd, int cmdLength)
{
	int ret_write = 0;
	int ret_echo = 0;
	int retry = 0;

	while (retry < I2C_RETRY) {
		mutex_lock(&info->io_mutex);
		ret_write = fts_write_internal(info, cmd, cmdLength, false);
		mutex_unlock(&info->io_mutex);
		retry++;
		if (ret_write >= OK) {
			ret_echo = checkEcho(info, cmd, cmdLength);
			if (ret_echo >= OK)
				break;
		}
		mdelay(I2C_WAIT_BEFORE_RETRY);
	}
	if (ret_write < OK) {
		dev_err(info->dev, "fts_writeFwCmd: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	} else if (ret_echo < OK) {
		dev_err(info->dev, "fts_writeFwCmd: check echo ERROR %08X\n", ret_echo);
		return ret_echo;
	}

	return OK;
}

int fts_writeFwCmd_heap(struct fts_ts_info *info, u8 *cmd, int cmdLength)
{
	int ret_write = 0;
	int ret_echo = 0;
	int retry = 0;

	while (retry < I2C_RETRY) {
		ret_write = fts_write_internal(info, cmd, cmdLength, true);
		retry++;
		if (ret_write >= OK) {
			ret_echo = checkEcho(info, cmd, cmdLength);
			if (ret_echo >= OK)
				break;
		}
		mdelay(I2C_WAIT_BEFORE_RETRY);
	}
	if (ret_write < OK) {
		dev_err(info->dev, "fts_writeFwCmd: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	} else if (ret_echo < OK) {
		dev_err(info->dev, "fts_writeFwCmd: check echo ERROR %08X\n", ret_echo);
		return ret_echo;
	}

	return OK;
}

int fts_writeThenWriteRead(struct fts_ts_info *info, u8 *writeCmd1,
			   int writeCmdLength, u8 *readCmd1, int readCmdLength,
			   u8 *outBuf, int byteToRead)
{
	int ret;
	mutex_lock(&info->io_mutex);
	ret = fts_writeThenWriteRead_internal(info, writeCmd1, writeCmdLength,
						readCmd1, readCmdLength,
						outBuf, byteToRead, false);
	mutex_unlock(&info->io_mutex);
	return ret;
}

int fts_writeThenWriteRead_heap(struct fts_ts_info *info, u8 *writeCmd1,
				int writeCmdLength, u8 *readCmd1,
				int readCmdLength, u8 *outBuf, int byteToRead)
{
	return fts_writeThenWriteRead_internal(info, writeCmd1, writeCmdLength,
						readCmd1, readCmdLength,
						outBuf, byteToRead, true);
}

/**
  * Perform a chunked write with one byte op code and 1 to 8 bytes address
  * @param cmd byte containing the op code to write
  * @param addrSize address size in byte
  * @param address the starting address
  * @param data pointer of a byte array which contain the bytes to write
  * @param dataSize size of data
  * @return OK if success or an error code which specify the type of error
  */
/* this function works only if the address is max 8 bytes */
int fts_writeU8UX(struct fts_ts_info *info, u8 cmd, AddrSize addrSize,
		  u64 address, u8 *data, int dataSize)
{
	u8 *finalCmd;
	u8 *p;
	int remaining = dataSize;
	int toWrite = 0, i = 0;
	int ret = 0;

	if(addrSize > sizeof(u64)) {
		dev_err(info->dev, "%s: address size bigger than max allowed %lu. ERROR %08X\n",
			__func__, sizeof(u64), ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	mutex_lock(&info->io_mutex);

	finalCmd = info->io_write_buf;

	while (remaining > 0) {
		if (remaining > WRITE_CHUNK)
			toWrite = WRITE_CHUNK;
		else
			toWrite = remaining;

		finalCmd[0] = cmd;
		dev_dbg(info->dev, "%s: addrSize = %d, address = %llX\n",
			__func__, addrSize, address);

		p = (u8 *)&address + addrSize - 1;
		for (i = 0; i < addrSize; i++)
			finalCmd[i + 1] = *p--;
		memcpy(&finalCmd[addrSize + 1], data, toWrite);

		ret = fts_write_heap(info, finalCmd, 1 + addrSize + toWrite);
		if (ret < OK) {
			ret = ERROR_BUS_W;
			break;
		}

		address += toWrite;
		data += toWrite;
		remaining -= toWrite;
	}

	mutex_unlock(&info->io_mutex);
	if (ret < OK)
		dev_err(info->dev, " %s: ERROR %08X\n", __func__, ret);

	return ret;
}

/**
  * Perform a chunked write read with one byte op code and 1 to 8 bytes address
  * and dummy byte support.
  * @param cmd byte containing the op code to write
  * @param addrSize address size in byte
  * @param address the starting address
  * @param outBuf pointer of a byte array which contain the bytes to read
  * @param byteToRead number of bytes to read
  * @param bytes_to_skip if need to skip the first byte of each reading,
  * set to 1,
  *  otherwise if the first byte is valid set to 0.
  * @return OK if success or an error code which specify the type of error
  */
int fts_writeReadU8UX(struct fts_ts_info *info, u8 cmd, AddrSize addrSize,
		      u64 address, u8 *outBuf, int byteToRead,
		      int bytes_to_skip)
{
	u8 *finalCmd;
	u8 *buff;
	u8 *p;
	int remaining = byteToRead;
	int toRead = 0, i = 0;
	int ret = 0;

	if(addrSize > sizeof(u64)) {
		dev_err(info->dev, "%s: address size bigger than max allowed %lu. ERROR %08X\n",
			__func__, sizeof(u64), ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	mutex_lock(&info->io_mutex);

	finalCmd = info->io_write_buf;
	buff = info->io_read_buf;

	while (remaining > 0) {
		if (remaining > READ_CHUNK)
			toRead = READ_CHUNK;
		else
			toRead = remaining;

		finalCmd[0] = cmd;
		dev_dbg(info->dev, "%s: addrSize = %d, address = %llX\n",
			__func__, addrSize, address);

		p = (u8 *)&address + addrSize - 1;
		for (i = 0; i < addrSize; i++)
			finalCmd[i + 1] = *p--;

		ret = fts_writeRead_heap(info, finalCmd, 1 + addrSize,
					 buff, toRead + bytes_to_skip);
		if (ret < OK) {
			ret = ERROR_BUS_WR;
			break;
		}
		memcpy(outBuf, buff + bytes_to_skip, toRead);

		address += toRead;
		outBuf += toRead;
		remaining -= toRead;
	}

	mutex_unlock(&info->io_mutex);
	if (ret < OK)
		dev_err(info->dev, "%s: read error... ERROR %08X\n",
			__func__, ret);

	return ret;
}

/**
  * Perform a chunked write followed by a second write with one byte op code
  * for each write and 1 to 8 bytes address (the sum of the 2 address size of
  * the two writes can not exceed 8 bytes)
  * @param cmd1 byte containing the op code of first write
  * @param addrSize1 address size in byte of first write
  * @param cmd2 byte containing the op code of second write
  * @param addrSize2 address size in byte of second write
  * @param address the starting address
  * @param data pointer of a byte array which contain the bytes to write
  * @param dataSize size of data
  * @return OK if success or an error code which specify the type of error
  */
/* this function works only if the sum of two addresses in the two commands is
 * max 8 bytes */
int fts_writeU8UXthenWriteU8UX(struct fts_ts_info *info, u8 cmd1,
			       AddrSize addrSize1, u8 cmd2, AddrSize addrSize2,
			       u64 address, u8 *data, int dataSize)
{
	u8 *finalCmd1;
	u8 *finalCmd2;
	u8 *p;
	int remaining = dataSize;
	int toWrite = 0, i = 0;
	int ret = 0;

	mutex_lock(&info->io_mutex);

	finalCmd1 = info->io_write_buf;
	finalCmd2 = info->io_extra_write_buf;

	while (remaining > 0) {
		if (remaining > WRITE_CHUNK)
			toWrite = WRITE_CHUNK;
		else
			toWrite = remaining;

		finalCmd1[0] = cmd1;
		p = (u8 *)&address + addrSize1 + addrSize2 - 1;
		for (i = 0; i < addrSize1; i++)
			finalCmd1[i + 1] = *p--;

		finalCmd2[0] = cmd2;
		for (i = 0; i < addrSize2; i++)
			finalCmd2[i + 1] = *p--;

		memcpy(&finalCmd2[addrSize2 + 1], data, toWrite);

		ret = fts_write_heap(info, finalCmd1, 1 + addrSize1);
		if (ret < OK) {
			ret = ERROR_BUS_W;
			dev_err(info->dev, "%s: first write error. ERROR %08X\n",
				__func__, ret);
			break;
		}

		ret = fts_write_heap(info, finalCmd2, 1 + addrSize2 + toWrite);
		if (ret < OK) {
			ret = ERROR_BUS_W;
			dev_err(info->dev, "%s: second write error. ERROR %08X\n",
				__func__, ret);
			break;
		}

		address += toWrite;
		data += toWrite;
		remaining -= toWrite;
	}

	mutex_unlock(&info->io_mutex);

	return ret;
}

/**
  * Perform a chunked write  followed by a write read with one byte op code
  * and 1 to 8 bytes address for each write and dummy byte support.
  * @param cmd1 byte containing the op code of first write
  * @param addrSize1 address size in byte of first write
  * @param cmd2 byte containing the op code of second write read
  * @param addrSize2 address size in byte of second write read
  * @param address the starting address
  * @param outBuf pointer of a byte array which contain the bytes to read
  * @param byteToRead number of bytes to read
  * @param bytes_to_skip if need to skip the first byte of each reading,
  * set to 1,
  *  otherwise if the first byte is valid set to 0.
  * @return OK if success or an error code which specify the type of error
  */
/* this function works only if the sum of two addresses in the two commands is
 * max 8 bytes */
int fts_writeU8UXthenWriteReadU8UX(struct fts_ts_info *info, u8 cmd1,
				   AddrSize addrSize1, u8 cmd2,
				   AddrSize addrSize2, u64 address, u8 *outBuf,
				   int byteToRead, int bytes_to_skip)
{
	u8 *finalCmd1;
	u8 *finalCmd2;
	u8 *buff;
	u8 *p;
	int remaining = byteToRead;
	int toRead = 0, i = 0;
	int ret = 0;

	mutex_lock(&info->io_mutex);
	finalCmd1 = info->io_write_buf;
	finalCmd2 = info->io_extra_write_buf;
	buff = info->io_read_buf;

	while (remaining > 0) {
		if (remaining > READ_CHUNK)
			toRead = READ_CHUNK;
		else
			toRead = remaining;

		finalCmd1[0] = cmd1;
		p = (u8 *)&address + addrSize1 + addrSize2 - 1;
		for (i = 0; i < addrSize1; i++)
			finalCmd1[i + 1] = *p--;

		finalCmd2[0] = cmd2;
		for (i = 0; i < addrSize2; i++)
			finalCmd2[i + 1] = *p--;

		ret = fts_write_heap(info, finalCmd1, 1 + addrSize1);
		if (ret < OK) {
			ret = ERROR_BUS_W;
			dev_err(info->dev, "%s: first write error. ERROR %08X\n",
				__func__, ret);
			break;
		}

		ret = fts_writeRead_heap(info, finalCmd2, 1 + addrSize2,
					      buff, toRead + bytes_to_skip);
		if (ret < OK) {
			ret = ERROR_BUS_WR;
			dev_err(info->dev, "%s: read error. ERROR %08X\n",
					__func__, ret);
			break;
		}
		memcpy(outBuf, buff + bytes_to_skip, toRead);

		address += toRead;
		outBuf += toRead;
		remaining -= toRead;
	}

	mutex_unlock(&info->io_mutex);

	return ret;
}
