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
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <drm/drm_mode.h>

#include "auth-state.h"
#include "dpcd.h"
#include "hdcp-log.h"
#include "teeif.h"

#define HDCP_V2_3 (5)
#define HDCP_V1   (1)
#define HDCP_NONE (0)

static enum auth_state hdcp_auth_state = HDCP_AUTH_RESET;
static bool enc_enabled = false;

static const char auth_state_str[10][20] = {
	"HDCP_AUTH_RESET",
	"HDCP_AUTH_IDLE",
	"HDCP1_AUTH_PROGRESS",
	"HDCP1_AUTH_DONE",
	"HDCP2_AUTH_PROGRESS",
	"HDCP2_AUTH_DONE",
	"HDCP2_AUTH_RP",
	"HDCP_AUTH_ABORT",
	"HDCP_AUTH_SHUTDOWN",
};

static int hdcp_tee_enable_enc_22(void) {
	if (enc_enabled)
		return 0;

	msleep(200);
	int ret = hdcp_tee_set_protection(HDCP_V2_3);
	if (ret)
		return ret;

	hdcp_dplink_update_cp(DRM_MODE_CONTENT_PROTECTION_ENABLED);
	enc_enabled = true;
	return 0;
}

static int hdcp_tee_enable_enc_13(void) {
	if (enc_enabled)
		return 0;

	int ret = hdcp_tee_set_protection(HDCP_V1);
	if (ret)
		return ret;

	hdcp_dplink_update_cp(DRM_MODE_CONTENT_PROTECTION_ENABLED);
	enc_enabled = true;
	return 0;
}

static int hdcp_tee_disable_enc(void) {
	if (!enc_enabled)
		return 0;

	hdcp_dplink_update_cp(DRM_MODE_CONTENT_PROTECTION_DESIRED);
	int ret = hdcp_tee_set_protection(HDCP_NONE);
	if (ret)
		return ret;
	enc_enabled = false;
	return 0;
}

const char* get_auth_state_str(uint32_t state) {
	return auth_state_str[order_base_2(state)];
}

enum auth_state hdcp_get_auth_state(void) {
	enum auth_state state = hdcp_auth_state;
	return state;
}

bool is_hdcp_auth_aborted(void) {
	return hdcp_get_auth_state() & (HDCP_AUTH_ABORT | HDCP_AUTH_SHUTDOWN);
}

static void hdcp_transition_auth_state(enum auth_state new_state) {
	if (hdcp_auth_state == new_state)
		return;
	hdcp_info("set auth state from %s to %s\n",
		get_auth_state_str(hdcp_auth_state),
		get_auth_state_str(new_state));
	hdcp_auth_state = new_state;

	if (new_state & HDCP1_AUTH_DONE)
		hdcp_tee_enable_enc_13();

	if (new_state & HDCP2_AUTH_DONE) {
		hdcp_tee_enable_enc_22();
	}

	if (new_state & (HDCP_AUTH_IDLE | HDCP_AUTH_ABORT | HDCP_AUTH_SHUTDOWN))
		hdcp_tee_disable_enc();
}

int hdcp_set_auth_state(enum auth_state state) {
	if (state & (HDCP_AUTH_ABORT | HDCP_AUTH_SHUTDOWN)) {
		hdcp_transition_auth_state(state);
		return 0;
	}

	uint32_t allowed_state = 0;

	switch (hdcp_auth_state) {
	case HDCP_AUTH_RESET:
	case HDCP_AUTH_IDLE:
		allowed_state = HDCP2_AUTH_PROGRESS | HDCP1_AUTH_PROGRESS;
		break;
	case HDCP2_AUTH_PROGRESS:
		allowed_state = HDCP2_AUTH_DONE | HDCP_AUTH_IDLE;
		break;
	case HDCP1_AUTH_PROGRESS:
		allowed_state = HDCP1_AUTH_DONE | HDCP_AUTH_IDLE;
		break;
	case HDCP1_AUTH_DONE:
		allowed_state = HDCP_AUTH_IDLE;
		break;
	case HDCP2_AUTH_DONE:
		allowed_state = HDCP2_AUTH_RP | HDCP_AUTH_IDLE;
		break;
	case HDCP2_AUTH_RP:
		allowed_state = HDCP2_AUTH_PROGRESS;
		break;
	case HDCP_AUTH_ABORT:
		allowed_state = HDCP_AUTH_RESET;
		break;
	case HDCP_AUTH_SHUTDOWN:
		allowed_state = 0;
	}

	if (!(state & allowed_state)) {
		hdcp_info("set auth state from %s to %s failed\n",
			get_auth_state_str(hdcp_auth_state),
			get_auth_state_str(state));
		return -1;
	}

	hdcp_transition_auth_state(state);
	return 0;
}

