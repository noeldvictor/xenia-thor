#!/usr/bin/env python3
"""Generate the direct-host-resolve SPIR-V bytecode headers.

Ported from XenDroid's tools/build/gen_android_spirv.py, reduced to the 90
direct-host-resolve compute variants and adapted to drive the Android NDK's
shaderc tools (glslc / spirv-opt / spirv-dis) instead of glslangValidator,
since this machine has no Vulkan SDK. Output format matches xenia's committed
`xb buildshaders` headers (disassembly comment + uint32_t array) so the
generated headers live alongside the stock ones in
src/xenia/gpu/shaders/bytecode/vulkan_spirv/.

Usage: gen_dhr_spirv.py [--tools <dir with glslc/spirv-opt/spirv-dis>]
"""

import argparse
import glob
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER_DIR = os.path.normpath(
    os.path.join(HERE, "..", "..", "src", "xenia", "gpu", "shaders"))
OUT_DIR = os.path.join(SHADER_DIR, "bytecode", "vulkan_spirv")

XESL_WRAPPER = (
    "#version 460\n"
    "#extension GL_EXT_control_flow_attributes : require\n"
    "#extension GL_EXT_samplerless_texture_functions : require\n"
    "#extension GL_GOOGLE_include_directive : require\n"
    "#include \"%s\"\n"
)


def variants():
    """(ident, entry_source_basename, [define, ...]) for all 90 variants.

    Mirrors XenDroid's direct_host_resolve_variants(): 24 fast-color +
    60 full-color + 6 depth.
    """
    result = []
    for source_uint in (0, 1):
        for bpp in (32, 64):
            for msaa in (1, 2, 4):
                for scaled in (0, 1):
                    ident = "resolve_host_color"
                    if source_uint:
                        ident += "_uint"
                    ident += f"_{bpp}bpp_{msaa}xmsaa"
                    defines = [
                        f"XE_RESOLVE_HOST_COLOR_BPP={bpp}",
                        f"XE_RESOLVE_HOST_COLOR_MSAA_SAMPLES={msaa}",
                        f"XE_RESOLVE_HOST_COLOR_SOURCE_UINT={source_uint}",
                    ]
                    if scaled:
                        ident += "_scaled"
                        defines.append("XE_RESOLVE_RESOLUTION_SCALED=1")
                    ident += "_cs"
                    result.append(
                        (ident, "resolve_host_color_entry.xesli", defines))
        for bpp in (8, 16, 32, 64, 128):
            for msaa in (1, 2, 4):
                for scaled in (0, 1):
                    ident = "resolve_host_color_full"
                    if source_uint:
                        ident += "_uint"
                    ident += f"_{bpp}bpp_{msaa}xmsaa"
                    defines = [
                        f"XE_RESOLVE_HOST_COLOR_FULL_DEST_BPP={bpp}",
                        f"XE_RESOLVE_HOST_COLOR_MSAA_SAMPLES={msaa}",
                        f"XE_RESOLVE_HOST_COLOR_SOURCE_UINT={source_uint}",
                    ]
                    if scaled:
                        ident += "_scaled"
                        defines.append("XE_RESOLVE_RESOLUTION_SCALED=1")
                    ident += "_cs"
                    result.append(
                        (ident, "resolve_host_color_full_entry.xesli", defines))
    for msaa in (1, 2, 4):
        for scaled in (0, 1):
            ident = f"resolve_host_depth_32bpp_{msaa}xmsaa"
            defines = [f"XE_RESOLVE_HOST_DEPTH_MSAA_SAMPLES={msaa}"]
            if scaled:
                ident += "_scaled"
                defines.append("XE_RESOLVE_RESOLUTION_SCALED=1")
            ident += "_cs"
            result.append((ident, "resolve_host_depth_entry.xesli", defines))
    return result


def find_tools(tools_dir):
    ext = ".exe" if os.name == "nt" else ""
    names = ("glslc" + ext, "spirv-opt" + ext, "spirv-dis" + ext)
    if tools_dir:
        return tuple(os.path.join(tools_dir, n) for n in names)
    # Default: newest Android NDK shader-tools, else PATH.
    sdk = os.environ.get(
        "ANDROID_SDK_ROOT",
        os.path.expanduser("~/AppData/Local/Android/Sdk"))
    candidates = sorted(
        glob.glob(os.path.join(sdk, "ndk", "*", "shader-tools", "*")))
    for cand in reversed(candidates):
        if all(os.path.isfile(os.path.join(cand, n)) for n in names):
            return tuple(os.path.join(cand, n) for n in names)
    return names


def write_header(out_path, ident, spv_path, dis_path):
    with open(out_path, "w") as out:
        out.write("// Generated with `xb buildshaders`.\n#if 0\n")
        with open(dis_path, "r") as dis_file:
            dis_data = dis_file.read()
            if dis_data:
                out.write(dis_data)
                if dis_data[-1] != "\n":
                    out.write("\n")
        out.write("#endif\n\nconst uint32_t %s[] = {" % ident)
        with open(spv_path, "rb") as spv_file:
            index = 0
            while True:
                word = spv_file.read(4)
                if len(word) == 0:
                    break
                if len(word) != 4:
                    raise RuntimeError("SPIR-V binary is misaligned")
                if index % 6 == 0:
                    out.write("\n    ")
                else:
                    out.write(" ")
                index += 1
                out.write("0x%08X," % struct.unpack("<I", word)[0])
        out.write("\n};\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tools", default=None)
    args = parser.parse_args()
    glslc, spirv_opt, spirv_dis = find_tools(args.tools)

    os.makedirs(OUT_DIR, exist_ok=True)
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for ident, entry, defines in variants():
            wrapper = os.path.join(tmp, ident + ".comp")
            with open(wrapper, "w") as wf:
                wf.write(XESL_WRAPPER % entry)
            raw_spv = os.path.join(tmp, ident + ".raw.spv")
            opt_spv = os.path.join(tmp, ident + ".spv")
            dis_txt = os.path.join(tmp, ident + ".txt")
            cmd = [glslc, "-fshader-stage=compute", "--target-env=vulkan1.1",
                   "-DSHADING_LANGUAGE_GLSL_XE=1"]
            cmd += ["-D" + d for d in defines]
            cmd += ["-I", SHADER_DIR, "-o", raw_spv, wrapper]
            for step in (cmd,
                         [spirv_opt, "-O", "-O", raw_spv, "-o", opt_spv],
                         [spirv_dis, "-o", dis_txt, opt_spv]):
                r = subprocess.run(step, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True)
                if r.returncode != 0:
                    print(f"FAILED {ident}: {step[0]}", file=sys.stderr)
                    if r.stderr:
                        sys.stderr.write(r.stderr)
                    failed += 1
                    break
            else:
                write_header(os.path.join(OUT_DIR, ident + ".h"), ident,
                             opt_spv, dis_txt)
                print(f"OK {ident}")
    if failed:
        print(f"{failed} variant(s) FAILED", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
