// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2021 Qorvo US, Inc.
 *
 */

#ifndef __QM357XX_FWPKG_H__
#define __QM357XX_FWPKG_H__

/* Pkg version word conversion macro */
#define PKG_VER_TO_U32(maj, min) (((maj) << 16) | ((min) << 24))

/* Pkg magic word conversion macro */
#define MAGIC_STR_TO_U32(x) \
	((uint32_t)(((x)[3]) | ((x)[2] << 8) | ((x)[1] << 16) | ((x)[0] << 24)))

/* Package size defines */
#define CRYPTO_FIRMWARE_PACK_ENC_DATA_SIZE 16
#define CRYPTO_FIRMWARE_PACK_FW_VERSION_SIZE 32
#define CRYPTO_FIRMWARE_PACK_TAG_SIZE 16

/* Firmware image size define */
#define CRYPTO_FW_PKG_IMG_HDR_IMG_NUM_MAX 8

/* Field values for macro firmware package */
#define CRYPTO_MACRO_FIRMWARE_PACK_MAGIC_VALUE MAGIC_STR_TO_U32("FWMP")

/* Field values for firmware package */
#define CRYPTO_FIRMWARE_PACK_MAGIC_VALUE MAGIC_STR_TO_U32("CFWP")
#define CRYPTO_FIRMWARE_PACK_VERSION PKG_VER_TO_U32(1, 0)
#define CRYPTO_FIRMWARE_PACK_ENC_MODE_NOT_ENCRYPTED 0x0
#define CRYPTO_FIRMWARE_PACK_ENC_ALGO_128BIT_AES_CTR 0x00
#define CRYPTO_FIRMWARE_PACK_ENC_KEY_L2_SIZE 64

/* Field values for firmware image */
#define CRYPTO_FIRMWARE_IMAGE_MAGIC_VALUE MAGIC_STR_TO_U32("IMGS")
#define CRYPTO_FIRMWARE_IMAGE_VERSION PKG_VER_TO_U32(1, 0)

/* Field values for firmware chunks */
#define CRYPTO_FIRMWARE_CHUNK_MAGIC_VALUE MAGIC_STR_TO_U32("CFWC")
#define CRYPTO_FIRMWARE_CHUNK_VERSION PKG_VER_TO_U32(1, 0)
#define CRYPTO_FIRMWARE_CHUNK_MIN_SIZE 16

/* fw certificate sizes */
#define CRYPTO_IMAGES_CERT_KEY_SIZE 840
#define CRYPTO_IMAGES_CERT_CONTENT_SIZE 868
#define CRYPTO_IMAGES_CERT_PKG_SIZE \
	(2 * CRYPTO_IMAGES_CERT_KEY_SIZE + CRYPTO_IMAGES_CERT_CONTENT_SIZE)
#define CRYPTO_IMAGES_NB_CERTS 3
#define CRYPTO_IMAGES_MAX_NB_IMAGES 8

#define CRYPTO_FIRMWARE_IMAGE_HDR_TOTAL_SIZE \
	(sizeof(struct fw_pkg_img_hdr_t) +   \
	 CRYPTO_IMAGES_NB_CERTS * sizeof(struct fw_img_desc_t))

/* Encryption mode enum. */
enum fw_pkg_enc_mode_e {
	CRYPTO_FIRMWARE_PACK_ENC_MODE_NONE,
	CRYPTO_FIRMWARE_PACK_ENC_MODE_ENCRYPTED
};

/* Package type enum. */
enum fw_pkg_package_type_e {
	CRYPTO_FIRMWARE_PACK_PACKAGE_TYPE_ICV = 0x01,
	CRYPTO_FIRMWARE_PACK_PACKAGE_TYPE_OEM = 0x02
};

/* IV type enum. */
enum fw_pkg_iv_type_e {
	CRYPTO_FIRMWARE_PACK_IV_TYPE_HDR,
	CRYPTO_FIRMWARE_PACK_IV_TYPE_IMG
};

/* Firmware Package Header fields */
struct fw_pkg_hdr_t {
	uint32_t magic; /**< Magic number. */
	uint32_t version; /**< Version. */
	uint8_t package_type; /**< Package type. */
	uint8_t enc_mode; /**< Encryption mode. */
	uint8_t enc_algo; /**< Encryption algorithm. */
	uint8_t reserved; /**< Reserved for alignment. */
	uint8_t enc_data
		[CRYPTO_FIRMWARE_PACK_ENC_DATA_SIZE]; /**< Encryption data. */
	uint8_t enc_key_l2
		[CRYPTO_FIRMWARE_PACK_ENC_KEY_L2_SIZE]; /**< Code encryption L2 key data. */
	uint8_t fw_version /**< Firmware version included in the package. */
		[CRYPTO_FIRMWARE_PACK_FW_VERSION_SIZE];
	uint32_t payload_len; /**< Payload length. */
	uint8_t tag[CRYPTO_FIRMWARE_PACK_TAG_SIZE]; /**< AES-CMAC Tag. */
} __attribute__((packed));

/* Firmware Image Metadata fields */
struct fw_img_desc_t {
	uint32_t offset; /**< Offset. */
	uint32_t length; /**< Length. */
} __attribute__((packed));

/* Firmware Image Header fields */
struct fw_pkg_img_hdr_t {
	uint32_t magic; /**< Magic number. */
	uint32_t version; /**< Version number. */
	uint32_t cert_chain_offset; /**< Certificate chain offset. */
	uint16_t cert_chain_length; /**< Certificate chain length. */
	uint8_t num_descs; /**< Number of images descriptors. */
	uint8_t reserved; /**< Reserved data. */
	struct fw_img_desc_t descs
		[CRYPTO_FW_PKG_IMG_HDR_IMG_NUM_MAX]; /**< Firmware image metadata. */
} __attribute__((packed));

/* Firmware Chunk fields */
struct fw_pkg_payload_chunk_t {
	uint32_t magic; /**< Magic number. */
	uint32_t version; /**< Version number. */
	uint32_t length; /**< Length. */
	uint8_t payload[]; /**< Payload data pointer. */
} __attribute__((packed));

/* Firmware Macro Package Header fields */
struct fw_macro_pkg_hdr_t {
	uint32_t magic; /**< Magic number. */
	uint32_t version; /**< Version. */
	uint8_t nb_descriptors;
	uint8_t reserved[3];
	struct fw_img_desc_t img_desc[];
} __attribute__((packed));

#define MACRO_PKG_HASH_SIZE (32)
#define COMPUTE_FW_MACRO_PKG_HDR_SIZE(ndescs) \
	(sizeof(struct fw_macro_pkg_hdr_t) +  \
	 ndescs * sizeof(struct fw_img_desc_t) + MACRO_PKG_HASH_SIZE)

#endif /* __QM357XX_FWPKG_H__ */
