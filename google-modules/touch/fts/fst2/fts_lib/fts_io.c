/*
  *
  **************************************************************************
  **                        STMicroelectronics				**
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

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <stdarg.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/of_gpio.h>
#ifdef I2C_INTERFACE
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#else
#include <linux/spi/spidev.h>
#endif

static void *client;	/* /< bus client retrived by the OS and
				  * used to execute the bus transfers */

#include "fts_io.h"
#include "fts_error.h"

/*#define DEBUG_LOG*/
extern struct sys_info system_info;
static int reset_gpio = GPIO_NOT_DEFINED;

#ifdef I2C_INTERFACE
/**
  * Retrieve the pointer of the i2c_client struct representing the IC as i2c
  *slave
  * @return client if it was previously set or NULL in all the other cases
  */
struct i2c_client *get_client()
{
	if (client != NULL)
		return (struct i2c_client *)client;
	else
		return NULL;
}
#else
/**
  * Retrieve the pointer of the spi_client struct representing the IC as spi
  *slave
  * @return client if it was previously set or NULL in all the other cases
  */
struct spi_device *get_client()
{
	if (client != NULL)
		return (struct spi_device *)client;
	else
		return NULL;
}
#endif

/**
  * Retrieve the pointer to the device struct of the IC
  * @return a the device struct pointer if client was previously set
  * or NULL in all the other cases
  */
struct device *get_dev(void)
{
	if (client != NULL)
		return &(get_client()->dev);
	else
		return NULL;
}

/**
  * Set the reset_gpio variable with the actual gpio number of the board link to
  *the reset pin
  * @param gpio gpio number link to the reset pin of the IC
  */
void set_reset_gpio(int gpio)
{
	reset_gpio = gpio;
	log_info(1, "%s: reset_gpio = %d\n", __func__, reset_gpio);
}

/**
  * Initialize the static client variable of the fts_lib library in order
  * to allow any i2c/spi transaction in the driver (Must be called in the probe)
  * @param clt pointer to i2c_client or spi_device struct which identify the bus
  * slave device
  * @return OK
  */
int open_channel(void *clt)
{
	client = clt;
#ifndef I2C_INTERFACE
	log_info(1, "%s: spi_master: flags = %04X !\n", __func__,
		 ((struct spi_device *)client)->master->flags);
	log_info(1,
		 "%s: spi_device: max_speed = %d chip select = %02X bits_per_words = %d mode = %04X !\n",
		__func__,
		 ((struct spi_device *)client)->max_speed_hz,
		 ((struct spi_device *)client)->chip_select,
		 ((struct spi_device *)client)->bits_per_word,
		 ((struct spi_device *)client)->mode);
	log_info(1, "%s: openChannel: completed!\n", __func__);
#endif
	return OK;
}

/**
  * Perform a direct bus read
  * @param out_buf pointer of a byte array which should contain the byte read
  *from the IC
  * @param byte_to_read number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
int fts_read(u8 *out_buf, int byte_to_read)
{
	int ret = -1;
	int retry = 0;

#ifdef I2C_INTERFACE
	struct i2c_msg I2CMsg[1];

	I2CMsg[0].addr = (__u16)I2C_SAD;
	I2CMsg[0].flags = (__u16)I2C_M_RD;
	I2CMsg[0].len = (__u16)byte_to_read;
	I2CMsg[0].buf = (__u8 *)out_buf;
#else
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };

	spi_message_init(&msg);

	transfer[0].len = byte_to_read;
	transfer[0].delay_usecs = SPI_DELAY_CS;
	transfer[0].tx_buf = NULL;
	transfer[0].rx_buf = out_buf;
	spi_message_add_tail(&transfer[0], &msg);
#endif

	if (client == NULL)
		return ERROR_BUS_O;
	while (retry < I2C_RETRY && ret < OK) {
#ifdef I2C_INTERFACE
		ret = i2c_transfer(get_client()->adapter, I2CMsg, 1);
#else
		ret = spi_sync(get_client(), &msg);
#endif
		retry++;
		if (ret < OK)
			msleep(I2C_WAIT_BEFORE_RETRY);
	}
	if (ret < 0) {
		log_info(1, "%s: ERROR %08X\n", __func__, ERROR_BUS_R);
		return ERROR_BUS_R;
	}
	return OK;
}

/**
  * Perform a bus write followed by a bus read without a stop condition
  * @param cmd byte array containing the command to write
  * @param cmd_length size of cmd
  * @param out_buf pointer of a byte array which should contain the bytes read
  *from the IC
  * @param byte_to_read number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
int fts_write_read(u8 *cmd, int cmd_length, u8 *out_buf, int byte_to_read)
{
	int ret = -1;
	int retry = 0;
#ifdef I2C_INTERFACE
	struct i2c_msg I2CMsg[2];
#ifdef DEBUG_LOG
	int i = 0;
#endif
	/* write msg */
	I2CMsg[0].addr = (__u16)I2C_SAD;
	I2CMsg[0].flags = (__u16)0;
	I2CMsg[0].len = (__u16)cmd_length;
	I2CMsg[0].buf = (__u8 *)cmd;

	/* read msg */
	I2CMsg[1].addr = (__u16)I2C_SAD;
	I2CMsg[1].flags = I2C_M_RD;
	I2CMsg[1].len = byte_to_read;
	I2CMsg[1].buf = (__u8 *)out_buf;
#else
#ifdef DEBUG_LOG
	int i = 0;
#endif
	struct spi_message msg;
	struct spi_transfer transfer[2] = { { 0 }, { 0 } };

	spi_message_init(&msg);

	transfer[0].len = cmd_length;
	transfer[0].tx_buf = cmd;
	transfer[0].rx_buf = NULL;
	spi_message_add_tail(&transfer[0], &msg);

	transfer[1].len = byte_to_read;
	transfer[1].delay_usecs = SPI_DELAY_CS;
	transfer[1].tx_buf = NULL;
	transfer[1].rx_buf = out_buf;
	spi_message_add_tail(&transfer[1], &msg);

#endif

	if (client == NULL)
		return ERROR_BUS_O;

	while (retry < I2C_RETRY && ret < OK) {
#ifdef I2C_INTERFACE
		ret = i2c_transfer(get_client()->adapter, I2CMsg, 2);
#else
		ret = spi_sync(get_client(), &msg);
#endif
		retry++;
		if (ret < OK)
			msleep(I2C_WAIT_BEFORE_RETRY);
	}
#ifdef DEBUG_LOG
	log_info(1, "%s: W: ", __func__);
	for (i = 0; i < cmd_length; i++)
		printk(KERN_CONT "%02X ", cmd[i]);
	printk(KERN_CONT "R: ");
	for (i = 0; i < byte_to_read; i++)
		printk(KERN_CONT "%02X ", out_buf[i]);
	printk(KERN_CONT "\n");
#endif
	if (ret < 0) {
		log_info(1, "%s: ERROR %08X\n", __func__, ERROR_BUS_WR);
		return ERROR_BUS_WR;
	}
	return OK;
}


/**
  * Perform a bus write
  * @param cmd byte array containing the command to write
  * @param cmd_length size of cmd
  * @return OK if success or an error code which specify the type of error
  */
int fts_write(u8 *cmd, int cmd_length)
{
	int ret = -1;
	int retry = 0;
#ifdef I2C_INTERFACE
	struct i2c_msg I2CMsg[1];
#ifdef DEBUG_LOG
	int i = 0;
#endif
	I2CMsg[0].addr = (__u16)I2C_SAD;
	I2CMsg[0].flags = (__u16)0;
	I2CMsg[0].len = (__u16)cmd_length;
	I2CMsg[0].buf = (__u8 *)cmd;
#else
#ifdef DEBUG_LOG
	int i = 0;
#endif
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };

	spi_message_init(&msg);

	transfer[0].len = cmd_length;
	transfer[0].delay_usecs = SPI_DELAY_CS;
	transfer[0].tx_buf = cmd;
	transfer[0].rx_buf = NULL;
	spi_message_add_tail(&transfer[0], &msg);
#endif


	if (client == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__, ERROR_BUS_O);
		return ERROR_BUS_O;
	}
	while (retry < I2C_RETRY && ret < OK) {
#ifdef I2C_INTERFACE
		ret = i2c_transfer(get_client()->adapter, I2CMsg, 1);
#else
		ret = spi_sync(get_client(), &msg);
#endif
		retry++;
		if (ret < OK)
			msleep(I2C_WAIT_BEFORE_RETRY);
	}
#ifdef DEBUG_LOG
	log_info(1, "%s: W: ", __func__);
	for (i = 0; i < cmd_length; i++)
		printk(KERN_CONT "%02X ", cmd[i]);
	printk(KERN_CONT "\n");
#endif

	if (ret < 0) {
		log_info(1, "%s: ERROR %08X\n", __func__, ERROR_BUS_W);
		return ERROR_BUS_W;
	}
	return OK;
}


/**
  * Perform a chunked write with one byte op code and 1 to 8 bytes address
  * @param cmd byte containing the op code to write
  * @param addr_size address size in byte
  * @param address the starting address
  * @param data pointer of a byte array which contain the bytes to write
  * @param data_size size of data
  * @return OK if success or an error code which specify the type of error
  */
int fts_write_u8ux(u8 cmd, addr_size_t addr_size, u64 address, u8 *data,
						int data_size)
{
	u8 *final_cmd = NULL;
	u8 offset = 0;
	int remaining = data_size;
	int to_write = 0, i = 0;

	if (cmd == FTS_CMD_NONE) {
		final_cmd = (u8 *)kmalloc(sizeof(u8) *
			(addr_size + WRITE_CHUNK), GFP_KERNEL);
		if (final_cmd == NULL) {
			log_info(1, "%s: Error allocating memory\n", __func__);
			return ERROR_BUS_W;
		}
		offset = 0;
	} else {
		final_cmd = (u8 *)kmalloc(sizeof(u8) *
			(1 + addr_size + WRITE_CHUNK), GFP_KERNEL);
		if (final_cmd == NULL) {
			log_info(1, "%s: Error allocating memory\n", __func__);
			return ERROR_BUS_W;
		}
		offset = 1;
	}

	if (addr_size <= sizeof(u64)) {
		while (remaining > 0) {
			if (remaining >= WRITE_CHUNK) {
				to_write = WRITE_CHUNK;
				remaining -= WRITE_CHUNK;
			} else {
				to_write = remaining;
				remaining = 0;
			}
			if (cmd != FTS_CMD_NONE) {
				final_cmd[0] = cmd;
				log_info(0, "%s: cmd[0] = %02X\n",
					__func__, final_cmd[0]);
			}
			log_info(0, "%s: addr_size_t = %d\n", __func__,
				addr_size);
			for (i = 0; i < addr_size; i++) {
				final_cmd[i + offset] =
				(u8)((address >> ((addr_size - 1 - i) *
					8)) & 0xFF);
				log_info(0, "%s: cmd[%d] = %02X\n", __func__,
					i + offset, final_cmd[i + offset]);
			}
			for (i = 0; i < to_write; i++)
				log_info(0, "%s: data[%d] = %02X\n",
					__func__, i, data[i]);

			memcpy(&final_cmd[addr_size + offset], data, to_write);

			if (fts_write(final_cmd, offset +
				addr_size + to_write) < OK) {
				log_info(0, "%s: ERROR %08X\n",
				__func__, ERROR_BUS_W);
				kfree(final_cmd);
				return ERROR_BUS_W;
			}

			address += to_write;
			data += to_write;
		}
	} else
		log_info(1,
			"%s: address size bigger than max allowed %ld... ERROR %08X\n",
			__func__, sizeof(u64), ERROR_OP_NOT_ALLOW);

	kfree(final_cmd);
	return OK;
}

/**
  * Perform a chunked write read with one byte op code and 1 to 8 bytes address
  * and dummy byte support.
  * @param cmd byte containing the op code to write
  * @param addr_size address size in byte
  * @param address the starting address
  * @param out_buf pointer of a byte array which contain the bytes to read
  * @param byte_to_read number of bytes to read
  * @param has_dummy_byte  if the first byte of each reading is dummy (must be
  * skipped)
  * set to 1, otherwise if it is valid set to 0 (or any other value)
  * @return OK if success or an error code which specify the type of error
  */
int fts_write_read_u8ux(u8 cmd, addr_size_t addr_size, u64 address,
			u8 *out_buf, int byte_to_read, int has_dummy_byte)
{
	u8 *final_cmd = NULL;
	u8 offset = 0;
	u8 *buff = NULL;
	int remaining = byte_to_read;
	int to_read = 0, i = 0;

	buff =  (u8 *)kmalloc(sizeof(u8) * (READ_CHUNK + 1), GFP_KERNEL);
	if (buff == NULL) {
		log_info(1, "%s: Error allocating memory\n", __func__);
		return ERROR_BUS_WR;
	}

	if (cmd == FTS_CMD_NONE) {
		final_cmd = (u8 *)kmalloc(sizeof(u8) *
					(addr_size + WRITE_CHUNK), GFP_KERNEL);
		if (final_cmd == NULL) {
			log_info(1, "%s: Error allocating memory\n", __func__);
			kfree(buff);
			return ERROR_BUS_WR;
		}
		offset = 0;
	} else {
		final_cmd = (u8 *)kmalloc(sizeof(u8) *
			(1 + addr_size + WRITE_CHUNK), GFP_KERNEL);
		if (final_cmd == NULL) {
			log_info(1, "%s: Error allocating memory\n", __func__);
			kfree(buff);
			return ERROR_BUS_WR;
		}
		offset = 1;
	}

	while (remaining > 0) {
		if (remaining >= READ_CHUNK) {
			to_read = READ_CHUNK;
			remaining -= READ_CHUNK;
		} else {
			to_read = remaining;
			remaining = 0;
		}

		if (cmd != FTS_CMD_NONE) {
			final_cmd[0] = cmd;
			log_info(0, "%s: cmd[0] = %02X\n",
				__func__, final_cmd[0]);
		}
		for (i = 0; i < addr_size; i++) {
			final_cmd[i + offset] =
			(u8)((address >> ((addr_size - 1 - i) * 8)) & 0xFF);
			log_info(0, "%s: cmd[%d] = %02X\n",
			__func__, i + offset, final_cmd[i + offset]);
		}

		if (has_dummy_byte == 1) {
			if (fts_write_read(final_cmd, offset + addr_size,
				buff, to_read + 1) < OK) {
				log_info(1,
					"%s: read error... ERROR %08X\n",
					__func__, ERROR_BUS_WR);
				kfree(final_cmd);
				kfree(buff);
				return ERROR_BUS_WR;
			}
			memcpy(out_buf, buff + 1, to_read);
		} else {
			if (fts_write_read(final_cmd, offset + addr_size, buff,
				to_read) < OK) {
				log_info(1,
					"%s: read error... ERROR %08X\n",
					__func__, ERROR_BUS_WR);
				kfree(final_cmd);
				kfree(buff);
				return ERROR_BUS_WR;
			}
			memcpy(out_buf, buff, to_read);
		}

		address += to_read;
		out_buf += to_read;
	}
	kfree(final_cmd);
	kfree(buff);
	return OK;
}

/** @addtogroup events_group
  * @{
  */

/**
  * Poll the FIFO looking for a specified event within a timeout.
  * @param event_to_search pointer to an array of int where each element
  * correspond to a byte of the event to find.
  * If the element of the array has value -1, the byte of the event,
  * in the same position of the element is ignored.
  * @param event_bytes size of event_to_search
  * @param read_data pointer to an array of byte which will contain the event
  *found
  * @param time_to_wait time to wait before going in timeout
  * @return OK if success or an error code which specify the type of error
  */
int poll_for_event(int *event_to_search, int event_bytes, u8 *read_data,
						int time_to_wait)
{
	int i, find, retry, count_err;
	char temp[128] = { 0 };

	find = 0;
	retry = 0;
	count_err = 0;
	msleep(TIMEOUT_RESOLUTION);
	while (find != 1 && retry < time_to_wait &&
			fts_read_fw_reg(FIFO_READ_ADDR, read_data, 8) >= OK) {
		/* Log of errors */
		if (read_data[0] == EVT_ID_ERROR) {
			log_info(1, "%s: %s", __func__,
			print_hex("ERROR EVENT = ", read_data,
				FIFO_EVENT_SIZE, temp));
			switch (read_data[1]) {
			case EVT_TYPE_ERROR_ITO_FORCETOGND:
				printk("{ITO:Force Short to GND Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_SENSETOGND:
				printk("{ITO:Sense short to GND Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_FLTTOGND:
				printk("{ITO:Float Pin short to GND Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_FORCETOVDD:
				printk("{ITO:Force short to VDD Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_SENSETOVDD:
				printk("{ITO:Sense short to VDD Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_FLTTOVDD:
				printk("{ITO:Float Pin short to VDD Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_FORCE_P2P:
				printk("{ITO:Force Pin to Pin Short Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_SENSE_P2P:
				printk("{ITO:Sense Pin to Pin Short Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_FLT_P2P:
				printk("{ITO:Sense Pin to Pin Short Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_FORCEOPEN:
				printk("{ITO:Force Open Error}\n");
				break;
			case EVT_TYPE_ERROR_ITO_SENSEOPEN:
				printk("{ITO:Sense Open Error}\n");
				break;
			default:
				printk("\n");
				break;

			}
			memset(temp, 0, 128);
			count_err++;
		} else {
			if (read_data[0] != EVT_ID_NOEVENT) {
				log_info(1, "%s %s\n",
				__func__, print_hex("READ EVENT = ",
				read_data, FIFO_EVENT_SIZE, temp));
				memset(temp, 0, 128);
			}
			if (read_data[0] == EVT_ID_CONTROLLER_READY &&
				event_to_search[0] != EVT_ID_CONTROLLER_READY) {
				log_info(1, "%s Unmanned Controller Ready Event! Setting reset flags...\n",
					__func__);
			}
		}
		find = 1;
		for (i = 0; i < event_bytes; i++) {
			if (event_to_search[i] != -1 &&
				(int)read_data[i] != event_to_search[i]) {
				find = 0;
				break;
			}
		}

		retry++;
		msleep(TIMEOUT_RESOLUTION);
	}
	if ((retry >= time_to_wait) && find != 1) {
		log_info(1, "%s ERROR %08X\n", __func__, ERROR_TIMEOUT);
		return ERROR_TIMEOUT;
	} else if (find == 1) {
		log_info(1, "%s: %s\n", __func__,
			print_hex("FOUND EVENT = ",
			read_data, FIFO_EVENT_SIZE, temp));
		memset(temp, 0, 128);
		/* kfree(temp); */
		log_info(1,
		"%s: Event found in (%d iterations)! Number of errors found = %d\n",
		__func__, retry, count_err);
		return count_err;
	} else {
		log_info(1, "%s: ERROR %08X\n", __func__, ERROR_BUS_R);
		return ERROR_BUS_R;
	}
}
/** @}*/

/**
  * Print messages after converting data in hex format combined with label
  * @param label message label to be printed
  * @param  buff data pointer of bytes to print in hex format
  * @param count variable for data size
  * @param result data pointer to print the combined message
  */

char *print_hex(char *label, u8 *buff, int count, u8 *result)
{
	int i, offset;

	offset = strlen(label);
	strlcpy(result, label, offset + 1); /* +1 for terminator char */

	for (i = 0; i < count; i++) {
		snprintf(&result[offset], 4, "%02X ", buff[i]);
		/* this append automatically a null terminator char */
		offset += 3;
	}
	return result;
}

/**
  * Print messages in the kernel log
  * @param force if 1, the log is printed always otherwise only if DEBUG is
  * defined, the log will be printed
  * @param msg string containing the message to print
  * @param ... additional parameters that are used in msg according the format
  * of printf
  */

void log_info(int force, const char *msg, ...)
{
	if (force == 1
#ifdef DEBUG
		|| 1
#endif
		) {
		char log_buffer[120];
		va_list args;

		//printk("%s", "[ FTS ] ");
		va_start(args, msg);
		vscnprintf(log_buffer, sizeof(log_buffer), msg, args);
		//vprintk(msg, args);
		va_end(args);
		pr_info("%s", log_buffer);
	}
}

/**
  * Convert an array of bytes to an array of u16 taking two bytes at time,
  * src has LSB first.
  * @param src pointer to the source byte array
  * @param src_length size of src
  * @param dst pointer to the destination array.
  * @return the final size of dst (half of the source) or ERROR_OP_NOT_ALLOW
  * if the size of src is not multiple of 2.
  */
int u8_to_u16n(u8 *src, int src_length, u16 *dst)
{
	int i, j;

	if (src_length % 2 != 0)
		return ERROR_OP_NOT_ALLOW;
	j = 0;
	dst = (u16 *)kmalloc((src_length / 2) * sizeof(u16),
		GFP_KERNEL);
	for (i = 0; i < src_length; i += 2) {
		dst[j] = ((src[i + 1] & 0x00FF) << 8) + (src[i] &
			0x00FF);
		j++;
	}

	return src_length / 2;
}

/**
  * Convert an array of 2 bytes to a u16, src has LSB first (little endian).
  * @param src pointer to the source byte array
  * @param dst pointer to the destination u16.
  * @return OK
  */
int u8_to_u16(u8 *src, u16 *dst)
{
	*dst = (u16)(((src[1] & 0x00FF) << 8) + (src[0] & 0x00FF));
	return OK;
}
/**
  * Convert an array of 2 bytes to a u16, src has MSB first (big endian).
  * @param src pointer to the source byte array
  * @param dst pointer to the destination u16.
  * @return OK
  */
int u8_to_u16_be(u8 *src, u16 *dst)
{
	*dst = (u16)(((src[0] & 0x00FF) << 8) + (src[1] & 0x00FF));
	return OK;
}

/**
  * Convert an array of u16 to an array of u8, dst has MSB first (big endian).
  * @param src pointer to the source array of u16
  * @param src_length size of src
  * @param dst pointer to the destination array of u8. This array should be free
  * when no need anymore
  * @return size of dst (src size multiply by 2)
  */
int u16_to_u8n_be(u16 *src, int src_length, u8 *dst)
{
	int i, j;

	dst = (u8 *)kmalloc((2 * src_length) * sizeof(u8), GFP_KERNEL);
	j = 0;
	for (i = 0; i < src_length; i++) {
		dst[j] = (u8)(src[i] & 0xFF00) >> 8;
		dst[j + 1] = (u8)(src[i] & 0x00FF);
		j += 2;
	}

	return src_length * 2;
}


/**
  * Convert a u16 to an array of 2 u8, dst has MSB first (big endian).
  * @param src u16 to convert
  * @param dst pointer to the destination array of 2 u8.
  * @return OK
  */
int u16_to_u8_be(u16 src, u8 *dst)
{
	dst[0] = (u8)((src & 0xFF00) >> 8);
	dst[1] = (u8)(src & 0x00FF);
	return OK;
}

/**
  * Convert a u16 to an array of 2 u8, dst has LSB first (little endian).
  * @param src u16 to convert
  * @param dst pointer to the destination array of 2 u8.
  * @return OK
  */
int u16_to_u8(u16 src, u8 *dst)
{
	dst[1] = (u8)((src & 0xFF00) >> 8);
	dst[0] = (u8)(src & 0x00FF);
	return OK;
}

/**
  * Convert an array of bytes to a u32, src has LSB first (little endian).
  * @param src array of bytes to convert
  * @param dst pointer to the destination u32 variable.
  * @return OK
  */
int u8_to_u32(u8 *src, u32 *dst)
{
	*dst = (u32)(((src[3] & 0xFF) << 24) + ((src[2] & 0xFF) << 16) +
		((src[1] & 0xFF) << 8) + (src[0] & 0xFF));
	return OK;
}

/**
  * Convert an array of bytes to a u32, src has MSB first (big endian).
  * @param src array of bytes to convert
  * @param dst pointer to the destination u32 variable.
  * @return OK
  */
int u8_to_u32_be(u8 *src, u32 *dst)
{
	*dst = (u32)(((src[0] & 0xFF) << 24) + ((src[1] & 0xFF) << 16) +
		((src[2] & 0xFF) << 8) + (src[3] & 0xFF));
	return OK;
}

/**
  * Convert a u32 to an array of 4 bytes, dst has LSB first (little endian).
  * @param src u32 value to convert
  * @param dst pointer to the destination array of 4 bytes.
  * @return OK
  */
int u32_to_u8(u32 src, u8 *dst)
{
	dst[3] = (u8)((src & 0xFF000000) >> 24);
	dst[2] = (u8)((src & 0x00FF0000) >> 16);
	dst[1] = (u8)((src & 0x0000FF00) >> 8);
	dst[0] = (u8)(src & 0x000000FF);
	return OK;
}

/**
  * Convert a u32 to an array of 4 bytes, dst has MSB first (big endian).
  * @param src u32 value to convert
  * @param dst pointer to the destination array of 4 bytes.
  * @return OK
  */
int u32_to_u8_be(u32 src, u8 *dst)
{
	dst[0] = (u8)((src & 0xFF000000) >> 24);
	dst[1] = (u8)((src & 0x00FF0000) >> 16);
	dst[2] = (u8)((src & 0x0000FF00) >> 8);
	dst[3] = (u8)(src & 0x000000FF);
	return OK;
}

/**
  * Convert an array of bytes to an u64, src has MSB first (big endian).
  * @param src array of bytes
  * @param dest pointer to the destination u64.
  * @param size size of src (can be <= 8)
  * @return OK if success or ERROR_OP_NOT_ALLOW if size exceed 8
  */
int u8_to_u64_be(u8 *src, u64 *dest, int size)
{
	int i = 0;

	if (size > sizeof(u64))
		return ERROR_OP_NOT_ALLOW;
	*dest = 0;

	for (i = 0; i < size; i++)
		*dest |= (u64)(src[i]) << ((size - 1 - i) * 8);


	return OK;
}

/**
  * Convert an u64 to an array of bytes, dest has MSB first (big endian).
  * @param src value of u64
  * @param dest pointer to the destination array of bytes.
  * @param size size of src (can be <= 8)
  * @return OK if success or ERROR_OP_NOT_ALLOW if size exceed 8
  */
int u64_to_u8_be(u64 src, u8 *dest, int size)
{
	int i = 0;

	if (size > sizeof(u64))
		return ERROR_OP_NOT_ALLOW;
	for (i = 0; i < size; i++)
		dest[i] = (u8)((src >> ((size - 1 - i) * 8)) & 0xFF);

	return OK;
}

/**
  * Convert a value of an id in a bitmask with a 1 in the position of the value
  *of the id
  * @param id Value of the ID to convert
  * @param mask pointer to the bitmask that will be updated with the value of id
  * @param size dimension in bytes of mask
  * @return OK if success or ERROR_OP_NOT_ALLOW if size of mask is not enough to
  *contain ID
  */
int from_id_to_mask(u8 id, u8 *mask, int size)
{
	if (((int)((id) / 8)) < size) {
		log_info(1, "%s: ID = %d Index = %d Position = %d !\n",
		__func__, id, ((int)((id) / 8)), (id % 8));
		mask[((int)((id) / 8))] |= 0x01 << (id % 8);
		return OK;
	}
	log_info(1, "%s: Bitmask too small! Impossible contain ID = %d %d>=%d! ERROR %08X\n",
		__func__, id, ((int)((id) / 8)), size,
		ERROR_OP_NOT_ALLOW);
	return ERROR_OP_NOT_ALLOW;
}

/**
  * Perform a system reset of the IC.
  * If the reset pin is associated to a gpio, the function execute an hw reset
  * (toggling of reset pin) otherwise send an hw command to the IC
  * @param poll_event varaiable to enable polling for controller ready event
  * @return OK if success or an error code which specify the type of error
  */
int fts_system_reset(int poll_event)
{
	int res = 0;
	u8 data = SYSTEM_RESET_VAL;
	int event_to_search = EVT_ID_CONTROLLER_READY;
	u8 read_data[8] = { 0x00 };
	int add = 0x001C;
	uint8_t int_data = 0x01;

	if (reset_gpio == GPIO_NOT_DEFINED) {
		res = fts_write_u8ux(FTS_CMD_HW_REG_W, BITS_32, SYS_RST_ADDR,
			&data, 1);
		if (res < OK) {
			log_info(1, "%s ERROR %08X\n", __func__, res);
			return res;
		}
	} else {
		gpio_set_value(reset_gpio, 0);
		msleep(20);
		gpio_set_value(reset_gpio, 1);
		res = OK;
	}

	if (poll_event) {
		res = poll_for_event(&event_to_search, 1, read_data,
			TIMEOUT_GENERAL);
		if (res < OK)
			log_info(1, "%s ERROR %08X\n", __func__, res);
	} else
		msleep(100);

	res = fts_write_fw_reg(add, &int_data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
	}

	return res;
}

/**
  * Perform a firmware register write.
  * @param address varaiable to point register offset
  * @param data address pointer  to array of data bytes
  * @param length size of data to write
  * @return OK if success or an error code which specify the type of error
  */
int fts_write_fw_reg(u16 address, u8 *data, uint32_t length)
{
	int res = 0;

#ifdef I2C_INTERFACE
	res = fts_write_u8ux(FTS_CMD_NONE, FW_ADDR_SIZE, address, data, length);
#else
	int remaining = length;
	int to_write_now = 0;
	int written_already = 0;

	while (remaining > 0) {
		/*FW chunk size limit for spi FW REG W is 128 bytes*/
		to_write_now =
		remaining > SPI_REG_W_CHUNK ? SPI_REG_W_CHUNK : remaining;
		res = fts_write_u8ux(FTS_CMD_REG_SPI_W, FW_ADDR_SIZE,
			address + written_already,
			data + written_already, to_write_now);
		remaining -= to_write_now;
		written_already += to_write_now;
	}
#endif
	if (res < OK)
		log_info(1, "%s ERROR %08X\n", __func__, res);

	return res;
}

/**
  * Perform a firmware register read.
  * @param address varaiable to point register offset
  * @param read_data pointer to buffer to read data
  * @param read_length size of data to read
  * @return OK if success or an error code which specify the type of error
  */
int fts_read_fw_reg(u16 address, u8 *read_data, uint32_t read_length)
{
	int res = 0;

#ifdef I2C_INTERFACE
	res = fts_write_read_u8ux(FTS_CMD_NONE, FW_ADDR_SIZE,
			address, read_data, read_length, DUMMY_BYTE);
#else
	int remaining = read_length;
	int to_read_now = 0;
	int read_already = 0;

	while (remaining > 0) {
		/*FW chunk size limit for spi FW REG R is 4096 bytes*/
		to_read_now =
		remaining > SPI_REG_R_CHUNK ? SPI_REG_R_CHUNK : remaining;
		res = fts_write_read_u8ux(FTS_CMD_REG_SPI_R, FW_ADDR_SIZE,
			address + read_already, read_data + read_already,
			to_read_now, DUMMY_BYTE);
		remaining -= to_read_now;
		read_already += to_read_now;
	}
#endif
	if (res < OK)
		log_info(1, "%s ERROR %08X\n", __func__, res);

	return res;
}

/**
  * Perform a hdm data write to frame buffer.
  * @param address varaiable to framebuffer
  * @param data address pointer  to array of data bytes
  * @param length size of data to write
  * @return OK if success or an error code which specify the type of error
  */
int fts_write_hdm(u16 address, u8 *data, int length)
{
	int res = 0;

#ifdef I2C_INTERFACE
	res = fts_write_u8ux(FTS_CMD_NONE, FW_ADDR_SIZE, address, data, length);
#else
	int remaining = length;
	int to_write_now = 0;
	int written_already = 0;

	while (remaining > 0) {
		/*FW chunk size limit for spi FW HDM W is 4096 bytes*/
		to_write_now =
		remaining > SPI_HDM_W_CHUNK ? SPI_HDM_W_CHUNK : remaining;
		res = fts_write_u8ux(FTS_CMD_HDM_SPI_W, FW_ADDR_SIZE,
			address + written_already,
			data + written_already, to_write_now);
		remaining -= to_write_now;
		written_already += to_write_now;
	}
#endif
	if (res < OK)
		log_info(1, "%s ERROR %08X\n", __func__, res);
	return res;
}

/**
  * Perform a hdm data read from frame buffer
  * after hdm request
  * @param address varaiable to point register offset
  * @param read_data pointer to buffer to read data
  * @param read_length size of data to read
  * @return OK if success or an error code which specify the type of error
  */
int fts_read_hdm(u16 address, u8 *read_data, uint32_t read_length)
{
	int res = 0;

#ifdef I2C_INTERFACE
	res = fts_write_read_u8ux(FTS_CMD_NONE, FW_ADDR_SIZE,
		address, read_data, read_length, DUMMY_BYTE);
#else
	int remaining = read_length;
	int to_read_now = 0;
	int read_already = 0;

	while (remaining > 0) {
		/*FW chunk size limit for spi FW HDM R is 4096 bytes*/
		to_read_now =
		remaining > SPI_HDM_R_CHUNK ? SPI_HDM_R_CHUNK : remaining;
		res = fts_write_read_u8ux(FTS_CMD_HDM_SPI_R, FW_ADDR_SIZE,
			address + read_already, read_data + read_already,
			to_read_now, DUMMY_BYTE);
		remaining -= to_read_now;
		read_already += to_read_now;
	}
#endif
	if (res < OK)
		log_info(1, "%s ERROR %08X\n", __func__, res);

	return res;
}

/**
  * Perform a firmware register read to check bit has been reset
  * as part of the fw request command in a time out period
  * @param address variable to point register offset
  * @param bit_to_set variable to check fw register bit
  * @param time_to_wait time out in ms resolution
  * @return OK if success or an error code which specify the type of error
  */
int poll_fw_reg_clear_status(u16 address, u8 bit_to_check, int time_to_wait)
{
	int i = 0;
	int res = 0;
	u8 data = 0;

	for (i = 0; i < time_to_wait; i++) {
		msleep(TIMEOUT_RESOLUTION);
		res = fts_read_fw_reg(address, &data, 1);
		if (res < OK) {
			log_info(1, "%s ERROR %08X\n", __func__, res);
			return res;
		}
		if ((data & (0x01 << bit_to_check)) == 0x00)
			break;
	}
	if (i == time_to_wait) {
		log_info(1, "%s FW reg status timeout.. RegVal: %02X\n",
				__func__, data);
		return ERROR_TIMEOUT;
	}
	return OK;
}

/**
  * Perform a firmware request by setting the regsiter bits.
  * conditional auto clear option for waiting for that bits to be
  * cleared by fw based on status of operation
  * @param address variable to point register offset
  * @param bit_to_set variable to set fw register bit
  * @param auto_clear flag to poll for the bit to clear after request
  * @param time_to_wait variable to set time out for auto clear
  * @return OK if success or an error code which specify the type of error
  */
int fts_fw_request(u16 address, u8 bit_to_set, u8 auto_clear,
						int time_to_wait)
{
	int res = 0;
	u8 data = 0x00;

	res = fts_read_fw_reg(address, &data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}
	data = data | (0x01 << bit_to_set);
	res = fts_write_fw_reg(address, &data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	if (auto_clear) {
		res = poll_fw_reg_clear_status(address, bit_to_set,
					time_to_wait);
		if (res < OK) {
			log_info(1, "%s ERROR %08X\n", __func__, res);
			return res;
		}
	} else
		msleep(TIMEOUT_RESOLUTION * time_to_wait);

	return res;

}

/**
  * Perform a firmware request to save the data from hdm to system
  * conditional save_to_flash option for saving data to flash
  * @param save_to_flash variable to set option for save to flash
  * @return OK if success or an error code which specify the type of error
  */
int fts_hdm_write_request(u8 save_to_flash)
{
	int res = 0;

	res = fts_fw_request(HDM_WRITE_REQ_ADDR, 0, 1,
				TIMEOUT_FW_REG_STATUS);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	if (save_to_flash) {
		res = fts_fw_request(FLASH_SAVE_ADDR, 7, 1,
				TIMEOUT_FW_REG_STATUS);
		if (res < OK) {
			log_info(1, "%s ERROR %08X\n", __func__, res);
			return res;
		}
	}
	return res;
}

/**
  * Perform a firmware register write to request hdm data
  * wait for fw to clear the hdm register or timeout
  * @param type variable different type of hdm data
  * @return OK if success or an error code which specify the type of error
  */
int fts_request_hdm(u8 type)
{
	int res = 0;
	u8 data = type;
	u8 read_buff = 0;
	int i = 0;

	res = fts_write_fw_reg(HDM_REQ_ADDR, &data, 1);
	if (res < OK) {
		log_info(1, "%s ERROR %08X\n", __func__, res);
		return res;
	}

	for (i = 0; i < TIMEOUT_FW_REG_STATUS; i++) {
		msleep(TIMEOUT_RESOLUTION);
		res = fts_read_fw_reg(HDM_REQ_ADDR, &read_buff, 1);
		if (res < OK) {
			log_info(1, "%s ERROR %08X\n", __func__, res);
			return res;
		}
		if (read_buff == 0x00)
			break;
	}
	if (i == TIMEOUT_FW_REG_STATUS) {
		log_info(1, "%s HDM Request timeout.. RegVal: %02X\n",
				__func__, read_buff);
		return ERROR_TIMEOUT;
	}
	return OK;
}

/**
  * Read the System errors of size 8 bytes from Firmware register
  * and print it
  * @return OK if success or an error code which specify the type of error
  */
int fts_read_sys_errors(void)
{
	int res = 0;
	u8 data[8] = {0x00};
	int i = 0;

	res = fts_read_fw_reg(SYS_ERROR_ADDR, data, 8);
	if (res < OK) {
		log_info(1, "%s: ERROR %08X\n", __func__, res);
		return res;
	}
	log_info(1, "%s: system errors:\n", __func__);
	for (; i < 8; i++)
		log_info(1, "%s: 0x%04X: %02X\n", __func__, SYS_ERROR_ADDR + i,
		data[i]);
	return res;
}

/**
  * Perform hdm header data read.
  * initiates a firmware register hdm data request
  * followed by reading the header data based on
  * request status
  * @param type variable different type of hdm data
  * @param header pointer to data bytes for header
  * @return OK if success or an error code which specify the type of error
  */

int read_hdm_header(uint8_t type, u8 *header)
{
	int res = OK;

	res = fts_request_hdm(type);
	if (res < OK) {
		log_info(1, "%s: error requesting hdm: %02X\n", __func__, type);
		return res;
	}
	res = fts_read_hdm(FRAME_BUFFER_ADDR, header, COMP_HEADER_SIZE);
	if (res < OK) {
		log_info(1, "%s read total cx header ERROR %08X\n",
		__func__, res);
		return res;
	}

	log_info(1, "%s type: %02X, cnt: %02X, len: %d words\n", __func__,
		header[0], header[1], (u16)((header[3] << 8) + header[2]));
	if ((header[0] != type) && header[1] != 0)
		log_info(1, "%s HDM request error %08X\n", __func__,
		ERROR_TIMEOUT);
	return res;
}
/**
  * Read and pack the frame data related to the nodes
  * @param address address in memory when the frame data node start
  * @param size amount of data to read
  * @param frame pointer to an array of bytes which will contain the frame node
  *data
  * @return OK if success or an error code which specify the type of error
  */
int get_frame_data(u16 address, int size, short *frame)
{
	int i, j, res;
	u8 *data = (u8 *)kmalloc(size * sizeof(u8), GFP_KERNEL);

	if (data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__, ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	res = fts_write_read_u8ux(FTS_CMD_HW_REG_R, BITS_32,
			FRAME_BUFFER_ADDRESS + address, data, size, DUMMY_BYTE);
	if (res < OK) {
		log_info(1, "%s: ERROR %08X\n", __func__, ERROR_BUS_R);
		kfree(data);
		data = NULL;
		return ERROR_BUS_R;
	}
	j = 0;
	for (i = 0; i < size; i += 2) {
		frame[j] = (short)((data[i + 1] << 8) + data[i]);
		j++;
	}
	kfree(data);
	data = NULL;
	return OK;
}

/**
  * Read a MS Frame from frame buffer memory
  * @param type type of MS frame to read
  * @param frame pointer to MutualSenseFrame variable which will contain the
  * data
  * @return OK if success or an error code which specify the type of error
  */
int get_ms_frame(ms_frame_type_t type, struct mutual_sense_frame *frame)
{
	u16 offset;
	int res, force_len, sense_len;

	force_len = system_info.u8_scr_tx_len;
	sense_len = system_info.u8_scr_rx_len;

	if (force_len == 0x00 || sense_len == 0x00 ||
		force_len == 0xFF || sense_len == 0xFF) {
		log_info(1, "%s: number of channels not initialized ERROR %08X\n",
			__func__, ERROR_CH_LEN);
		return ERROR_CH_LEN | ERROR_GET_FRAME;
	}

	frame->node_data = NULL;

	log_info(1, "%s: Starting to get frame %02X\n", __func__,
type);
	switch (type) {
	case MS_RAW:
		offset = system_info.u16_ms_scr_raw_addr;
		break;
	case MS_STRENGTH:
		offset = system_info.u16_ms_scr_strength_addr;
		break;
	case MS_FILTER:
		offset = system_info.u16_ms_scr_filter_addr;
		break;
	case MS_BASELINE:
		offset = system_info.u16_ms_scr_baseline_addr;
		break;
	default:
			log_info(1, "%s: Invalid type ERROR %08X\n",
				__func__, ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME);
			return ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME;
	}

	frame->node_data_size = (force_len * sense_len);
	frame->header.force_node = force_len;
	frame->header.sense_node = sense_len;
	frame->header.type = type;

	log_info(1, "%s: Force_len = %d Sense_len = %d Offset = %04X\n",
		__func__, force_len, sense_len, offset);

	frame->node_data = (short *)kmalloc(frame->node_data_size *
		sizeof(short), GFP_KERNEL);
	if (frame->node_data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}

	res = get_frame_data(offset, frame->node_data_size *
			BYTES_PER_NODE, (frame->node_data));
	if (res < OK) {
		log_info(1, "%s %s: ERROR %08X\n",
		__func__, ERROR_GET_FRAME_DATA);
		kfree(frame->node_data);
		frame->node_data = NULL;
		return res | ERROR_GET_FRAME_DATA | ERROR_GET_FRAME;
	}
	/* if you want to access one node i,j,
	  * compute the offset like: offset = i*columns + j = > frame[i, j] */

	log_info(1, "%s Frame acquired!\n", __func__);
	return OK;
	/* return the number of data put inside frame */

}

/**
  * Read a SS Frame from frame buffer
  * @param type type of SS frame to read
  * @param frame pointer to SelfSenseFrame variable which will contain the data
  * @return OK if success or an error code which specify the type of error
  */
int get_ss_frame(ss_frame_type_t type, struct self_sense_frame *frame)
{
	u16 self_force_offset = 0;
	u16 self_sense_offset = 0;
	int res, force_len, sense_len;

	force_len = system_info.u8_scr_tx_len;
	sense_len = system_info.u8_scr_rx_len;

	if (force_len == 0x00 || sense_len == 0x00 ||
		force_len == 0xFF || sense_len == 0xFF) {
		log_info(1, "%s: number of channels not initialized ERROR %08X\n",
			__func__, ERROR_CH_LEN);
		return ERROR_CH_LEN | ERROR_GET_FRAME;
	}

	frame->force_data = NULL;
	frame->sense_data = NULL;
	frame->header.force_node = force_len;
	frame->header.sense_node = sense_len;

	log_info(1, "%s: Starting to get frame %02X\n", __func__, type);
	switch (type) {
	case SS_RAW:
		self_force_offset = system_info.u16_ss_tch_tx_raw_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_raw_addr;
		break;
	case SS_FILTER:
		self_force_offset = system_info.u16_ss_tch_tx_filter_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_filter_addr;
		break;
	case SS_BASELINE:
		self_force_offset = system_info.u16_ss_tch_tx_baseline_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_baseline_addr;
		break;
	case SS_STRENGTH:
		self_force_offset = system_info.u16_ss_tch_tx_strength_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_strength_addr;
		break;
	case SS_DETECT_RAW:
		self_force_offset = system_info.u16_ss_det_tx_raw_addr;
		self_sense_offset = system_info.u16_ss_det_rx_raw_addr;
		frame->header.force_node = (self_force_offset == 0) ?
					0 : frame->header.force_node;
		frame->header.sense_node = (self_sense_offset == 0) ?
					0 : frame->header.sense_node;
		break;
	case SS_DETECT_STRENGTH:
		self_force_offset = system_info.u16_ss_det_tx_strength_addr;
		self_sense_offset = system_info.u16_ss_det_rx_strength_addr;
		frame->header.force_node = (self_force_offset == 0) ?
					0 : frame->header.force_node;
		frame->header.sense_node = (self_sense_offset == 0) ?
					0 : frame->header.sense_node;
		break;
	case SS_DETECT_BASELINE:
		self_force_offset = system_info.u16_ss_det_tx_baseline_addr;
		self_sense_offset = system_info.u16_ss_det_rx_baseline_addr;
		frame->header.force_node = (self_force_offset == 0) ?
					0 : frame->header.force_node;
		frame->header.sense_node = (self_sense_offset == 0) ?
					0 : frame->header.sense_node;
		break;
	case SS_DETECT_FILTER:
		self_force_offset = system_info.u16_ss_det_tx_filter_addr;
		self_sense_offset = system_info.u16_ss_det_rx_filter_addr;
		frame->header.force_node = (self_force_offset == 0) ?
					0 : frame->header.force_node;
		frame->header.sense_node = (self_sense_offset == 0) ?
					0 : frame->header.sense_node;
		break;
	default:
		log_info(1, "%s: Invalid type ERROR %08X\n", __func__,
					ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME);
		return ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME;
	}
	frame->header.type = type;
	log_info(1, "%s: Force_len = %d Sense_len = %d Offset_force = %04X Offset_sense = %04X\n",
		__func__, frame->header.force_node, frame->header.sense_node,
		self_force_offset, self_sense_offset);
	frame->force_data = (short *)kmalloc(frame->header.force_node *
						sizeof(short), GFP_KERNEL);
	if (frame->force_data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
			ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}

	frame->sense_data = (short *)kmalloc(frame->header.sense_node *
						sizeof(short), GFP_KERNEL);
	if (frame->sense_data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
				ERROR_ALLOC | ERROR_GET_FRAME);
		kfree(frame->force_data);
		frame->force_data = NULL;
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}

	if (self_force_offset) {
		res = get_frame_data(self_force_offset,
			frame->header.force_node *
			BYTES_PER_NODE, (frame->force_data));
		if (res < OK) {
			log_info(1, "%s: error while reading force data ERROR %08X\n",
					__func__, ERROR_GET_FRAME_DATA);
			kfree(frame->force_data);
			frame->force_data = NULL;
			kfree(frame->sense_data);
			frame->sense_data = NULL;
			return res | ERROR_GET_FRAME_DATA | ERROR_GET_FRAME;
		}
	}

	if (self_sense_offset) {
		res = get_frame_data(self_sense_offset,
			frame->header.sense_node *
			BYTES_PER_NODE, (frame->sense_data));
		if (res < OK) {
			log_info(1, "%s: error while reading force data ERROR %08X\n",
			__func__, res | ERROR_GET_FRAME_DATA |
			ERROR_GET_FRAME);
			kfree(frame->force_data);
			frame->force_data = NULL;
			kfree(frame->sense_data);
			frame->sense_data = NULL;
			return res | ERROR_GET_FRAME_DATA | ERROR_GET_FRAME;
		}
	}
	log_info(1, "%s Frame acquired!\n", __func__);
	return OK;
}

/**
  * Read a Sync Frame from frame buffer which contain MS and SS data collected
  *for the same scan
  * @param type type of Sync frame to read, possible values:
  *  LOAD_SYNC_FRAME_RAW, LOAD_SYNC_FRAME_FILTER, LOAD_SYNC_FRAME_BASELINE,
  * LOAD_SYNC_FRAME_STRENGTH
  * @param msFrame pointer to MutualSenseFrame variable which will contain the
  *MS data
  * @param ssFrame pointer to SelfSenseFrame variable which will contain the SS
  *data
  * @return OK if success or an error code which specify the type of error
  */
int get_sync_frame(u8 type, struct mutual_sense_frame *ms_frame,
			struct self_sense_frame *ss_frame)
{
	int res;
	u8 header_data[SYNC_FRAME_HEADER_SIZE] = {0x00};
	u32 address = 0;
	u8 *sync_frame_data = NULL;
	u64 sync_frame_size = 0;
	int i = 0, j = 0;
	int offset;

	ms_frame->node_data = NULL;
	ss_frame->force_data = NULL;
	ss_frame->sense_data = NULL;

	res = read_hdm_header(type, header_data);
	if (res < OK) {
		log_info(1, "%s: read hdm header error\n", __func__);
		return res | ERROR_GET_FRAME;
	}
	ms_frame->header.force_node = ss_frame->header.force_node =
						header_data[5];
	ms_frame->header.sense_node = ss_frame->header.sense_node =
						header_data[6];
	ms_frame->header.type = type;
	log_info(1, "%s: tx_count: %d rx_count: %d\n", __func__,
		ms_frame->header.force_node, ms_frame->header.sense_node);

	if (ms_frame->header.force_node == 0x00 ||
		ms_frame->header.sense_node == 0x00 ||
		ms_frame->header.force_node == 0xFF ||
		ms_frame->header.sense_node == 0xFF) {
		log_info(1,
		"%s: force/sense length cannot be empty.Invalid sync frame header\n");
		return ERROR_CH_LEN | ERROR_GET_FRAME;
	}
	sync_frame_size = (header_data[5] * header_data[6] * 2) +
				(header_data[5] * 2) + (header_data[6] * 2);
	log_info(1, "%s: sync frame size: %d\n", __func__, sync_frame_size);
	sync_frame_data = (u8 *)kmalloc(sync_frame_size *
			sizeof(u8), GFP_KERNEL);
	if (sync_frame_data == NULL) {
		log_info(1, "%s: ERROR %08X\n",
				__func__, ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}
	address = FRAME_BUFFER_ADDR + SYNC_FRAME_HEADER_SIZE + header_data[4];
	log_info(1, "%s: sync frame address: 0x%04X\n", __func__, address);
	res = fts_read_hdm(address, sync_frame_data, sync_frame_size);
	if (res < OK) {
		log_info(1, "%s: sync frame read ERROR %08X\n",
				__func__, ERROR_ALLOC | ERROR_GET_FRAME);
		kfree(sync_frame_data);
		sync_frame_data = NULL;
		return res | ERROR_ALLOC | ERROR_GET_FRAME;
	}

	ms_frame->node_data_size = ms_frame->header.force_node *
						ms_frame->header.sense_node;
	ms_frame->node_data = (short *)kmalloc(ms_frame->node_data_size *
						sizeof(short), GFP_KERNEL);
	if (ms_frame->node_data == NULL)	{
		log_info(1, "%s: ERROR %08X\n",
				__func__, ERROR_ALLOC | ERROR_GET_FRAME);
		res = ERROR_ALLOC | ERROR_GET_FRAME;
		goto goto_end;
	}
	offset = ms_frame->node_data_size * 2;
	for (; i < offset; i += 2) {
		ms_frame->node_data[j] =
		(short)((sync_frame_data[i + 1] << 8) + sync_frame_data[i]);
		j++;
	}

	ss_frame->force_data = (short *)kmalloc(ss_frame->header.force_node *
						sizeof(short), GFP_KERNEL);
	if (ss_frame->force_data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
					ERROR_ALLOC | ERROR_GET_FRAME);
		res = ERROR_ALLOC | ERROR_GET_FRAME;
		goto goto_end;
	}

	j = 0;
	offset = ss_frame->header.force_node * 2 + i;
	log_info(1, "%s: sync frame ss force: %d\n", __func__, i);
	for (; i < offset; i += 2) {
		ss_frame->force_data[j] =
		(short)((sync_frame_data[i + 1] << 8) + sync_frame_data[i]);
		j++;
	}

	ss_frame->sense_data = (short *)kmalloc(ss_frame->header.sense_node *
						sizeof(short), GFP_KERNEL);
	if (ss_frame->sense_data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
					ERROR_ALLOC | ERROR_GET_FRAME);
		res = ERROR_ALLOC | ERROR_GET_FRAME;
		goto goto_end;
	}

	offset = ss_frame->header.sense_node * 2 + i;
	log_info(1, "%s: sync frame ss sense: %d\n", __func__, i);
	j = 0;
	for (; i < offset; i += 2) {
		ss_frame->sense_data[j] =
		(short)((sync_frame_data[i + 1] << 8) +	sync_frame_data[i]);
		j++;
	}

goto_end:
	if (sync_frame_data != NULL)	{
		kfree(sync_frame_data);
		sync_frame_data = NULL;
	}
	if (res < OK) {
		if (ms_frame->node_data != NULL) {
			kfree(ms_frame->node_data);
			ms_frame->node_data = NULL;
		}
		if (ss_frame->force_data != NULL) {
			kfree(ss_frame->force_data);
			ss_frame->force_data = NULL;
		}

		if (ss_frame->sense_data != NULL) {
			kfree(ss_frame->sense_data);
			ss_frame->sense_data = NULL;
		}
		log_info(1, "%s: Getting Sync Frame FAILED! ERROR %08X!\n",
					__func__, res);
	} else
		log_info(1, "%s: Getting Sync Frame Finished!!\n", __func__);

	return res;
}

/**
  * Perform all the steps to read the necessary info for MS Initialization data
  * from the buffer and store it in a mutual_sense_cx_data variable
  * @param type type of MS Initialization data to read
  * @param ms_cx_data pointer to mutual_sense_cx_data variable which will contain the MS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
int get_mutual_cx_data(u8 type, struct mutual_sense_cx_data *ms_cx_data)
{
	int res;
	u32 address = 0;
	u8 header_data[COMP_HEADER_SIZE] = { 0x00 };

	ms_cx_data->node_data = NULL;
	if (!(type == HDM_REQ_CX_MS_TOUCH || type == HDM_REQ_CX_MS_LOW_POWER)) {
		log_info(1,
			"%s: Choose a MS type of compensation data ERROR %08X\n",
			__func__, ERROR_OP_NOT_ALLOW | ERROR_GET_CX);
		return ERROR_OP_NOT_ALLOW | ERROR_GET_CX;
	}

	res = read_hdm_header(type, header_data);
	if (res < OK) {
		log_info(1, "%s: read hdm header error\n", __func__);
		return res | ERROR_GET_CX;
	}

	ms_cx_data->header.force_node = header_data[4];
	ms_cx_data->header.sense_node = header_data[5];
	ms_cx_data->header.type = type;
	log_info(1, "%s: tx_count: %d rx_count: %d\n", __func__,
		ms_cx_data->header.force_node, ms_cx_data->header.sense_node);
	if (ms_cx_data->header.force_node == 0x00 ||
		ms_cx_data->header.sense_node == 0x00 ||
		ms_cx_data->header.force_node == 0xFF ||
		ms_cx_data->header.sense_node == 0xFF) {
		log_info(1,
		"%s: force/sense length cannot be empty.Invalid header\n");
		return ERROR_CH_LEN | ERROR_GET_CX;
	}

	ms_cx_data->cx1 = header_data[8];
	log_info(1, "%s: cx1: %d\n", __func__, ms_cx_data->cx1);
	ms_cx_data->node_data_size = ms_cx_data->header.force_node *
				ms_cx_data->header.sense_node;
	address = FRAME_BUFFER_ADDR + COMP_HEADER_SIZE;
	log_info(1, "%s: compensation data address: 0x%04X, size: %d\n",
			__func__, address, ms_cx_data->node_data_size);

	ms_cx_data->node_data = (i8 *)kmalloc(ms_cx_data->node_data_size *
						(sizeof(i8)), GFP_KERNEL);
	if (ms_cx_data->node_data == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
					__func__, ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	res = fts_read_hdm(address, ms_cx_data->node_data,
				ms_cx_data->node_data_size);
	if (res < OK) {
		log_info(1, "%s: sync frame read ERROR %08X\n",
			__func__, ERROR_ALLOC | ERROR_GET_FRAME);
		kfree(ms_cx_data->node_data);
		ms_cx_data->node_data = NULL;
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}
	log_info(1, "%s: Read Mutual CX data done!!\n", __func__);
	return OK;

}

/**
  * Perform all the steps to read the necessary info for SS Initialization data
  * from the buffer and store it in a self_sense_cx_data variable
  * @param type type of SS Initialization data to read
  * @param ss_cx_data pointer to self_sense_cx_data variable which will contain the SS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
int get_self_cx_data(u8 type, struct self_sense_cx_data *ss_cx_data)
{
	int res;
	u32 address = 0;
	u8 header_data[COMP_HEADER_SIZE] = { 0x00 };
	int size = 0;
	u8 *data = NULL;

	ss_cx_data->ix2_tx = NULL;
	ss_cx_data->ix2_rx = NULL;
	ss_cx_data->cx2_tx = NULL;
	ss_cx_data->cx2_rx = NULL;

	if (!(type == HDM_REQ_CX_SS_TOUCH ||
		type == HDM_REQ_CX_SS_TOUCH_IDLE)) {
		log_info(1,
			"%s: Choose a SS type of compensation data ERROR %08X\n",
			__func__, ERROR_OP_NOT_ALLOW | ERROR_GET_CX);
		return ERROR_OP_NOT_ALLOW | ERROR_GET_CX;
	}

	res = read_hdm_header(type, header_data);
	if (res < OK) {
		log_info(1, "%s: read hdm header error\n", __func__);
		return res | ERROR_GET_CX;
	}

	ss_cx_data->header.force_node = header_data[4];
	ss_cx_data->header.sense_node = header_data[5];
	ss_cx_data->header.type = type;
	log_info(1, "%s: tx_count: %d rx_count: %d\n", __func__,
			ss_cx_data->header.force_node,
			ss_cx_data->header.sense_node);
	if (ss_cx_data->header.force_node == 0x00 ||
		ss_cx_data->header.sense_node == 0x00 ||
		ss_cx_data->header.force_node == 0xFF ||
		ss_cx_data->header.sense_node == 0xFF) {
		log_info(1,
		"%s: force/sense length cannot be empty.Invalid header\n");
		return ERROR_CH_LEN | ERROR_GET_CX;
	}
	ss_cx_data->tx_ix0 = header_data[8];
	ss_cx_data->rx_ix0 = header_data[9];
	ss_cx_data->tx_ix1 = header_data[10];
	ss_cx_data->rx_ix1 = header_data[11];
	ss_cx_data->tx_max_n = header_data[12];
	ss_cx_data->rx_max_n = header_data[13];
	ss_cx_data->tx_cx1 = (i8)header_data[14];
	ss_cx_data->rx_cx1 = (i8)header_data[15];
	log_info(1,
		"%s: tx_ix1 = %d rx_ix1 = %d  tx_cx1 = %d  rx_cx1 = %d\n",
		__func__, ss_cx_data->tx_ix1, ss_cx_data->rx_ix1,
		ss_cx_data->tx_cx1, ss_cx_data->rx_cx1);
	log_info(1, "%s: tx_max_n = %d  rx_max_n = %d tx_ix0 = %d  rx_ix0 = %d\n",
		__func__, ss_cx_data->tx_max_n, ss_cx_data->rx_max_n,
		ss_cx_data->tx_ix0, ss_cx_data->rx_ix0);

	size = (ss_cx_data->header.force_node * 2) +
			(ss_cx_data->header.sense_node * 2);
	data = (u8 *)kmalloc(size * (sizeof(u8)), GFP_KERNEL);
	if (data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
				ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_CX;
	}

	address = FRAME_BUFFER_ADDR + COMP_HEADER_SIZE;
	log_info(1, "%s: compensation data address: 0x%04X, size: %d\n",
			__func__, address, size);

	res = fts_read_hdm(address, data, size);
	if (res < OK) {
		log_info(1, "%s: sync frame read ERROR %08X\n", __func__,
				ERROR_ALLOC | ERROR_GET_FRAME);
		kfree(data);
		data = NULL;
		return ERROR_ALLOC | ERROR_GET_CX;
	}

	ss_cx_data->ix2_tx = (u8 *)kmalloc(ss_cx_data->header.force_node *
						(sizeof(i8)), GFP_KERNEL);
	if (ss_cx_data->ix2_tx == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
				__func__, ERROR_ALLOC);
		res = ERROR_ALLOC | ERROR_GET_CX;
		goto goto_end;
	}
	ss_cx_data->ix2_rx = (u8 *)kmalloc(ss_cx_data->header.sense_node *
						(sizeof(i8)), GFP_KERNEL);
	if (ss_cx_data->ix2_rx == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
				__func__, ERROR_ALLOC);
		res = ERROR_ALLOC | ERROR_GET_CX;
		goto goto_end;
	}
	ss_cx_data->cx2_tx = (i8 *)kmalloc(ss_cx_data->header.force_node *
						(sizeof(i8)), GFP_KERNEL);
	if (ss_cx_data->cx2_tx == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
					__func__, ERROR_ALLOC);
		res = ERROR_ALLOC | ERROR_GET_CX;
		goto goto_end;
	}
	ss_cx_data->cx2_rx = (i8 *)kmalloc(ss_cx_data->header.sense_node *
						(sizeof(i8)), GFP_KERNEL);
	if (ss_cx_data->cx2_rx == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
				__func__, ERROR_ALLOC);
		res = ERROR_ALLOC | ERROR_GET_CX;
		goto goto_end;
	}
	size = 0;
	memcpy(ss_cx_data->ix2_tx, data, ss_cx_data->header.force_node);
	size += ss_cx_data->header.force_node;
	memcpy(ss_cx_data->ix2_rx, data + size, ss_cx_data->header.sense_node);
	size += ss_cx_data->header.sense_node;
	memcpy(ss_cx_data->cx2_tx, data + size, ss_cx_data->header.force_node);
	size += ss_cx_data->header.force_node;
	memcpy(ss_cx_data->cx2_rx, data + size, ss_cx_data->header.sense_node);

goto_end:
	if (data != NULL) {
		kfree(data);
		data = NULL;
	}
	if (res < OK) {
		if (ss_cx_data->ix2_tx != NULL) {
			kfree(ss_cx_data->ix2_tx);
			ss_cx_data->ix2_tx = NULL;
		}
		if (ss_cx_data->ix2_rx != NULL) {
			kfree(ss_cx_data->ix2_rx);
			ss_cx_data->ix2_rx = NULL;
		}
		if (ss_cx_data->cx2_tx != NULL) {
			kfree(ss_cx_data->cx2_tx);
			ss_cx_data->cx2_tx = NULL;
		}
		if (ss_cx_data->cx2_rx != NULL) {
			kfree(ss_cx_data->cx2_rx);
			ss_cx_data->cx2_rx = NULL;
		}
	} else
		log_info(1, "%s: Read Self CX data done!!\n", __func__);

	return res;

}

/**
  * Perform all the steps to read the necessary info for TOT MS Initialization
  * data from the buffer and store it in a mutual_total_cx_data variable
  * @param type type of TOT MS Initialization data to read
  * @param tot_ms_cx_data pointer to a mutual_total_cx_data variable
  * which will contain the TOT MS initialization data
  * @return OK if success or an error code which specify the type of error
  */
int get_mutual_total_cx_data(u8 type, struct mutual_total_cx_data *tot_ms_cx_data)
{
	int res;
	u32 address = 0;
	int size = 0;
	int i, j = 0;
	u8 *data = NULL;
	u8 header_data[COMP_HEADER_SIZE] = { 0x00 };
	tot_ms_cx_data->node_data = NULL;
	if (!(type == HDM_REQ_TOT_CX_MS_TOUCH ||
		type == HDM_REQ_TOT_CX_MS_LOW_POWER)) {
		log_info(1,
			"%s: Choose a MS total type of compensation data ERROR %08X\n",
			__func__, ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	res = read_hdm_header(type, header_data);
	if (res < OK) {
		log_info(1, "%s: read hdm header error\n", __func__);
		return res | ERROR_GET_CX;
	}

	tot_ms_cx_data->header.force_node = header_data[4];
	tot_ms_cx_data->header.sense_node = header_data[5];
	tot_ms_cx_data->header.type = type;
	log_info(1, "%s: tx_count: %d rx_count: %d\n", __func__,
		tot_ms_cx_data->header.force_node,
		tot_ms_cx_data->header.sense_node);
	if (tot_ms_cx_data->header.force_node == 0x00 ||
		tot_ms_cx_data->header.sense_node == 0x00 ||
		tot_ms_cx_data->header.force_node == 0xFF ||
		tot_ms_cx_data->header.sense_node == 0xFF) {
		log_info(1, "%s: force/sense length cannot be empty.. Invalid sysn frame header\n");
		return ERROR_CH_LEN | ERROR_GET_CX;
	}

	size = tot_ms_cx_data->header.force_node *
		tot_ms_cx_data->header.sense_node * 2;
	address = FRAME_BUFFER_ADDR + COMP_HEADER_SIZE;
	log_info(1, "%s: compensation data address: 0x%04X, size: %d\n",
		__func__, address, size);

	data = (u8 *)kmalloc(size * sizeof(u8), GFP_KERNEL);
	if (data == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
		__func__, ERROR_ALLOC);
		return ERROR_ALLOC | ERROR_GET_CX;
	}

	res = fts_read_hdm(address, data, size);
	if (res < OK) {
		log_info(1, "%s: Total Mutual CX read ERROR %08X\n", __func__,
			ERROR_ALLOC | ERROR_GET_FRAME);
		kfree(tot_ms_cx_data->node_data);
		tot_ms_cx_data->node_data = NULL;
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}

	tot_ms_cx_data->node_data_size = size / 2;
	tot_ms_cx_data->node_data = (short *)kmalloc(
					tot_ms_cx_data->node_data_size *
					(sizeof(short)), GFP_KERNEL);
	if (tot_ms_cx_data->node_data == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
		__func__, ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	for (i = 0; i < size; i += 2) {
		tot_ms_cx_data->node_data[j] = (short)((data[i + 1] << 8) +
						data[i]);
		j++;
	}

	log_info(1, "%s: Read Mutual Total CX data done!!\n", __func__);
	return OK;

}

/**
  * Perform all the steps to read the necessary info for TOT SS Initialization
  * data from the buffer and store it in a self_total_cx_data variable
  * @param type type of TOT MS Initialization data to read
  * @param tot_ss_cx_data pointer to a self_total_cx_data variable
  * which will contain the TOT MS initialization data
  * @return OK if success or an error code which specify the type of error
  */
int get_self_total_cx_data(u8 type, struct self_total_cx_data *tot_ss_cx_data)
{
	int res;
	u32 address = 0;
	u8 header_data[COMP_HEADER_SIZE] = { 0x00 };
	int size = 0;
	int i, j = 0;
	u8 *data = NULL;
	tot_ss_cx_data->ix_tx = NULL;
	tot_ss_cx_data->ix_rx = NULL;

	if (!(type == HDM_REQ_TOT_IX_SS_TOUCH ||
		type == HDM_REQ_TOT_IX_SS_TOUCH_IDLE)) {
		log_info(1,
			"%s: Choose a SS type of compensation data ERROR %08X\n",
			__func__, ERROR_OP_NOT_ALLOW | ERROR_GET_CX);
		return ERROR_OP_NOT_ALLOW | ERROR_GET_CX;
	}

	res = read_hdm_header(type, header_data);
	if (res < OK) {
		log_info(1, "%s: read hdm header error\n", __func__);
		return res | ERROR_GET_CX;
	}

	tot_ss_cx_data->header.force_node = header_data[4];
	tot_ss_cx_data->header.sense_node = header_data[5];
	tot_ss_cx_data->header.type = type;
	log_info(1, "%s: tx_count: %d rx_count: %d\n", __func__,
		tot_ss_cx_data->header.force_node,
		tot_ss_cx_data->header.sense_node);
	if (tot_ss_cx_data->header.force_node == 0x00 ||
		tot_ss_cx_data->header.sense_node == 0x00 ||
		tot_ss_cx_data->header.force_node == 0xFF ||
		tot_ss_cx_data->header.sense_node == 0xFF) {
		log_info(1, "%s: force/sense length cannot be empty.. Invalid sysn frame header\n");
		return ERROR_CH_LEN | ERROR_GET_CX;
	}

	size = (tot_ss_cx_data->header.force_node * 2) +
		(tot_ss_cx_data->header.sense_node * 2);
	data = (u8 *)kmalloc(size * (sizeof(u8)), GFP_KERNEL);
	if (data == NULL) {
		log_info(1, "%s: ERROR %08X\n", __func__,
		ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_CX;
	}

	address = FRAME_BUFFER_ADDR + COMP_HEADER_SIZE;
	log_info(1, "%s: compensation data address: 0x%04X, size: %d\n",
		__func__, address, size);

	res = fts_read_hdm(address, data, size);
	if (res < OK) {
		log_info(1, "%s: self cx read ERROR %08X\n", __func__,
			ERROR_ALLOC | ERROR_GET_FRAME);
		kfree(data);
		data = NULL;
		return ERROR_ALLOC | ERROR_GET_CX;
	}

	tot_ss_cx_data->ix_tx = (u16 *)kmalloc(tot_ss_cx_data->header.force_node
				 * (sizeof(u16)), GFP_KERNEL);
	if (tot_ss_cx_data->ix_tx == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
			__func__, ERROR_ALLOC);
		res = ERROR_ALLOC | ERROR_GET_CX;
		goto goto_end;
	}
	tot_ss_cx_data->ix_rx = (u16 *)kmalloc(tot_ss_cx_data->header.sense_node
				* (sizeof(u16)), GFP_KERNEL);
	if (tot_ss_cx_data->ix_rx == NULL) {
		log_info(1, "%s: can not allocate node_data... ERROR %08X",
			__func__, ERROR_ALLOC);
		res = ERROR_ALLOC | ERROR_GET_CX;
		goto goto_end;
	}

	for (i = 0; i < (tot_ss_cx_data->header.force_node * 2); i += 2) {
		tot_ss_cx_data->ix_tx[j] = (u16)((data[i + 1] << 8) + data[i]);
		j++;
	}

	j = 0;
	for (i = (tot_ss_cx_data->header.force_node * 2); i < size; i += 2) {
		tot_ss_cx_data->ix_rx[j] = (u16)((data[i + 1] << 8) + data[i]);
		j++;
	}

goto_end:
	if (data != NULL) {
		kfree(data);
		data = NULL;
	}
	if (res < OK) {
		if (tot_ss_cx_data->ix_tx != NULL) {
			kfree(tot_ss_cx_data->ix_tx);
			tot_ss_cx_data->ix_tx = NULL;
		}
		if (tot_ss_cx_data->ix_rx != NULL) {
			kfree(tot_ss_cx_data->ix_rx);
			tot_ss_cx_data->ix_rx = NULL;
		}
	} else
		log_info(1, "%s: Read Self CX data done!!\n", __func__);

	return res;

}
