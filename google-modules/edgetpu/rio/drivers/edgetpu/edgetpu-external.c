// SPDX-License-Identifier: GPL-2.0
/*
 * Utility functions for interfacing other modules with Edge TPU ML accelerator.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/device.h>
#include <linux/file.h>
#include <linux/err.h>

#include <soc/google/tpu-ext.h>

#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"

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

	ret = edgetpu_chip_get_ext_mailbox_index(req.mbox_type, &req.start, &req.end);
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
	struct edgetpu_client *client;
	struct edgetpu_device_group *group;
	struct file *file = client_info->tpu_file;
	enum edgetpu_context_id context;
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
	context = edgetpu_group_context_id_locked(group);
	if (context == EDGETPU_CONTEXT_INVALID || context & EDGETPU_CONTEXT_DOMAIN_TOKEN) {
		ret = -EINVAL;
		goto out_group_unlock;
	}

	offload_info->client_id = context;

out_group_unlock:
	mutex_unlock(&group->lock);
	edgetpu_device_group_put(group);
out:
	fput(file);
	return ret;
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
	default:
		return -ENOENT;
	}
}
EXPORT_SYMBOL_GPL(edgetpu_ext_driver_cmd);
