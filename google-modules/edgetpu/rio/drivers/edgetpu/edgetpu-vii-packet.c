// SPDX-License-Identifier: GPL-2.0
/*
 * Interface to access and set fields in VII packets, regardless of their format.
 *
 * Copyright (C) 2024 Google LLC
 */

#include "edgetpu-config.h"
#include "edgetpu-vii-packet.h"
#include "edgetpu-vii-litebuf.h"
#include "edgetpu.h"

static void log_unknown_format_warning(struct edgetpu_dev *etdev)
{
	etdev_warn_ratelimited(etdev, "VII packet accessed while format is unknown");
}

size_t edgetpu_vii_command_packet_size(struct edgetpu_dev *etdev)
{
	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		return sizeof(struct edgetpu_vii_command);
	case EDGETPU_VII_FORMAT_LITEBUF:
		return sizeof(struct edgetpu_vii_litebuf_command);
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

size_t edgetpu_vii_response_packet_size(struct edgetpu_dev *etdev)
{
	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		return sizeof(struct edgetpu_vii_response);
	case EDGETPU_VII_FORMAT_LITEBUF:
		return sizeof(struct edgetpu_vii_litebuf_response);
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

/* Command accessors */

u64 edgetpu_vii_command_get_seq_number(struct edgetpu_dev *etdev, void *cmd)
{
	struct edgetpu_vii_command *fb_cmd __maybe_unused;
	struct edgetpu_vii_litebuf_command *lb_cmd __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_cmd = cmd;
		return fb_cmd->seq;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_cmd = cmd;
		return lb_cmd->seq;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

void  edgetpu_vii_command_set_seq_number(struct edgetpu_dev *etdev, void *cmd, u64 seq_number)
{
	struct edgetpu_vii_command *fb_cmd __maybe_unused;
	struct edgetpu_vii_litebuf_command *lb_cmd __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_cmd = cmd;
		fb_cmd->seq = seq_number;
		break;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_cmd = cmd;
		lb_cmd->seq = seq_number;
		break;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}
}

u16 edgetpu_vii_command_get_code(struct edgetpu_dev *etdev, void *cmd)
{
	struct edgetpu_vii_command *fb_cmd __maybe_unused;
	struct edgetpu_vii_litebuf_command *lb_cmd __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_cmd = cmd;
		return fb_cmd->code;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_cmd = cmd;
		/*
		 * The value normally thought of as the "command code" for VII is within the litebuf
		 * and not exposed to the driver. Since this value is only used for logging under
		 * VII, just return the type of command.
		 */
		return lb_cmd->type;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

u32 edgetpu_vii_command_get_client_id(struct edgetpu_dev *etdev, void *cmd)
{
	struct edgetpu_vii_command *fb_cmd __maybe_unused;
	struct edgetpu_vii_litebuf_command *lb_cmd __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_cmd = cmd;
		return fb_cmd->client_id;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_cmd = cmd;
		return lb_cmd->client_id;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

void edgetpu_vii_command_set_client_id(struct edgetpu_dev *etdev, void *cmd, u32 client_id)
{
	struct edgetpu_vii_command *fb_cmd __maybe_unused;
	struct edgetpu_vii_litebuf_command *lb_cmd __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_cmd = cmd;
		fb_cmd->client_id = client_id;
		break;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_cmd = cmd;
		lb_cmd->client_id = client_id;
		break;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}
}

u32 edgetpu_vii_command_get_additional_info(struct edgetpu_dev *etdev, void *cmd,
					    u16 *additional_info_size)
{
	struct edgetpu_vii_litebuf_command *lb_cmd __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		/* Flatbuffer VII does not support additional info. */
		break;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_cmd = cmd;
		if (additional_info_size)
			*additional_info_size = lb_cmd->additional_info_size;
		return lb_cmd->additional_info_address;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	if (additional_info_size)
		*additional_info_size = 0;
	return 0;
}

void edgetpu_vii_command_set_additional_info(struct edgetpu_dev *etdev, void *cmd,
					     u32 additional_info_addr, u16 additional_info_size)
{
	struct edgetpu_vii_litebuf_command *lb_cmd __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		/* Flatbuffer VII does not support additional info. */
		break;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_cmd = cmd;
		lb_cmd->additional_info_address = additional_info_addr;
		lb_cmd->additional_info_size = additional_info_size;
		break;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}
}

/* Response accessors */

u64 edgetpu_vii_response_get_seq_number(struct edgetpu_dev *etdev, void *resp)
{
	struct edgetpu_vii_response *fb_resp __maybe_unused;
	struct edgetpu_vii_litebuf_response *lb_resp __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_resp = resp;
		return fb_resp->seq;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_resp = resp;
		return lb_resp->seq;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

void edgetpu_vii_response_set_seq_number(struct edgetpu_dev *etdev, void *resp, u64 seq_number)
{
	struct edgetpu_vii_response *fb_resp __maybe_unused;
	struct edgetpu_vii_litebuf_response *lb_resp __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_resp = resp;
		fb_resp->seq = seq_number;
		break;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_resp = resp;
		lb_resp->seq = seq_number;
		break;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}
}

u16 edgetpu_vii_response_get_code(struct edgetpu_dev *etdev, void *resp)
{
	struct edgetpu_vii_response *fb_resp __maybe_unused;
	struct edgetpu_vii_litebuf_response *lb_resp __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_resp = resp;
		return fb_resp->code;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_resp = resp;
		return lb_resp->code;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

void edgetpu_vii_response_set_code(struct edgetpu_dev *etdev, void *resp, u16 code)
{
	struct edgetpu_vii_response *fb_resp __maybe_unused;
	struct edgetpu_vii_litebuf_response *lb_resp __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_resp = resp;
		fb_resp->code = code;
		break;
	case EDGETPU_VII_FORMAT_LITEBUF:
		lb_resp = resp;
		lb_resp->code = code;
		break;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}
}

u64 edgetpu_vii_response_get_retval(struct edgetpu_dev *etdev, void *resp)
{
	struct edgetpu_vii_response *fb_resp __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_resp = resp;
		return fb_resp->retval;
	case EDGETPU_VII_FORMAT_LITEBUF:
		/* Litebuf VII does not have a retval field. */
		break;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}

	return 0;
}

void edgetpu_vii_response_set_retval(struct edgetpu_dev *etdev, void *resp, u64 retval)
{
	struct edgetpu_vii_response *fb_resp __maybe_unused;

	switch (etdev->vii_format) {
	case EDGETPU_VII_FORMAT_FLATBUFFER:
		fb_resp = resp;
		fb_resp->retval = retval;
		break;
	case EDGETPU_VII_FORMAT_LITEBUF:
		/* Litebuf VII does not have a retval field. */
		break;
	case EDGETPU_VII_FORMAT_UNKNOWN:
		log_unknown_format_warning(etdev);
		break;
	}
}
