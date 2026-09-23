"""Build our Turnip from Windows in one call: WSL build, patch list, zip copied to scratch.

  python tools/turnip/build.py [MESA_REF] [--patches DIR|none] [--plain-cpu]

Runs tools/turnip/build_turnip.sh in WSL (Ubuntu). Python calls wsl.exe
directly, so Git Bash cannot rewrite the /mnt/<drive> script path (2026-09-23:
the build "passed" with exit 0 and built nothing). Fails when the log has no
"done:" line. Copies the zip to scratch/tools/turnip/ (git-ignored) and prints
one JSON object: zip, mesa sha, patches applied, seconds.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT_DIR = os.path.join(ROOT, 'scratch', 'tools', 'turnip')


def wsl_path(path):
    path = os.path.abspath(path).replace('\\', '/')
    drive, rest = path[0].lower(), path[2:]
    return '/mnt/%s%s' % (drive, rest)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref', nargs='?', default='885dd3a17a')
    ap.add_argument('--patches', default='', help='patch dir, or "none" (default: tools/turnip/patches)')
    ap.add_argument('--plain-cpu', action='store_true', help='NDK default CPU flags (no -tuned)')
    args = ap.parse_args()
    script = wsl_path(os.path.join(ROOT, 'tools', 'turnip', 'build_turnip.sh'))
    cmd = ['wsl', '-d', 'Ubuntu', '--']
    if args.plain_cpu:
        cmd += ['env', 'TURNIP_CPU_FLAGS=']
    cmd += ['bash', script, args.ref]
    if args.patches:
        cmd.append(args.patches if args.patches == 'none' else wsl_path(args.patches))
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    log = [l for l in (proc.stdout + proc.stderr).splitlines() if 'Failed to translate' not in l]
    done = next((l.split('done:', 1)[1].strip() for l in log if l.startswith('done:')), None)
    result = {'exit': proc.returncode, 'seconds': round(time.time() - t0),
              'mesa': next((l for l in log if l.startswith('== mesa')), '?'),
              'patches': [l.split('patch', 1)[1].strip() for l in log if l.strip().startswith('patch ')]}
    if proc.returncode or not done:
        result['error'] = 'no "done:" line - the build did not produce a zip'
        result['tail'] = log[-25:]
        print(json.dumps(result, indent=1))
        return 1
    os.makedirs(OUT_DIR, exist_ok=True)
    local = os.path.join(OUT_DIR, os.path.basename(done))
    subprocess.run(['wsl', '-d', 'Ubuntu', '--', 'cp', done, wsl_path(local)], check=True,
                   capture_output=True)
    result['zip'] = os.path.relpath(local, ROOT).replace('\\', '/')
    result['bytes'] = os.path.getsize(local)
    print(json.dumps(result, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
