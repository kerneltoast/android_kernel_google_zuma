# Building with Bazel (recommended)

```shell
# Files are copied to out/{branch}/dist
$ tools/bazel run //private/google-modules/soc/gs:slider_dist
```

See `build/kernel/kleaf/README.md` for details.

## Disable LTO

**Note**: This only works on `raviole-mainline` branch.

```shell
# Files are copied to out/{branch}/dist
$ tools/bazel run --lto=none //private/google-modules/soc/gs:slider_dist
```

# ABI monitoring with Bazel (recommended)

**Note**: ABI monitoring is not supported on `raviole-mainline` branch.

```shell
# Compare ABI and build files for distribution
$ tools/bazel build //private/google-modules/soc/gs:slider_abi

# Update symbol list common/android/abi_gki_aarch64_pixel
$ tools/bazel run //private/google-modules/soc/gs:slider_abi_update_symbol_list

# Update ABI common/android/abi_gki_aarch64.xml
$ tools/bazel run //common:kernel_aarch64_abi_update

# Copy files to distribution
$ tools/bazel run //private/google-modules/soc/gs:slider_abi_dist
```

# Building with `build_slider.sh` (legacy)

```shell
$ build/build_slider.sh
```

## Disable LTO

**Note**: This only works on `raviole-mainline` branch.

```shell
$ LTO=none build/build_slider.sh
```
