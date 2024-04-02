/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for CPU Idle metrics
 *
 * Copyright 2022 Google LLC
 */

#if IS_ENABLED(CONFIG_CPUIDLE_METRICS)
inline void cpuidle_metrics_histogram_append(int uid, s64 time_us);
int cpuidle_metrics_init(struct kobject *metrics_kobj);
void cpuidle_metrics_histogram_register(char* name, int uid, s64 target_residency_us);
#else
inline void cpuidle_metrics_histogram_append(int uid, s64 time_us) {};
int cpuidle_metrics_init(struct kobject *metrics_kobj) { return 0; };
void cpuidle_metrics_histogram_register(char* name, int uid, s64 target_residency_us) {};
#endif
