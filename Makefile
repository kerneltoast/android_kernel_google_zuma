KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

M ?= $(shell pwd)
O := $(abspath $(KDIR))
B := $(abspath $O/$M)

$(info *** Building Android in $O/$M )

KBUILD_OPTIONS += CONFIG_QM35_SPI=m

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) INSTALL_MOD_STRIP=1 $(KBUILD_OPTIONS) $(@)
