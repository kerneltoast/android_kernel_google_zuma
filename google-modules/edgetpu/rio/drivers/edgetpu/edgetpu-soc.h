/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Edge TPU driver SoC-specific APIs.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __EDGETPU_SOC_H__
#define __EDGETPU_SOC_H__

#include <linux/types.h>

#include "edgetpu-internal.h"
#include "edgetpu-kci.h"

/* SoC-specific calls for the following functions. */

/* Probe-time check to ensure supplier device drivers have probed first. */
int edgetpu_soc_check_supplier_devices(struct device *dev);

/* Probe-time early stage init, before power on. */
int edgetpu_soc_early_init(struct edgetpu_dev *etdev);

/* Probe-time after power on init. */
int edgetpu_soc_post_power_on_init(struct edgetpu_dev *etdev);

/* Module remove-time exit. */
void edgetpu_soc_exit(struct edgetpu_dev *etdev);

/* Prep for running firmware: set access control, etc. */
int edgetpu_soc_prepare_firmware(struct edgetpu_dev *etdev);

/*
 * Power management get TPU clock rate.
 * @flags can be used by platform-specific code to pass additional flags to the SoC
 *        handler; for calls from generic code this value must be zero.
 */
long edgetpu_soc_pm_get_rate(struct edgetpu_dev *etdev, int flags);

/* Power down */
void edgetpu_soc_pm_power_down(struct edgetpu_dev *etdev);

/* Is the TPU block powered off? */
bool edgetpu_soc_pm_is_block_off(struct edgetpu_dev *etdev);

/* Init SoC PM system */
int edgetpu_soc_pm_init(struct edgetpu_dev *etdev);

/* De-init SoC PM system */
void edgetpu_soc_pm_exit(struct edgetpu_dev *etdev);

/* Handle bringing control cluster LPM up. */
int edgetpu_soc_pm_lpm_up(struct edgetpu_dev *etdev);

/* Wait for control cluster LPM down. */
void edgetpu_soc_pm_lpm_down(struct edgetpu_dev *etdev);

/* Called after firmware is started on power up. */
void edgetpu_soc_pm_post_fw_start(struct edgetpu_dev *etdev);

/*
 * Log TPU block power state for debugging.  The block may be required to be powered up,
 * depending on the SoC family implementation.  Calls from common code must ensure device is
 * powered up.
 */
void edgetpu_soc_pm_dump_block_state(struct edgetpu_dev *etdev);

/*
 * Handle Reverse KCI commands for SoC family.
 * Note: This will get called from the system's work queue.
 * Code should not block for extended periods of time
 */
void edgetpu_soc_handle_reverse_kci(struct edgetpu_dev *etdev,
				    struct gcip_kci_response_element *resp);

/* Init thermal subsystem SoC specifics for TPU */
void edgetpu_soc_thermal_init(struct edgetpu_dev *etdev);

/* De-init thermal subsystem SoC specifics for TPU */
void edgetpu_soc_thermal_exit(struct edgetpu_dev *etdev);

/* Activates the context of @pasid. */
int edgetpu_soc_activate_context(struct edgetpu_dev *etdev, int pasid);

/* Deactivates the context of @pasid. */
void edgetpu_soc_deactivate_context(struct edgetpu_dev *etdev, int pasid);

/* Set security CSRs for TPU CPU / instruction remap region. */
void edgetpu_soc_set_tpu_cpu_security(struct edgetpu_dev *etdev);

/* Parse and setup IRQs at probe time. */
int edgetpu_soc_setup_irqs(struct edgetpu_dev *etdev);

#endif /* __EDGETPU_SOC_H__ */
