"""The command processor's CPU cost per draw on the PC, from a GPU trace.

  python tools/pc/trace_bench.py TRACE.xtr [TRACE.xtr ...] [--iterations 30]
      [--cvars "a=1"] [--thor-profile] [--a "x=false" --b "x=true"]

Replays each trace on the Vulkan trace dump, then plays the captured frame
--iterations times more with warm caches (trace_dump_bench_iterations) and
vulkan_trace_draw_outcomes_per_frame on. Every replay logs its wall time and
the per-frame CPU buckets of the draw path (IssueDraw total, emit, setup,
bindings, process, vertex residency, prep, textures, render targets,
pipeline, state). The first two replays are cold and are dropped; the table
shows the median of the others, per frame and per draw.

With --a and --b: the same trace with each cvar set, in the order A B A B
(drift on the PC is larger than many levers), and the change per bucket.
x64 is not the Thor's CPU: use it for the direction and the size of a change
in the command processor, then judge the speed on the device.
"""
import argparse
import os
import re
import statistics
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

ROOT = backend_ab.ROOT
OUT = os.path.join(ROOT, 'scratch', 'trace_bench')
BENCH_RE = re.compile(r'Trace bench: replay (\d+) (\d+) us')
CPU_RE = re.compile(r'GPU draw cpu/frame: (.*)')
TEX_RE = re.compile(r'GPU tex cpu/frame: (.*)')
TEX_FIELDS = ('calls', 'checked', 'changed', 'loads', 'load_us', 'update_us')
BUCKETS = ('issuedraw_us', 'emit_us', 'setup_us', 'bind_us', 'process_us', 'vfres_us',
           'prep_us', 'tex_us', 'rt_us', 'pipe_us', 'state_us')
COLD = 2


def run(trace, cvars, iterations, tag):
    out = os.path.join(OUT, tag)
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(backend_ab.BIN, 'xenia-gpu-vulkan-trace-dump.exe')
    log = os.path.join(out, 'dump.log')
    if os.path.exists(log):
        os.remove(log)
    args = [exe, '--target_trace_file=' + trace, '--trace_dump_path=' + out + os.sep,
            '--log_file=' + log, '--trace_dump_bench_iterations=%d' % iterations,
            '--vulkan_trace_draw_outcomes_per_frame=true']
    args += ['--' + c for c in backend_ab.backend_cvars('vulkan', cvars)]
    try:
        subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=600)
    except subprocess.TimeoutExpired:
        return None
    text = open(log, encoding='utf-8', errors='replace').read() if os.path.exists(log) else ''
    walls = [int(m.group(2)) for m in BENCH_RE.finditer(text)]
    frames = []
    for m in CPU_RE.finditer(text):
        fields = dict(kv.split('=', 1) for kv in m.group(1).split() if '=' in kv)
        frames.append({k: int(v) for k, v in fields.items() if v.isdigit()})
    # The first frame line is the capture playback; then one per replay.
    frames = frames[1:] if len(frames) > len(walls) else frames
    tex = []
    for m in TEX_RE.finditer(text):
        fields = dict(kv.split('=', 1) for kv in m.group(1).split() if '=' in kv)
        tex.append({k: int(v) for k, v in fields.items() if v.isdigit()})
    tex = tex[1:] if len(tex) > len(walls) else tex
    for frame, t in zip(frames, tex):
        frame.update({'tex_' + k: v for k, v in t.items()})
    return {'wall': walls[COLD:], 'frames': frames[COLD:]}


def summary(result):
    if not result or not result['frames']:
        return None
    med = {'wall_us': statistics.median(result['wall']) if result['wall'] else 0}
    for key in ('draws',) + BUCKETS + tuple('tex_' + k for k in TEX_FIELDS):
        med[key] = statistics.median(f.get(key, 0) for f in result['frames'])
    return med


def print_row(label, s):
    draws = max(s['draws'], 1)
    print('  %-10s draws %5d  wall %7.0f us  issuedraw %7.0f us = %5.2f us/draw  %s' % (
        label, s['draws'], s['wall_us'], s['issuedraw_us'], s['issuedraw_us'] / draws,
        ' '.join('%s %.0f' % (k[:-3], s[k]) for k in BUCKETS[1:] if s[k])))
    if s.get('tex_calls'):
        print('  %-10s textures: %s' % ('', ' '.join('%s %.0f' % (k, s['tex_' + k]) for k in TEX_FIELDS)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--iterations', type=int, default=30)
    ap.add_argument('--cvars', default='')
    ap.add_argument('--thor-profile', action='store_true')
    ap.add_argument('--a', default=None)
    ap.add_argument('--b', default=None)
    args = ap.parse_args()
    exe = os.path.join(backend_ab.BIN, 'xenia-gpu-vulkan-trace-dump.exe')
    if not os.path.exists(exe):
        print('build it: tools/pc/build_pc.py --project xenia-gpu-vulkan-trace-dump')
        return 1
    common = [c for c in args.cvars.split() if c]
    if args.thor_profile:
        common += backend_ab.thor_profile_cvars(include_vrs=False)
    for trace in args.traces:
        name = os.path.splitext(os.path.basename(trace))[0]
        print(name)
        if args.a is None or args.b is None:
            s = summary(run(trace, common, args.iterations, name))
            if s:
                print_row('median', s)
            else:
                print('  no bench lines (replay failed or hung)')
            continue
        sides = {'A': [], 'B': []}
        for round_ in range(2):
            for side, extra in (('A', args.a), ('B', args.b)):
                r = run(trace, common + [c for c in extra.split() if c], args.iterations,
                        '%s_%s%d' % (name, side, round_))
                if r:
                    sides[side].append(r)
        merged = {}
        for side, results in sides.items():
            merged[side] = summary({'wall': sum((r['wall'] for r in results), []),
                                    'frames': sum((r['frames'] for r in results), [])})
        if not merged['A'] or not merged['B']:
            print('  a side has no bench lines')
            continue
        print_row('A', merged['A'])
        print_row('B', merged['B'])
        a, b = merged['A'], merged['B']
        print('  B-A       wall %+.1f%%  issuedraw %+.1f%%  %s' % (
            100.0 * (b['wall_us'] - a['wall_us']) / max(a['wall_us'], 1),
            100.0 * (b['issuedraw_us'] - a['issuedraw_us']) / max(a['issuedraw_us'], 1),
            ' '.join('%s %+.0f' % (k[:-3], b[k] - a[k]) for k in BUCKETS[1:]
                     if a[k] or b[k])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
