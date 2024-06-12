M ?= $(shell pwd)

KBASE_PATH_RELATIVE = $(M)

EXTRA_CFLAGS += -Werror

include $(KERNEL_SRC)/../private/google-modules/soc/gs/Makefile.include

KBUILD_OPTIONS  += CONFIG_EXYNOS_HDCP2=m

EXTRA_SYMBOLS  += $(OUT_DIR)/../private/google-modules/trusty/Module.symvers

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
	$(KBUILD_OPTIONS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
