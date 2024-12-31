# SPDX-License-Identifier: GPL-2.0

# Set KERNEL_SRC to google-modules so that the include paths are set correctly
# for in-tree compilation, since the modules assume that `google-modules` exists
# in the same directory as the kernel tree (one level up), whereas we've got
# `google-modules` inside the kernel tree. This way, when the modules include
# with `-I$(KERNEL_SRC)/../google-modules/SOME_MODULE`, inclusion works as-is.
export KERNEL_SRC := $(srctree)/google-modules

obj-y += amplifiers/cs35l41/
obj-y += amplifiers/cs40l26/
obj-y += amplifiers/snd_soc_wm_adsp/
obj-y += aoc/
obj-y += aoc/alsa/
obj-y += aoc/usb/
obj-y += bluetooth/broadcom/
obj-y += bms/
obj-y += bms/misc/
obj-y += display/common/gs_drm/
obj-y += display/common/gs_panel/
obj-y += display/samsung/
obj-y += edgetpu/rio/drivers/edgetpu/
obj-y += gps/broadcom/bcm47765/
obj-y += gpu/mali_kbase/
obj-y += gpu/mali_pixel/
obj-y += gxp/zuma/
obj-y += hdcp/samsung/
obj-y += lwis/
obj-y += nfc/
obj-y += power/mitigation/
obj-y += power/reset/
obj-y += radio/samsung/s5300/
obj-y += soc/gs/
obj-y += touch/common/
obj-y += touch/fts/ftm5/
obj-y += touch/goodix/
obj-y += trusty/
obj-y += uwb/qorvo/qm35/qm35s/
obj-y += video/gchips/
obj-y += wlan/bcm4398/
