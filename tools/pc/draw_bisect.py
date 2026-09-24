"""Which draws make the Vulkan frame differ from D3D12? Bisect a GPU trace.

  python tools/pc/draw_bisect.py TRACE.xtr [--region x0,y0,x1,y1]
      [--mode prefix|vulkan|d3d12|both] [--filter "colorcontrol&0x10"]
      [--cvars "a=1 b=2"] [--max-leaves 6] [--max-evals 40] [--min-gain 0.05]

The draws are numbered from the last swap, the same on every backend
(gpu_debug_skip_draws / gpu_debug_log_draws in command_processor.cc). The
mismatch set M is the pixels (inside --region, the whole frame by default)
where the Vulkan and the D3D12 replays differ by more than 8. A range of draws
is skipped, and if the share of M it explains is --min-gain or more, the range
holds a cause and the search splits it (earliest first) down to single draws.
The modes:
- prefix (default): every draw after N is skipped on both backends, and a
  binary search finds the first N where the two partial frames differ (by
  --min-gain of the final mismatch, or at least 100 pixels) - the first draw
  that renders differently. The resolves still run, so the partial frame is
  what the frame's render targets held at that draw. Two replays per step,
  about 12 steps. The other modes can pick a clear or a background draw
  (skipping it changes both frames the same way; Banjo 23271 draw 77 and Blue
  Dragon 21407 draw 49, 2026-09-24).
- vulkan (one replay per step): skipped on Vulkan only; explained =
  the M pixels that then match the normal D3D12 frame. Finds geometry that
  Vulkan draws and D3D12 does not (Banjo's vines, 2026-09-24).
- d3d12: skipped on D3D12 only, against the normal Vulkan frame. Finds
  geometry that Vulkan does not draw.
- both: skipped on both, the M pixels that then agree. Finds a draw whose
  result differs, but also "explains" any background it removes.
In the vulkan and d3d12 modes, skipping every draw leaves a black frame that
matches nothing, so give candidates: --filter is a Python expression over the
draw log fields (prim, count, vs, ps, depthcontrol, colorcontrol, modecntl,
clipcntl, vskill as integers, and index), for example "colorcontrol & 0x10"
(alpha to mask), "prim == 4" or "index > 49" (past a draw that only removes
the background);
the search then splits the list of matching draws.
Each leaf is printed with its state from the draw log (primitive, count,
shader hashes, RB_DEPTHCONTROL, RB_COLORCONTROL, PA_SU_SC_MODE_CNTL) - compare
them with the D3D12 behavior.

Each evaluation is one replay (two in the "both" mode, about 10-30 s each), so
the search is capped by --max-evals. Needs both trace dumps (tools/pc/build_pc.py --project
xenia-gpu-vulkan-trace-dump / xenia-gpu-d3d12-trace-dump).
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageChops

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

OUT = os.path.join(backend_ab.ROOT, 'scratch', 'draw_bisect')
DRAW_RE = re.compile(r'GPU debug draw (\d+): (.*)$')
FIELD_RE = re.compile(r'(prim|count|vs|ps|depthcontrol|colorcontrol|modecntl|'
                      r'clipcntl|vskill) ([0-9A-Fa-f]+)')
HEX_FIELDS = ('vs', 'ps', 'depthcontrol', 'colorcontrol', 'modecntl', 'clipcntl')


def draw_fields(text):
    return {k: int(v, 16 if k in HEX_FIELDS else 10)
            for k, v in FIELD_RE.findall(text)}


def to_ranges(indices):
    """[1, 2, 3, 7] -> "1-3,7"."""
    parts = []
    start = prev = None
    for i in indices:
        if start is None:
            start = prev = i
        elif i == prev + 1:
            prev = i
        else:
            parts.append('%d-%d' % (start, prev) if prev != start else str(start))
            start = prev = i
    if start is not None:
        parts.append('%d-%d' % (start, prev) if prev != start else str(start))
    return ','.join(parts)


def load(path):
    return Image.open(path).convert('RGB')


def diff_mask(a, b, region):
    d = ImageChops.difference(a, b).convert('L').point(
        lambda v: 255 if v > 8 else 0)
    if region:
        keep = Image.new('L', d.size, 0)
        keep.paste(255, region)
        d = ImageChops.multiply(d, keep)
    return d


def count_in(mask, within):
    return sum(1 for p, m in zip(mask.getdata(), within.getdata()) if p and m)


def prefix_search(args, cvars, region, out, draws, last, total):
    """The first draw after which the Vulkan and the D3D12 frames differ."""
    need = max(100, int(args.min_gain * total))
    cache = {}

    def mismatch(n):
        if n in cache:
            return cache[n]
        skip = ['gpu_debug_skip_draws=%d-%d' % (n + 1, last)] if n < last else []
        tag = 'prefix_%d' % n
        vk, _ = backend_ab.replay('vulkan', args.trace, os.path.join(out, tag, 'vk'),
                                  cvars + skip)
        d3d, _ = backend_ab.replay('d3d12', args.trace, os.path.join(out, tag, 'd3d12'),
                                   cvars + skip)
        count = 0
        if vk and d3d:
            count = sum(1 for p in diff_mask(load(vk), load(d3d), region).getdata() if p)
        print('  draws 0-%d: %d pixels differ' % (n, count))
        cache[n] = count
        return count

    lo, hi = 0, last
    if mismatch(lo) >= need:
        hi = lo
    steps = 0
    while hi - lo > 1 and steps < args.max_evals:
        steps += 1
        mid = (lo + hi) // 2
        if mismatch(mid) >= need:
            hi = mid
        else:
            lo = mid
    print('first divergent draw (the partial frames differ from here, %d+ pixels):'
          % need)
    for i in range(max(0, hi - 2), min(last, hi + 2) + 1):
        print('  %s%d  %s' % ('>' if i == hi else ' ', i, draws.get(i, '?')))
    print('images in', os.path.relpath(out, backend_ab.ROOT),
          '(prefix_<N>/vk and d3d12)')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--region', default='')
    ap.add_argument('--mode', choices=('prefix', 'vulkan', 'd3d12', 'both'),
                    default='prefix')
    ap.add_argument('--filter', default='')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--max-leaves', type=int, default=6)
    ap.add_argument('--max-evals', type=int, default=40)
    ap.add_argument('--min-gain', type=float, default=0.05)
    args = ap.parse_args()
    cvars = [c for c in args.cvars.split() if c]
    region = tuple(int(v) for v in args.region.split(',')) if args.region else None
    name = os.path.splitext(os.path.basename(args.trace))[0]
    out = os.path.join(OUT, name)

    vk0, _ = backend_ab.replay('vulkan', args.trace, os.path.join(out, 'vk'),
                               cvars + ['gpu_debug_log_draws=true'])
    d3d0, _ = backend_ab.replay('d3d12', args.trace, os.path.join(out, 'd3d12'), cvars)
    if not vk0 or not d3d0:
        print('no image (vulkan %s, d3d12 %s)' % (bool(vk0), bool(d3d0)))
        return 1
    draws = {}
    with open(os.path.join(out, 'vk', 'dump.log'), encoding='utf-8',
              errors='replace') as f:
        for line in f:
            m = DRAW_RE.search(line)
            if m:
                draws[int(m.group(1))] = m.group(2).strip()
    if not draws:
        print('no "GPU debug draw" lines - rebuild the trace dumps')
        return 1
    last = max(draws)
    vk0_image = load(vk0)
    d3d0_image = load(d3d0)
    mismatch = diff_mask(vk0_image, d3d0_image, region)
    total = sum(1 for p in mismatch.getdata() if p)
    print('%s: %d draws, %d mismatched pixels%s, mode %s' % (
        name, last + 1, total, ' in %s' % (region,) if region else '',
        args.mode))
    if not total:
        return 0

    if args.mode == 'prefix':
        return prefix_search(args, cvars, region, out, draws, last, total)

    candidates = sorted(draws)
    if args.filter:
        candidates = [i for i in candidates
                      if eval(args.filter, {},
                              dict(draw_fields(draws[i]), index=i))]
        print('%d draws match the filter %r' % (len(candidates), args.filter))
        if not candidates:
            return 0

    evals = [0]

    def gain(subset):
        evals[0] += 1
        ranges = to_ranges(subset)
        skip = ['gpu_debug_skip_draws=' + ranges]
        tag = 'skip_%d_%d_%d' % (subset[0], subset[-1], len(subset))
        if args.mode == 'd3d12':
            vk_image = vk0_image
        else:
            vk, _ = backend_ab.replay('vulkan', args.trace,
                                      os.path.join(out, tag, 'vk'),
                                      cvars + skip)
            if not vk:
                return 0.0
            vk_image = load(vk)
        if args.mode == 'vulkan':
            d3d_image = d3d0_image
        else:
            d3d, _ = backend_ab.replay('d3d12', args.trace,
                                       os.path.join(out, tag, 'd3d12'),
                                       cvars + skip)
            if not d3d:
                return 0.0
            d3d_image = load(d3d)
        still = count_in(diff_mask(vk_image, d3d_image, region), mismatch)
        g = 1.0 - still / total
        print('  skip %s: %.1f%% of the mismatch gone' % (
            ranges if len(ranges) < 60 else '%d draws %d..%d' % (
                len(subset), subset[0], subset[-1]), 100 * g))
        return g

    leaves = []
    stack = [candidates]
    while stack and len(leaves) < args.max_leaves and evals[0] < args.max_evals:
        subset = stack.pop()
        g = gain(subset)
        if g < args.min_gain:
            if subset is candidates:
                print('skipping all %d candidates explains %.1f%% - give '
                      '--filter or another --mode' % (len(subset), 100 * g))
            continue
        if len(subset) == 1:
            leaves.append((subset[0], g))
            continue
        mid = len(subset) // 2
        # Earliest first: the left half is popped first.
        stack.append(subset[mid:])
        stack.append(subset[:mid])
    print('causes (draw, share of the mismatch it explains, state):')
    for index, g in leaves:
        print('  %d  %.1f%%  %s' % (index, 100 * g, draws.get(index, '?')))
    if stack:
        print('not searched (cap): %s' % ', '.join(to_ranges(s) for s in stack))
    print('images in', os.path.relpath(out, backend_ab.ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
