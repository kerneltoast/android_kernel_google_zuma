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
#include <linux/module.h>

#include "exynos-hdcp-interface.h"

#include "auth-control.h"
#include "auth13.h"
#include "auth22.h"
#include "hdcp.h"
#include "hdcp-log.h"
#include "teeif.h"

static struct hdcp_device *hdcp_dev;

static enum auth_state state;

static unsigned long max_ver = 2;
module_param(max_ver, ulong, 0664);
MODULE_PARM_DESC(max_ver,
	"support up to specific hdcp version by setting max_ver=x");

int hdcp_get_auth_state(void) {
	return state;
}

static int run_hdcp2_auth(void) {
	int ret;
	int i;

	state = HDCP2_AUTH_PROGRESS;
	for (i = 0; i < 5; ++i) {
		ret = hdcp22_dplink_authenticate();
		if (ret == 0) {
			state = HDCP2_AUTH_DONE;
			/* HDCP2.2 spec defined 200ms */
			msleep(200);
			hdcp_tee_enable_enc_22();
			return 0;
		} else if (ret != -EAGAIN) {
			return ret;
		}
		hdcp_info("HDCP22 Retry...\n");
	}

	return -EIO;
}

static void hdcp_worker(struct work_struct *work) {
	int ret;
	bool hdcp2_capable = false;
	bool hdcp1_capable = false;

	struct hdcp_device *hdcp_dev =
		container_of(work, struct hdcp_device, hdcp_work.work);

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
		state = HDCP1_AUTH_PROGRESS;
		ret = hdcp13_dplink_authenticate();
		if (ret == 0) {
			hdcp_info("HDCP13 Authentication Success\n");
			state = HDCP1_AUTH_DONE;
			hdcp_dev->hdcp2_fallback_count += hdcp2_capable;
			hdcp_dev->hdcp1_success_count += !hdcp2_capable;
			return;
		}

		state = HDCP_AUTH_IDLE;
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
	if (state == HDCP2_AUTH_PROGRESS || state == HDCP2_AUTH_DONE) {
		if (hdcp22_dplink_handle_irq() == -EAGAIN)
			schedule_delayed_work(&hdcp_dev->hdcp_work, 0);
		return;
	}

	if (state == HDCP1_AUTH_DONE) {
		if (hdcp13_dplink_handle_irq() == -EAGAIN)
			schedule_delayed_work(&hdcp_dev->hdcp_work, 0);
		return;
	}
}
EXPORT_SYMBOL_GPL(hdcp_dplink_handle_irq);


void hdcp_dplink_connect_state(enum dp_state dp_hdcp_state) {
	hdcp_info("Displayport connect info (%d)\n", dp_hdcp_state);
	hdcp_tee_connect_info((int)dp_hdcp_state);
	if (dp_hdcp_state == DP_DISCONNECT) {
		hdcp13_dplink_abort();
		hdcp22_dplink_abort();
		hdcp_tee_disable_enc();
		state = HDCP_AUTH_IDLE;
		if (delayed_work_pending(&hdcp_dev->hdcp_work))
			cancel_delayed_work(&hdcp_dev->hdcp_work);
		return;
	}

	schedule_delayed_work(&hdcp_dev->hdcp_work,
		msecs_to_jiffies(HDCP_SCHEDULE_DELAY_MSEC));
	return;
}
EXPORT_SYMBOL_GPL(hdcp_dplink_connect_state);

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
