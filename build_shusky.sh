#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

parameters=
if [ "${BUILD_AOSP_KERNEL}" = "1" ]; then
  echo "WARNING: BUILD_AOSP_KERNEL is deprecated." \
    "Use --kernel_package=@//aosp instead." >&2
  parameters="--kernel_package=@//aosp"
fi

if [ "${BUILD_STAGING_KERNEL}" = "1" ]; then
  echo "WARNING: BUILD_STAGING_KERNEL is deprecated." \
    "Use --kernel_package=@//aosp-staging instead." >&2
  parameters="--kernel_package=@//aosp-staging"
fi

# Find `--page_size=16k` in the list of arguments, and append to bazel
# parameters to build a 16k page size kernel.
for arg in "$@"; do
  if [[ "$arg" == "--page_size=16k" ]]; then
    parameters+=" --page_size=16k"
    parameters+=" --config=no_download_gki"
    parameters+=" --config=no_download_gki_fips140"
  else
    remaining_args+=("$arg")
  fi
done

set -- "${remaining_args[@]}" # Update $@ with the remaining arguments

exec tools/bazel run \
    ${parameters} \
    --config=stamp \
    --config=shusky \
    //private/devices/google/shusky:zuma_shusky_dist "$@"
