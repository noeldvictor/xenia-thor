"""Build the Windows app (xenia.exe) and the Vulkan trace dump in one call.

  python tools/pc/build_pc.py [--project xenia-app] [--no-trace-dump]

Finds MSBuild (Visual Studio 2022 Build Tools or any edition), builds
build/<project>.vcxproj with Configuration "Release Windows" and Platform x64
(the only working pair; 2026-09-23 it took three tries to find), then
build/xenia-gpu-vulkan-trace-dump.vcxproj (most of the objects are shared, so
it adds seconds), and prints one JSON object: exit, seconds, the
compiler/linker errors, and whether xenia.exe and the trace dump changed.

2026-10-02: only xenia-app was built, so every trace tool (cvar_ab, frame_cost,
backend_ab, trace_ab, shader_lab --traces) ran a trace dump from the day
before; a new cvar was an unknown flag there, and each replay waited 300 s on
an error dialog - a 30-trace A/B ran 40 minutes for nothing.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release', 'xenia.exe')
TRACE_DUMP = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release',
                          'xenia-gpu-vulkan-trace-dump.exe')


def find_msbuild():
    for base in (r'C:\Program Files (x86)\Microsoft Visual Studio', r'C:\Program Files\Microsoft Visual Studio'):
        hits = sorted(glob.glob(os.path.join(base, '*', '*', 'MSBuild', 'Current', 'Bin', 'MSBuild.exe')))
        if hits:
            return hits[-1]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default='xenia-app')
    ap.add_argument('--no-trace-dump', action='store_true',
                    help='do not also build xenia-gpu-vulkan-trace-dump')
    args = ap.parse_args()
    msbuild = find_msbuild()
    if not msbuild:
        print(json.dumps({'exit': 1, 'error': 'MSBuild.exe not found'}))
        return 1
    projects = [args.project] + ([] if args.no_trace_dump else ['xenia-gpu-vulkan-trace-dump'])
    before = {p: os.path.getmtime(p) if os.path.exists(p) else 0 for p in (EXE, TRACE_DUMP)}
    t0 = time.time()
    returncode, errors = 0, set()
    for project in projects:
        proc = subprocess.run([msbuild, os.path.join(ROOT, 'build', project + '.vcxproj'),
                               '/p:Configuration=Release Windows', '/p:Platform=x64', '/m', '/v:m',
                               '/nologo'],
                              cwd=ROOT, capture_output=True, text=True, encoding='utf-8',
                              errors='replace')
        errors |= {l.strip()[:300] for l in proc.stdout.splitlines()
                   if re.search(r'error (C|LNK|MSB)\d+|: error :', l)}
        returncode = returncode or proc.returncode
        if proc.returncode:
            break
    after = {p: os.path.getmtime(p) if os.path.exists(p) else 0 for p in (EXE, TRACE_DUMP)}

    def stamp(t):
        return time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(t)) if t else None
    print(json.dumps({'exit': returncode, 'seconds': round(time.time() - t0),
                      'errors': sorted(errors)[:20], 'projects': projects,
                      'exe': os.path.relpath(EXE, ROOT), 'exe_updated': after[EXE] > before[EXE],
                      'exe_time': stamp(after[EXE]),
                      'trace_dump_updated': after[TRACE_DUMP] > before[TRACE_DUMP],
                      'trace_dump_time': stamp(after[TRACE_DUMP])},
                     indent=1))
    return returncode


if __name__ == '__main__':
    sys.exit(main())
