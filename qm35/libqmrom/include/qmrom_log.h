/*
 * Copyright 2021 Qorvo US, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
 *
 * This file is provided under the Apache License 2.0, or the
 * GNU General Public License v2.0.
 *
 */
#ifndef __QMROM_LOGGER_H__
#define __QMROM_LOGGER_H__

enum log_level_e {
	LOG_QUIET = 0,
	LOG_ERR,
	LOG_WARN,
	LOG_INFO,
	LOG_DBG,
	LOG_LVL_MAX = LOG_DBG,
};

extern enum log_level_e __log_level__;

void set_log_level(enum log_level_e lvl);

static inline int is_debug_mode(void)
{
	return __log_level__ >= LOG_DBG;
}

static inline int is_log_level_allowed(enum log_level_e lvl)
{
	return (__log_level__ >= lvl);
}

void hexdump(enum log_level_e lvl, void *address, unsigned short length);
void hexrawdump(enum log_level_e lvl, void *address, unsigned short length);

#ifndef __KERNEL__
#include <stdio.h>
#define LOG_ERR(...)                                  \
	do {                                          \
		if (__log_level__ > LOG_QUIET)        \
			fprintf(stderr, __VA_ARGS__); \
	} while (0)
#define LOG_WARN(...)                                 \
	do {                                          \
		if (__log_level__ > LOG_ERR)          \
			fprintf(stderr, __VA_ARGS__); \
	} while (0)
#define LOG_INFO(...)                                 \
	do {                                          \
		if (__log_level__ > LOG_WARN)         \
			fprintf(stdout, __VA_ARGS__); \
	} while (0)
#define LOG_DBG(...)                                  \
	do {                                          \
		if (__log_level__ > LOG_INFO)         \
			fprintf(stdout, __VA_ARGS__); \
	} while (0)
#else
#include <linux/device.h>

extern struct device *__qmrom_log_dev__;

void qmrom_set_log_device(struct device *dev, enum log_level_e lvl);

#define LOG_ERR(...)                                                     \
	do {                                                             \
		if (__qmrom_log_dev__)                                   \
			if (__log_level__ > LOG_QUIET)                   \
				dev_err(__qmrom_log_dev__, __VA_ARGS__); \
	} while (0)
#define LOG_WARN(...)                                                     \
	do {                                                              \
		if (__qmrom_log_dev__)                                    \
			if (__log_level__ > LOG_ERR)                      \
				dev_warn(__qmrom_log_dev__, __VA_ARGS__); \
	} while (0)
#define LOG_INFO(...)                                                     \
	do {                                                              \
		if (__qmrom_log_dev__)                                    \
			if (__log_level__ > LOG_WARN)                     \
				dev_info(__qmrom_log_dev__, __VA_ARGS__); \
	} while (0)
#define LOG_DBG(...)                                                     \
	do {                                                             \
		if (__qmrom_log_dev__)                                   \
			if (__log_level__ > LOG_INFO)                    \
				dev_dbg(__qmrom_log_dev__, __VA_ARGS__); \
	} while (0)
#endif

#endif /* __QMROM_LOGGER_H__ */
