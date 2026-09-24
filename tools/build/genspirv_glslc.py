#!/usr/bin/env python3
"""Regenerate Vulkan SPIR-V headers for the given .xesl shaders with the NDK
shader tools. This is the same pipeline as `xb buildshaders` (glslang, then
spirv-opt -O, then spirv-dis; the NDK spirv-opt has no --canonicalize-ids), but it uses glslc from the
Android NDK because no Vulkan SDK is installed on this PC.

Usage: python tools/build/genspirv_glslc.py <shader.cs.xesl> [...]

A plain <name>.<stage>.glsl file (Vulkan only, with its own #version, such as
the tessellation vertex and control shaders) is compiled as it is.
"""
import os
import subprocess
import sys

NDK_TOOLS = os.environ.get(
    'XE_NDK_SHADER_TOOLS',
    r'C:\Users\leanerdesigner\AppData\Local\Android\Sdk\ndk\29.0.14206865'
    r'\shader-tools\windows-x86_64')
GLSLC = os.path.join(NDK_TOOLS, 'glslc.exe')
SPIRV_OPT = os.path.join(NDK_TOOLS, 'spirv-opt.exe')
SPIRV_DIS = os.path.join(NDK_TOOLS, 'spirv-dis.exe')

STAGES = {'vs': 'vert', 'hs': 'tesc', 'ds': 'tese', 'gs': 'geom',
          'ps': 'frag', 'cs': 'comp'}
WRAPPER = ('#version 460\n'
           '#extension GL_EXT_control_flow_attributes : require\n'
           '#extension GL_EXT_samplerless_texture_functions : require\n'
           '#extension GL_GOOGLE_include_directive : require\n'
           '#include "%s"\n')


def build(src_path):
    src_name = os.path.basename(src_path)
    plain_glsl = src_name.endswith('.glsl')
    if (not (src_name.endswith('.xesl') or plain_glsl) or
            src_name[-8] != '.'):
        raise SystemExit('not a .<stage>.xesl or .<stage>.glsl file: ' +
                         src_path)
    identifier = src_name[:-5].replace('.', '_')
    stage = STAGES[identifier[-2:]]
    src_dir = os.path.dirname(os.path.abspath(src_path))
    out_dir = os.environ.get('XE_SPIRV_OUT_DIR') or os.path.join(
        src_dir, 'bytecode', 'vulkan_spirv')
    os.makedirs(out_dir, exist_ok=True)
    base = os.path.join(out_dir, identifier)
    raw = base + '.glslc.spv'
    opt = base + '.spv'
    dis = base + '.txt'
    print('- %s > vulkan_spirv' % src_path)
    if plain_glsl:
        subprocess.run([GLSLC, '-x', 'glsl', '-fshader-stage=' + stage,
                        '--target-env=vulkan1.0', '-I', src_dir, '-o', raw,
                        os.path.abspath(src_path)], check=True)
    else:
        subprocess.run([GLSLC, '-x', 'glsl', '-fshader-stage=' + stage,
                        '--target-env=vulkan1.0',
                        '-DSHADING_LANGUAGE_GLSL_XE=1', '-I', src_dir, '-o',
                        raw, '-'],
                       input=(WRAPPER % src_name).encode(), check=True)
    subprocess.run([SPIRV_OPT, '-O', raw, '-o', opt],
                   check=True)
    os.remove(raw)
    subprocess.run([SPIRV_DIS, '-o', dis, opt], check=True)
    with open(opt, 'rb') as f:
        data = f.read()
    if len(data) % 4:
        raise SystemExit('misaligned SPIR-V: ' + opt)
    with open(dis, 'r') as f:
        dis_text = f.read()
    lines = ['// Generated with `xb buildshaders`.', '#if 0']
    body = dis_text if dis_text.endswith('\n') else dis_text + '\n'
    out = '\n'.join(lines) + '\n' + body + '#endif\n\nconst uint32_t %s[] = {' % identifier
    words = [int.from_bytes(data[i:i + 4], sys.byteorder)
             for i in range(0, len(data), 4)]
    for index, w in enumerate(words):
        out += ('\n    ' if index % 6 == 0 else ' ') + '0x%08X,' % w
    out += '\n};\n'
    with open(base + '.h', 'w', newline='\n') as f:
        f.write(out)
    os.remove(dis)
    os.remove(opt)
    print('  %d words' % len(words))


if __name__ == '__main__':
    for p in sys.argv[1:]:
        build(p)
