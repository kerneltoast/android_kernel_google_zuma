// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of the QM35 UCI stack for linux.
 *
 * Copyright (c) 2022 Qorvo US, Inc.
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
 * QM35 LOG layer HSSPI Protocol
 */

#include <linux/version.h>
#include <linux/printk.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <linux/kernel_read_file.h>
#endif
#include <linux/poll.h>
#include <linux/vmalloc.h>

#include <qmrom.h>
#include <qmrom_log.h>
#include <qmrom_spi.h>

#include "qm35.h"
#include "debug.h"

#define QMROM_RETRIES 10

static void *priv_from_file(const struct file *filp)
{
	return filp->f_path.dentry->d_inode->i_private;
}

static struct qm35_ctx *rom_test_prepare(struct file *filp)
{
	struct debug *debug = priv_from_file(filp);
	struct qm35_ctx *qm35_hdl = container_of(debug, struct qm35_ctx, debug);

	qm35_hsspi_stop(qm35_hdl);
	qmrom_set_log_device(&qm35_hdl->spi->dev, LOG_DBG);
	return qm35_hdl;
}

static void rom_test_unprepare(struct qm35_ctx *qm35_hdl)
{
	qmrom_set_log_device(&qm35_hdl->spi->dev, LOG_WARN);
	qm35_hsspi_start(qm35_hdl);
}

static struct firmware *file2firmware(const char *filename, size_t file_size)
{
	struct firmware *firmware =
		kmalloc(sizeof(struct firmware), GFP_KERNEL | __GFP_ZERO);
	if (!firmware) {
		goto fail;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	{
		ssize_t bytes_read = kernel_read_file_from_path(
			filename, 0, (void **)&firmware->data, INT_MAX,
			&firmware->size, READING_FIRMWARE);
		if (bytes_read < 0) {
			pr_err("kernel_read_file_from_path(%s) returned %d\n",
			       filename, (int)bytes_read);
			goto fail;
		}
		if (bytes_read != firmware->size) {
			pr_err("kernel_read_file_from_path returned %zu; expected %zu\n",
			       bytes_read, firmware->size);
			goto fail;
		}
	}
#else
	{
		loff_t size = 0;
		int ret = kernel_read_file_from_path(filename,
						     (void **)&firmware->data,
						     &size, INT_MAX,
						     READING_FIRMWARE);
		if (ret < 0) {
			pr_err("kernel_read_file_from_path(%s) returned %d\n",
			       filename, ret);
			goto fail;
		}
		firmware->size = (size_t)size;
	}
#endif

	print_hex_dump(KERN_DEBUG, "Bin file:", DUMP_PREFIX_ADDRESS, 16, 1,
		       firmware->data, 16, false);
	return firmware;

fail:
	if (firmware) {
		if (firmware->data)
			vfree(firmware->data);
		kfree(firmware);
	}
	return NULL;
}

static ssize_t rom_probe(struct file *filp, const char __user *buff,
			 size_t count, loff_t *off)
{
	struct qm35_ctx *qm35_hdl = rom_test_prepare(filp);
	struct qmrom_handle *h;

	pr_info("Starting the probe test...\n");
	h = qmrom_init(&qm35_hdl->spi->dev, qm35_hdl, qm35_hdl->gpio_ss_rdy,
		       QMROM_RETRIES, qmrom_spi_reset_device);
	if (!h) {
		pr_err("qmrom_init failed\n");
		goto end;
	}

	pr_info("chip_revision %#2x\n", h->chip_rev);
	pr_info("device version %#02x\n", h->device_version);
	if (h->chip_rev != 0xa001) {
		pr_info("lcs_state %d\n", h->lcs_state);
		print_hex_dump(KERN_DEBUG, "soc_id:", DUMP_PREFIX_NONE, 16, 1,
			       h->soc_id, sizeof(h->soc_id), false);
		print_hex_dump(KERN_DEBUG, "uuid:", DUMP_PREFIX_NONE, 16, 1,
			       h->uuid, sizeof(h->uuid), false);
	}
	qmrom_deinit(h);

end:
	rom_test_unprepare(qm35_hdl);
	return count;
}

static ssize_t rom_flash_dbg_cert(struct file *filp, const char __user *buff,
				  size_t count, loff_t *off)
{
	struct qm35_ctx *qm35_hdl = rom_test_prepare(filp);
	struct firmware *certificate = NULL;
	struct qmrom_handle *h = NULL;
	char *filename;
	int err;

	filename = kmalloc(count, GFP_KERNEL);
	if (!filename) {
		count = -ENOMEM;
		goto end;
	}

	err = copy_from_user(filename, buff, count);
	if (err) {
		pr_err("copy_from_user failed with error %d\n", err);
		count = err;
		goto end;
	}
	filename[count - 1] = '\0';
	certificate = file2firmware(filename, DEBUG_CERTIFICATE_SIZE);
	if (!certificate) {
		pr_err("%s: file retrieval failed, abort\n", __func__);
		count = -1;
		goto end;
	}

	/* Flash the debug certificate */
	pr_info("Flashing debug certificate %s...\n", filename);

	h = qmrom_init(&qm35_hdl->spi->dev, qm35_hdl, qm35_hdl->gpio_ss_rdy,
		       QMROM_RETRIES, qmrom_spi_reset_device);
	if (!h) {
		pr_err("qmrom_init failed\n");
		goto end;
	}
	err = qmrom_flash_dbg_cert(h, certificate);
	if (err)
		pr_err("Flashing debug certificate %s failed with %d!\n",
		       filename, err);
	else
		pr_info("Flashing debug certificate %s succeeded!\n", filename);

end:
	if (h)
		qmrom_deinit(h);
	if (certificate) {
		if (certificate->data)
			vfree(certificate->data);
		kfree(certificate);
	}
	rom_test_unprepare(qm35_hdl);
	return count;
}

static ssize_t rom_erase_dbg_cert(struct file *filp, const char __user *buff,
				  size_t count, loff_t *off)
{
	struct qm35_ctx *qm35_hdl = rom_test_prepare(filp);
	struct qmrom_handle *h = NULL;
	int err;

	pr_info("Erasing debug certificate...\n");

	h = qmrom_init(&qm35_hdl->spi->dev, qm35_hdl, qm35_hdl->gpio_ss_rdy,
		       QMROM_RETRIES, qmrom_spi_reset_device);
	if (!h) {
		pr_err("qmrom_init failed\n");
		goto end;
	}
	err = qmrom_erase_dbg_cert(h);
	if (err)
		pr_err("Erasing debug certificate failed with %d!\n", err);
	else
		pr_info("Erasing debug certificate succeeded!\n");

end:
	if (h)
		qmrom_deinit(h);
	rom_test_unprepare(qm35_hdl);
	return count;
}

static ssize_t rom_flash_fw(struct file *filp, const char __user *buff,
			    size_t count, loff_t *off)
{
	struct qm35_ctx *qm35_hdl = rom_test_prepare(filp);
	struct firmware *fw = NULL;
	struct qmrom_handle *h = NULL;
	char *filename;
	int rc;

	filename = kmalloc(count, GFP_KERNEL);
	if (!filename) {
		count = -ENOMEM;
		goto end;
	}

	rc = copy_from_user(filename, buff, count);
	if (rc) {
		pr_err("copy_from_user failed with error %d\n", rc);
		goto end;
	}
	filename[count - 1] = '\0';
	fw = file2firmware(filename, 0);
	if (!fw) {
		pr_err("%s: file retrieval failed, abort\n", __func__);
		rc = -1;
		goto end;
	}

	pr_info("Flashing image %s (%pK->data %pK)...\n", filename, fw,
		fw->data);

	h = qmrom_init(&qm35_hdl->spi->dev, qm35_hdl, qm35_hdl->gpio_ss_rdy,
		       QMROM_RETRIES, qmrom_spi_reset_device);
	if (!h) {
		pr_err("qmrom_init failed\n");
		rc = -1;
		goto end;
	}
	rc = qmrom_flash_fw(h, fw);
	if (rc)
		pr_err("Flashing firmware %s failed with %d!\n", filename, rc);
	else
		pr_info("Flashing firmware %s succeeded!\n", filename);

end:
	if (h)
		qmrom_deinit(h);
	if (fw) {
		if (fw->data)
			vfree(fw->data);
		kfree(fw);
	}
	rom_test_unprepare(qm35_hdl);
	return count;
}

static const struct file_operations rom_probe_fops = { .owner = THIS_MODULE,
						       .write = rom_probe };

static const struct file_operations rom_flash_dbg_cert_fops = {
	.owner = THIS_MODULE,
	.write = rom_flash_dbg_cert
};

static const struct file_operations rom_erase_dbg_cert_fops = {
	.owner = THIS_MODULE,
	.write = rom_erase_dbg_cert
};

static const struct file_operations rom_flash_fw_fops = {
	.owner = THIS_MODULE,
	.write = rom_flash_fw
};

void debug_rom_code_init(struct debug *debug)
{
	struct dentry *file;
	file = debugfs_create_file("rom_probe", 0200, debug->fw_dir, debug,
				   &rom_probe_fops);
	if (!file) {
		pr_err("qm35: failed to create uwb0/fw/rom_probe\n");
		return;
	}

	file = debugfs_create_file("rom_flash_dbg_cert", 0200, debug->fw_dir,
				   debug, &rom_flash_dbg_cert_fops);
	if (!file) {
		pr_err("qm35: failed to create uwb0/fw/rom_flash_dbg_cert\n");
		return;
	}

	file = debugfs_create_file("rom_erase_dbg_cert", 0200, debug->fw_dir,
				   debug, &rom_erase_dbg_cert_fops);
	if (!file) {
		pr_err("qm35: failed to create uwb0/fw/rom_erase_dbg_cert\n");
		return;
	}

	file = debugfs_create_file("rom_flash_fw", 0200, debug->fw_dir, debug,
				   &rom_flash_fw_fops);
	if (!file) {
		pr_err("qm35: failed to create uwb0/fw/rom_flash_fw\n");
		return;
	}
}
