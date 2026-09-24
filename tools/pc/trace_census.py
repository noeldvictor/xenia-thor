"""Which Xenos features does a trace use? A census per frame.

  python tools/pc/trace_census.py TRACE.xtr [TRACE.xtr ...] [--cvars "a=1"]

Replays each trace on the Vulkan trace dump with gpu_debug_log_draws and counts
the draws that use the features the SPIR-V path had gaps in (DXBC had them):
tessellated patches, vertex kill (and its OR mode), alpha to mask, user clip
planes (and cull-only), point, rectangle and quad lists. A feature with draws
here and no SPIR-V support is the next Vulkan-only glitch - check the frame
with backend_ab.py and draw_bisect.py.

2026-09-24: Banjo's tessellation, vertex kill and alpha to mask were each found
one frame at a time; this lists them for every trace in one call.
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

OUT = os.path.join(backend_ab.ROOT, 'scratch', 'trace_census')
DRAW_RE = re.compile(r'GPU debug draw (\d+): (.*)$')
FIELD_RE = re.compile(r'(prim|count|depthcontrol|colorcontrol|modecntl|clipcntl|'
                      r'vskill) ([0-9A-Fa-f]+)')
HEX = ('depthcontrol', 'colorcontrol', 'modecntl', 'clipcntl')

# (column, test on the draw fields)
FEATURES = [
    ('patch', lambda f: f.get('prim') in (0x11, 0x12)),
    ('vkill', lambda f: f.get('vskill') == 1),
    ('vkill_or', lambda f: f.get('vskill') == 1 and f.get('clipcntl', 0) >> 21 & 1),
    ('a2mask', lambda f: f.get('colorcontrol', 0) >> 4 & 1),
    ('ucp', lambda f: f.get('clipcntl', 0) & 0x3F and not f.get('clipcntl', 0) >> 16 & 1),
    ('ucp_cull', lambda f: f.get('clipcntl', 0) & 0x3F and not f.get('clipcntl', 0) >> 16 & 1
     and f.get('clipcntl', 0) >> 17 & 1),
    ('points', lambda f: f.get('prim') == 1),
    ('rects', lambda f: f.get('prim') == 8),
    ('quads', lambda f: f.get('prim') == 13),
]


def census(log_path):
    counts = dict((name, 0) for name, _ in FEATURES)
    total = 0
    with open(log_path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = DRAW_RE.search(line)
            if not m:
                continue
            total += 1
            fields = {k: int(v, 16 if k in HEX else 10)
                      for k, v in FIELD_RE.findall(m.group(2))}
            for name, test in FEATURES:
                if test(fields):
                    counts[name] += 1
    return total, counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--cvars', default='')
    args = ap.parse_args()
    cvars = [c for c in args.cvars.split() if c] + ['gpu_debug_log_draws=true']
    print('%-18s %6s ' % ('trace', 'draws') +
          ' '.join('%8s' % name for name, _ in FEATURES) + '  failed')
    for trace in args.traces:
        name = os.path.splitext(os.path.basename(trace))[0]
        out = os.path.join(OUT, name)
        _, failed = backend_ab.replay('vulkan', trace, out, cvars)
        log = os.path.join(out, 'dump.log')
        if not os.path.exists(log):
            print('%-18s no log' % name)
            continue
        total, counts = census(log)
        if not total:
            print('%-18s no "GPU debug draw" lines - rebuild the trace dump' % name)
            continue
        print('%-18s %6d ' % (name, total) +
              ' '.join('%8d' % counts[n] for n, _ in FEATURES) + '  %d' % failed)
    return 0


if __name__ == '__main__':
    sys.exit(main())
