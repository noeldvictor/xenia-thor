"""Validate every SPIR-V shader a trace translates (the Thor runs these).

  python tools/pc/spirv_validate.py TRACE.xtr [TRACE.xtr ...] [--cvars "a=1"]
      [--env vulkan1.2]

Replays each trace on the Vulkan trace dump with --dump_shaders, then runs
spirv-val (WSL, /usr/bin/spirv-val) on every translated module
(*.vulkan.bin.*) and groups the failures by the rule they break, with one
example shader for each. Turnip is strict; a module the desktop driver takes
may not load there. Exit code 1 if a module fails.

2026-09-24: 19 of 364 Banjo shaders broke VUID-StandaloneSpirv-None-10684 (the
register array reused a constant buffer's ArrayStride array type).
"""
import argparse
import collections
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BIN = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release')
OUT = os.path.join(ROOT, 'scratch', 'spirv_validate')
RULE_RE = re.compile(r'\[(VUID-[^\]]+)\]|error: line \d+: (.*)')


def to_wsl(path):
    path = os.path.abspath(path).replace('\\', '/')
    return '/mnt/' + path[0].lower() + path[2:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--env', default='vulkan1.2')
    args = ap.parse_args()
    exe = os.path.join(BIN, 'xenia-gpu-vulkan-trace-dump.exe')
    if not os.path.exists(exe):
        print('build it: tools/pc/build_pc.py --project xenia-gpu-vulkan-trace-dump')
        return 1
    shaders = os.path.join(OUT, 'shaders')
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(shaders)
    for trace in args.traces:
        try:
            subprocess.run([exe, '--target_trace_file=' + trace,
                            '--trace_dump_path=' + OUT + os.sep,
                            '--log_file=' + os.path.join(OUT, 'dump.log'),
                            '--dump_shaders=' + shaders] +
                           ['--' + c for c in args.cvars.split() if c],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=300)
        except subprocess.TimeoutExpired:
            print('%s: replay hung (300 s) - killed' % trace)
    modules = sorted(glob.glob(os.path.join(shaders, '*.vulkan.bin.*')))
    if not modules:
        print('no translated modules dumped')
        return 1
    # One WSL call for all modules: "name<TAB>status<TAB>first error line".
    script = ('cd "%s" && for f in *.vulkan.bin.*; do '
              'o=$(spirv-val --target-env %s "$f" 2>&1); '
              'if [ $? -ne 0 ]; then printf "%%s\\t%%s\\n" "$f" '
              '"$(echo "$o" | head -1)"; fi; done' % (to_wsl(shaders), args.env))
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    result = subprocess.run(['wsl', '-e', 'sh', '-c', script], capture_output=True,
                            text=True, env=env)
    groups = collections.OrderedDict()
    for line in result.stdout.splitlines():
        if '\t' not in line:
            continue
        name, error = line.split('\t', 1)
        m = RULE_RE.search(error)
        rule = (m.group(1) or m.group(2)) if m else error
        groups.setdefault(rule[:120], []).append(name)
    failed = sum(len(v) for v in groups.values())
    print('%d modules, %d fail spirv-val (%s)' % (len(modules), failed, args.env))
    for rule, names in groups.items():
        print('  %4d  %s' % (len(names), rule))
        print('        e.g. %s' % names[0])
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
