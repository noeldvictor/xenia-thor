"""Build the Windows app (xenia.exe) in one call and check that the exe is new.

  python tools/pc/build_pc.py [--project xenia-app]

Finds MSBuild (Visual Studio 2022 Build Tools or any edition), builds
build/<project>.vcxproj with Configuration "Release Windows" and Platform x64
(the only working pair; 2026-09-23 it took three tries to find), and prints one
JSON object: exit, seconds, the compiler/linker errors, and whether
build/bin/Windows/Release/xenia.exe changed.
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


def find_msbuild():
    for base in (r'C:\Program Files (x86)\Microsoft Visual Studio', r'C:\Program Files\Microsoft Visual Studio'):
        hits = sorted(glob.glob(os.path.join(base, '*', '*', 'MSBuild', 'Current', 'Bin', 'MSBuild.exe')))
        if hits:
            return hits[-1]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default='xenia-app')
    args = ap.parse_args()
    msbuild = find_msbuild()
    if not msbuild:
        print(json.dumps({'exit': 1, 'error': 'MSBuild.exe not found'}))
        return 1
    before = os.path.getmtime(EXE) if os.path.exists(EXE) else 0
    t0 = time.time()
    proc = subprocess.run([msbuild, os.path.join(ROOT, 'build', args.project + '.vcxproj'),
                           '/p:Configuration=Release Windows', '/p:Platform=x64', '/m', '/v:m', '/nologo'],
                          cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
    out = proc.stdout.splitlines()
    errors = sorted({l.strip()[:300] for l in out if re.search(r'error (C|LNK|MSB)\d+|: error :', l)})
    after = os.path.getmtime(EXE) if os.path.exists(EXE) else 0
    print(json.dumps({'exit': proc.returncode, 'seconds': round(time.time() - t0), 'errors': errors[:20],
                      'exe': os.path.relpath(EXE, ROOT), 'exe_updated': after > before,
                      'exe_time': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(after)) if after else None},
                     indent=1))
    return proc.returncode


if __name__ == '__main__':
    sys.exit(main())
