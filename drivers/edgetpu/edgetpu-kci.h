/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernel Control Interface, implements the protocol between AP kernel and TPU
 * firmware.
 *
 * Copyright (C) 2019 Google, Inc.
 */
#ifndef __EDGETPU_KCI_H__
#define __EDGETPU_KCI_H__

#include <linux/dma-direction.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <gcip/gcip-fault-injection.h>
#include <gcip/gcip-kci.h>

#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"

/*
 * Maximum number of outstanding KCI requests from firmware
 * This is used to size a circular buffer, so it must be a power of 2
 */
#define REVERSE_KCI_BUFFER_SIZE		(32)

/* Edgetpu KCI structure */
struct edgetpu_kci {
	struct gcip_kci *kci;
	struct edgetpu_mailbox *mailbox;
	/* Command queue buffer */
	struct edgetpu_coherent_mem cmd_queue_mem;
	/* Response queue buffer */
	struct edgetpu_coherent_mem resp_queue_mem;
};

/* VII response element */
/* The size of this structure must match the runtime definition. */
struct edgetpu_vii_response_element {
	u64 seq;
	u16 code;
	u8 reserved[6];	/* padding */
	u64 retval;
} __packed;

struct edgetpu_kci_device_group_detail {
	u8 n_dies;
	/* virtual ID from 0 ~ n_dies - 1 */
	/* ID 0 for the group master */
	u8 vid;
	u8 reserved[6]; /* padding */
};

struct edgetpu_kci_open_device_detail {
	/* The client privilege level. */
	u16 client_priv;
	/*
	 * Virtual context ID @mailbox_id is associated to.
	 * For device groups with @mailbox_detachable attribute the mailbox attached to the group
	 * can be different after wakelock re-acquired. Firmware uses this VCID to identify the
	 * device group.
	 */
	u16 vcid;
	/*
	 * Extra flags for the attributes of this request.
	 * Set RESERVED bits to 0 to ensure backwards compatibility.
	 *
	 * Bitfields:
	 *   [0:0]   - first_open: Specifies if this is the first time we are calling mailbox open
	 *             KCI for this VCID after it has been allocated to a device group. This allows
	 *             firmware to clean up/reset the memory allocator for that partition.
	 *   [31:1]  - RESERVED
	 */
	u32 flags;
};

/* Argument struct for `GCIP_KCI_CODE_ALLOCATE_VMBOX`. Must match firmware definition. */
struct edgetpu_kci_allocate_vmbox_detail {
	/*
	 * ID encoding security realm, VM ID, and client page-table ID.
	 * - Security realm is always "non-secure" for kernel-driver (bits TBD)
	 * - VM ID is always 0 for now (bits TBD)
	 * - Page-table ID is equal to the domain's PASID obtained from the iommu driver (bits tbd)
	 */
	u32 client_id;
	/* Not used by TPU */
	u8 reserved_num_cores;
	/* The VCID assigned to the device group */
	u8 slice_index;
	/*
	 * Specifies if this is the first time we are calling allocate vmbox KCI for this VCID
	 * after it has been allocated to a device group. This allows firmware to clean up/reset
	 * the memory allocator for that partition.
	 */
	bool first_open;
	/*
	 * Specifies whether the client that will use this virtual mailbox is a first-party
	 * application or not. Firmware's use of this information is transparent to the Kernel.
	 */
	bool first_party;
	u8 reserved[56];
};

/* Argument struct for `GCIP_KCI_CODE_RELEASE_VMBOX`. Must match firmware definition. */
struct edgetpu_kci_release_vmbox_detail {
	/* ID of the VMbox to be released. The same as was passed to allocate_vmbox. */
	u32 client_id;
	u8 reserved[60];
};

/*
 * Initializes a KCI object.
 *
 * Will request a mailbox from @mgr and allocate cmd/resp queues.
 */
int edgetpu_kci_init(struct edgetpu_mailbox_manager *mgr, struct edgetpu_kci *etkci);
/*
 * Re-initializes the initialized KCI object.
 *
 * This function is used when the TPU device is reset, it re-programs CSRs
 * related to KCI mailbox.
 *
 * Returns 0 on success, -errno on error.
 */
int edgetpu_kci_reinit(struct edgetpu_kci *etkci);
/*
 * Releases resources allocated by @kci.
 *
 * Note: must invoke this function after the interrupt of mailbox disabled and
 * before free the mailbox pointer.
 */
void edgetpu_kci_release(struct edgetpu_dev *etdev, struct edgetpu_kci *etkci);

/*
 * Sends a FIRMWARE_INFO command and expects a response with a
 * gcip_fw_info struct filled out, including what firmware type is running,
 * along with build CL and time.
 * Also serves as an initial handshake with firmware at load time.
 *
 * @fw_info: a struct gcip_fw_info to be filled out by fw
 *
 * Returns >=0 gcip_fw_flavor when response received from firmware,
 *         <0 on error communicating with firmware (typically -ETIMEDOUT).
 */
enum gcip_fw_flavor edgetpu_kci_fw_info(struct edgetpu_kci *etkci, struct gcip_fw_info *fw_info);

/*
 * Retrieves usage tracking data from firmware, update info on host.
 * Also used as a watchdog ping to firmware.
 *
 * Returns KCI response code on success or < 0 on error (typically -ETIMEDOUT).
 */
int edgetpu_kci_update_usage(struct edgetpu_dev *etdev);

/*
 * Works the same as edgetpu_kci_update_usage() except the caller of this
 * function must guarantee the device stays powered up, typically by calling
 * edgetpu_pm_get() or by calling this function from the power management
 * functions themselves.
 *
 * Returns KCI response code on success or < 0 on error (typically -ETIMEDOUT).
 */
int edgetpu_kci_update_usage_locked(struct edgetpu_dev *etdev);

struct gcip_telemetry_kci_args;

/*
 * Sends the "Map Log Buffer" command and waits for remote response.
 *
 * Returns the code of response, or a negative errno on error.
 */
int edgetpu_kci_map_log_buffer(struct gcip_telemetry_kci_args *args);

/*
 * Sends the "Map Trace Buffer" command and waits for remote response.
 *
 * Returns the code of response, or a negative errno on error.
 */
int edgetpu_kci_map_trace_buffer(struct gcip_telemetry_kci_args *args);

/* debugfs mappings dump */
void edgetpu_kci_mappings_show(struct edgetpu_dev *etdev, struct seq_file *s);

/* Send shutdown request to firmware */
int edgetpu_kci_shutdown(struct edgetpu_kci *etkci);

/* Request dump of inaccessible segments from firmware.
 *
 * @init_buffer flag is used to indicate that the req is only sent to set the dump buffer address
 * and size in FW.
 */
int edgetpu_kci_get_debug_dump(struct edgetpu_kci *etkci, tpu_addr_t tpu_addr, size_t size,
			       bool init_buffer);

/*
 * Inform the firmware to prepare to serve VII mailboxes included in @mailbox_map.
 *
 * You usually shouldn't call this directly - consider using edgetpu-mailbox.h interfaces instead.
 */
int edgetpu_kci_open_device(struct edgetpu_kci *etkci, u32 mailbox_map, u32 client_priv, s16 vcid,
			    bool first_open);

/*
 * Inform the firmware that the VII mailboxes included in @mailbox_map are closed.
 *
 * You usually shouldn't call this directly - consider using edgetpu-mailbox.h interfaces instead.
 */
int edgetpu_kci_close_device(struct edgetpu_kci *etkci, u32 mailbox_map);

/* Cancel work queues or wait until they're done */
void edgetpu_kci_cancel_work_queues(struct edgetpu_kci *etkci);

/*
 * Notify the firmware about throttling and the corresponding power level.
 * The request is sent only if the device is already powered on.
 *
 * Returns KCI response code on success or < 0 on error (typically -ETIMEDOUT).
 */
int edgetpu_kci_notify_throttling(struct edgetpu_dev *etdev, u32 level);

/*
 * Request the firmware to {un}block modulating bus clock speeds
 *
 * Used to prevent conflicts when sending a thermal policy request
 */
int edgetpu_kci_block_bus_speed_control(struct edgetpu_dev *etdev, bool block);

/*
 * Request firmware open a virtual VII mailbox for a client, routed through in-kernel VII
 *
 * You usually shouldn't call this directly - consider using edgetpu-mailbox.h interfaces instead.
 */
int edgetpu_kci_allocate_vmbox(struct edgetpu_kci *etkci, u32 client_id, u8 slice_index,
			       bool first_open, bool first_party);

/*
 * Request firmware close a virtual VII mailbox for a client, routed through in-kernel VII
 *
 * You usually shouldn't call this directly - consider using edgetpu-mailbox.h interfaces instead.
 */
int edgetpu_kci_release_vmbox(struct edgetpu_kci *etkci, u32 client_id);

/* Set the firmware tracing level. */
int edgetpu_kci_firmware_tracing_level(void *data, unsigned long level,
				       unsigned long *active_level);

/*
 * Request the firmware to enable or disable the thermal throttling.
 *
 * Returns KCI response code on success or < 0 on error (typically -ETIMEDOUT).
 */

int edgetpu_kci_thermal_control(struct edgetpu_dev *etdev, bool enable);

/*
 * Sends device properties to firmware.
 * The KCI command will be sent only when @device_prop is initialized.
 */
int edgetpu_kci_set_device_properties(struct edgetpu_kci *etkci,
				      struct edgetpu_dev_prop *device_prop);
/*
 * Sends min/max frequency limits for firmware to enforce when handling client power requests.
 *
 * Arguments are in kHz and inclusive. For example, a max of 1000 kHz will allow frequencies up to
 * and including 1000 kHz. If a value of 0 is requested for a given limit, than no limit is enforced
 * when considering client power state requests.
 *
 * Note that thermal constraints can still override a minimum limit set by this KCI command.
 *
 * Returns 0 on success or < 0 on error.
 */
int edgetpu_kci_set_freq_limits(struct edgetpu_kci *etkci, u32 min_freq, u32 max_freq);

/*
 * Send an ack to the FW after handling a reverse KCI request.
 *
 * The FW may wait for a response from the kernel for an RKCI request so a
 * response could be sent as an ack.
 */
int edgetpu_kci_resp_rkci_ack(struct edgetpu_dev *etdev,
			      struct gcip_kci_response_element *rkci_cmd);

/*
 * Flush any pending reverse KCI requests.
 *
 * All pending requests at time of call will be complete upon return. Requests arriving after the
 * call may or may not be still pending.
 *
 * Returns true if any work was pending, false if the worker was already idle.
 */
bool edgetpu_kci_flush_rkci(struct edgetpu_dev *etdev);

static inline void edgetpu_kci_update_usage_async(struct edgetpu_kci *etkci)
{
	gcip_kci_update_usage_async(etkci->kci);
}

/**
 * edgetpu_kci_fault_injection() - Sends the fault injection KCI command to the firmware.
 * @injection: The container of fault injection data.
 *
 * Return: 0 if the command is sent successfully.
 */
int edgetpu_kci_fault_injection(struct gcip_fault_inject *injection);

/**
 * edgetpu_kci_fw_debug_cmd() - Send firmware debug service command data.
 * @daddr: device address within etdev->fw_debug_mem.sgt of command data.
 * @count: number of bytes of command data to send.
 */
int edgetpu_kci_fw_debug_cmd(struct edgetpu_dev *etdev, dma_addr_t daddr, size_t count);

/**
 * edgetpu_kci_fw_debug_reset() - Send firmware debug service reset while waiting for async
 * response to the previous command.
 */
int edgetpu_kci_fw_debug_reset(struct edgetpu_dev *etdev);

#endif /* __EDGETPU_KCI_H__ */
