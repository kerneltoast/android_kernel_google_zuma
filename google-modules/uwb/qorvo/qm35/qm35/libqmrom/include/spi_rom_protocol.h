/*
 * Copyright 2021 Qorvo US, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
 *
 * This file is provided under the Apache License 2.0, or the
 * GNU General Public License v2.0.
 *
 */
#ifndef __SPI_ROM_PROTOCOL_H__
#define __SPI_ROM_PROTOCOL_H__

#include <qmrom_error.h>
#include <qmrom.h>

#ifndef __KERNEL__
#include <stdint.h>
#else
#include <linux/types.h>
#endif

#define SPI_PROTO_WRONG_RESP SPI_PROTO_ERR_BASE - 1

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
struct stc {
	union {
		struct {
			union {
				struct {
					uint8_t reserved : 5;
					uint8_t read : 1; // Read indication: Set to one by the host to tell the SOC SPI driver that this transaction is a read, otherwise it is set to zero.
					uint8_t pre_read : 1; // Pre-read indication: Set to one by the host to tell the SOC SPI driver that the next transaction will be a read, otherwise it is set to zero
					uint8_t write : 1; // Write indication: Set to one by the host when it is doing a write transaction. Set to zero when the host is not doing a write.
				} host_flags;
				struct {
					uint8_t reserved : 4;
					uint8_t err : 1; // Error indication: This is set to one by the SOC SPI driver to tell the HOST that it has detected some error in the SPI protocol.
					uint8_t ready : 1; //
					uint8_t out_active : 1; // Output active indication: Set to one by the SOC SPI driver to tell the HOST that it is outputting data on MISO line and expecting that the host is doing a read transaction at this time. This is set to zero for all other transactions.
					uint8_t out_waiting : 1; // Output data waiting indication: Set to one by the SOC SPI driver to tell the HOST there is data awaiting reading. This is set to zero when there is no data pending output.
				} soc_flags;
				uint8_t raw_flags;
			};
			uint8_t ul;
			uint16_t len;
		};
		uint32_t all;
	};
	uint8_t payload[0];
} __attribute__((packed));
#pragma GCC diagnostic pop

/* Host to Soc (HS) masks */
#define SPI_HS_WRITE_CMD_BIT_MASK 0x80
#define SPI_HS_PRD_CMD_BIT_MASK 0x40
#define SPI_HS_RD_CMD_BIT_MASK 0x20

/* Soc to Host (SH) masks */
#define SPI_SH_ODW_BIT_MASK 0x80
#define SPI_SH_ACTIVE_BIT_MASK 0x40
#define SPI_SH_READY_CMD_BIT_MASK 0x20
#define SPI_DEVICE_READY_FLAGS SPI_SH_ODW_BIT_MASK

/* Communication parameters */
#define MAX_STC_FRAME_LEN 2048
#define MAX_STC_PAYLOAD_LEN (MAX_STC_FRAME_LEN - sizeof(struct stc))
#define SPI_NUM_READS_FOR_READY 1
#define SPI_NUM_FAILS_RETRY 4
#define SPI_ET_PROTOCOL 5
#define SPI_RST_LOW_DELAY_MS 20
#define SPI_INTERCMD_DELAY_MS 1
#define SPI_DEVICE_POLL_RETRY 10
#define SPI_READY_TIMEOUT_MS 50
#define SPI_ET_VERSION_LOCATION 0x601f0000

/* ROM boot proto */
#define SPI_ROM_READ_VERSION_SIZE_A0 3
#define SPI_ROM_READ_IMAGE_CERT_SIZE_A0 3
#define SPI_ROM_READ_IMAGE_SIZE_SIZE_A0 3

#define SPI_ROM_READ_VERSION_SIZE_B0 5
#define SPI_ROM_READ_INFO_SIZE_B0 50
#define SPI_ROM_READ_IMAGE_CERT_SIZE_B0 1
#define SPI_ROM_READ_IMAGE_SIZE_SIZE_B0 1

#define SPI_ROM_WRITE_KEY_CERT_SIZE 6
#define SPI_ROM_WRITE_IMAGE_CERT_SIZE 40
#define SPI_ROM_WRITE_IMAGE_SIZE_SIZE 4
#define SPI_ROM_DBG_CERT_SIZE_SIZE 5

#endif /* __SPI_ROM_PROTOCOL_H__ */
