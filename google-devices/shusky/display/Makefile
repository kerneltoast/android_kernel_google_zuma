M ?= $(shell pwd)

KBASE_PATH_RELATIVE = $(M)

KBUILD_OPTIONS += CONFIG_DRM_PANEL_GOOGLE_BIGSURF=m
KBUILD_OPTIONS += CONFIG_DRM_PANEL_GOOGLE_HK3=m
KBUILD_OPTIONS += CONFIG_DRM_PANEL_GOOGLE_SHORELINE=m

EXTRA_CFLAGS += -DDYNAMIC_DEBUG_MODULE=1
EXTRA_CFLAGS += -I$(KERNEL_SRC)/../private/google-modules/display/common
EXTRA_CFLAGS += -I$(KERNEL_SRC)/../private/google-modules/display/samsung
EXTRA_CFLAGS += -I$(KERNEL_SRC)/../private/google-modules/display/samsung/include/uapi
EXTRA_CFLAGS += -Werror

EXTRA_SYMBOLS += $(OUT_DIR)/../private/google-modules/display/samsung/Module.symvers

include $(KERNEL_SRC)/../private/google-modules/soc/gs/Makefile.include

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
	$(KBUILD_OPTIONS) \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" \
	$(@)
