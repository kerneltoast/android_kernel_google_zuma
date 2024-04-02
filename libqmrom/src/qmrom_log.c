/*
 * Copyright 2021 Qorvo US, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
 *
 * This file is provided under the Apache License 2.0, or the
 * GNU General Public License v2.0.
 *
 */
#include <qmrom_log.h>

#define LOG_PRINT(lvl, ...)                    \
	do {                                   \
		switch (lvl) {                 \
		case LOG_ERR:                  \
			LOG_ERR(__VA_ARGS__);  \
			break;                 \
		case LOG_WARN:                 \
			LOG_WARN(__VA_ARGS__); \
			break;                 \
		case LOG_INFO:                 \
			LOG_INFO(__VA_ARGS__); \
			break;                 \
		case LOG_DBG:                  \
			LOG_DBG(__VA_ARGS__);  \
			break;                 \
		default:                       \
			break;                 \
		}                              \
	} while (0)

enum log_level_e __log_level__ = LOG_INFO;

void set_log_level(enum log_level_e lvl)
{
	__log_level__ = lvl;
}

#ifdef __KERNEL__
#include <linux/device.h>

struct device *__qmrom_log_dev__ = NULL;

void qmrom_set_log_device(struct device *dev, enum log_level_e lvl)
{
	__qmrom_log_dev__ = dev;
	__log_level__ = lvl;
}
#endif

void hexdump(enum log_level_e lvl, void *_array, unsigned short length)
{
	unsigned char *array = _array;
	int i;

	if (lvl < __log_level__)
		return;

	for (i = 0; i < length; i += 16) {
		int j = 0;
		for (; j < 15 && i + j + 1 < length; j++)
			LOG_PRINT(lvl, "0x%02x, ", array[i + j]);
		LOG_PRINT(lvl, "0x%02x\n", array[i + j]);
	}
}

void hexrawdump(enum log_level_e lvl, void *_array, unsigned short length)
{
	unsigned char *array = _array;
	int i;

	if (lvl < __log_level__)
		return;

	for (i = 0; i < length; i++) {
		LOG_PRINT(lvl, "%02x", array[i]);
	}
}
