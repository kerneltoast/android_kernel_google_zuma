// SPDX-License-Identifier: GPL-2.0

/*
 * This file is part of the QM35 UCI stack for linux.
 *
 * Copyright (c) 2021 Qorvo US, Inc.
 *
 * This software is provided under the GNU General Public License, version 2
 * (GPLv2), as well as under a Qorvo commercial license.
 *
 * You may choose to use this software under the terms of the GPLv2 License,
 * version 2 ("GPLv2"), as published by the Free Software Foundation.
 * You should have received a copy of the GPLv2 along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * This program is distributed under the GPLv2 in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GPLv2 for more
 * details.
 *
 * If you cannot meet the requirements of the GPLv2, you may not use this
 * software for any purpose without first obtaining a commercial license from
 * Qorvo.
 * Please contact Qorvo to inquire about licensing terms.
 *
 * QM35 UCI over HSSPI protocol
 */

#include <linux/version.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#ifdef CONFIG_QM35_DEBOUNCE_TIME_US
#include <linux/ktime.h>
#endif

#include <qmrom.h>
#include <qmrom_spi.h>
#include <qmrom_log.h>
#include <spi_rom_protocol.h>

#include <fwupdater.h>

#include "qm35.h"
#include "qm35-sscd.h"
#include "uci_ioctls.h"
#include "hsspi.h"
#include "hsspi_uci.h"
#include "hsspi_test.h"

#define QM35_REGULATOR_DELAY_US 1000
#define QMROM_RETRIES 10

#ifndef CONFIG_FLASHING_RETRIES
#define CONFIG_FLASHING_RETRIES 10 /* Intentionally high value */
#endif

/* Redefine certificate error here since original definitions are separate */
#define REGULATORS_ENABLED(x) (x->vdd1 || x->vdd2 || x->vdd3 || x->vdd4)

#ifndef NO_UWB_HAL
#define NO_UWB_HAL false
#endif

static int qm_firmware_load(struct qm35_ctx *qm35_hdl);
static void qm35_regulators_set(struct qm35_ctx *qm35_hdl, bool on);

static const struct file_operations uci_fops;

static const struct of_device_id qm35_dt_ids[] = {
	{ .compatible = "qorvo,qm35" },
	{},
};
MODULE_DEVICE_TABLE(of, qm35_dt_ids);

static bool flash_on_probe = false;
module_param(flash_on_probe, bool, 0444);
MODULE_PARM_DESC(flash_on_probe, "Flash during the module probe");

int spi_speed_hz;
module_param(spi_speed_hz, int, 0644);
MODULE_PARM_DESC(spi_speed_hz, "SPI speed (if not set use DTS's one)");

static char *fwname = NULL;
module_param(fwname, charp, 0444);
MODULE_PARM_DESC(fwname, "Use fwname as firmware binary to flash QM35");

static bool wake_use_wakeup = true;
module_param(wake_use_wakeup, bool, 0644);
MODULE_PARM_DESC(wake_use_wakeup, "Use wakeup pin to wake up QM35");

static bool wake_use_csn = false;
module_param(wake_use_csn, bool, 0644);
MODULE_PARM_DESC(wake_use_csn, "Use HSSPI CSn pin to wake up QM35");

static bool wake_on_ssirq = true;
module_param(wake_on_ssirq, bool, 0644);
MODULE_PARM_DESC(wake_on_ssirq,
		 "Allow QM35 to wakeup the platform using ss_irq");

int trace_spi_xfers;
module_param(trace_spi_xfers, int, 0644);
MODULE_PARM_DESC(trace_spi_xfers, "Trace all the SPI transfers");

int qmrom_retries = QMROM_RETRIES;
module_param(qmrom_retries, int, 0644);
MODULE_PARM_DESC(qmrom_retries, "QMROM retries");

int flashing_retries = CONFIG_FLASHING_RETRIES;
module_param(flashing_retries, int, 0644);
MODULE_PARM_DESC(flashing_retries, "Flashing retries");

int reset_on_error = 1;
module_param(reset_on_error, int, 0644);
MODULE_PARM_DESC(reset_on_error, "Reset the QM35 on successive errors");

int log_qm_traces = 1;
module_param(log_qm_traces, int, 0444);
MODULE_PARM_DESC(log_qm_traces, "Logs the QM35 traces in the kernel messages");

int fu_spi_speed_hz;
module_param(fu_spi_speed_hz, int, 0644);
MODULE_PARM_DESC(fu_spi_speed_hz, "FW updater SPI speed");

int qmrom_spi_speed_hz;
module_param(qmrom_spi_speed_hz, int, 0644);
MODULE_PARM_DESC(qmrom_spi_speed_hz, "FW updater SPI speed");

static uint8_t qm_soc_id[QM357XX_ROM_SOC_ID_LEN];
static uint16_t qm_dev_id;

/*
 * uci_open() : open operation for uci device
 *
 */
static int uci_open(struct inode *inode, struct file *file)
{
	struct miscdevice *uci_dev = file->private_data;
	struct qm35_ctx *qm35_hdl =
		container_of(uci_dev, struct qm35_ctx, uci_dev);

	return hsspi_register(&qm35_hdl->hsspi, &qm35_hdl->uci_layer.hlayer);
}

/*
 * uci_ioctl() - ioctl operation for uci device.
 *
 */
static long uci_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{
	void __user *argp = (void __user *)args;
	struct miscdevice *uci_dev = filp->private_data;
	struct qm35_ctx *qm35_hdl =
		container_of(uci_dev, struct qm35_ctx, uci_dev);
	int ret;

	switch (cmd) {
	case QM35_CTRL_GET_STATE: {
		return copy_to_user(argp, &qm35_hdl->state,
				    sizeof(qm35_hdl->state)) ?
			       -EFAULT :
			       0;
	}
	case QM35_CTRL_RESET: {
		qm35_hsspi_stop(qm35_hdl);

		ret = qm35_reset(qm35_hdl, QM_RESET_LOW_MS, true);

		msleep(QM_BOOT_MS);

		qm35_hsspi_start(qm35_hdl);

		if (ret)
			return ret;

		return copy_to_user(argp, &qm35_hdl->state,
				    sizeof(qm35_hdl->state)) ?
			       -EFAULT :
			       0;
	}
	case QM35_CTRL_FW_UPLOAD: {
		qm35_hsspi_stop(qm35_hdl);

		ret = qm_firmware_load(qm35_hdl);

		msleep(QM_BOOT_MS);

		qm35_hsspi_start(qm35_hdl);

		if (ret)
			return ret;

		return copy_to_user(argp, &qm35_hdl->state,
				    sizeof(qm35_hdl->state)) ?
			       -EFAULT :
			       0;
	}
	case QM35_CTRL_POWER: {
		unsigned int on;

		ret = get_user(on, (unsigned int __user *)argp);
		if (ret)
			return ret;

		qm35_hsspi_stop(qm35_hdl);

		if (NO_UWB_HAL) {
			on = true;
			dev_warn(
				&qm35_hdl->spi->dev,
				"qm35_regulators_set always ON due to NO_UWB_HAL");
		}

		if (REGULATORS_ENABLED(qm35_hdl))
			qm35_regulators_set(qm35_hdl, on);

		/*
		 * Always reset QM as regulators could be shared with
		 * other devices and power may not be controlled as
		 * expected
		 */
		qm35_reset(qm35_hdl, QM_RESET_LOW_MS, on);

		msleep(QM_BOOT_MS);

		/* If reset or power on */
		if (!REGULATORS_ENABLED(qm35_hdl) ||
		    (REGULATORS_ENABLED(qm35_hdl) && on))
			qm35_hsspi_start(qm35_hdl);

		return 0;
	}
	default:
		dev_err(&qm35_hdl->spi->dev, "unknown ioctl %x to %s device\n",
			cmd, qm35_hdl->uci_dev.name);
		return -EINVAL;
	}
}

/*
 * uci_release() - release operation for uci device.
 *
 */
static int uci_release(struct inode *inode, struct file *filp)
{
	struct miscdevice *uci_dev = filp->private_data;
	struct qm35_ctx *qm35_hdl =
		container_of(uci_dev, struct qm35_ctx, uci_dev);

	hsspi_unregister(&qm35_hdl->hsspi, &qm35_hdl->uci_layer.hlayer);
	return 0;
}

static ssize_t uci_read(struct file *filp, char __user *buf, size_t len,
			loff_t *off)
{
	struct miscdevice *uci_dev = filp->private_data;
	struct qm35_ctx *qm35_hdl =
		container_of(uci_dev, struct qm35_ctx, uci_dev);
	struct uci_packet *p;
	int ret;

	p = uci_layer_read(&qm35_hdl->uci_layer, len,
			   filp->f_flags & O_NONBLOCK);
	if (IS_ERR(p))
		return PTR_ERR(p);

	ret = copy_to_user(buf, p->data, p->length);
	if (!ret)
		ret = p->length;

	uci_packet_free(p);
	return ret;
}

static ssize_t uci_write(struct file *filp, const char __user *buf, size_t len,
			 loff_t *off)
{
	struct miscdevice *uci_dev = filp->private_data;
	struct qm35_ctx *qm35_hdl =
		container_of(uci_dev, struct qm35_ctx, uci_dev);
	struct uci_packet *p;
	DECLARE_COMPLETION_ONSTACK(comp);
	int ret;

	if (len > U16_MAX)
		return -EINVAL;

	p = uci_packet_alloc(len);
	if (!p)
		return -ENOMEM;

	p->write_done = &comp;

	if (copy_from_user(p->data, buf, len)) {
		ret = -EFAULT;
		goto free;
	}

	ret = hsspi_send(&qm35_hdl->hsspi, &qm35_hdl->uci_layer.hlayer,
			 &p->blk);
	if (ret)
		goto free;

	wait_for_completion(&comp);

	ret = p->status ? p->status : len;
free:
	uci_packet_free(p);
	return ret;
}

static __poll_t uci_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct miscdevice *uci_dev = filp->private_data;
	struct qm35_ctx *qm35_ctx =
		container_of(uci_dev, struct qm35_ctx, uci_dev);
	__poll_t mask = 0;

	poll_wait(filp, &qm35_ctx->uci_layer.wq, wait);

	if (uci_layer_has_data_available(&qm35_ctx->uci_layer))
		mask |= EPOLLIN;

	return mask;
}

static const struct file_operations uci_fops = {
	.owner = THIS_MODULE,
	.open = uci_open,
	.release = uci_release,
	.unlocked_ioctl = uci_ioctl,
	.read = uci_read,
	.write = uci_write,
	.poll = uci_poll,
};

static irqreturn_t qm35_irq_handler(int irq, void *qm35_ctx)
{
	struct qm35_ctx *qm35_hdl = qm35_ctx;

	spin_lock(&qm35_hdl->lock);
	qm35_hdl->state = QM35_CTRL_STATE_READY;
	spin_unlock(&qm35_hdl->lock);

	disable_irq_nosync(irq);

	hsspi_set_output_data_waiting(&qm35_hdl->hsspi);

	return IRQ_HANDLED;
}

static void reenable_ss_irq(struct hsspi *hsspi)
{
	struct qm35_ctx *qm35_hdl = container_of(hsspi, struct qm35_ctx, hsspi);

	enable_irq(qm35_hdl->spi->irq);
}

static irqreturn_t qm35_ss_rdy_handler(int irq, void *data)
{
	struct qm35_ctx *qm35_hdl = data;
#ifdef CONFIG_QM35_DEBOUNCE_TIME_US
	static ktime_t old_time;
	ktime_t current_time;

	current_time = ktime_get();

	if (ktime_after(ktime_add(old_time,
				  CONFIG_QM35_DEBOUNCE_TIME_US * 1000),
			current_time))
		return IRQ_HANDLED;

	old_time = current_time;
#endif
	/* Should be low already but in case we just woke up QM */
	if (wake_use_csn)
		gpiod_set_value(qm35_hdl->gpio_csn, 0);
	if (wake_use_wakeup)
		gpiod_set_value(qm35_hdl->gpio_wakeup, 0);

	if (!qm35_hdl->flashing) {
		hsspi_set_spi_slave_ready_irq(&qm35_hdl->hsspi);
	} else {
		spin_lock(&qm35_hdl->lock);
		qm35_hdl->qmrom_qm_ready = true;
		spin_unlock(&qm35_hdl->lock);
		wake_up_interruptible(&qm35_hdl->qmrom_wq_ready);
	}

	return IRQ_HANDLED;
}

static void qm35_wakeup(struct hsspi *hsspi)
{
	struct qm35_ctx *qm35_hdl = container_of(hsspi, struct qm35_ctx, hsspi);

	if (wake_use_csn)
		gpiod_set_value(qm35_hdl->gpio_csn, 1);
	if (wake_use_wakeup)
		gpiod_set_value(qm35_hdl->gpio_wakeup, 1);

	/* The wake up will be cleared only when ss-rdy is raised again */
}

static void qm35_reset_hook(struct hsspi *hsspi)
{
	struct qm35_ctx *qm35_hdl = container_of(hsspi, struct qm35_ctx, hsspi);

	if (reset_on_error)
		qm35_reset(qm35_hdl, QM_RESET_LOW_MS, true);
	usleep_range(QM_BEFORE_RESET_MS * 1000, QM_BEFORE_RESET_MS * 1000);
}

static irqreturn_t qm35_exton_handler(int irq, void *data)
{
	struct qm35_ctx *qm35_hdl = data;

	hsspi_clear_spi_slave_ready(&qm35_hdl->hsspi);

	if (qm35_hdl->hsspi.waiting_ss_rdy)
		qm35_wakeup(&qm35_hdl->hsspi);

	return IRQ_HANDLED;
}

void qm35_hsspi_start(struct qm35_ctx *qm35_hdl)
{
	/* nothing to do as HSSPI is already started */
	if (qm35_hdl->hsspi.state == HSSPI_RUNNING)
		return;

	enable_irq(qm35_hdl->ss_rdy_irq);
#ifdef CONFIG_QM35_RISING_IRQ_NOT_TRIGGERED
	/* Some IRQ controller will trigger a rising edge if
	 * the gpio is high when enabling the IRQ, some will
	 * not (RPI board for example). In the second case we
	 * can miss an event depending on if the level rise
	 * before or after enable_irq. Besides, the handler
	 * can also run *after* hsspi_start, breaking the
	 * hsspi thread with a false information. So let's
	 * sleep and force the SS_READY bit after.
	 */

	if (gpiod_get_value(qm35_hdl->gpio_ss_rdy))
		hsspi_set_spi_slave_ready(&qm35_hdl->hsspi);
#endif

	hsspi_start(&qm35_hdl->hsspi);
}

void qm35_hsspi_stop(struct qm35_ctx *qm35_hdl)
{
	/* nothing to do as HSSPI is already stopped */
	if (qm35_hdl->hsspi.state == HSSPI_STOPPED)
		return;

	hsspi_stop(&qm35_hdl->hsspi);

	disable_irq_nosync(qm35_hdl->ss_rdy_irq);

	clear_bit(HSSPI_FLAGS_SS_READY, qm35_hdl->hsspi.flags);
}

int qm35_reset_sync(struct qm35_ctx *qm35_hdl)
{
	int ret;

	qm35_hsspi_stop(qm35_hdl);
	ret = qm35_reset(qm35_hdl, QM_RESET_LOW_MS, true);
	msleep(QM_BOOT_MS);
	qm35_hsspi_start(qm35_hdl);

	return ret;
}

static int qm_firmware_flash_fw(struct qm35_ctx *qm35_hdl,
				struct qmrom_handle *h,
				const struct firmware *fw)
{
	int rc = 0, nb_retries = flashing_retries;
	do {
		/* If the previous flashing failed, re-enter the QM
		 * rom code so it will be in the same initial state
		 */
		if (rc)
			qmrom_reboot_bootloader(h);

		rc = qm357xx_rom_flash_fw(h, fw);
		if (rc)
			dev_err(&qm35_hdl->spi->dev,
				"Attempt %d: ROM flashing failed with %d!\n",
				flashing_retries - nb_retries, rc);
		if (rc == PEG_ERR_FIRST_KEY_CERT_OR_FW_VER)
			break;
	} while (rc && --nb_retries > 0);
	return rc;
}

static int qm_firmware_flash_macro_pkg(struct qm35_ctx *qm35_hdl,
				       struct qmrom_handle *h,
				       const struct firmware *fw)
{
	int rc, nb_retries = flashing_retries;
	const uint8_t *fw_data;
	uint32_t fw_size;

	rc = qm357xx_rom_fw_macro_pkg_get_fw_idx(fw, 1, &fw_size, &fw_data);
	if (rc) {
		dev_err(&qm35_hdl->spi->dev,
			"FW MACRO PACKAGE corrupted = %d\n", rc);
		return rc;
	}
	if (*(uint32_t *)fw_data != CRYPTO_FIRMWARE_PACK_MAGIC_VALUE) {
		rc = -EINVAL;
		dev_err(&qm35_hdl->spi->dev,
			"FW PACKAGE not found - magic is %04x, size is %d\n",
			*(uint32_t *)fw_data, fw_size);
		return rc;
	}

	do {
		/* If the previous flashing failed, re-enter the QM
		 * rom code so it will be in the same initial state
		 */
		if (spi_speed_hz)
			qmrom_spi_set_freq(spi_speed_hz);
		else
			qmrom_spi_set_freq(DEFAULT_SPI_CLOCKRATE);

		if (rc)
			qmrom_reboot_bootloader(h);

		/* The Macro Package contain the fw updater and the package
		 * simply flash the first and provide the second to
		 * the updater lib
		 */
		h->skip_check_fw_boot = true;
		rc = qm357xx_rom_flash_fw(h, fw);
		h->skip_check_fw_boot = false;
		if (rc) {
			dev_err(&qm35_hdl->spi->dev,
				"Attempt %d: fw updater flashing failed with %d!\n",
				flashing_retries - nb_retries, rc);
			if (rc == PEG_ERR_FIRST_KEY_CERT_OR_FW_VER)
				break;
			continue;
		}

		/* Now flash the firmware proper */
		if (fu_spi_speed_hz)
			qmrom_spi_set_freq(fu_spi_speed_hz);
		else
			qmrom_spi_set_freq(FWUPDATER_SPI_SPEED_HZ);
		rc = run_fwupdater(h, fw_data, fw_size);
		if (rc) {
			dev_err(&qm35_hdl->spi->dev,
				"Attempt %d: fw app flashing failed with %d!\n",
				flashing_retries - nb_retries, rc);
		}
	} while (rc && --nb_retries > 0);
	return rc;
}

static int qm_firmware_flashing(void *handle, struct qmrom_handle *h,
				bool *is_macro_pkg, bool use_prod_fw)
{
	struct qm35_ctx *qm35_hdl = (struct qm35_ctx *)handle;
	struct spi_device *spi = qm35_hdl->spi;
	int rc = 0;
	const struct firmware *fw;
#ifdef C0_WRITE_STATS
	uint64_t elapsed_time_ns;
	ktime_t start_time;
#endif

	fw = qmrom_spi_get_firmware(&spi->dev, h, is_macro_pkg, use_prod_fw);
	if (fw == NULL) {
		dev_err(&spi->dev, "Firmware file not present!\n");
		return -1;
	}

#ifdef C0_WRITE_STATS
	start_time = ktime_get();
#endif

	qm35_hdl->flashing = true;
	if (!*is_macro_pkg)
		rc = qm_firmware_flash_fw(qm35_hdl, h, fw);
	else
		rc = qm_firmware_flash_macro_pkg(qm35_hdl, h, fw);

#ifdef C0_WRITE_STATS
	elapsed_time_ns = ktime_to_ns(ktime_sub(ktime_get(), start_time));
	if (!rc)
		dev_warn(&spi->dev, "Firmware flashed in %llu us\n",
			 div_u64(elapsed_time_ns, 1000));
#endif

	qmrom_spi_release_firmware(fw);
	qm35_hdl->flashing = false;

	/* Reset the device anyway */
	qmrom_spi_reset_device(qm35_hdl);
	return rc;
}

static int qm_firmware_load(struct qm35_ctx *qm35_hdl)
{
	struct spi_device *spi = qm35_hdl->spi;
	unsigned int state = qm35_get_state(qm35_hdl);
	struct qmrom_handle *h;
	bool is_macro_pkg = false;
	int ret;

	qm35_set_state(qm35_hdl, QM35_CTRL_STATE_FW_DOWNLOADING);

	qmrom_set_log_device(&spi->dev, LOG_WARN);

	enable_irq(qm35_hdl->ss_rdy_irq);

	qm35_hdl->flashing = true;

	h = qmrom_init(&spi->dev, qm35_hdl, qm35_hdl, qm35_hdl->gpio_ss_irq,
		       qmrom_spi_speed_hz, qmrom_retries,
		       qmrom_spi_reset_device, DEVICE_GEN_QM357XX);
	if (!h) {
		pr_err("qmrom_init failed\n");
		ret = -1;
		goto out;
	}

	qm35_hdl->flashing = false;

	dev_info(&spi->dev, "    chip_ver:  %x\n", h->chip_rev);
	dev_info(&spi->dev, "    dev_id:    deca%04x\n", h->device_version);

	if (h->chip_rev != CHIP_REVISION_A0) {
		dev_info(&spi->dev, "    soc_id:    %*phN\n",
			 QM357XX_ROM_SOC_ID_LEN, h->qm357xx_soc_info.soc_id);
		dev_info(&spi->dev, "    uuid:      %*phN\n",
			 QM357XX_ROM_UUID_LEN, h->qm357xx_soc_info.uuid);
		dev_info(&spi->dev, "    lcs_state: %u\n",
			 h->qm357xx_soc_info.lcs_state);

		memcpy(&qm_dev_id, &h->device_version, sizeof(qm_dev_id));
		memcpy(qm_soc_id, h->qm357xx_soc_info.soc_id,
		       QM357XX_ROM_SOC_ID_LEN);

		debug_soc_info_available(&qm35_hdl->debug);
	} else {
		dev_dbg(&spi->dev,
			"SoC info not supported on chip revision A0\n");
	}

	dev_dbg(&spi->dev, "Starting device flashing!\n");
	ret = qm_firmware_flashing(qm35_hdl, h, &is_macro_pkg, true);
	if (ret) {
		qmrom_reboot_bootloader(h);
		ret = qm_firmware_flashing(qm35_hdl, h, &is_macro_pkg, false);
	}

	if (ret)
		dev_err(&spi->dev, "Firmware download failed with %d!\n", ret);
	else
		dev_info(&spi->dev, "Device flashing succeeded!\n");

out:
	disable_irq_nosync(qm35_hdl->ss_rdy_irq);

	qm35_set_state(qm35_hdl, state);

	qm35_hdl->flashing = false;

	return ret;
}

int qm_get_dev_id(struct qm35_ctx *qm35_hdl, uint16_t *dev_id)
{
	memcpy(dev_id, &qm_dev_id, sizeof(qm_dev_id));

	return 0;
}

int qm_get_soc_id(struct qm35_ctx *qm35_hdl, uint8_t *soc_id)
{
	memcpy(soc_id, qm_soc_id, QM357XX_ROM_SOC_ID_LEN);

	return 0;
}

/**
 * hsspi_irqs_setup() - setup all irqs needed by HSSPI
 * @qm35_ctx: pointer to &struct qm35_ctx
 *
 * SS_IRQ
 * ------
 *
 * If `ss-irq-gpios` exists in the DTS, it is used. If not, it's using
 * the `interrupts` definition from the SPI device.
 *
 * SS_READY
 * --------
 *
 * The `ss-ready-gpios` is mandatory. It is used by the FW to signal
 * the driver that it can handle a SPI transaction.
 *
 * Return: 0 if no error, -errno otherwise
 */
static int hsspi_irqs_setup(struct qm35_ctx *qm35_ctx)
{
	int ret, irq;

	/* Get READY GPIO */
	qm35_ctx->gpio_ss_rdy =
		devm_gpiod_get(&qm35_ctx->spi->dev, "ss-ready", GPIOD_IN);
	if (IS_ERR(qm35_ctx->gpio_ss_rdy))
		return PTR_ERR(qm35_ctx->gpio_ss_rdy);

	irq = gpiod_to_irq(qm35_ctx->gpio_ss_rdy);
	if (irq < 0) {
		dev_err(&qm35_ctx->spi->dev,
			"gpiod_to_irq(ss-ready) returns %d", irq);
		return irq;
	}
	qm35_ctx->ss_rdy_irq = irq;
	ret = devm_request_irq(&qm35_ctx->spi->dev, irq, &qm35_ss_rdy_handler,
			       IRQF_TRIGGER_RISING, "hsspi-ss-rdy", qm35_ctx);
	if (ret) {
		dev_err(&qm35_ctx->spi->dev, "%s: devm_request_irq returned %d",
			__func__, ret);
		return ret;
	}
	/* devm_request_irq enables the interrupt. On probe, the hsspi is in
	 * HSSPI_STOPPED state by default. In this state, the interrupt is
	 * expected to be disabled. */
	disable_irq(irq);

	/* get SS_IRQ GPIO */
	qm35_ctx->gpio_ss_irq =
		devm_gpiod_get(&qm35_ctx->spi->dev, "ss-irq", GPIOD_IN);
	if (IS_ERR(qm35_ctx->gpio_ss_irq)) {
		dev_err(&qm35_ctx->spi->dev,
			"gpiod_get_index(ss-irq) returned %pK",
			qm35_ctx->gpio_ss_irq);
	}

	qm35_ctx->spi->irq = gpiod_to_irq(qm35_ctx->gpio_ss_irq);
	if (qm35_ctx->spi->irq < 0) {
		dev_err(&qm35_ctx->spi->dev, "gpiod_to_irq(ss-irq) returned %d",
			qm35_ctx->spi->irq);
		return qm35_ctx->spi->irq;
	}
	ret = devm_request_irq(&qm35_ctx->spi->dev, qm35_ctx->spi->irq,
			       &qm35_irq_handler, IRQF_TRIGGER_HIGH,
			       "hsspi-ss-irq", qm35_ctx);
	if (ret) {
		dev_err(&qm35_ctx->spi->dev, "%s: devm_request_irq returned %d",
			__func__, ret);
		return ret;
	}

	if (wake_on_ssirq) {
		ret = enable_irq_wake(qm35_ctx->spi->irq);
		if (ret) {
			return ret;
		}
	}

	qm35_ctx->hsspi.odw_cleared = reenable_ss_irq;
	qm35_ctx->hsspi.wakeup = qm35_wakeup;
	qm35_ctx->hsspi.reset_qm35 = qm35_reset_hook;

	/* Get exton */
	qm35_ctx->gpio_exton =
		devm_gpiod_get_optional(&qm35_ctx->spi->dev, "exton", GPIOD_IN);
	if (qm35_ctx->gpio_exton) {
		if (IS_ERR(qm35_ctx->gpio_exton))
			return PTR_ERR(qm35_ctx->gpio_exton);

		irq = gpiod_to_irq(qm35_ctx->gpio_exton);
		if (irq < 0) {
			dev_err(&qm35_ctx->spi->dev,
				"gpiod_to_irq(exton) returned %d", irq);
			return irq;
		}

		ret = devm_request_irq(&qm35_ctx->spi->dev, irq,
				       &qm35_exton_handler,
				       IRQF_TRIGGER_FALLING, "hsspi-exton",
				       qm35_ctx);
		if (ret)
			return ret;

		if (!gpiod_get_value(qm35_ctx->gpio_exton))
			hsspi_clear_spi_slave_ready(&qm35_ctx->hsspi);
	}

	/* Get spi csn */
	if (wake_use_csn) {
		qm35_ctx->gpio_csn = devm_gpiod_get(&qm35_ctx->spi->dev, "csn",
						    GPIOD_OUT_HIGH);
		if (IS_ERR(qm35_ctx->gpio_csn))
			return PTR_ERR(qm35_ctx->gpio_csn);
	}

	/* Get wakeup */
	if (wake_use_wakeup) {
		qm35_ctx->gpio_wakeup = devm_gpiod_get(&qm35_ctx->spi->dev,
						       "wakeup", GPIOD_OUT_LOW);
		if (IS_ERR(qm35_ctx->gpio_wakeup))
			return PTR_ERR(qm35_ctx->gpio_wakeup);
	}

	return 0;
}

static int qm35_regulator_set_one(struct regulator *reg, bool on)
{
	if (!reg)
		return 0;

	return on ? regulator_enable(reg) : regulator_disable(reg);
}

static void qm35_regulators_set(struct qm35_ctx *qm35_hdl, bool on)
{
	static const char *str_fmt = "failed to %s %s regulator: %d\n";
	struct device *dev = &qm35_hdl->spi->dev;
	const char *on_str = on ? "enable" : "disable";
	bool is_enabled;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&qm35_hdl->lock, flags);

	is_enabled = qm35_hdl->regulators_enabled;
	qm35_hdl->regulators_enabled = on;

	spin_unlock_irqrestore(&qm35_hdl->lock, flags);

	/* nothing to do we are already in the desired state */
	if (is_enabled == on)
		return;

	ret = qm35_regulator_set_one(qm35_hdl->vdd1, on);
	if (ret)
		dev_err(dev, str_fmt, on_str, "vdd1", ret);

	ret = qm35_regulator_set_one(qm35_hdl->vdd2, on);
	if (ret)
		dev_err(dev, str_fmt, on_str, "vdd2", ret);

	ret = qm35_regulator_set_one(qm35_hdl->vdd3, on);
	if (ret)
		dev_err(dev, str_fmt, on_str, "vdd3", ret);

	ret = qm35_regulator_set_one(qm35_hdl->vdd4, on);
	if (ret)
		dev_err(dev, str_fmt, on_str, "vdd4", ret);

	/* wait for regulator stabilization */
	usleep_range(QM35_REGULATOR_DELAY_US, QM35_REGULATOR_DELAY_US + 100);
}

static void qm35_regulators_setup_one(struct regulator **reg,
				      struct device *dev, const char *name)
{
	static const char *str_fmt =
		"%s regulator not defined in device tree: %d\n";
	struct regulator *tmp;

	tmp = devm_regulator_get_optional(dev, name);
	if (IS_ERR(tmp)) {
		dev_notice(dev, str_fmt, name, PTR_ERR(tmp));
		tmp = NULL;
	}
	*reg = tmp;
}

static void qm35_regulators_setup(struct qm35_ctx *qm35_hdl)
{
	struct device *dev = &qm35_hdl->spi->dev;

	qm35_regulators_setup_one(&qm35_hdl->vdd1, dev, "qm35-vdd1");
	qm35_regulators_setup_one(&qm35_hdl->vdd2, dev, "qm35-vdd2");
	qm35_regulators_setup_one(&qm35_hdl->vdd3, dev, "qm35-vdd3");
	qm35_regulators_setup_one(&qm35_hdl->vdd4, dev, "qm35-vdd4");

	qm35_hdl->regulators_enabled = false;
	qm35_hdl->state = QM35_CTRL_STATE_RESET;
}

static int qm35_probe(struct spi_device *spi)
{
	struct qm35_ctx *qm35_ctx;
	struct miscdevice *uci_misc;
	int ret = 0;

	if (fwname) {
		qmrom_set_fwname(fwname);
	}

	if (spi_speed_hz) {
		spi->max_speed_hz = spi_speed_hz;

		ret = spi_setup(spi);
		if (ret) {
			dev_err(&spi->dev,
				"spi_setup: requested spi speed=%d ret=%d\n",
				spi_speed_hz, ret);
			return ret;
		}
	}

	qm35_ctx = devm_kzalloc(&spi->dev, sizeof(*qm35_ctx), GFP_KERNEL);
	if (!qm35_ctx)
		return -ENOMEM;

	qm35_ctx->spi = spi;
	qm35_ctx->log_qm_traces = log_qm_traces;
	spin_lock_init(&qm35_ctx->lock);
	init_waitqueue_head(&qm35_ctx->qmrom_wq_ready);

	spi_set_drvdata(spi, qm35_ctx);

	qm35_ctx->gpio_reset =
		devm_gpiod_get_optional(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(qm35_ctx->gpio_reset)) {
		ret = PTR_ERR(qm35_ctx->gpio_reset);
		return ret;
	}

	qm35_regulators_setup(qm35_ctx);

	uci_misc = &qm35_ctx->uci_dev;
	uci_misc->minor = MISC_DYNAMIC_MINOR;
	uci_misc->name = UCI_DEV_NAME;
	uci_misc->fops = &uci_fops;
	uci_misc->parent = &spi->dev;

	/* we need the debugfs root initialized here to be able
	 * to display the soc info populated if flash_on_probe
	 * is set for chips different than A0
	 */
	ret = debug_init_root(&qm35_ctx->debug, NULL);
	if (ret) {
		debug_deinit(&qm35_ctx->debug);
		goto poweroff;
	}

	ret = hsspi_init(&qm35_ctx->hsspi, spi);
	if (ret)
		goto poweroff;

	ret = uci_layer_init(&qm35_ctx->uci_layer);
	if (ret)
		goto hsspi_deinit;

	ret = debug_init(&qm35_ctx->debug);
	if (ret)
		goto uci_layer_deinit;

	ret = hsspi_test_init(&qm35_ctx->hsspi);
	if (ret)
		goto debug_deinit;

	ret = coredump_layer_init(&qm35_ctx->coredump_layer, &qm35_ctx->debug);
	if (ret)
		goto hsspi_test_deinit;

	ret = log_layer_init(&qm35_ctx->log_layer, &qm35_ctx->debug);
	if (ret)
		goto coredump_layer_deinit;

	ret = hsspi_register(&qm35_ctx->hsspi,
			     &qm35_ctx->coredump_layer.hlayer);
	if (ret)
		goto log_layer_deinit;

	ret = hsspi_register(&qm35_ctx->hsspi, &qm35_ctx->log_layer.hlayer);
	if (ret)
		goto coredump_layer_unregister;

	msleep(QM_BOOT_MS);

	ret = hsspi_irqs_setup(qm35_ctx);
	if (ret)
		goto log_layer_unregister;

	if (flash_on_probe) {
		qm35_regulators_set(qm35_ctx, true);
		ret = qm_firmware_load(qm35_ctx);
		qm35_regulators_set(qm35_ctx, false);
		if (ret)
			goto log_layer_unregister;
	}

	hsspi_set_gpios(&qm35_ctx->hsspi, qm35_ctx->gpio_ss_rdy,
			qm35_ctx->gpio_exton);

	if (!NO_UWB_HAL) {
		/* If regulators not available, QM is powered on */
		if (!REGULATORS_ENABLED(qm35_ctx))
			qm35_hsspi_start(qm35_ctx);
	} else {
		qm35_regulators_set(qm35_ctx, true);
		usleep_range(100000, 100000);
		qm35_reset(qm35_ctx, QM_RESET_LOW_MS, true);
		msleep(QM_BOOT_MS);
		qm35_hsspi_start(qm35_ctx);
	}

	ret = misc_register(&qm35_ctx->uci_dev);
	if (ret) {
		dev_err(&spi->dev, "Failed to register uci device\n");
		goto log_layer_unregister;
	}

	ret = qm35_register_coredump(qm35_ctx);
	if (ret)
		dev_warn(&spi->dev, "SSCD registration failed: %d\n", ret);

	dev_info(&spi->dev, "Registered: [%s] misc device\n", uci_misc->name);

	dev_info(&spi->dev, "QM35 spi driver version " DRV_VERSION " probed\n");
	return 0;

log_layer_unregister:
	hsspi_unregister(&qm35_ctx->hsspi, &qm35_ctx->log_layer.hlayer);
coredump_layer_unregister:
	hsspi_unregister(&qm35_ctx->hsspi, &qm35_ctx->coredump_layer.hlayer);
log_layer_deinit:
	log_layer_deinit(&qm35_ctx->log_layer);
coredump_layer_deinit:
	coredump_layer_deinit(&qm35_ctx->coredump_layer);
hsspi_test_deinit:
	hsspi_test_deinit(&qm35_ctx->hsspi);
debug_deinit:
	debug_deinit(&qm35_ctx->debug);
uci_layer_deinit:
	uci_layer_deinit(&qm35_ctx->uci_layer);
hsspi_deinit:
	hsspi_deinit(&qm35_ctx->hsspi);
poweroff:
	qm35_regulators_set(qm35_ctx, false);
	return ret;
}

static void qm35_remove(struct spi_device *spi)
{
	struct qm35_ctx *qm35_hdl = spi_get_drvdata(spi);

	misc_deregister(&qm35_hdl->uci_dev);

	qm35_hsspi_stop(qm35_hdl);

	hsspi_unregister(&qm35_hdl->hsspi, &qm35_hdl->log_layer.hlayer);
	hsspi_unregister(&qm35_hdl->hsspi, &qm35_hdl->coredump_layer.hlayer);

	log_layer_deinit(&qm35_hdl->log_layer);
	coredump_layer_deinit(&qm35_hdl->coredump_layer);
	hsspi_test_deinit(&qm35_hdl->hsspi);
	debug_deinit(&qm35_hdl->debug);
	uci_layer_deinit(&qm35_hdl->uci_layer);

	qm35_unregister_coredump(qm35_hdl);

	hsspi_deinit(&qm35_hdl->hsspi);

	qm35_regulators_set(qm35_hdl, false);

	dev_info(&spi->dev, "Deregistered: [%s] misc device\n",
		 qm35_hdl->uci_dev.name);
}

#ifdef CONFIG_PM_SLEEP
static int qm35_pm_suspend(struct device *dev)
{
	struct qm35_ctx *qm35_hdl = dev_get_drvdata(dev);

	qm35_hsspi_stop(qm35_hdl);

	return 0;
}

static int qm35_pm_resume(struct device *dev)
{
	struct qm35_ctx *qm35_hdl = dev_get_drvdata(dev);

	qm35_hsspi_start(qm35_hdl);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(qm35_spi_ops, qm35_pm_suspend, qm35_pm_resume);

static struct spi_driver qm35_spi_driver = {
	.driver = {
		.name           = "qm35",
		.of_match_table = of_match_ptr(qm35_dt_ids),
		.pm = pm_sleep_ptr(&qm35_spi_ops),
	},
	.probe =	qm35_probe,
	.remove =	qm35_remove,
};
module_spi_driver(qm35_spi_driver);

MODULE_AUTHOR("Qorvo US, Inc.");
MODULE_DESCRIPTION("QM35 SPI device interface");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
