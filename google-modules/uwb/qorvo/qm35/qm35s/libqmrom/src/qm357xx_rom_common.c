// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2022 Qorvo US, Inc.
 *
 */

#include <qmrom_utils.h>
#include <qmrom_log.h>
#include <qmrom_spi.h>
#include <qmrom.h>
#include <spi_rom_protocol.h>

#include <qm357xx_fwpkg.h>

int qm357xx_rom_b0_probe_device(struct qmrom_handle *handle);
int qm357xx_rom_c0_probe_device(struct qmrom_handle *handle);

int qm357xx_rom_write_cmd(struct qmrom_handle *handle, uint8_t cmd)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = 1;
	handle->hstc->payload[0] = cmd;

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

int qm357xx_rom_write_cmd32(struct qmrom_handle *handle, uint32_t cmd)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = sizeof(cmd);
	memcpy(handle->hstc->payload, &cmd, sizeof(cmd));

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

int qm357xx_rom_write_size_cmd(struct qmrom_handle *handle, uint8_t cmd,
			       uint16_t data_size, const char *data)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = data_size + 1;
	handle->hstc->payload[0] = cmd;
	memcpy(&handle->hstc->payload[1], data, data_size);

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

int qm357xx_rom_write_size_cmd32(struct qmrom_handle *handle, uint32_t cmd,
				 uint16_t data_size, const char *data)
{
	handle->hstc->all = 0;
	handle->hstc->host_flags.write = 1;
	handle->hstc->ul = 1;
	handle->hstc->len = data_size + sizeof(cmd);
	memcpy(handle->hstc->payload, &cmd, sizeof(cmd));
	memcpy(&handle->hstc->payload[sizeof(cmd)], data, data_size);

	return qmrom_spi_transfer(handle->spi_handle, (char *)handle->sstc,
				  (const char *)handle->hstc,
				  sizeof(struct stc) + handle->hstc->len);
}

/*
 * Unfortunately, B0 and C0 have different
 * APIs to get the chip version...
 *
 */
int qm357xx_rom_probe_device(struct qmrom_handle *handle)
{
	int rc;

	/* Test C0 first */
	rc = qm357xx_rom_c0_probe_device(handle);
	if (!rc)
		return rc;

	/* Test B0 next */
	rc = qm357xx_rom_b0_probe_device(handle);
	if (!rc)
		return rc;

	/* None matched!!! */
	return -1;
}

int qm357xx_rom_flash_dbg_cert(struct qmrom_handle *handle,
			       struct firmware *dbg_cert)
{
	if (!handle->qm357xx_rom_ops.flash_debug_cert) {
		LOG_ERR("%s: flash debug certificate not support on this device\n",
			__func__);
		return -EINVAL;
	}
	return handle->qm357xx_rom_ops.flash_debug_cert(handle, dbg_cert);
}

int qm357xx_rom_erase_dbg_cert(struct qmrom_handle *handle)
{
	if (!handle->qm357xx_rom_ops.erase_debug_cert) {
		LOG_ERR("%s: erase debug certificate not support on this device\n",
			__func__);
		return -EINVAL;
	}
	return handle->qm357xx_rom_ops.erase_debug_cert(handle);
}

int qm357xx_rom_flash_fw(struct qmrom_handle *handle, const struct firmware *fw)
{
	int rc = 0;
	struct unstitched_firmware all_fws = { 0 };

	if (*(uint32_t *)&fw->data[0] ==
	    CRYPTO_MACRO_FIRMWARE_PACK_MAGIC_VALUE) {
		//macro package detected
		//write the FW UPDATER
		rc = qm357xx_rom_unpack_fw_macro_pkg(fw, &all_fws);
		if (rc) {
			LOG_ERR("%s: Unpack macro FW package unsuccessful!\n",
				__func__);
			return rc;
		}
	} else {
		rc = qm357xx_rom_unstitch_fw(fw, &all_fws, handle->chip_rev);
		if (rc) {
			LOG_ERR("%s: Unstitched fw flashing not supported yet\n",
				__func__);
			return rc;
		}
	}
	rc = qm357xx_rom_flash_unstitched_fw(handle, &all_fws);
	qmrom_free(all_fws.key1_crt);
	return rc;
}

int qm357xx_rom_flash_unstitched_fw(struct qmrom_handle *handle,
				    const struct unstitched_firmware *fw)
{
	if (!handle->qm357xx_rom_ops.flash_unstitched_fw) {
		LOG_ERR("%s: flash un-stitched firmware not supported on this device\n",
			__func__);
		return -EINVAL;
	}
	return handle->qm357xx_rom_ops.flash_unstitched_fw(handle, fw);
}

int qm357xx_rom_unstitch_fw(const struct firmware *fw,
			    struct unstitched_firmware *unstitched_fw,
			    enum chip_revision_e revision)
{
	uint32_t tot_len = 0;
	uint32_t fw_img_sz = 0;
	uint32_t fw_crt_sz = 0;
	uint32_t key1_crt_sz = 0;
	uint32_t key2_crt_sz = 0;
	uint8_t *p_key1;
	uint8_t *p_key2;
	uint8_t *p_crt;
	uint8_t *p_fw;
	int ret = 0;

	if (fw->size < 2 * sizeof(key1_crt_sz)) {
		LOG_ERR("%s: Not enough data (%zu) to unstitch\n", __func__,
			fw->size);
		return -EINVAL;
	}
	LOG_INFO("%s: Unstitching %zu bytes\n", __func__, fw->size);

	/* key1 */
	key1_crt_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + key1_crt_sz + sizeof(key1_crt_sz) + sizeof(key2_crt_sz) >
	    fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (key1)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(key1_crt_sz);
	p_key1 = (uint8_t *)&fw->data[tot_len];
	tot_len += key1_crt_sz;

	/* key2 */
	key2_crt_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + key2_crt_sz + sizeof(key2_crt_sz) + sizeof(fw_crt_sz) >
	    fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (key2)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(key2_crt_sz);
	p_key2 = (uint8_t *)&fw->data[tot_len];
	tot_len += key2_crt_sz;

	/* cert */
	fw_crt_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + fw_crt_sz + sizeof(fw_crt_sz) + sizeof(fw_img_sz) >
	    fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (content cert)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(fw_crt_sz);
	p_crt = (uint8_t *)&fw->data[tot_len];
	tot_len += fw_crt_sz;

	/* fw */
	fw_img_sz = *(uint32_t *)&fw->data[tot_len];
	if (tot_len + fw_img_sz + sizeof(fw_img_sz) != fw->size) {
		LOG_ERR("%s: Invalid or corrupted stitched file at offset \
				%" PRIu32 " (firmware)\n",
			__func__, tot_len);
		ret = -EINVAL;
		goto out;
	}
	tot_len += sizeof(fw_img_sz);
	p_fw = (uint8_t *)&fw->data[tot_len];

	/* Alloc array of 4 structs to key1_crt, and assign others. */
	qmrom_alloc(unstitched_fw->key1_crt, 4 * sizeof(struct firmware));
	if (!unstitched_fw->key1_crt) {
		ret = -ENOMEM;
		goto out;
	}
	unstitched_fw->key2_crt = unstitched_fw->key1_crt + 1;
	unstitched_fw->fw_crt = unstitched_fw->key1_crt + 2;
	unstitched_fw->fw_img = unstitched_fw->key1_crt + 3;

	unstitched_fw->key1_crt->size = key1_crt_sz;
	unstitched_fw->key1_crt->data = p_key1;

	unstitched_fw->key2_crt->size = key2_crt_sz;
	unstitched_fw->key2_crt->data = p_key2;

	unstitched_fw->fw_crt->size = fw_crt_sz;
	unstitched_fw->fw_crt->data = p_crt;

	unstitched_fw->fw_img->size = fw_img_sz;
	unstitched_fw->fw_img->data = p_fw;

	return 0;

out:
	if (unstitched_fw->key1_crt)
		qmrom_free(unstitched_fw->key1_crt);
	return ret;
}

int qm357xx_rom_fw_macro_pkg_get_fw_idx(const struct firmware *fw, int idx,
					uint32_t *fw_size,
					const uint8_t **fw_data)
{
	struct fw_macro_pkg_hdr_t *fw_macro_pkg_hdr =
		(struct fw_macro_pkg_hdr_t *)fw->data;
	struct fw_img_desc_t *fw_pkg;

	if (fw_macro_pkg_hdr->nb_descriptors >= 1 &&
	    idx < fw_macro_pkg_hdr->nb_descriptors) {
		fw_pkg = &fw_macro_pkg_hdr->img_desc[idx];
		if (fw_pkg->offset + fw_pkg->length > fw->size) {
			LOG_ERR("Wrong FW PKG offset = %04x; len = %04x; idx = %d!\n",
				fw_pkg->offset, fw_pkg->length, idx);
			return -EINVAL;
		}
	} else {
		LOG_ERR("%s: No FW pkg found in macro package! nb_descriptors = %d\n",
			__func__, fw_macro_pkg_hdr->nb_descriptors);
		return -EINVAL;
	}
	*fw_size = fw_pkg->length;
	*fw_data = &fw->data[fw_pkg->offset];
	return 0;
}

int qm357xx_rom_unpack_fw_macro_pkg(const struct firmware *fw,
				    struct unstitched_firmware *all_fws)
{
	int rc = 0;
	const uint8_t *fw_data;
	uint32_t fw_size;
	struct firmware fw_pkg;

	rc = qm357xx_rom_fw_macro_pkg_get_fw_idx(fw, 0, &fw_size, &fw_data);
	if (rc) {
		LOG_ERR("%s: FW MACRO PACKAGE corrupted = %d\n", __func__, rc);
		return rc;
	}

	fw_pkg.data = (const uint8_t *)fw_data;
	fw_pkg.size = fw_size;
	return qm357xx_rom_unpack_fw_pkg(&fw_pkg, all_fws);
}

int qm357xx_rom_unpack_fw_pkg(const struct firmware *fw_pkg,
			      struct unstitched_firmware *all_fws)
{
	int rc = 0;
	struct fw_pkg_img_hdr_t *fw_pkg_img_hdr;

	fw_pkg_img_hdr =
		(struct fw_pkg_img_hdr_t *)(fw_pkg->data +
					    sizeof(struct fw_pkg_hdr_t));

	if (fw_pkg_img_hdr->magic != CRYPTO_FIRMWARE_IMAGE_MAGIC_VALUE) {
		LOG_ERR("%s: Invalid or corrupted file! magic = %04x\n",
			__func__, fw_pkg_img_hdr->magic);
		return -EINVAL;
	}

	/* Alloc array of 4 structs to key1_crt, and assign others. */
	qmrom_alloc(all_fws->key1_crt, 4 * sizeof(struct firmware));
	if (!all_fws->key1_crt) {
		rc = -ENOMEM;
		goto err;
	}
	all_fws->key2_crt = all_fws->key1_crt + 1;
	all_fws->fw_crt = all_fws->key1_crt + 2;
	all_fws->fw_img = all_fws->key1_crt + 3;

	all_fws->key1_crt->data = (const uint8_t *)fw_pkg_img_hdr +
				  fw_pkg_img_hdr->cert_chain_offset;
	all_fws->key1_crt->size = CRYPTO_IMAGES_CERT_KEY_SIZE;

	all_fws->key2_crt->data =
		all_fws->key1_crt->data + CRYPTO_IMAGES_CERT_KEY_SIZE;
	all_fws->key2_crt->size = CRYPTO_IMAGES_CERT_KEY_SIZE;

	all_fws->fw_crt->data =
		all_fws->key2_crt->data + CRYPTO_IMAGES_CERT_KEY_SIZE;
	all_fws->fw_crt->size = CRYPTO_IMAGES_CERT_CONTENT_SIZE;

	all_fws->fw_img->data = (const uint8_t *)fw_pkg_img_hdr +
				fw_pkg_img_hdr->descs[0].offset;
	all_fws->fw_img->size = fw_pkg_img_hdr->descs[0].length;

	return 0;

err:
	if (all_fws->key1_crt)
		qmrom_free(all_fws->key1_crt);
	return rc;
}
