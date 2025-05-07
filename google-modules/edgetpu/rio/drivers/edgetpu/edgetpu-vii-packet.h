/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interface to access and set fields in VII packets, encapsulating knowledge of which VII format
 * is in use as well as the layout of each format's packets.
 *
 * Copyright (C) 2024 Google LLC
 */
#ifndef __EDGETPU_VII_PACKET_H__
#define __EDGETPU_VII_PACKET_H__

#include <linux/types.h>

#include "edgetpu-internal.h"

size_t edgetpu_vii_command_packet_size(struct edgetpu_dev *etdev);
size_t edgetpu_vii_response_packet_size(struct edgetpu_dev *etdev);

/* Command accessors */
u64 edgetpu_vii_command_get_seq_number(struct edgetpu_dev *etdev, void *cmd);
void edgetpu_vii_command_set_seq_number(struct edgetpu_dev *etdev, void *cmd, u64 seq_number);

u16 edgetpu_vii_command_get_code(struct edgetpu_dev *etdev, void *cmd);

u32 edgetpu_vii_command_get_client_id(struct edgetpu_dev *etdev, void *cmd);
void edgetpu_vii_command_set_client_id(struct edgetpu_dev *etdev, void *cmd, u32 client_id);

/*
 * Returns daddr of the additional_info of @cmd.
 * @additional_info_size is an optional pointer (can be NULL) to get the size of it.
 */
u32 edgetpu_vii_command_get_additional_info(struct edgetpu_dev *etdev, void *cmd,
					    u16 *additional_info_size);
void edgetpu_vii_command_set_additional_info(struct edgetpu_dev *etdev, void *cmd,
					     u32 additional_info_addr, u16 additional_info_size);

/* Response accessors */
u64 edgetpu_vii_response_get_seq_number(struct edgetpu_dev *etdev, void *resp);
void edgetpu_vii_response_set_seq_number(struct edgetpu_dev *etdev, void *resp, u64 seq_number);

u16 edgetpu_vii_response_get_code(struct edgetpu_dev *etdev, void *resp);
void edgetpu_vii_response_set_code(struct edgetpu_dev *etdev, void *resp, u16 code);

u64 edgetpu_vii_response_get_retval(struct edgetpu_dev *etdev, void *resp);
void edgetpu_vii_response_set_retval(struct edgetpu_dev *etdev, void *resp, u64 retval);

#endif /* __EDGETPU_VII_PACKET_H__*/
