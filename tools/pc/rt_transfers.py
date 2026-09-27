"""What moves EDRAM between render targets in a frame, and why?

  python tools/pc/rt_transfers.py TRACE.xtr [TRACE.xtr ...] [--cvars "a=1 b=2"]
      [--thor-profile] [--draws 3]

Replays each trace on the Vulkan trace dump with the transfer trace
(gpu_trace_render_target_transfers), the draw log (gpu_debug_log_draws, now
with RB_SURFACE_INFO, RB_DEPTH_INFO and RB_COLOR_INFO 0) and the per-frame
lines, and prints for the frame:
- the ownership transfers grouped by destination <- source render target
  (base in tiles, pitch in tiles, MSAA, format), with the draws that caused
  them - each transfer ends the render pass and draws the old owner's pixels
  into the new one, a GMEM store and reload on the Thor;
- the render pass breaks by cause and the 4x MSAA depth clears drawn into
  the 1x render target (gpu_fold_msaa_depth_clears), and why the others were
  not (the first condition that failed).

2026-09-27: Gears of War clears each shadow depth region with a 4x MSAA
depth-only rectangle and draws the casters at 1x on the same EDRAM: two
transfers per shadow and 18 of the frame's "depth" breaks. This listing
named it in one replay; the fold removes 13 to 23 transfers a frame.
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

OUT = os.path.join(backend_ab.ROOT, 'scratch', 'rt_transfers')
DRAW_RE = re.compile(r'GPU debug draw (\d+): prim (\d+) count (\d+).*?'
                     r'depthcontrol ([0-9A-F]+).*?surface ([0-9A-F]+) '
                     r'depthinfo ([0-9A-F]+) color0info ([0-9A-F]+)')
XFER_RE = re.compile(r'RT transfer: slot (\d+) dest (.*?) <- source (.*?) tiles \[(\d+), (\d+)\)')
FOLD_RE = re.compile(r'MSAA fold: rejected - (.*)$')
BREAK_RE = re.compile(r'GPU pass breaks/frame: (.*)$')
OUTCOME_RE = re.compile(r'GPU draw outcomes/frame: .*?rt_transfers=(\d+).*?brk_open=(\d+)')


def surface(value):
    v = int(value, 16)
    return 'pitch %d %dx' % (v & 0x3FFF, 1 << ((v >> 16) & 3))


def analyze(log_path, draws_shown):
    last_draw = None
    draws = {}
    pairs = collections.OrderedDict()
    rejections = collections.Counter()
    breaks = ''
    outcome = None
    for line in open(log_path, encoding='utf-8', errors='replace'):
        m = DRAW_RE.search(line)
        if m:
            last_draw = int(m.group(1))
            draws[last_draw] = 'prim %s count %s depthcontrol %s %s depth base %d' % (
                m.group(2), m.group(3), m.group(4), surface(m.group(5)),
                int(m.group(6), 16) & 0xFFF)
            continue
        m = XFER_RE.search(line)
        if m:
            key = ('color' if m.group(1) != '0' else 'depth', m.group(2), m.group(3))
            pairs.setdefault(key, []).append((last_draw, int(m.group(5)) - int(m.group(4))))
            continue
        m = FOLD_RE.search(line)
        if m:
            rejections[m.group(1).strip()] += 1
            continue
        m = BREAK_RE.search(line)
        if m:
            breaks = m.group(1).strip()
            continue
        m = OUTCOME_RE.search(line)
        if m:
            outcome = (int(m.group(1)), int(m.group(2)))
    total = sum(len(v) for v in pairs.values())
    print('  %d transfers%s' % (total, '' if outcome is None else
                                '; frame line: rt_transfers=%d, render pass breaks %d' % outcome))
    if breaks:
        print('  pass breaks by cause: ' + breaks)
    for (slot, dest, source), hits in sorted(pairs.items(), key=lambda kv: -len(kv[1])):
        tiles = sum(h[1] for h in hits)
        print('  %3d x %-5s %s <- %s (%d tiles)' % (len(hits), slot, dest, source, tiles))
        for draw, _ in hits[:draws_shown]:
            if draw is not None:
                print('          at draw %d: %s' % (draw, draws.get(draw, '')))
    if rejections:
        print('  4x MSAA depth draws not folded:')
        for reason, n in rejections.most_common():
            print('    %3d  %s' % (n, reason))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--cvars', default='', help='extra cvars, name=value, separated by spaces')
    ap.add_argument('--thor-profile', action='store_true',
                    help='the Thor GPU settings (Android defaults + default-on toggles)')
    ap.add_argument('--draws', type=int, default=3, help='draws shown per transfer pair')
    args = ap.parse_args()
    cvars = []
    if args.thor_profile:
        cvars += backend_ab.thor_profile_cvars(include_vrs=False)
    cvars += [c for c in args.cvars.split() if c]
    cvars += ['gpu_trace_render_target_transfers=100000', 'gpu_debug_log_draws=true',
              'vulkan_trace_draw_outcomes_per_frame=true']
    for trace in args.traces:
        name = os.path.splitext(os.path.basename(trace))[0]
        out = os.path.join(OUT, name)
        for old in ('dump.log',):
            if os.path.exists(os.path.join(out, old)):
                os.remove(os.path.join(out, old))
        print(name)
        png, failed = backend_ab.replay('vulkan', trace, out, cvars)
        log = os.path.join(out, 'dump.log')
        if not os.path.exists(log):
            print('  no log - the replay failed')
            continue
        analyze(log, args.draws)
    return 0


if __name__ == '__main__':
    sys.exit(main())
