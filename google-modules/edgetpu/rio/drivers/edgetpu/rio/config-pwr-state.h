/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip-dependent power configuration and states.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __RIO_CONFIG_PWR_STATE_H__
#define __RIO_CONFIG_PWR_STATE_H__

/* TPU Power States. See zuma-dvfs table. */
enum edgetpu_pwr_state {
	TPU_OFF = 0,
	TPU_ACTIVE_MIN = 226000,
	TPU_ACTIVE_ULTRA_LOW = 455000,
	TPU_ACTIVE_VERY_LOW = 627000,
	TPU_ACTIVE_SUB_LOW = 712000,
	TPU_ACTIVE_LOW = 845000,
	TPU_ACTIVE_MEDIUM = 1066000,
	TPU_ACTIVE_NOM = 1119000,
};

#define MIN_ACTIVE_STATE TPU_ACTIVE_MIN
#define TPU_POLICY_MAX	TPU_ACTIVE_NOM
#define EDGETPU_NUM_STATES 7

#endif /* __RIO_CONFIG_PWR_STATE_H__ */
