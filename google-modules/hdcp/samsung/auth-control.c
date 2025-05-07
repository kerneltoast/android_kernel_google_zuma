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

#include "exynos-hdcp-interface.h"

#include "auth-control.h"
#include "auth-state.h"
#include "auth13.h"
#include "auth22.h"
#include "hdcp.h"
#include "hdcp-log.h"
#include "teeif.h"

static struct hdcp_device *hdcp_dev;

static unsigned long max_ver = 2;
module_param(max_ver, ulong, 0664);
MODULE_PARM_DESC(max_ver,
	"support up to specific hdcp version by setting max_ver=x");

static unsigned long max_retry_count = 5;
module_param(max_retry_count, ulong, 0664);
MODULE_PARM_DESC(max_retry_count,
	"set number of allowed retry times by setting max_retry_count=x");

static uint32_t hdcp_auth_try_count = 0;

static int run_hdcp2_auth(void) {
	int ret;
	int i;

	if (hdcp_set_auth_state(HDCP2_AUTH_PROGRESS))
		return -EBUSY;

	for (i = 0; i < 5; ++i) {
		ret = hdcp22_dplink_authenticate();
		if (ret == 0) {
			return (hdcp_set_auth_state(HDCP2_AUTH_DONE)) ?
				-EBUSY : ret;
		} else if (ret != -EAGAIN) {
			return (hdcp_set_auth_state(HDCP_AUTH_IDLE)) ?
				-EBUSY : ret;
		}
		hdcp_info("HDCP22 Retry(%d)...\n", i);
	}

	return (hdcp_set_auth_state(HDCP_AUTH_IDLE)) ? -EBUSY : -EIO;
}

static int run_hdcp1_auth(void) {
	int ret;
	bool second_stage_required;

	if (hdcp_set_auth_state(HDCP1_AUTH_PROGRESS))
		return -EBUSY;

	ret = hdcp13_dplink_authenticate(&second_stage_required);
	if (ret)
		return (hdcp_set_auth_state(HDCP_AUTH_IDLE)) ? -EBUSY : ret;

	if (hdcp_set_auth_state(HDCP1_AUTH_DONE))
		return -EBUSY;

	if (!second_stage_required)
		return 0;

	ret = hdcp13_dplink_repeater_auth();
	if (ret)
		return (hdcp_set_auth_state(HDCP_AUTH_IDLE)) ? -EBUSY : ret;
	return 0;
}


static void hdcp_worker(struct work_struct *work) {
	int err;
	uint32_t requested_lvl;
	int ret;
	ktime_t delta;
	bool hdcp2_capable = false;
	bool hdcp1_capable = false;

	struct hdcp_device *hdcp_dev =
		container_of(work, struct hdcp_device, hdcp_work.work);

	enum auth_state state = hdcp_get_auth_state();
	if (!(state & (HDCP_AUTH_RESET | HDCP_AUTH_IDLE | HDCP2_AUTH_RP))) {
		hdcp_info("HDCP auth is skipped during %s state\n",
			get_auth_state_str(state));
		return;
	}

	err = hdcp_tee_get_cp_level(&requested_lvl);
	if (!err && !requested_lvl && max_ver <= 2) {
		hdcp_info("CP not requested\n");
		return;
	}

	delta = ktime_sub(ktime_get(), hdcp_dev->connect_time);
	if (ktime_to_ms(delta) < HDCP_SCHEDULE_DELAY_MSEC) {
		hdcp_info("HDCP auth will start soon\n");
		msleep(HDCP_SCHEDULE_DELAY_MSEC - ktime_to_ms(delta));
	}

	if (max_ver >= 2) {
		hdcp_info("Trying HDCP22...\n");
		ret = run_hdcp2_auth();
		if (ret == 0) {
			hdcp_info("HDCP22 Authentication Success\n");
			hdcp_dev->hdcp2_success_count++;
			return;
		}
		hdcp2_capable = (ret != -EOPNOTSUPP);
		hdcp_info("HDCP22 Authentication Failed.\n");
	} else {
		hdcp_info("Not trying HDCP22. max_ver is %lu\n", max_ver);
	}

	if (max_ver >= 1) {
		hdcp_info("Trying HDCP13...\n");
		ret = run_hdcp1_auth();
		if (ret == 0) {
			hdcp_info("HDCP13 Authentication Success\n");
			hdcp_dev->hdcp2_fallback_count += hdcp2_capable;
			hdcp_dev->hdcp1_success_count += !hdcp2_capable;
			return;
		}
		hdcp1_capable = (ret != -EOPNOTSUPP);
		hdcp_info("HDCP13 Authentication Failed.\n");
	} else {
		hdcp_info("Not trying HDCP13. max_ver is %lu\n", max_ver);
	}

	hdcp_dev->hdcp2_fail_count += (hdcp2_capable);
	hdcp_dev->hdcp1_fail_count += (!hdcp2_capable && hdcp1_capable);
	hdcp_dev->hdcp0_count += (!hdcp2_capable && !hdcp1_capable);
}

void hdcp_dplink_handle_irq(void) {
	int ret = 0;

	enum auth_state state = hdcp_get_auth_state();
	switch (state) {
	case HDCP2_AUTH_PROGRESS:
		hdcp22_dplink_handle_irq();
		break;
	case HDCP2_AUTH_DONE:
		ret = hdcp22_dplink_handle_irq();
		break;
	case HDCP1_AUTH_DONE:
		ret = hdcp13_dplink_handle_irq();
		break;
	default:
		hdcp_info("HDCP irq ignored during %s state\n",
			get_auth_state_str(state));
	}

	if (ret == -EFAULT) {
		hdcp_set_auth_state(HDCP_AUTH_IDLE);
		if (hdcp_auth_try_count >= max_retry_count) {
			hdcp_err("HDCP disabled until next physical re-connect"\
				 "tried %lu times\n", max_retry_count);
			return;
		}
		hdcp_auth_try_count++;
	}

	if (ret == -EAGAIN || ret == -EFAULT)
		hdcp_auth_worker_schedule(hdcp_dev);
}
EXPORT_SYMBOL_GPL(hdcp_dplink_handle_irq);

void hdcp_dplink_connect_state(enum dp_state dp_hdcp_state) {
	int tee_connect_info = dp_hdcp_state == DP_SHUTDOWN ?
			       DP_DISCONNECT : dp_hdcp_state;

	hdcp_info("Displayport connect info (%d)\n", dp_hdcp_state);

	if (dp_hdcp_state == DP_PHYSICAL_DISCONNECT) {
		hdcp_dev->connect_time = 0;
		hdcp_auth_try_count = 0;
		return;
	}

	hdcp_tee_connect_info(tee_connect_info);
	if (dp_hdcp_state == DP_DISCONNECT || dp_hdcp_state == DP_SHUTDOWN) {
		hdcp_set_auth_state(dp_hdcp_state == DP_SHUTDOWN ?
			HDCP_AUTH_SHUTDOWN : HDCP_AUTH_ABORT);
		cancel_delayed_work_sync(&hdcp_dev->hdcp_work);
		return;
	}

	hdcp_dev->connect_time = ktime_get();

	if (hdcp_auth_try_count >= max_retry_count) {
		hdcp_err("HDCP disabled until next physical re-connect"\
			 "tried %lu times\n", max_retry_count);
		return;
	}

	hdcp_auth_try_count++;
	hdcp_set_auth_state(HDCP_AUTH_RESET);
	hdcp_auth_worker_schedule(hdcp_dev);
	return;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_connect_state);

void hdcp_auth_worker_schedule(struct hdcp_device *dev) {
	schedule_delayed_work(&dev->hdcp_work, 0);
}

int hdcp_auth_worker_init(struct hdcp_device *dev) {
	if (hdcp_dev)
		return -EACCES;

	hdcp_dev = dev;
	INIT_DELAYED_WORK(&hdcp_dev->hdcp_work, hdcp_worker);
	return 0;
}

int hdcp_auth_worker_deinit(struct hdcp_device *dev) {
	if (hdcp_dev != dev)
		return -EACCES;

	cancel_delayed_work_sync(&hdcp_dev->hdcp_work);
	hdcp_dev = NULL;
	return 0;
}
