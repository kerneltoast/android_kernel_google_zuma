/*
 * Broadcom Verhoeff Checksum Library.
 *
 * Copyright (C) 2024, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 */

/*
 *
 *    Copyright (c) 2013-2017 Nest Labs, Inc.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file implements an object for Verhoeff's check-digit
 *      algorithm for base-32 strings.
 *
 */

#include <typedefs.h>
#include <string.h>
#include <verhoeff_chksum.h>

static const uint8 base32_chars[] = "abcdefghijklmnopqrstuvwxyz234567";

static int
base32char_to_val(int8 p)
{
	return strchr((const char*)base32_chars, p) - (const char*)base32_chars;
}

static uint8
base32val_to_char(int i)
{
	return base32_chars[i];
}

static int
verhoeff_dihedral_multiply(int x, int y, int n)
{
	int n2 = n * 2;

	x = x % n2;
	y = y % n2;

	if (x < n) {
		if (y < n) {
			return  (x +  y) % n;
		} else {
			return ((x + (y - n)) % n) + n;
		}
	} else {
		if (y < n) {
			return ((n + (x - n) - y) % n) + n;
		} else {
			return  (n + (x - n) - (y - n)) % n;
		}
	}
}

static int verhoeff_dihedral_invert(int val, int n)
{
	if (val > 0 && val < n) {
		return n - val;
	} else {
		return val;
	}
}

static const uint8 verhoeff_permtable[32] = {
	7, 2, 1, 30, 16, 20, 27, 11,
	31, 6, 8, 13, 29, 5, 10, 21,
	22, 3, 24, 0, 23, 25, 12, 9,
	28, 14, 4, 15, 17, 18, 19, 26
};

static int verhoeff_permute(int val, int iter_count)
{
	val = val % 32;
	if (iter_count == 0) {
		return val;
	} else {
		return verhoeff_permute(verhoeff_permtable[val], iter_count - 1);
	}
}

bool bcm_verify_verhoeff_checksum(uint8* buf, uint16 buf_len)
{
	int c = 0;
	int val = 0;
	int p = 0;
	uint i = 0;

	if (!buf || buf_len == 0) {
		return FALSE;
	}

	for (i = 1u; i <= buf_len - 1u; i++) {
		val = base32char_to_val(buf[buf_len - 1u - i]);
		p = verhoeff_permute(val, i);
		c = verhoeff_dihedral_multiply(c, p, 16u);
	}

	c = verhoeff_dihedral_invert(c, 16u);
	return base32val_to_char(c) == buf[buf_len - 1u];
}
