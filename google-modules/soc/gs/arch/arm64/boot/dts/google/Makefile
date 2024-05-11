# SPDX-License-Identifier: GPL-2.0

gs101_raviole_overlays := \
	gs101-slider.dtbo \
	gs101-slider2.dtbo \
	gs101-slider2-o6r4.dtbo \
	gs101-whitefin.dtbo \
	gs101-whitefin2.dtbo \
	gs101-whitefin2v2.dtbo \
	gs101-oriole.dtbo \
	gs101-oriole-evt1_1.dtbo \
	gs101-oriole-evt-wingboard.dtbo \
	gs101-oriole-evt1.dtbo \
	gs101-oriole-proto1.dtbo \
	gs101-oriole-proto1_1.dtbo \
	gs101-raven.dtbo \
	gs101-raven-evt1_1.dtbo \
	gs101-raven-evt-wingboard.dtbo \
	gs101-raven-evt1.dtbo \
	gs101-raven-proto1.dtbo \
	gs101-raven-proto1_1.dtbo

gs101_raviole_dtbs := gs101-a0.dtb gs101-b0.dtb

zuma_overlays := \
	zuma-emulator.dtbo \
	zuma-hybrid.dtbo

zuma_dtbs := \
	zuma-a0-foplp.dtb \
	zuma-a0-ipop.dtb \
	zuma-b0-foplp.dtb \
	zuma-b0-ipop.dtb

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
			$(eval $(call DTBS_overlay_template, $(overlay), $(base), $(base:%.dtb=%))) \
			$(eval $(call DTB_object_template, $3, $(overlay), $(base:%.dtb=%)))))
endef

# Create the -dtbs and dtb-y constructs. We can do this by looping through the
# base dtbs and apply each overlay to it. Kbuild will use these -dtbs
# constructs to determine which dtbs are the base dtbs and which are the
# overlays. For the base dtbs, kbuild will expose the symbols as needed for the
# overlays. Additionally, kbuild will statically apply the overlay to the base
# dtb which could be used to verify the overlay applies cleanly.
$(call multi_dtbs_overlay, $(gs101_raviole_dtbs), $(gs101_raviole_overlays), $(CONFIG_SOC_GS101))
$(call multi_dtbs_overlay, $(zuma_dtbs), $(zuma_overlays), $(CONFIG_SOC_ZUMA))
