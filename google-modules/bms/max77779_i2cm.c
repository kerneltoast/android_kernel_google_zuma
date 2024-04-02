// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>

#include "max77779_pmic.h"

#define DONEI_SET(v) (((v) << MAX77779_I2CM_INTERRUPT_DONEI_SHIFT) & \
		MAX77779_I2CM_INTERRUPT_DONEI_MASK)
#define DONEI_GET(v) (((v) & MAX77779_I2CM_INTERRUPT_DONEI_MASK) >> \
		MAX77779_I2CM_INTERRUPT_DONEI_SHIFT)
#define ERRI_SET(v) (((v) << MAX77779_I2CM_INTERRUPT_ERRI_SHIFT) & \
		MAX77779_I2CM_INTERRUPT_ERRI_MASK)
#define ERRI_GET(v) (((v) & MAX77779_I2CM_INTERRUPT_ERRI_MASK) >> \
		MAX77779_I2CM_INTERRUPT_ERRI_SHIFT)

#define DONEIM_SET(v) (((v) << MAX77779_I2CM_INTMASK_DONEIM_SHIFT) & \
		MAX77779_I2CM_INTMASK_DONEIM_MASK)
#define DONEIM_GET(v) (((v) & MAX77779_I2CM_INTMASK_DONEIM_MASK) >> \
		MAX77779_I2CM_INTMASK_DONEIM_SHIFT)
#define ERRIM_SET(v) (((v) << MAX77779_I2CM_INTMASK_ERRIM_SHIFT) & \
		MAX77779_I2CM_INTMASK_ERRIM_MASK)
#define ERRIM_GET(v) (((v) & MAX77779_I2CM_INTMASK_ERRIM_MASK) >> \
		MAX77779_I2CM_INTMASK_ERRIM_SHIFT)

#define ERROR_SET(v) (((v) << MAX77779_I2CM_STATUS_ERROR_SHIFT) & \
		MAX77779_I2CM_STATUS_ERROR_MASK)
#define ERROR_GET(v) (((v) & MAX77779_I2CM_STATUS_ERROR_MASK) >> \
		MAX77779_I2CM_STATUS_ERROR_SHIFT)

#define I2CEN_SET(v) (((v) << MAX77779_I2CM_CONTROL_I2CEN_SHIFT) & \
		MAX77779_I2CM_CONTROL_I2CEN_MASK)
#define I2CEN_GET(v) (((v) & MAX77779_I2CM_CONTROL_I2CEN_MASK) >> \
		MAX77779_I2CM_CONTROL_I2CEN_SHIFT)
#define CLOCK_SPEED_SET(v) (((v) << MAX77779_I2CM_CONTROL_CLOCK_SPEED_SHIFT) & \
		MAX77779_I2CM_CONTROL_CLOCK_SPEED_MASK)
#define CLOCK_SPEED_GET(v) (((v) & MAX77779_I2CM_CONTROL_CLOCK_SPEED_MASK) >> \
		MAX77779_I2CM_CONTROL_CLOCK_SPEED_SHIFT)

#define SID_SET(v) (((v) << MAX77779_I2CM_SLADD_SLAVE_ID_SHIFT) & \
		MAX77779_I2CM_SLADD_SLAVE_ID_MASK)
#define SID_GET(v) (((v) & MAX77779_I2CM_SLADD_SLAVE_ID_MASK) >> \
		MAX77779_I2CM_SLADD_SLAVE_ID_SHIFT)

#define TXCNT_SET(v) (((v) << MAX77779_I2CM_TXDATA_CNT_TXCNT_SHIFT) & \
		MAX77779_I2CM_TXDATA_CNT_TXCNT_MASK)
#define TXCNT_GET(v) (((v) & MAX77779_I2CM_TXDATA_CNT_TXCNT_MASK) >> \
		MAX77779_I2CM_TXDATA_CNT_TXCNT_SHIFT)

#define I2CMWRITE_SET(v) (((v) << MAX77779_I2CM_CMD_I2CMWRITE_SHIFT) & \
		MAX77779_I2CM_CMD_I2CMWRITE_MASK)
#define I2CMWRITE_GET(v) (((v) & MAX77779_I2CM_CMD_I2CMWRITE_MASK) >> \
		MAX77779_I2CM_CMD_I2CMWRITE_SHIFT)

#define I2CMREAD_SET(v) (((v) << MAX77779_I2CM_CMD_I2CMREAD_SHIFT) & \
		MAX77779_I2CM_CMD_I2CMREAD_MASK)
#define I2CMREAD_GET(v) (((v) & MAX77779_I2CM_CMD_I2CMREAD_MASK) >> \
		MAX77779_I2CM_CMD_I2CMREAD_SHIFT)

#define I2CM_ERR_ARBITRATION_LOSS(status_err)	(!!((status_err) & BIT(0)))
#define I2CM_ERR_TIMEOUT(status_err)		(!!((status_err) & BIT(1)))
#define I2CM_ERR_ADDRESS_NACK(status_err)	(!!((status_err) & BIT(2)))
#define I2CM_ERR_DATA_NACK(status_err)		(!!((status_err) & BIT(3)))
#define I2CM_ERR_RX_FIFO_NA(status_err)		(!!((status_err) & BIT(4)))
#define I2CM_ERR_START_OUT_SEQ(status_err)	(!!((status_err) & BIT(5)))
#define I2CM_ERR_STOP_OUT_SEQ(status_err)	(!!((status_err) & BIT(6)))

#define I2CM_MAX_REGISTER			MAX77779_I2CM_RX_BUFFER_31

#define MAX77779_TIMEOUT_DEFAULT		0xff
#define MAX77779_MAX_TIMEOUT			0xff
#define MAX77779_COMPLETION_TIMEOUT_MS_DEFAULT	20
#define MAX77779_MAX_SPEED			0x03
#define MAX77779_SPEED_DEFAULT			0x00

#define MAX77779_I2CM_MAX_WRITE \
		(MAX77779_I2CM_TX_BUFFER_33 - MAX77779_I2CM_TX_BUFFER_0 + 1)
#define MAX77779_I2CM_MAX_READ \
		(MAX77779_I2CM_RX_BUFFER_31 - MAX77779_I2CM_RX_BUFFER_0 + 1)

struct max77779_i2cm_info {
	struct i2c_adapter	adap;  /* bus */
	struct i2c_client	*client;
	struct device		*dev;
	struct regmap		*regmap;
	struct completion	xfer_done;
	unsigned int		timeout;
	unsigned int		completion_timeout_ms;
	unsigned int		speed;
	u8			reg_vals[I2CM_MAX_REGISTER + 1];
};

static int max77779_i2cm_done(struct max77779_i2cm_info *info,
		unsigned int *status)
{
	unsigned int timeout;

	timeout = msecs_to_jiffies(info->completion_timeout_ms);
	if (!wait_for_completion_timeout(&info->xfer_done, timeout)) {
		dev_err(info->dev, "Xfer timed out.\n");
		return -ETIMEDOUT;
	}

	return regmap_read(info->regmap, MAX77779_I2CM_STATUS, status);
}

static irqreturn_t max777x9_i2cm_irq(int irq, void *ptr)
{
	struct max77779_i2cm_info *info = ptr;
	unsigned int val;
	int err;

	err = regmap_read(info->regmap, MAX77779_I2CM_INTERRUPT, &val);
	if (err) {
		dev_err(info->dev, "Failed to read Interrupt (%d).\n",
				err);
		return IRQ_NONE;
	}
	if (DONEI_GET(val))
		complete(&info->xfer_done);

	/* clear interrupt */
	regmap_write(info->regmap, MAX77779_I2CM_INTERRUPT,
			ERRI_SET(1) | DONEI_SET(1));
	return IRQ_HANDLED;
}

static inline void set_regval(struct max77779_i2cm_info *info,
		unsigned int reg, unsigned int val)
{
	u8 val8 = (u8)val;

	if (reg > I2CM_MAX_REGISTER) {
		dev_err(info->dev, "reg too large %#04x\n", reg);
		return;
	}
	info->reg_vals[reg] = val8;
}

static int max77779_i2cm_xfer(struct i2c_adapter *adap,
		struct i2c_msg *msgs, int num_msgs)
{
	struct max77779_i2cm_info *info =
			container_of(adap, struct max77779_i2cm_info, adap);
	struct regmap *regmap = info->regmap;
	unsigned int txdata_cnt = 0;
	unsigned int tx_data_buffer = MAX77779_I2CM_TX_BUFFER_0;
	unsigned int rxdata_cnt = 0;
	unsigned int rx_data_buffer = MAX77779_I2CM_RX_BUFFER_0;
	unsigned int cmd = 0;
	int i, j;
	int err = 0;
	unsigned int status; /* result of status register */
	uint8_t status_err;

	set_regval(info, MAX77779_I2CM_INTERRUPT, DONEI_SET(1) | ERRI_SET(1));
	set_regval(info, MAX77779_I2CM_INTMASK, ERRIM_SET(0) | DONEIM_SET(0));
	set_regval(info, MAX77779_I2CM_TIMEOUT, info->timeout);
	set_regval(info, MAX77779_I2CM_CONTROL,
			I2CEN_SET(1) | CLOCK_SPEED_SET(info->speed));
	set_regval(info, MAX77779_I2CM_SLADD, SID_SET(msgs[0].addr));

	/* parse message into regval buffer */
	for (i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];

		if (msg->flags & I2C_M_RD) {
			rxdata_cnt += msg->len;
			if (rxdata_cnt  > MAX77779_I2CM_MAX_READ) {
				dev_err(info->dev, "read too large %d > %d\n",
						rxdata_cnt,
						MAX77779_I2CM_MAX_READ);
				return -EINVAL;
			}

			cmd |= I2CMREAD_SET(1);
		} else {
			txdata_cnt += msg->len;
			if (txdata_cnt  > MAX77779_I2CM_MAX_WRITE) {
				dev_err(info->dev, "write too large %d > %d\n",
						txdata_cnt,
						MAX77779_I2CM_MAX_WRITE);
				return -EINVAL;
			}
			cmd |= I2CMWRITE_SET(1);
			for (j = 0; j < msg->len; j++) {
				u8 buf = msg->buf[j];

				set_regval(info, tx_data_buffer, buf);
				tx_data_buffer++;
			}
		}
	}

	set_regval(info, MAX77779_I2CM_TXDATA_CNT, txdata_cnt);

	err = regmap_raw_write(regmap, MAX77779_I2CM_INTERRUPT,
			&info->reg_vals[MAX77779_I2CM_INTERRUPT],
			tx_data_buffer - MAX77779_I2CM_INTERRUPT);
	if (err) {
		dev_err(info->dev, "regmap_raw_write returned %d\n", err);
		goto xfer_done;
	}

	if (rxdata_cnt > 0) {
		err = regmap_write(regmap, MAX77779_I2CM_RXDATA_CNT,
				rxdata_cnt - 1);
		if (err) {
			dev_err(info->dev, "regmap_write returned %d\n", err);
			goto xfer_done;
		}
	}

	err = regmap_write(regmap, MAX77779_I2CM_CMD, cmd);
	if (err) {
		dev_err(info->dev, "regmap_write returned %d\n", err);
		goto xfer_done;
	}

	err = max77779_i2cm_done(info, &status);
	if (err)
		goto xfer_done;
	status_err = ERROR_GET(status);                 /* bit */
	if (I2CM_ERR_ADDRESS_NACK(status_err))          /*  2  */
		err = -ENXIO;
	else if (I2CM_ERR_DATA_NACK(status_err))        /*  3  */
		err = -ENXIO;
	else if (I2CM_ERR_RX_FIFO_NA(status_err))       /*  4  */
		err = -ENOBUFS;
	else if (I2CM_ERR_TIMEOUT(status_err))          /*  1  */
		err = -ETIMEDOUT;
	else if (I2CM_ERR_START_OUT_SEQ(status_err))    /*  5  */
		err = -EBADMSG;
	else if (I2CM_ERR_STOP_OUT_SEQ(status_err))     /*  6  */
		err = -EBADMSG;
	else if (I2CM_ERR_ARBITRATION_LOSS(status_err)) /*  0  */
		err = -EAGAIN;
	if (err) {
		dev_err(info->dev, "I2CM status Error (%#04x).\n", status_err);
		goto xfer_done;
	}

	if (!rxdata_cnt) /* nothing to read we are done. */
		goto xfer_done;

	err = regmap_raw_read(regmap, MAX77779_I2CM_RX_BUFFER_0,
			&info->reg_vals[MAX77779_I2CM_RX_BUFFER_0], rxdata_cnt);
	if (err) {
		dev_err(info->dev, "Error reading = %d\n", err);
		goto xfer_done;
	}

	rx_data_buffer = MAX77779_I2CM_RX_BUFFER_0;
	for (i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];

		if (msg->flags & I2C_M_RD) {
			for (j = 0; j < msg->len; j++) {
				msg->buf[j] = info->reg_vals[rx_data_buffer];
				rx_data_buffer++;
			}
		}
	}

xfer_done:
	set_regval(info, MAX77779_I2CM_INTERRUPT, DONEI_SET(1) | ERRI_SET(1));
	set_regval(info, MAX77779_I2CM_INTMASK, ERRIM_SET(1) | DONEIM_SET(1));

	regmap_raw_write(regmap, MAX77779_I2CM_INTERRUPT,
			&info->reg_vals[MAX77779_I2CM_INTERRUPT], 2);

	if (err) {
		dev_err(info->dev, "Xfer Error (%d)\n", err);
		return err;
	}

	return num_msgs;
}

static u32 max77779_i2cm_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA |
	       I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA|
	       I2C_FUNC_SMBUS_I2C_BLOCK |
	       I2C_FUNC_I2C;
}

static const struct i2c_algorithm max77779_i2cm_algorithm = {
	.master_xfer		= max77779_i2cm_xfer,
	.functionality		= max77779_i2cm_func,
};

static const struct regmap_config max77779_i2cm_regmap_cfg = {
	.name = "max77779_i2cm_regmap_cfg",
	.reg_bits = 8,
	.val_bits = 8,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = I2CM_MAX_REGISTER,
};

static const struct i2c_adapter_quirks max77779_i2cm_quirks = {
	.flags = I2C_AQ_COMB_WRITE_THEN_READ |
		 I2C_AQ_NO_ZERO_LEN |
		 I2C_AQ_NO_REP_START,
	.max_num_msgs = 2,
	.max_write_len = MAX77779_I2CM_MAX_WRITE,
	.max_read_len = MAX77779_I2CM_MAX_READ,
	.max_comb_1st_msg_len = MAX77779_I2CM_MAX_WRITE,
	.max_comb_2nd_msg_len = MAX77779_I2CM_MAX_READ
};

static int max77779_i2cm_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct max77779_i2cm_info *info = NULL;
	struct device *dev = &client->dev;
	int err = 0;

	if (client->irq < 0)
		return -EPROBE_DEFER;

	if (!IS_ENABLED(CONFIG_OF))
		return -EINVAL;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* Device Tree Setup */
	err = of_property_read_u32(dev->of_node, "max77779,timeout",
			&info->timeout);
	if (err || (info->timeout > MAX77779_MAX_TIMEOUT)) {
		dev_warn(dev, "Invalid max77779,timeout set to max.\n");
		info->timeout = MAX77779_TIMEOUT_DEFAULT;
	}

	err = of_property_read_u32(dev->of_node, "max77779,speed",
			&info->speed);
	if (err || (info->speed > MAX77779_MAX_SPEED)) {
		dev_warn(dev, "Invalid max77779,speed - set to min.\n");
		info->speed = MAX77779_SPEED_DEFAULT;
	}

	err = of_property_read_u32(dev->of_node,
			"max77779,completion_timeout_ms",
			&info->completion_timeout_ms);
	if (err)
		info->completion_timeout_ms =
			MAX77779_COMPLETION_TIMEOUT_MS_DEFAULT;

	/* setup data structures */
	info->client = client;
	info->dev = dev;
	i2c_set_clientdata(client, info);
	info->regmap = devm_regmap_init_i2c(client, &max77779_i2cm_regmap_cfg);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "Failed to initialize regmap.\n");
		return -EINVAL;
	}
	init_completion(&info->xfer_done);

	if (client->irq) {
		err = devm_request_threaded_irq(info->dev, client->irq, NULL,
				max777x9_i2cm_irq,
				IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_ONESHOT,
				"max777x9_i2cm", info);
		if (err < 0) {
			dev_err(dev, "Failed to get irq thread.\n");
		} else {
			/*
			* write I2CM_MASK to disable interrupts, they
			* will be enabled during xfer.
			*/
			err = regmap_write(info->regmap, MAX77779_I2CM_INTERRUPT,
					DONEI_SET(1) | ERRI_SET(1));
			if (err) {
				dev_err(dev, "Failed to setup interrupts.\n");
				return -EIO;
			}
		}
	}

	/* setup the adapter */
	strscpy(info->adap.name, "max77779-i2cm", sizeof(info->adap.name));
	info->adap.owner   = THIS_MODULE;
	info->adap.algo    = &max77779_i2cm_algorithm;
	info->adap.retries = 2;
	info->adap.class   = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	info->adap.dev.of_node = dev->of_node;
	info->adap.algo_data = info;
	info->adap.dev.parent = info->dev;
	info->adap.nr = -1;
	info->adap.quirks = &max77779_i2cm_quirks;

	err = i2c_add_numbered_adapter(&info->adap);
	if (err < 0)
		dev_err(dev, "failed to add bus to i2c core\n");

	return err;
}

static int max77779_i2cm_remove(struct i2c_client *client)
{
	struct max77779_i2cm_info *info = dev_get_drvdata(&(client->dev));

	devm_kfree(info->dev, info);
	return 0;
}

static const struct i2c_device_id id[] = {
	{ "max77779_i2cm", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id max77779_i2cm_match_table[] = {
	{ .compatible = "max77779_i2cm",},
	{ },
};
#endif

static struct i2c_driver driver = {
	.probe		= max77779_i2cm_probe,
	.remove		= max77779_i2cm_remove,
	.id_table	= id,
	.driver = {
		.name   = "max77779_i2cm",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = max77779_i2cm_match_table,
#endif
	},
};

static int __init max77779_i2cm_init(void)
{
	return i2c_add_driver(&driver);
}

static void __exit max77779_i2cm_exit(void)
{
	i2c_del_driver(&driver);
}

late_initcall(max77779_i2cm_init);
module_exit(max77779_i2cm_exit);

MODULE_DESCRIPTION("Maxim 77779 I2C Bridge Driver");
MODULE_AUTHOR("Jim Wylder <jwylder@google.com>");
MODULE_LICENSE("GPL");
