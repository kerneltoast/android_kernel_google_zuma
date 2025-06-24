/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Edge TPU software WDT interface.
 *
 * Copyright (C) 2020 Google, Inc.
 */
#ifndef __EDGETPU_SW_WDT_H__
#define __EDGETPU_SW_WDT_H__

#include <linux/atomic.h>
#include <linux/workqueue.h>

#include "edgetpu-internal.h"

#define EDGETPU_ACTIVE_DEV_BEAT_MS 15000 /* 15 seconds */
#define EDGETPU_DORMANT_DEV_BEAT_MS 60000 /* 60 seconds */

struct edgetpu_sw_wdt_action_work {
	struct work_struct work;
	struct edgetpu_dev *etdev;
};

struct edgetpu_sw_wdt {
	struct delayed_work dwork;
	/* edgetpu device this watchdog is monitoring. */
	struct edgetpu_dev *etdev;
	/* Time period in jiffies in pinging device firmware. */
	unsigned long hrtbeat_jiffs;
	/* Heartbeat rates passed in edgetpu_sw_wdt_create(), in jiffies. */
	unsigned long hrtbeat_active;
	unsigned long hrtbeat_dormant;
	/* Work information for watchdog bite. */
	struct edgetpu_sw_wdt_action_work et_action_work;
	/* Flag to mark that watchdog is disabled. */
	bool is_wdt_disabled;
	/*
	 * When this counter is greater than zero, use @hrtbeat_active heartbeat
	 * rate, otherwise @hrtbeat_dormant.  Initial value is zero.
	 */
	atomic_t active_counter;
};

/*
 * Creates and assigns a watchdog object to @etdev->etdev_sw_wdt.
 *
 * @active_ms and @dormant_ms are the time periods in pinging device firmware
 * for active mode and dormant mode, respectively.
 */
int edgetpu_sw_wdt_create(struct edgetpu_dev *etdev, unsigned long active_ms,
			  unsigned long dormant_ms);
int edgetpu_sw_wdt_start(struct edgetpu_dev *etdev);
void edgetpu_sw_wdt_stop(struct edgetpu_dev *etdev);
void edgetpu_sw_wdt_destroy(struct edgetpu_dev *etdev);
/*
 * Increases the @active_counter.
 *
 * If @active_counter was zero, watchdog will be restarted with the active
 * heartbeat rate.
 */
void edgetpu_sw_wdt_inc_active_ref(struct edgetpu_dev *etdev);
/*
 * Decreases the @active_counter.
 *
 * If @active_counter was one, watchdog will be restarted with the dormant
 * heartbeat rate.
 */
void edgetpu_sw_wdt_dec_active_ref(struct edgetpu_dev *etdev);

/*
 * Trigger immediate sw watchdog timeout.  Called on fatal errors.
 */
void edgetpu_watchdog_bite(struct edgetpu_dev *etdev);

#endif /* __EDGETPU_SW_WDT_H__ */
