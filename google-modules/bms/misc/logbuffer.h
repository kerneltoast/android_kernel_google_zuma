/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019-2022 Google LLC
 */

#ifndef __GOOGLE_LOGBUFFER_H_
#define __GOOGLE_LOGBUFFER_H_

#include <linux/stdarg.h>

struct logbuffer;

void logbuffer_log(struct logbuffer *instance, const char *fmt, ...);
void logbuffer_logk(struct logbuffer *instance, int loglevel, const char *fmt, ...);
void logbuffer_vlog(struct logbuffer *instance, const char *fmt,
		    va_list args);
int dev_logbuffer_logk(struct device *dev, struct logbuffer *instance, int loglevel,
		       const char *fmt, ...);

/*
 * Registers a new log buffer entry.
 * param name: name of the file in the /d/logbuffer/ directory.
 * returns the pointer to the logbuffer metadata.
 */
struct logbuffer *logbuffer_register(const char *name);

void logbuffer_unregister(struct logbuffer *instance);

#endif /* __GOOGLE_LOGBUFFER_H_ */

