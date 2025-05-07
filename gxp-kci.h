/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel Control Interface, implements the protocol between DSP Kernel driver and MCU firmware.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GXP_KCI_H__
#define __GXP_KCI_H__

#include <linux/bits.h>
#include <linux/rwsem.h>

#include <gcip/gcip-fault-injection.h>
#include <gcip/gcip-kci.h>
#include <gcip/gcip-telemetry.h>

#include "gxp-internal.h"
#include "gxp-mailbox.h"
#include "gxp-mcu-firmware.h"
#include "gxp-vd.h"

/*
 * Maximum number of outstanding KCI requests from firmware
 * This is used to size a circular buffer, so it must be a power of 2
 */
#define GXP_REVERSE_KCI_BUFFER_SIZE (32)

/* Timeout for KCI responses from the firmware (milliseconds) */
#ifndef GXP_KCI_TIMEOUT
#if IS_GXP_TEST
#define GXP_KCI_TIMEOUT (200) /* Fake firmware could respond in a short time. */
#elif IS_ENABLED(CONFIG_GXP_IP_ZEBU)
#define GXP_KCI_TIMEOUT (10000) /* 10 secs. */
#else
#define GXP_KCI_TIMEOUT (5000) /* 5 secs. */
#endif
#endif /* GXP_KCI_TIMEOUT */

/*
 * Operations of `allocate_vmbox` KCI command.
 * The bits of @operation of `struct gxp_kci_allocate_vmbox_detail` will be set with these.
 */
#define KCI_ALLOCATE_VMBOX_OP_ALLOCATE_VMBOX BIT(0)
#define KCI_ALLOCATE_VMBOX_OP_LINK_OFFLOAD_VMBOX BIT(1)

/*
 * Type of chip to link offload virtual mailbox.
 * @offload_type of `struct gxp_kci_allocate_vmbox_detail` will be set with these.
 */
#define KCI_ALLOCATE_VMBOX_OFFLOAD_TYPE_TPU 0

#define KCI_CIRCULAR_QUEUE_WRAP_BIT BIT(15)

/*
 * Chip specific reverse KCI request codes.
 */
enum gxp_reverse_rkci_code {
	GXP_RKCI_CODE_PM_QOS_BTS = GCIP_RKCI_CHIP_CODE_FIRST + 3,
	GXP_RKCI_CODE_CORE_TELEMETRY_READ = GCIP_RKCI_CHIP_CODE_FIRST + 4,
};

struct gxp_mcu;

struct gxp_kci {
	struct gxp_dev *gxp;
	struct gxp_mcu *mcu;
	struct gxp_mailbox *mbx;

	struct gxp_mapped_resource cmd_queue_mem;
	struct gxp_mapped_resource resp_queue_mem;
	struct gxp_mapped_resource descriptor_mem;

	/* If false, the kernel driver won't send RKCI ACK responses. */
	bool enable_rkci_ack;
	/* Protects @enable_rkci_ack. */
	struct rw_semaphore enable_rkci_ack_lock;
};

/* Used when sending the details about allocate_vmbox KCI command. */
struct gxp_kci_allocate_vmbox_detail {
	/* Client ID. */
	u32 client_id;
	/* The number of required cores. */
	u8 num_cores;
	/*
	 * Slice index of client_id used for identifying the 12KB slice buffer of memory to be
	 * used for MCU<->core mailbox.
	 */
	u8 slice_index;
	/* Whether it's the first time allocating a VMBox for this VD. */
	bool first_open;
	/* Reserved */
	u8 reserved[57];
} __packed;

/* Used when sending the details about release_vmbox KCI command. */
struct gxp_kci_release_vmbox_detail {
	/* Client ID. */
	u32 client_id;
	/* Reserved */
	u8 reserved[60];
} __packed;

/* Used when sending the details about {link,unlink}_offload_vmbox KCI command. */
struct gxp_kci_link_unlink_offload_vmbox_detail {
	/* DSP Client ID. */
	u32 client_id;
	/* Client ID of offload mailbox. */
	u32 offload_client_id;
	/*
	 * Chip type of offload mailbox.
	 * See enum gcip_kci_offload_chip_type.
	 */
	u8 offload_chip_type;
	/* Reserved */
	u8 reserved[55];
} __packed;

/*
 * Initializes a KCI object.
 *
 * Will request a mailbox from @mgr and allocate cmd/resp queues.
 */
int gxp_kci_init(struct gxp_mcu *mcu);

/*
 * Re-initializes the initialized KCI object.
 *
 * This function is used when the DSP device is reset, it re-programs CSRs
 * related to KCI mailbox.
 *
 * Returns 0 on success, -errno on error.
 */
int gxp_kci_reinit(struct gxp_kci *gkci);

/*
 * Enables / disables the IRQ handler of the KCI mailbox.
 *
 * The driver should disable the IRQ handler before calling the `gxp_kci_cancel_work_queues`
 * function to prevent a race condition that KCI works are scheduled while canceling them.
 */
void gxp_kci_enable_irq_handler(struct gxp_kci *gkci);
void gxp_kci_disable_irq_handler(struct gxp_kci *gkci);

/*
 * Enables / disables sending RKCI ACK responses to the MCU firmware.
 *
 * Since the RKCI requests are processed asynchronously, if the MCU sends RKCI requests right before
 * the SHUTDOWN KCI, the kernel driver may handle them after the MCU transits to the PG state and
 * sending RKCI ACK responses will cause waking the MCU up. It will eventually lead to the MCU boot
 * failure at the next run.
 *
 * To prevent those situations, the driver should control the timing of disabling / enabling sending
 * RKCI responses properly utilizing these functions.
 */
void gxp_kci_enable_rkci_ack(struct gxp_kci *gkci);
void gxp_kci_disable_rkci_ack(struct gxp_kci *gkci);

/* Cancel work queues or wait until they're done */
void gxp_kci_cancel_work_queues(struct gxp_kci *gkci);

/*
 * Releases resources allocated by @kci.
 *
 * Note: must invoke this function after the interrupt of mailbox disabled and
 * before free the mailbox pointer.
 */
void gxp_kci_exit(struct gxp_kci *gkci);

/*
 * Sends a FIRMWARE_INFO command and expects a response with a
 * gxp_mcu_firmware_info struct filled out, including what firmware type is running,
 * along with build CL and time.
 * Also serves as an initial handshake with firmware at load time.
 *
 * @fw_info: a struct gxp_mcu_firmware_info to be filled out by fw
 *
 * Returns >=0 gcip_fw_flavor when response received from firmware,
 *         <0 on error communicating with firmware (typically -ETIMEDOUT).
 */
enum gcip_fw_flavor gxp_kci_fw_info(struct gxp_kci *gkci,
				    struct gcip_fw_info *fw_info);

/*
 * Retrieves usage tracking data from firmware, update info on host.
 * Also used as a watchdog ping to firmware.
 *
 * Returns KCI response code on success or < 0 on error (typically -ETIMEDOUT).
 */
int gxp_kci_update_usage(struct gxp_kci *gkci);
void gxp_kci_update_usage_async(struct gxp_kci *gkci);

/*
 * Works the same as gxp_kci_update_usage() except the caller of this
 * function must guarantee the device stays powered up.
 *
 * Returns KCI response code on success or < 0 on error (typically -ETIMEDOUT).
 */
int gxp_kci_update_usage_locked(struct gxp_kci *gkci);

/*
 * Sends the "Map Log Buffer" command and waits for remote response.
 *
 * Returns the code of response, or a negative errno on error.
 */
int gxp_kci_map_mcu_log_buffer(struct gcip_telemetry_kci_args *args);

/*
 * Sends the "Map Trace Buffer" command and waits for remote response.
 *
 * Returns the code of response, or a negative errno on error.
 */
int gxp_kci_map_mcu_trace_buffer(struct gcip_telemetry_kci_args *args);

/* Send shutdown request to firmware */
int gxp_kci_shutdown(struct gxp_kci *gkci);

/*
 * Set the GXP thermal throttling.
 *
 * Returns the code of response, or a negative errno on error.
 */
int gxp_kci_notify_throttling(struct gxp_kci *gkci, u32 rate);

/*
 * Allocates a virtual mailbox to communicate with MCU firmware.
 *
 * A new client wants to run a workload on DSP, it needs to allocate a virtual mailbox. Creating
 * mailbox will be initiated from the application by calling GXP_ALLOCATE_VIRTUAL_DEVICE ioctl.
 * Allocated virtual mailbox should be released by calling `gxp_kci_release_vmbox`.
 *
 * Returns the code of response, or a negative errno on error.
 */
int gxp_kci_allocate_vmbox(struct gxp_kci *gkci, u32 client_id, u8 num_cores,
			   u8 slice_index, bool first_open);

/*
 * Releases a virtual mailbox which is allocated by `gxp_kci_allocate_vmbox`.
 * This function will be called by `gxp_vd_release`.
 *
 * Returns the code of response, or a negative errno on error.
 */
int gxp_kci_release_vmbox(struct gxp_kci *gkci, u32 client_id);

/*
 * Links or unlinks @client_id (DSP client ID) and @offload_client_id (Client ID of offloading
 * chip). It will link them if @link is true. Otherwise, it will unlink them.
 *
 * Link: Should be called before sending offload commands from DSP to the target chip.
 * Unlink: Should be called after offloading is completed.
 *
 * The type of the target chip should be passed to the @offload_chip_type.
 *
 * Returns the code of response, or a negative errno on error.
 */
int gxp_kci_link_unlink_offload_vmbox(
	struct gxp_kci *gkci, u32 client_id, u32 offload_client_id,
	enum gcip_kci_offload_chip_type offload_chip_type, bool link);

/*
 * Send an ack to the FW after handling a reverse KCI request.
 *
 * The FW may wait for a response from the kernel for an RKCI request so a
 * response could be sent as an ack.
 */
void gxp_kci_resp_rkci_ack(struct gxp_kci *gkci,
			   struct gcip_kci_response_element *rkci_cmd);

/*
 * Send device properties to firmware.
 * The device_prop KCI will be sent only when it is initialized.
 */
int gxp_kci_set_device_properties(struct gxp_kci *gkci,
				  struct gxp_dev_prop *device_prop);

/**
 * gxp_kci_fault_injection() - Sends the fault injection KCI command to the firmware.
 * @injection: The container of fault injection data.
 *
 * Return: 0 if the command is sent successfully.
 */
int gxp_kci_fault_injection(struct gcip_fault_inject *injection);

/**
 * gxp_kci_set_freq_limits() - Sends the frequency limits to be honoured to the firmware.
 * @gkci: Reference to the GXP KCI mailbox struct.
 * @min_freq: minimum frequency limit to be set.
 * @max_freq: maximum frequency limit to be set.
 *
 * Return: 0 if the command is sent successfully.
 */
int gxp_kci_set_freq_limits(struct gxp_kci *gkci, u32 min_freq, u32 max_freq);

/**
 * gxp_kci_thermal_control() - Sends KCI command to enable/disable thermal throttling.
 * @gkci: Reference to the GXP KCI mailbox struct.
 * @enable: bool to indicate whether to enable or disable thermal throttling.
 *
 * Return: 0 if the command is sent successfully.
 */

int gxp_kci_thermal_control(struct gxp_kci *gkci, bool enable);

#endif /* __GXP_KCI_H__ */
