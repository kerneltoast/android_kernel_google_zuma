# SPDX-License-Identifier: GPL-2.0-only

"""
Zuma constants.
"""

ZUMA_DTBS = [
    # keep sorted
    "zuma-a0-foplp.dtb",
    "zuma-a0-ipop.dtb",
    "zuma-b0-foplp.dtb",
    "zuma-b0-ipop.dtb",
]

ZUMA_DPM_DTBOS = [
    "zuma-dpm-eng.dtbo",
    "zuma-dpm-user.dtbo",
    "zuma-dpm-userdebug.dtbo",
]

ZUMA_MODULE_OUTS = [
    # keep sorted
    "drivers/gpu/drm/display/drm_display_helper.ko",
    "drivers/i2c/i2c-dev.ko",
    "drivers/misc/eeprom/at24.ko",
    "drivers/misc/open-dice.ko",
    "drivers/perf/arm-cmn.ko",
    "drivers/perf/arm_dsu_pmu.ko",
    "drivers/scsi/sg.ko",
    "drivers/spi/spidev.ko",
    "drivers/watchdog/softdog.ko",
    "net/mac80211/mac80211.ko",
    "net/wireless/cfg80211.ko",
]
