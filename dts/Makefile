# SPDX-License-Identifier: GPL-2.0

shusky_overlays := \
	zuma-ripcurrent.dtbo \
	zuma-husky-proto1.dtbo \
	zuma-husky-proto1-wingboard.dtbo \
	zuma-husky-proto1_1.dtbo \
	zuma-husky-evt1.dtbo \
	zuma-husky-evt1_1.dtbo \
	zuma-husky-dvt1.dtbo \
	zuma-husky-pvt1.dtbo \
	zuma-husky-mp.dtbo \
	zuma-shiba-proto1.dtbo \
	zuma-shiba-proto1-wingboard.dtbo \
	zuma-shiba-proto1_1.dtbo \
	zuma-shiba-evt1.dtbo \
	zuma-shiba-evt1_b0.dtbo \
	zuma-shiba-evt1_1.dtbo \
	zuma-shiba-evt1_1-wingboard.dtbo \
	zuma-shiba-dvt1.dtbo \
	zuma-shiba-pvt1.dtbo \
	zuma-shiba-mp.dtbo

# DPM overlays
shusky_overlays += \
	zuma/zuma-dpm-eng.dtbo \
	zuma/zuma-dpm-userdebug.dtbo \
	zuma/zuma-dpm-user.dtbo

DTC_FLAGS_zuma-dpm-eng += -a 4096
DTC_FLAGS_zuma-dpm-user += -a 4096
DTC_FLAGS_zuma-dpm-userdebug += -a 4096

shusky_dtbs := \
	zuma/zuma-a0-foplp.dtb \
	zuma/zuma-a0-ipop.dtb \
	zuma/zuma-b0-foplp.dtb \
	zuma/zuma-b0-ipop.dtb

# Usage:
#   $(call DTBS_overlay_template, overlay, base_dtb, suffix)
#
# This is a generic template to create the following construct:
#   <overlay>-<suffix>-dtbs := <base_dtb>.dtb <overlay>.dtbo
define DTBS_overlay_template
	$(1:%.dtbo=%-$(strip $3)-dtbs) := $2 $1
endef

# Usage:
#   $(call DTB_object_template, CONFIG_, overlay, suffix)
#
# This is a generic template to create the following construct:
#   dtb-$(CONFIG_) += <overlay>-<suffix>.dtb
define DTB_object_template
	dtb-$(strip $1) += $(2:%.dtbo=%-$(strip $3).dtb)
endef

# Usage:
#   $(call multi_dtbs_overlay, list_of_base_dtbs, list_of_overlays, CONFIG_)
#
# For each (base_dtb,overlay) pair, this will create the following constructs:
#   <overlay>-<base_dtb>-dtbs := <base_dtb>.dtb <overlay>.dtbo
#   dtb-$(CONFIG_) += <overlay>-<base_dtb>.dtb
define multi_dtbs_overlay
	$(foreach base, $1, \
		$(foreach overlay, $2, \
			$(eval $(call DTBS_overlay_template, $(overlay), $(base), $(notdir $(base:%.dtb=%)))) \
			$(eval $(call DTB_object_template, $3, $(overlay), $(notdir $(base:%.dtb=%))))))
endef

# Create the -dtbs and dtb-y constructs. We can do this by looping through the
# base dtbs and apply each overlay to it. Kbuild will use these -dtbs
# constructs to determine which dtbs are the base dtbs and which are the
# overlays. For the base dtbs, kbuild will expose the symbols as needed for the
# overlays. Additionally, kbuild will statically apply the overlay to the base
# dtb which could be used to verify the overlay applies cleanly.
#$(call multi_dtbs_overlay, $(shusky_dtbs), $(shusky_overlays), $(CONFIG_SOC_ZUMA))
$(call multi_dtbs_overlay, $(shusky_dtbs), $(shusky_overlays), y)
