/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file declares the gcip_status_code enum, which is based on the absl::StatusCode enum.
 * Some helper functions are also provides for converting between gcip_status_code and errno.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __GCIP_STATUS_CODE_H__
#define __GCIP_STATUS_CODE_H__

#include <linux/errno.h>

/* The status code ported from absl::StatusCode, which is heavily used by firmwares. */
enum gcip_status_code {
	GCIP_STATUS_CODE_OK = 0,
	GCIP_STATUS_CODE_CANCELLED = 1,
	GCIP_STATUS_CODE_UNKNOWN = 2,
	GCIP_STATUS_CODE_INVALID_ARGUMENT = 3,
	GCIP_STATUS_CODE_DEADLINE_EXCEEDED = 4,
	GCIP_STATUS_CODE_NOT_FOUND = 5,
	GCIP_STATUS_CODE_ALREADY_EXISTS = 6,
	GCIP_STATUS_CODE_PERMISSION_DENIED = 7,
	GCIP_STATUS_CODE_RESOURCE_EXHAUSTED = 8,
	GCIP_STATUS_CODE_FAILED_PRECONDITION = 9,
	GCIP_STATUS_CODE_ABORTED = 10,
	GCIP_STATUS_CODE_OUT_OF_RANGE = 11,
	GCIP_STATUS_CODE_UNIMPLEMENTED = 12,
	GCIP_STATUS_CODE_INTERNAL = 13,
	GCIP_STATUS_CODE_UNAVAILABLE = 14,
	GCIP_STATUS_CODE_DATA_LOSS = 15,
	GCIP_STATUS_CODE_UNAUTHENTICATED = 16,
};

/**
 * gcip_status_code_convert_from_errno - Converts errno to gcip_status_code.
 * @errno: The errno to be converted.
 *
 * Return: The gcip_status_code corresponding to the errno.
 */
enum gcip_status_code gcip_status_code_convert_from_errno(int errno);

/**
 * gcip_status_code_convert_to_errno - Converts gcip_status_code to errno.
 * @status: The gcip_status_code to be converted.
 *
 * Return: The errno corresponding to the gcip_status_code.
 */
int gcip_status_code_convert_to_errno(enum gcip_status_code status);

#endif /* __GCIP_STATUS_CODE_H__ */
