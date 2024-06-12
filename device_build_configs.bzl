# SPDX-License-Identifier: GPL-2.0-or-later

"""Defines helper functions for creating debug and staging build configs."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_build_config",
    "kernel_module",
)

def _extracted_system_dlkm(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)

    inputs = []

    system_dlkm_archive = None
    for f in ctx.files.images:
        if f.basename == "system_dlkm_staging_archive.tar.gz":
            inputs.append(f)
            system_dlkm_archive = f
            break

    outs = []
    for m in ctx.attr.gki_modules:
        outs.append(ctx.actions.declare_file(m))
    common_outs = outs[0].dirname

    intermediates_dir = paths.join(
        ctx.bin_dir.path,
        paths.dirname(ctx.build_file_path),
        ctx.attr.name + "_intermediates",
    )

    command = hermetic_tools.setup
    command += """
        # Extract GKI modules
        mkdir -p {intermediates_dir}

        tar xf {system_dlkm_archive} -C {intermediates_dir}
        find {intermediates_dir} -name '*.ko' -exec cp -t {common_outs} {{}} \\+

        # Verify the outputs. We don't care if there are more modules extracted
        # than used. For example, we don't use zram.ko.
        all_modules=({all_modules})
        for m in "${{all_modules[@]}}"; do
            if ! [[ -f "{common_outs}/${{m}}" ]]; then
                echo "${{m}} is missing from $(basename {system_dlkm_archive})" >&2
                exit 1
            fi
        done
    """.format(
        all_modules = " ".join([m.basename for m in outs]),
        common_outs = common_outs,
        system_dlkm_archive = system_dlkm_archive.path,
        intermediates_dir = intermediates_dir,
    )

    ctx.actions.run_shell(
        mnemonic = "ExtractedSystemDlkm",
        inputs = inputs,
        outputs = outs,
        tools = hermetic_tools.deps,
        progress_message = "Extracting GKI modules",
        command = command,
    )

    return [DefaultInfo(files = depset(outs))]

extracted_system_dlkm = rule(
    doc = """Extracts the system_dlkm archive so that they can be copied to the dist_dir""",
    implementation = _extracted_system_dlkm,
    attrs = {
        "images": attr.label(
            doc = "The kernel_images target that contains the system_dlkm archive.",
            allow_files = True,
            mandatory = True,
        ),
        "gki_modules": attr.string_list(
            doc = "A list of GKI modules",
            allow_empty = False,
            mandatory = True,
        ),
    },
    toolchains = [hermetic_toolchain.type],
)

def _set_gki_kernel_dir_impl(ctx):
    output = ctx.actions.declare_file(ctx.label.name)

    output_cmd = ""
    if not ctx.attr.device_config_dir:
        output_cmd = "KERNEL_DIR={gki_kernel_dir}\n".format(
            gki_kernel_dir = str(ctx.attr.gki_kernel_dir[BuildSettingInfo].value),
        )
    else:
        output_cmd = """
            KERNEL_DIR="{device_config_dir}"
            GKI_KERNEL_DIR="{gki_kernel_dir}"
            """.format(
            device_config_dir = ctx.attr.device_config_dir,
            gki_kernel_dir = str(ctx.attr.gki_kernel_dir[BuildSettingInfo].value),
        )

    if ctx.file.gki_build_config_fragment:
        output_cmd += "GKI_BUILD_CONFIG_FRAGMENT={}\n".format(ctx.file.gki_build_config_fragment.path)

    ctx.actions.write(
        output = output,
        content = output_cmd,
    )
    return DefaultInfo(files = depset([output]))

set_gki_kernel_dir = rule(
    doc = """Creates a build config fragment file that defines the kernel
             directories and GKI_BUILD_CONFIG_FRAGMENT (if set).
             """,
    implementation = _set_gki_kernel_dir_impl,
    attrs = {
        "device_config_dir": attr.string(
            doc = "path to the device build config",
            mandatory = False,
        ),
        "gki_kernel_dir": attr.label(
            doc = "string_flag that contains the path to the GKI kernel source",
            mandatory = True,
        ),
        "gki_build_config_fragment": attr.label(
            doc = "file used as the debug build config fragment",
            allow_single_file = True,
            mandatory = False,
        ),
    },
)

def create_device_build_config(name, base_build_config, device_name, debug_fragment, gki_staging_fragment):
    """Generates device and kernel build configs using the build config fragments.

    Defines these targets:
    - `{name}`
    - `{name}.gki`

    Args:
      name: name of the main `kernel_build_config` target.
      base_build_config: the device build config.
      device_name: name of the device.
      debug_fragment: the GKI_BUILD_CONFIG_FRAGMENT used to enable debug configs.
      gki_staging_fragment: the staging kernel's build config fragment which is
                            concatenated with the base build configs.
    """

    set_gki_kernel_dir(
        name = "{}.gen".format(name),
        device_config_dir = "private/devices/google/{}".format(device_name),
        gki_kernel_dir = "//private/google-modules/soc/gs:gki_kernel_dir",
        gki_build_config_fragment = debug_fragment,
    )

    set_gki_kernel_dir(
        name = "{}.gki.gen".format(name),
        gki_kernel_dir = "//private/google-modules/soc/gs:gki_kernel_dir",
        gki_build_config_fragment = debug_fragment,
    )

    kernel_build_config(
        name = name,
        srcs = [
            # do not sort
            ":{}.gen".format(name),
            base_build_config,
        ] + ([gki_staging_fragment] if gki_staging_fragment else []),
    )

    kernel_build_config(
        name = "{}.gki".format(name),
        srcs = [
            # do not sort
            ":{}.gki.gen".format(name),
        ] + select({
            "//private/google-modules/soc/gs:gki_aosp": ["//aosp:build.config.gki.aarch64"],
            "//private/google-modules/soc/gs:gki_aosp_staging": ["//aosp-staging:build.config.gki.aarch64"],
        }) + ([gki_staging_fragment] if gki_staging_fragment else []),
    )

def device_build_configs(
        name,
        base_build_config,
        device_name,
        gki_staging_fragment = None):
    """Creates the full set of debug configs for a pixel device.

    Defines these targets for each debug config:
    - `{name}.{debug_name}`
    - `{name}.{debug_name}.gki`

    Args:
      name: name of the base `kernel_build_config` target
      base_build_config: the device build config
      device_name: name of the device
      gki_staging_fragment: the staging kernel's build config fragment
    """

    debug_types = [
        "blktest",
        "debug_api",
        "debug_kmemleak",
        "debug_locking",
        "debug_memory",
        "debug_memory_accounting",
        "kasan",
        "khwasan",
    ]
    debug_configs_mapping = {}
    debug_gki_configs_mapping = {}
    for debug_name in debug_types:
        create_device_build_config(
            name = "{name}.{debug_name}".format(name = name, debug_name = debug_name),
            base_build_config = base_build_config,
            device_name = device_name,
            debug_fragment = "//private/google-modules/soc/gs:build.config.slider.{}".format(debug_name),
            gki_staging_fragment = gki_staging_fragment,
        )
        debug_configs_mapping["//private/google-modules/soc/gs:{}".format(debug_name)] = \
            ["//private/devices/google/{device}:{name}.{debug_name}".format(
                name = name,
                device = device_name,
                debug_name = debug_name,
            )]
        debug_gki_configs_mapping["//private/google-modules/soc/gs:{}".format(debug_name)] = \
            ["//private/devices/google/{device}:{name}.{debug_name}.gki".format(
                name = name,
                device = device_name,
                debug_name = debug_name,
            )]

    create_device_build_config(
        name = "{}_mod".format(name),
        base_build_config = base_build_config,
        device_name = device_name,
        debug_fragment = None,
        gki_staging_fragment = gki_staging_fragment,
    )

    debug_configs_mapping["//conditions:default"] = \
        [":{name}_mod".format(name = name)]
    debug_gki_configs_mapping["//conditions:default"] = \
        [":{name}_mod.gki".format(name = name)]

    native.filegroup(
        name = "device_build_config",
        srcs = select(debug_configs_mapping),
    )

    native.filegroup(
        name = "gki_build_config",
        srcs = select(debug_gki_configs_mapping),
    )

def lto_dependant_kernel_module(
        name,
        outs,
        lto_outs,
        srcs = None,
        kernel_build = None,
        makefile = None,
        deps = None,
        **kwargs):
    """Wrapper over kernel_module to conditionally modify outs when LTO is set differently.

    Args:
        name: name of the module
        outs: See kernel_module.outs
        lto_outs: Like outs, but only appended to outs when LTO is not set to none
        kernel_build: See kernel_module.kernel_build
        makefile: See kernel_module.makefile
        deps: See kernel_module.deps
        **kwargs: common kwargs for all rules
    """
    kwargs_with_private_visibility = dict(
        kwargs,
        visibility = ["//visibility:private"],
    )

    kernel_module(
        name = name + "_internal",
        srcs = srcs,
        outs = outs + lto_outs,
        kernel_build = kernel_build,
        makefile = makefile,
        deps = deps,
        **kwargs_with_private_visibility
    )

    kernel_module(
        name = name + "_lto_none",
        srcs = srcs,
        outs = outs,
        kernel_build = kernel_build,
        makefile = makefile,
        deps = deps,
        **kwargs_with_private_visibility
    )

    native.alias(
        name = name,
        actual = select({
            "//private/google-modules/soc/gs:lto_none": name + "_lto_none",
            "//conditions:default": name + "_internal",
        }),
        **kwargs
    )
