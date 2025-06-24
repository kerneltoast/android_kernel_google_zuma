#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

exec tools/bazel run \
    --config=stamp \
    --config=zuma \
    //private/devices/google/zuma:dist "$@"
