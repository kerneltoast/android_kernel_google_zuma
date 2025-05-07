// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *
 * Samsung DisplayPort driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/smc.h>
#include <asm/cacheflush.h>
#include <linux/soc/samsung/exynos-smc.h>

#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/dma-mapping.h>

#include "exynos-hdcp-interface.h"

#include "auth-control.h"
#include "hdcp.h"
#include "hdcp-log.h"
#include "selftest.h"
#include "teeif.h"

#define EXYNOS_HDCP_DEV_NAME	"hdcp2"

static struct miscdevice hdcp;

static ssize_t hdcp_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	hdcp_info("Kicking off selftest\n");
	hdcp_protocol_self_test();
	return len;
}

static ssize_t hdcp_read(struct file *filp, char __user *buf,
			 size_t count, loff_t *f_pos)
{
	return 0;
}

static ssize_t hdcp2_success_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct hdcp_device *hdcp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", hdcp->hdcp2_success_count);
}
static DEVICE_ATTR_RO(hdcp2_success_count);

static ssize_t hdcp2_fallback_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct hdcp_device *hdcp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", hdcp->hdcp2_fallback_count);
}
static DEVICE_ATTR_RO(hdcp2_fallback_count);

static ssize_t hdcp2_fail_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct hdcp_device *hdcp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", hdcp->hdcp2_fail_count);
}
static DEVICE_ATTR_RO(hdcp2_fail_count);

static ssize_t hdcp1_success_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct hdcp_device *hdcp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", hdcp->hdcp1_success_count);
}
static DEVICE_ATTR_RO(hdcp1_success_count);

static ssize_t hdcp1_fail_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct hdcp_device *hdcp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", hdcp->hdcp1_fail_count);
}
static DEVICE_ATTR_RO(hdcp1_fail_count);

static ssize_t hdcp0_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct hdcp_device *hdcp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", hdcp->hdcp0_count);
}
static DEVICE_ATTR_RO(hdcp0_count);

static const struct attribute *hdcp_attrs[] = {
					&dev_attr_hdcp2_success_count.attr,
					&dev_attr_hdcp2_fallback_count.attr,
					&dev_attr_hdcp2_fail_count.attr,
					&dev_attr_hdcp1_success_count.attr,
					&dev_attr_hdcp1_fail_count.attr,
					&dev_attr_hdcp0_count.attr,
					NULL };

static irqreturn_t exynos_hdcp_irq_handler(int irq, void *dev_id) {
	struct hdcp_device* hdcp_dev = dev_id;

	hdcp_debug("irq handler called\n");
	hdcp_auth_worker_schedule(hdcp_dev);
	return IRQ_HANDLED;
}

static int exynos_hdcp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdcp_device *hdcp_dev = NULL;
	int ret = 0;
	int err;
	unsigned int irq;

	hdcp_dev = devm_kzalloc(dev, sizeof(struct hdcp_device), GFP_KERNEL);
	if (!hdcp_dev) {
		return -ENOMEM;
	}
	hdcp_dev->dev = dev;
	platform_set_drvdata(pdev, hdcp_dev);

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!irq) {
		hdcp_info("fail to get irq from dt\n");
		return -EINVAL;
	}

	err = devm_request_irq(dev, irq, exynos_hdcp_irq_handler,
		IRQF_TRIGGER_RISING, pdev->name, hdcp_dev);
	if (err) {
		hdcp_info("Fail to request IRQ handler. err(%d) irq(%d)\n",
			err, irq);
		return err;
	}

	hdcp_auth_worker_init(hdcp_dev);

	ret = sysfs_create_files(&dev->kobj, hdcp_attrs);
	if (ret)
		hdcp_info("failed to create hdcp sysfs files\n");

	return 0;
}

static int exynos_hdcp_remove(struct platform_device *pdev)
{
	struct hdcp_device *hdcp_dev = platform_get_drvdata(pdev);
	hdcp_auth_worker_deinit(hdcp_dev);
	return 0;
}

static const struct of_device_id exynos_hdcp_of_match_table[] = {
	{ .compatible = "samsung,exynos-hdcp", },
	{ },
};

static struct platform_driver exynos_hdcp_driver = {
	.probe = exynos_hdcp_probe,
	.remove = exynos_hdcp_remove,
	.driver = {
		.name = "exynos-hdcp",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_hdcp_of_match_table),
	}
};

static int __init hdcp_init(void)
{
	int ret;

	hdcp_info("hdcp2 driver init\n");

	ret = misc_register(&hdcp);
	if (ret) {
		hdcp_err("hdcp can't register misc on minor=%d\n",
				MISC_DYNAMIC_MINOR);
		return ret;
	}

	hdcp_tee_init();
	return platform_driver_register(&exynos_hdcp_driver);
}

static void __exit hdcp_exit(void)
{
	misc_deregister(&hdcp);
	hdcp_tee_close();

	platform_driver_unregister(&exynos_hdcp_driver);
}

static const struct file_operations hdcp_fops = {
	.owner		= THIS_MODULE,
	.write 		= hdcp_write,
	.read 		= hdcp_read,
};

static struct miscdevice hdcp = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= EXYNOS_HDCP_DEV_NAME,
	.fops	= &hdcp_fops,
};

module_init(hdcp_init);
module_exit(hdcp_exit);

MODULE_DESCRIPTION("Exynos Secure hdcp driver");
MODULE_AUTHOR("<hakmin_1.kim@samsung.com>");
MODULE_LICENSE("GPL v2");
