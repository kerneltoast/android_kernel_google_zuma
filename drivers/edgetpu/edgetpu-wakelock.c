// SPDX-License-Identifier: GPL-2.0
/*
 * Wakelock for the runtime to explicitly claim it's going to use the EdgeTPU
 * device.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>

#include "edgetpu-internal.h"
#include "edgetpu-wakelock.h"

static const char *const event_name[] = {
#define X(name, _) #name
	EDGETPU_WAKELOCK_EVENTS
#undef X
};

/*
 * Loops through the events and warns if any event has a non-zero counter.
 * Returns true if at least one non-zero counter is found.
 *
 * Caller holds @wakelock->lock.
 */
static bool wakelock_warn_non_zero_event(struct edgetpu_wakelock *wakelock)
{
	int i;
	bool ret = false;

	for (i = 0; i < EDGETPU_WAKELOCK_EVENT_END; i++) {
		if (wakelock->event_count[i]) {
			ret = true;
			etdev_warn(wakelock->etdev,
				   "%s has non-zero counter=%d", event_name[i],
				   wakelock->event_count[i]);
		}
	}
	return ret;
}

void edgetpu_wakelock_init(struct edgetpu_dev *etdev, struct edgetpu_wakelock *wakelock)
{
	memset(wakelock, 0, sizeof(*wakelock));
	wakelock->etdev = etdev;
	mutex_init(&wakelock->lock);
	/* Initialize client wakelock state to "released" */
	wakelock->req_count = 0;
}

bool edgetpu_wakelock_inc_event_locked(struct edgetpu_wakelock *wakelock,
				       enum edgetpu_wakelock_event evt)
{
	bool ret = true;

	if (!wakelock->req_count) {
		ret = false;
		etdev_warn(
			wakelock->etdev,
			"invalid increase event %d when wakelock is released",
			evt);
	} else {
		++wakelock->event_count[evt];
		/* integer overflow.. */
		if (unlikely(wakelock->event_count[evt] == 0)) {
			--wakelock->event_count[evt];
			ret = false;
			etdev_warn_once(wakelock->etdev,
					"int overflow on increasing event %d",
					evt);
		}
	}
	return ret;
}

bool edgetpu_wakelock_inc_event(struct edgetpu_wakelock *wakelock,
				enum edgetpu_wakelock_event evt)
{
	bool ret;

	mutex_lock(&wakelock->lock);
	ret = edgetpu_wakelock_inc_event_locked(wakelock, evt);
	mutex_unlock(&wakelock->lock);
	return ret;
}

bool edgetpu_wakelock_dec_event_locked(struct edgetpu_wakelock *wakelock,
				       enum edgetpu_wakelock_event evt)
{
	bool ret = true;

	if (!wakelock->event_count[evt]) {
		ret = false;
		etdev_warn(wakelock->etdev, "event %d unbalanced decreasing",
			   evt);
	} else {
		--wakelock->event_count[evt];
	}
	return ret;
}

bool edgetpu_wakelock_dec_event(struct edgetpu_wakelock *wakelock,
				enum edgetpu_wakelock_event evt)
{
	bool ret;

	mutex_lock(&wakelock->lock);
	ret = edgetpu_wakelock_dec_event_locked(wakelock, evt);
	mutex_unlock(&wakelock->lock);
	return ret;
}

uint edgetpu_wakelock_lock(struct edgetpu_wakelock *wakelock)
{
	mutex_lock(&wakelock->lock);
	return wakelock->req_count;
}

void edgetpu_wakelock_unlock(struct edgetpu_wakelock *wakelock)
{
	mutex_unlock(&wakelock->lock);
}

int edgetpu_wakelock_acquire(struct edgetpu_wakelock *wakelock)
{
	int ret;

	ret = wakelock->req_count++;
	/* integer overflow */
	if (unlikely(ret < 0)) {
		wakelock->req_count--;
		return -EOVERFLOW;
	}
	if (!ret)
		ktime_get_ts64(&wakelock->current_acquire_timestamp);
	return ret;
}

int edgetpu_wakelock_release(struct edgetpu_wakelock *wakelock)
{
	struct timespec64 curr;

	if (!wakelock->req_count) {
		etdev_warn(wakelock->etdev, "invalid wakelock release");
		return -EINVAL;
	}
	/* only need to check events when this is the last reference */
	if (wakelock->req_count == 1) {
		if (wakelock_warn_non_zero_event(wakelock)) {
			etdev_warn(
				   wakelock->etdev,
				   "detected non-zero events, refusing wakelock release");
			return -EAGAIN;
		}

		ktime_get_ts64(&curr);
		curr = timespec64_sub(curr, wakelock->current_acquire_timestamp);
		wakelock->total_acquired_time = timespec64_add(wakelock->total_acquired_time, curr);
	}

	return --wakelock->req_count;
}
