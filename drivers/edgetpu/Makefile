# SPDX-License-Identifier: GPL-2.0
#
# Makefile for EdgeTPU framework and dependent drivers.
#

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

ifndef CONFIG_EDGETPU_TEST
KBUILD_OPTIONS	+= CONFIG_RIO=m
endif

KBUILD_OPTIONS	+= CONFIG_GCIP=y

-include $(KERNEL_SRC)/../private/google-modules/soc/gs/Makefile.include

ifneq ("$(wildcard $(OUT_DIR)/../private/google-modules/power/mitigation/Module.symvers)","")
EXTRA_SYMBOLS	+= $(OUT_DIR)/../private/google-modules/power/mitigation/Module.symvers
endif

ifneq ("$(wildcard $(OUT_DIR)/../private/google-modules/soc/gs/drivers/soc/google/gsa/Module.symvers)","")
EXTRA_SYMBOLS += $(OUT_DIR)/../private/google-modules/soc/gs/drivers/soc/google/gsa/Module.symvers
endif

ifneq ("$(wildcard $(OUT_DIR)/../private/google-modules/iif/Module.symvers)","")
EXTRA_SYMBOLS += $(OUT_DIR)/../private/google-modules/iif/Module.symvers
endif

GCIP_DIR=gcip-kernel-driver/drivers/gcip

modules modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(GCIP_DIR) W=1 gcip.o
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 $(KBUILD_OPTIONS) \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(GCIP_DIR) W=1 $(@)
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 $(KBUILD_OPTIONS) $(@)
