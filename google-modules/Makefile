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
obj-y += gps/broadcom/bcm47765/
obj-y += gpu/mali_kbase/
obj-y += gpu/mali_pixel/
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

# These drivers each have their own unique copy of the gcip driver, which has
# global symbols with the same names. To prevent a symbol name conflict when
# linking these drivers directly into vmlinux, specify the composite objects
# that the drivers produce and hijack the make rule for those objects in order
# to pass along a header included via `-include` with macros to rename the
# global gcip symbols to something unique for each driver.
gcip-drivers := \
	edgetpu/rio/drivers/edgetpu/rio.o \
	gxp/zuma/gxp.o

obj-y += $(gcip-drivers)

# gcip.o make rule, for the first gcip.o generation step needed to find all the
# global symbols it has (via nm). This isn't the copy of gcip.o that will be
# linked into vmlinux; the final gcip.o is compiled by each driver when make
# recurses into the driver.
%/gcip.o:
	$(Q)$(MAKE) $(build)=$(@D) $@

# Check for global symbols in gcip.o which aren't prefixed.
chk_gcip_defs = $(if $(filter-out $2%,$(shell $(NM) -g -U -f just-symbols $<)),\
		$(cmd_and_savecmd),@:)

# Generate the header with macros to redefine all of gcip's global symbols with
# a prefix added onto them. The prefix is the name of the top-most directory of
# the driver, encapsulated within two underscores.
$(obj)/%/.gcip_defs.h: $(obj)/%/gcip-kernel-driver/drivers/gcip/gcip.o FORCE
	$(call chk_gcip_defs,gen_gcip_defs,__$(firstword $(subst /, ,$(*D)))__)

# Pass the generated include (named `.gcip_defs.h`) under each driver to its
# respective driver recursively so that the driver and its copy of gcip are
# edited with the prefixed versions of the global gcip symbols.
.SECONDEXPANSION:
$(obj)/%.o: $$(dir $$@).gcip_defs.h FORCE
	$(Q)$(MAKE) $(build)=$(@D) EXTRA_CFLAGS="-include $<" $@

# This takes one argument ($2) to specify the name of the symbol prefix.
#
# Generate a header to rename all of the global symbols exported by each copy of
# gcip.o. This is done by compiling gcip.o twice: once without modifications,
# and once more modified.
#
# In order to prevent endless prefixing, a sed filter is used to remove the
# generated prefix from any symbol names. That way, incremental rebuilds still
# result in correct macro generation.
#
# A nicer solution would be to use objcopy to rename the global symbols
# individually or turn them into local symbols, but unfortunately objcopy isn't
# compatible with GCC LTO-generated object files.
quiet_cmd_gen_gcip_defs = GCIP    $@
      cmd_gen_gcip_defs = \
	$(NM) -g -U -f just-symbols $< | \
	sed 's/^$2//' | \
	$(AWK) '{ printf "\#define %s $2%s\n", $$1, $$1 }' > $@
