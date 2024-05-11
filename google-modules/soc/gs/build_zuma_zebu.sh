#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e

export GKI_KERNEL_DIR=${GKI_KERNEL_DIR:-"aosp-staging"}
export KLEAF_SUPPRESS_BUILD_SH_DEPRECATION_WARNING=1

: ${OUT_DIR:="out/"}
export OUT_DIR

: ${BUILD_CONFIG:="private/google-modules/soc/gs/build.config.zuma_emulator"}

echo "Using build config ${BUILD_CONFIG}"

# TODO(b/239987494): Remove this when the core GKI fragment is removed.
# Since we can't have two build config fragments, any alternative use
# of GKI_BUILD_CONFIG_FRAGMENT must include CORE_GKI_FRAGMENT,
# i.e. `source ${CORE_GKI_FRAGMENT}`.
export CORE_GKI_FRAGMENT=private/google-modules/soc/gs/build.config.zuma.gki.fragment

FAST_BUILD=1 \
  BUILD_CONFIG="${BUILD_CONFIG}" \
  GKI_BUILD_CONFIG_FRAGMENT=${GKI_BUILD_CONFIG_FRAGMENT:-${CORE_GKI_FRAGMENT}} \
  GKI_BUILD_CONFIG="${GKI_KERNEL_DIR}/build.config.gki.aarch64" \
  build/build.sh "$@"

BASE_OUT=${OUT_DIR}/
DIST_DIR=${DIST_DIR:-${BASE_OUT}/dist/}
BUILDTOOLS_PREBUILT_BIN=build/kernel/build-tools/path/linux-x86
if ! BUILDTOOLS_PREBUILT_BIN_PATH=$(readlink -f $(dirname $0)/../../../../${BUILDTOOLS_PREBUILT_BIN}); then
	echo "Failed to find ${BUILDTOOLS_PREBUILT_BIN}" >&2
	exit 1
elif [ -d "${BUILDTOOLS_PREBUILT_BIN_PATH}" ]; then
	PATH=${BUILDTOOLS_PREBUILT_BIN_PATH}:${PATH}
fi
UNPACK_BOOTIMG_PATH="tools/mkbootimg/unpack_bootimg.py"
mkdir -p "${DIST_DIR}/zebu/"
#set -x
"$UNPACK_BOOTIMG_PATH" --boot_img "${DIST_DIR}/boot.img" --out "${DIST_DIR}/ext_bootimg"
"$UNPACK_BOOTIMG_PATH" --boot_img "${DIST_DIR}/vendor_kernel_boot.img" --out "${DIST_DIR}/ext_vendor_kernel_bootimg"
cat "${DIST_DIR}/ext_vendor_kernel_bootimg/vendor_ramdisk00" "${DIST_DIR}/ext_bootimg/ramdisk" > \
	      "${DIST_DIR}/zebu/zebu_ramdisk.img"
#set +x
cp -v "${DIST_DIR}/Image" "${DIST_DIR}/zebu/Image"
#cp -v "${DIST_DIR}/ext_vendor_bootimg/dtb" "${DIST_DIR}/zebu/devicetree.dtb"
#cp -v "${DIST_DIR}/zuma-out.dtb" "${DIST_DIR}/zebu/zuma-emulator.dtb"
#cp -v "${DIST_DIR}/zuma-out.dtb" "${DIST_DIR}/zebu/zuma-hybrid.dtb"
cp -v "${DIST_DIR}/zuma-out.dtb" "${DIST_DIR}/zebu/zuma-out.dtb"
