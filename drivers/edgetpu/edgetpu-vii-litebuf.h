/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Packet definitions for the litebuf-based version of the VII protocol.
 *
 * Copyright (C) 2024 Google LLC
 */
#ifndef __EDGETPU_VII_LITEBUF_H__
#define __EDGETPU_VII_LITEBUF_H__

/*
 * Total size of a VII command which contains:
 * 1. Info used by the Kernel Driver.
 * 2. A payload populated by Runtime or other entity.
 */
#define VII_CMD_SIZE_BYTES 128
/* Portion of the command used by Kernel Driver. */
#define VII_CMD_KERNEL_USABLE_BYTES 32
/* Size of the command payload. */
#define VII_CMD_PAYLOAD_SIZE_BYTES (VII_CMD_SIZE_BYTES - VII_CMD_KERNEL_USABLE_BYTES)

/* Possible command types. */
enum edgetpu_vii_litebuf_command_type {
	/* A RuntimeCommand Litebuf embedded in this struct. */
	EDGETPU_VII_LITEBUF_RUNTIME_COMMAND = 0,
	/* A RuntimeCommand Litebuf linked in a separate buffer. */
	EDGETPU_VII_LITEBUF_LARGE_RUNTIME_COMMAND = 1,
};

struct edgetpu_vii_litebuf_large_runtime_command {
	/*
	 * Device address of the buffer.
	 * The address must be 16-byte aligned to be parsed properly.
	 */
	u32 address;
	/*
	 * Size of the LiteBuf in bytes. Note that the current schema supports only up to 16 bit
	 * offsets, but use 32 bit type here in case larger commands need be supported in the
	 * future.
	 */
	u32 size_bytes;
};

/* Command sent via VII mailbox. */
struct edgetpu_vii_litebuf_command {
	/*
	 * Possible command payloads based on @type.
	 *
	 * Note that litebufs can only be parsed if they are 16-byte aliged. The driver does not
	 * enforce this alignment for packets en route to the mailbox queue, since the driver does
	 * not access the litebuf. Since the mailbox queues and packet size are 16-byte aligned,
	 * the packet will always be parsable by firmware from the queue.
	 */
	union {
		u8 runtime_command[VII_CMD_PAYLOAD_SIZE_BYTES];
		struct edgetpu_vii_litebuf_large_runtime_command large_runtime_command;
		u8 max_payload_size[VII_CMD_PAYLOAD_SIZE_BYTES];
	};
	u8 reserved_0[16];
	/* Sequence number. Must match the corresponding response. */
	u32 seq;
	/* Unique ID for each client that identifies client VM & security realm. */
	u32 client_id;
	/*
	 * Device address for optional buffer in remapped DRAM containing additional command info
	 * from kernel driver. Additional info is valid if the address is non-zero.
	 */
	u32 additional_info_address;
	/* Size of the additional info in bytes. */
	u16 additional_info_size;
	/* Type of this command. Value is an edgetpu_vii_litebuf_command_type. */
	u8 type;
	u8 reserved_1[1];
};

/*
 * Total size of a VII response which contains:
 * 1. Info used by the Kernel Driver.
 * 2. A payload for the Runtime or other entity.
 */
#define VII_RESP_SIZE_BYTES 64
/* Portion of the response used by Kernel Driver. */
#define VII_RESP_KERNEL_USABLE_BYTES 16
/* Size of the response payload. */
#define VII_RESP_PAYLOAD_SIZE_BYTES (VII_RESP_SIZE_BYTES - VII_RESP_KERNEL_USABLE_BYTES)

/* Possible response types. */
enum edgetpu_vii_litebuf_response_type {
	EDGETPU_VII_LITEBUF_RUNTIME_RESPONSE = 0,
};

/* Response sent via VII mailbox. */
struct edgetpu_vii_litebuf_response {
	/*
	 * Possible response payloads based on @type.
	 *
	 * Note that litebufs can only be parsed if they are 16-byte aliged. The driver does not
	 * enforce this alignment for packets en route from the mailbox queue, since the driver
	 * does not access the litebuf. User-space clients consuming responses are responsible for
	 * providing 16-byte aligned buffers for the driver to copy responses to.
	 */
	union {
		u8 runtime_response[VII_RESP_PAYLOAD_SIZE_BYTES];
		u8 max_payload_size[VII_RESP_PAYLOAD_SIZE_BYTES];
	};
	u8 reserved_0[4];
	/* Sequence number. Must match the corresponding command. */
	u32 seq;
	/* Unique ID for each client that identifies client VM & security realm. */
	u32 client_id;
	/* Status code indicating success or error. */
	u16 code;
	/* Type of this response. Value is an edgetpu_vii_litebuf_response_type. */
	u8 type;
	u8 reserved_1[1];
};

#endif /* __EDGETPU_VII_LITEBUF_H__*/
