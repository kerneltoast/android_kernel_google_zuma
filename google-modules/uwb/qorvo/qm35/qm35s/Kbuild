ccflags-y := -I$(srctree)/$(src)/libqmrom/include -I$(srctree)/$(src)/libfwupdater/include -Werror

ccflags-y += -DC0_WRITE_STATS -DCONFIG_FLASHING_RETRIES=10
ccflags-y += -DCONFIG_CHUNK_FLASHING_RETRIES=10 -DCONFIG_GLOBAL_CHUNK_FLASHING_RETRIES=50
ccflags-y += -DCONFIG_FWUPDATER_CHUNK_FLASHING_RETRIES=3
ccflags-y += -DCONFIG_FWUPDATER_GLOBAL_CHUNK_FLASHING_RETRIES=60

obj-$(CONFIG_QM35_SPI) := qm35.o

qm35-y := \
	qm35-spi.o \
	qm35-sscd.o \
	qm35_rb.o \
	qmrom_spi.o \
	libqmrom/src/qmrom_common.o \
	libqmrom/src/qm357xx_rom_common.o \
	libqmrom/src/qm357xx_rom_b0.o \
	libqmrom/src/qm357xx_rom_c0.o \
	libqmrom/src/qmrom_log.o \
	libfwupdater/src/fwupdater.o \
	hsspi.o \
	hsspi_uci.o \
	hsspi_log.o \
	hsspi_coredump.o \
	debug.o \
	hsspi_test.o

qm35-$(CONFIG_QM35_SPI_DEBUG_FW) += debug_qmrom.o
qm35-$(CONFIG_EVENT_TRACING) += qm35-trace.o
CFLAGS_qm35-trace.o = -I$(srctree)/$(src)
