// SPDX-License-Identifier: GPL-2.0-only
/*
 * Devfreq interface for the TPU device.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/units.h>

#include "edgetpu-config.h"
#include "edgetpu-devfreq.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"

static u32 edgetpu_devfreq_get_freq_table(void *data, u32 *dvfs_table, u32 max_size)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	int i, j;
	u32 distinct_freqs = 0;

	for (i = 0; i < etdev->num_active_states; i++) {
		/* Ignore the null frequency value. */
		if (!etdev->active_states[i])
			continue;

		/* Check if frequency value already exists in dvfs_table[]. */
		for (j = 0; j < distinct_freqs; j++) {
			if (dvfs_table[j] == etdev->active_states[i])
				break;
		}
		if (j != distinct_freqs)
			continue;

		/*
		 * Return 0 in case the number of available frequencies for the TPU exceeds the
		 * maximum number of frequencies the GCIP devfreq interface supports. On getting
		 * hit with this error consider increasing the maximum supported frequencies in the
		 * GCIP devfreq interface.
		 */
		if (distinct_freqs == max_size) {
			etdev_err(etdev,
				  "Number of distinct frequencies greater than max limit of %u.\n",
				  max_size);
			return 0;
		}

		/* `etdev->active_states` values are already in kHz. */
		dvfs_table[distinct_freqs++] = etdev->active_states[i];
	}

	return distinct_freqs;
}

static void edgetpu_devfreq_update_min_max_freq_range(void *data, u32 min_freq_khz,
						      u32 max_freq_khz)
{
	struct edgetpu_dev *etdev = (struct edgetpu_dev *)data;
	int ret;

	ret = edgetpu_pm_set_freq_limits(etdev, &min_freq_khz, &max_freq_khz);
	if (ret)
		etdev_err(etdev, "Failed to set [%u, %u] kHz range with error %d.", min_freq_khz,
			  max_freq_khz, ret);
}

static int edgetpu_devfreq_get_cur_freq(struct device *dev, unsigned long *freq_hz)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	long cur_freq_khz;

	cur_freq_khz = edgetpu_soc_pm_get_rate(etdev, 0);
	/*
	 * `edgetpu_soc_pm_get_rate` returns errors as negative errno values, but as a long rather
	 * than an int. Since the device frequency cannot be negative, treat all negative values as
	 * errors. This ensures the value is not changed by casting, just to use IS_ERR.
	 */
	if (cur_freq_khz < 0) {
		etdev_err(etdev, "Failed to fetch frequency with error %ld.", cur_freq_khz);
		cur_freq_khz = 0;
	}
	*freq_hz = cur_freq_khz * (unsigned long)HZ_PER_KHZ;

	return 0;
}

static const struct gcip_devfreq_ops devfreq_ops = {
	.get_freq_table = edgetpu_devfreq_get_freq_table,
	.update_min_max_freq_range = edgetpu_devfreq_update_min_max_freq_range,
	.get_cur_freq = edgetpu_devfreq_get_cur_freq,
};

int edgetpu_devfreq_create(struct edgetpu_dev *etdev)
{
	struct gcip_devfreq *devfreq;
	const struct gcip_devfreq_args args = {
		.dev = etdev->dev,
		.data = etdev,
		.ops = &devfreq_ops,
	};

	devfreq = gcip_devfreq_create(&args);
	if (IS_ERR(devfreq))
		return PTR_ERR(devfreq);

	etdev->devfreq = devfreq;
	return 0;
}

void edgetpu_devfreq_destroy(struct edgetpu_dev *etdev)
{
	if (!etdev->devfreq)
		return;

	gcip_devfreq_destroy(etdev->devfreq);
	etdev->devfreq = NULL;
}
