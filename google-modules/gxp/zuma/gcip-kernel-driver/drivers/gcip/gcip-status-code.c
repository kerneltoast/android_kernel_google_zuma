// SPDX-License-Identifier: GPL-2.0-only
/*
 * The helper functions to convert gcip_status_code
 *
 * Copyright (C) 2025 Google LLC
 */

#include <linux/errno.h>

#include <gcip/gcip-status-code.h>

enum gcip_status_code gcip_status_code_convert_from_errno(int errno)
{
	switch (errno) {
	case 0:
		return GCIP_STATUS_CODE_OK;
	case -ECANCELED:
		return GCIP_STATUS_CODE_CANCELLED;
	case -EINVAL:
	case -EFAULT:
		return GCIP_STATUS_CODE_INVALID_ARGUMENT;
	case -ETIME:
	case -ETIMEDOUT:
		return GCIP_STATUS_CODE_DEADLINE_EXCEEDED;
	case -ENOENT:
	case -ENODEV:
		return GCIP_STATUS_CODE_NOT_FOUND;
	case -EEXIST:
		return GCIP_STATUS_CODE_ALREADY_EXISTS;
	case -EACCES:
		return GCIP_STATUS_CODE_PERMISSION_DENIED;
	case -ENOMEM:
	case -ENOSPC:
		return GCIP_STATUS_CODE_RESOURCE_EXHAUSTED;
	case -EBUSY:
		return GCIP_STATUS_CODE_FAILED_PRECONDITION;
	case -EDEADLK:
	case -EOWNERDEAD:
	case -EALREADY:
		return GCIP_STATUS_CODE_ABORTED;
	case -ERANGE:
	case -EOVERFLOW:
		return GCIP_STATUS_CODE_OUT_OF_RANGE;
	case -EOPNOTSUPP:
		return GCIP_STATUS_CODE_UNIMPLEMENTED;
	case -EIO:
		return GCIP_STATUS_CODE_INTERNAL;
	case -EAGAIN:
		return GCIP_STATUS_CODE_UNAVAILABLE;
	case -EBADMSG:
	case -ENOTRECOVERABLE:
		return GCIP_STATUS_CODE_DATA_LOSS;
	case -EPERM:
		return GCIP_STATUS_CODE_UNAUTHENTICATED;
	default:
		return GCIP_STATUS_CODE_UNKNOWN;
	}
}

int gcip_status_code_convert_to_errno(enum gcip_status_code status)
{
	switch (status) {
	case GCIP_STATUS_CODE_OK:
		return 0;
	case GCIP_STATUS_CODE_CANCELLED:
		return -ECANCELED;
	case GCIP_STATUS_CODE_INVALID_ARGUMENT:
		return -EINVAL;
	case GCIP_STATUS_CODE_DEADLINE_EXCEEDED:
		return -ETIMEDOUT;
	case GCIP_STATUS_CODE_NOT_FOUND:
		return -ENOENT;
	case GCIP_STATUS_CODE_ALREADY_EXISTS:
		return -EEXIST;
	case GCIP_STATUS_CODE_PERMISSION_DENIED:
		return -EACCES;
	case GCIP_STATUS_CODE_RESOURCE_EXHAUSTED:
	case GCIP_STATUS_CODE_FAILED_PRECONDITION:
	case GCIP_STATUS_CODE_ABORTED:
		return -EBUSY;
	case GCIP_STATUS_CODE_OUT_OF_RANGE:
		return -ERANGE;
	case GCIP_STATUS_CODE_UNIMPLEMENTED:
		return -EOPNOTSUPP;
	case GCIP_STATUS_CODE_UNAVAILABLE:
		return -EAGAIN;
	case GCIP_STATUS_CODE_UNAUTHENTICATED:
		return -EPERM;
	case GCIP_STATUS_CODE_DATA_LOSS:
	case GCIP_STATUS_CODE_INTERNAL:
	case GCIP_STATUS_CODE_UNKNOWN:
	default:
		return -EIO;
	}
}
