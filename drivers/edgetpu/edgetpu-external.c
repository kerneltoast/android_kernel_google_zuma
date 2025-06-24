// SPDX-License-Identifier: GPL-2.0
/*
 * Utility functions for interfacing other modules with Edge TPU ML accelerator.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/platform_device.h>

#include <soc/google/tpu-ext.h>

#include <iif/iif-manager.h>

#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mobile-platform.h"

static int check_ext_mailbox_args(const char *func, struct edgetpu_dev *etdev,
				  struct edgetpu_ext_mailbox_ioctl *args)
{
	if (args->type != EDGETPU_EXT_MAILBOX_TYPE_TZ) {
		etdev_err(etdev, "%s: Invalid type %d != %d\n", func, args->type,
			  EDGETPU_EXT_MAILBOX_TYPE_TZ);
		return -EINVAL;
	}
	if (args->count != 1) {
		etdev_err(etdev, "%s: Invalid mailbox count: %d != 1\n", func, args->count);
		return -EINVAL;
	}
	return 0;
}

int edgetpu_acquire_ext_mailbox(struct edgetpu_client *client,
				struct edgetpu_ext_mailbox_ioctl *args)
{
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(client->etdev);
	int ret;

	ret = check_ext_mailbox_args(__func__, client->etdev, args);
	if (ret)
		return ret;

	mutex_lock(&etmdev->tz_mailbox_lock);
	if (etmdev->secure_client) {
		etdev_err(client->etdev, "TZ mailbox already in use by PID %d\n",
			  etmdev->secure_client->pid);
		mutex_unlock(&etmdev->tz_mailbox_lock);
		return -EBUSY;
	}
	ret = edgetpu_mailbox_enable_ext(client, EDGETPU_TZ_MAILBOX_ID, NULL, 0);
	if (!ret)
		etmdev->secure_client = client;
	mutex_unlock(&etmdev->tz_mailbox_lock);
	return ret;
}

int edgetpu_release_ext_mailbox(struct edgetpu_client *client,
				struct edgetpu_ext_mailbox_ioctl *args)
{
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(client->etdev);
	int ret = 0;

	ret = check_ext_mailbox_args(__func__, client->etdev, args);
	if (ret)
		return ret;

	mutex_lock(&etmdev->tz_mailbox_lock);
	if (!etmdev->secure_client) {
		etdev_warn(client->etdev, "TZ mailbox already released\n");
		mutex_unlock(&etmdev->tz_mailbox_lock);
		return 0;
	}
	if (etmdev->secure_client != client) {
		etdev_err(client->etdev,
			  "TZ mailbox owned by different client\n");
		mutex_unlock(&etmdev->tz_mailbox_lock);
		return -EBUSY;
	}
	etmdev->secure_client = NULL;
	ret = edgetpu_mailbox_disable_ext(client, EDGETPU_TZ_MAILBOX_ID);
	mutex_unlock(&etmdev->tz_mailbox_lock);
	return ret;
}

void edgetpu_ext_client_remove(struct edgetpu_client *client)
{
	struct edgetpu_mobile_platform_dev *etmdev = to_mobile_dev(client->etdev);

	mutex_lock(&etmdev->tz_mailbox_lock);
	if (etmdev->secure_client == client) {
		etmdev->secure_client = NULL;
		edgetpu_mailbox_disable_ext(client, EDGETPU_TZ_MAILBOX_ID);
	}
	mutex_unlock(&etmdev->tz_mailbox_lock);
}

static int edgetpu_get_ext_mailbox_index(u32 mbox_type, u32 *start, u32 *end)
{
	switch (mbox_type) {
	case EDGETPU_EXTERNAL_MAILBOX_TYPE_DSP:
		*start = EDGETPU_EXT_DSP_MAILBOX_START;
		*end = EDGETPU_EXT_DSP_MAILBOX_END;
		return 0;
	default:
		return -ENOENT;
	}
}

static enum edgetpu_ext_mailbox_type
edgetpu_external_client_to_mailbox_type(enum edgetpu_ext_client_type client_type)
{
	switch (client_type) {
	case EDGETPU_EXTERNAL_CLIENT_TYPE_DSP:
		return EDGETPU_EXTERNAL_MAILBOX_TYPE_DSP;
	case EDGETPU_EXTERNAL_CLIENT_TYPE_AOC:
		return EDGETPU_EXTERNAL_MAILBOX_TYPE_AOC;
	default:
		return -ENOENT;
	}
}

static int edgetpu_external_mailbox_info_get(struct edgetpu_ext_mailbox_info *info,
					     struct edgetpu_external_mailbox *ext_mailbox)
{
	int i;
	u32 count = ext_mailbox->count;
	struct edgetpu_mailbox_descriptor *desc;

	if (!info)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		desc = &ext_mailbox->descriptors[i];
		info->mailboxes[i].cmdq_pa = desc->cmd_queue_mem.phys_addr;
		info->mailboxes[i].respq_pa = desc->resp_queue_mem.phys_addr;
	}

	info->cmdq_size = ext_mailbox->attr.cmd_queue_size;
	info->respq_size = ext_mailbox->attr.resp_queue_size;

	return 0;
}

static int edgetpu_external_mailbox_alloc(struct device *edgetpu_dev,
					  struct edgetpu_ext_client_info *client_info,
					  struct edgetpu_ext_mailbox_info *info,
					  enum edgetpu_ext_client_type client_type)
{
	struct edgetpu_client *client;
	struct edgetpu_device_group *group;
	struct edgetpu_external_mailbox *ext_mailbox;
	struct edgetpu_external_mailbox_req req;
	int ret = 0;
	struct file *file;
	bool use_file = client_info->tpu_fd == -1;

	if (use_file)
		file = client_info->tpu_file;
	else
		file = fget(client_info->tpu_fd);

	if (!file)
		return -EBADF;

	if (use_file)
		get_file(file);

	if (!is_edgetpu_file(file)) {
		ret = -EINVAL;
		goto out;
	}

	client = file->private_data;
	if (!client || client->etdev->dev != edgetpu_dev) {
		ret = -EINVAL;
		goto out;
	}

	req.mbox_type = edgetpu_external_client_to_mailbox_type(client_type);
	req.mbox_map = client_info->mbox_map;

	ret = edgetpu_get_ext_mailbox_index(req.mbox_type, &req.start, &req.end);
	if (ret)
		goto out;

	mutex_lock(&client->group_lock);
	if (!client->group) {
		ret = -EINVAL;
		mutex_unlock(&client->group_lock);
		goto out;
	}
	group = edgetpu_device_group_get(client->group);
	mutex_unlock(&client->group_lock);

	if (copy_from_user(&req.attr, (void __user *)client_info->attr, sizeof(req.attr))) {
		if (!client_info->attr)
			etdev_dbg(client->etdev,
				  "Using VII mailbox attrs for external mailbox\n");
		req.attr = group->mbox_attr;
	}

	ret = edgetpu_mailbox_enable_ext(client, EDGETPU_MAILBOX_ID_USE_ASSOC, &req,
					 group->mbox_attr.client_priv);
	if (ret)
		goto error_put_group;
	mutex_lock(&group->lock);
	ext_mailbox = group->ext_mailbox;
	if (!ext_mailbox) {
		ret = -ENOENT;
		goto unlock;
	}
	ret = edgetpu_external_mailbox_info_get(info, ext_mailbox);
unlock:
	mutex_unlock(&group->lock);
error_put_group:
	edgetpu_device_group_put(group);
out:
	fput(file);
	return ret;
}

static int edgetpu_external_mailbox_free(struct device *edgetpu_dev,
					 struct edgetpu_ext_client_info *client_info)
{
	struct edgetpu_client *client;
	int ret = 0;
	struct file *file;
	bool use_file = client_info->tpu_fd == -1;

	if (use_file)
		file = client_info->tpu_file;
	else
		file = fget(client_info->tpu_fd);

	if (!file)
		return -EBADF;

	if (use_file)
		get_file(file);

	if (!is_edgetpu_file(file)) {
		ret = -EINVAL;
		goto out;
	}

	client = file->private_data;
	if (!client || client->etdev->dev != edgetpu_dev) {
		ret = -EINVAL;
		goto out;
	}

	ret = edgetpu_mailbox_disable_ext(client, EDGETPU_MAILBOX_ID_USE_ASSOC);
out:
	fput(file);
	return ret;
}

static int edgetpu_external_start_offload(struct device *edgetpu_dev,
					  struct edgetpu_ext_client_info *client_info,
					  struct edgetpu_ext_offload_info *offload_info)
{
	struct edgetpu_mobile_platform_dev *etmdev;
	struct edgetpu_client *client;
	struct edgetpu_device_group *group;
	struct file *file = client_info->tpu_file;
	struct edgetpu_iommu_domain *etdomain;
	int ret = 0;

	if (!file)
		return -EBADF;

	get_file(file);

	if (!is_edgetpu_file(file)) {
		ret = -EINVAL;
		goto out;
	}

	client = file->private_data;
	if (!client || client->etdev->dev != edgetpu_dev) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&client->group_lock);
	if (!client->group) {
		ret = -EINVAL;
		mutex_unlock(&client->group_lock);
		goto out;
	}
	group = edgetpu_device_group_get(client->group);
	mutex_unlock(&client->group_lock);

	mutex_lock(&group->lock);
	etdomain = edgetpu_group_domain_locked(group);
	if (edgetpu_mmu_domain_detached(etdomain)) {
		ret = -EINVAL;
		goto out_group_unlock;
	}

	etmdev = to_mobile_dev(client->etdev);
	if (client_info->flags & EDGETPU_EXT_SECURE_CLIENT && client == etmdev->secure_client)
		offload_info->client_id = EDGETPU_EXT_TZ_CONTEXT_ID;
	else
		offload_info->client_id = etdomain->pasid;

out_group_unlock:
	mutex_unlock(&group->lock);
	edgetpu_device_group_put(group);
out:
	fput(file);
	return ret;
}

static int edgetpu_external_get_iif_manager(struct device *edgetpu_dev,
					    struct iif_manager **iif_manager_ptr)
{
	struct platform_device *pdev = to_platform_device(edgetpu_dev);
	struct edgetpu_dev *etdev = platform_get_drvdata(pdev);

	if (!etdev->iif_mgr)
		return -ENODEV;

	*iif_manager_ptr = iif_manager_get(etdev->iif_mgr);

	return 0;
}

int edgetpu_ext_driver_cmd(struct device *edgetpu_dev, enum edgetpu_ext_client_type client_type,
			   enum edgetpu_ext_commands cmd_id, void *in_data, void *out_data)
{
	switch (cmd_id) {
	case ALLOCATE_EXTERNAL_MAILBOX:
		return edgetpu_external_mailbox_alloc(edgetpu_dev, in_data, out_data, client_type);
	case FREE_EXTERNAL_MAILBOX:
		return edgetpu_external_mailbox_free(edgetpu_dev, in_data);
	case START_OFFLOAD:
		return edgetpu_external_start_offload(edgetpu_dev, in_data, out_data);
	case GET_IIF_MANAGER:
		return edgetpu_external_get_iif_manager(edgetpu_dev, out_data);
	default:
		return -ENOENT;
	}
}
EXPORT_SYMBOL_GPL(edgetpu_ext_driver_cmd);
