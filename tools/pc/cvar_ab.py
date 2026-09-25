"""A cvar A/B on the Vulkan trace dump: does a lever change the picture?

  python tools/pc/cvar_ab.py TRACE.xtr [TRACE.xtr ...] --a "x=false" --b "x=true"
      [--cvars "common=1"] [--thor-profile] [--gpu vulkan|d3d12]

Replays every trace twice on the same backend, once with the --a cvars and
once with the --b cvars (plus --cvars for both), and compares the two frames:
the pixels that differ at all, the pixels off by more than 8, and the largest
difference. A translator or pipeline lever that must not change the output
(a specialization, a cheaper equivalent) shows 0 changed pixels on every
trace; a pair with changes goes to scratch/cvar_ab/ (A above, B below).
Exit code 1 if any trace changed.

2026-09-24: vulkan_tfetch_sign_specialize (pixel shaders without the texture
sign conversion when no bound texture needs it) - the proof that it is exact.
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageChops

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

ROOT = backend_ab.ROOT
OUT = os.path.join(ROOT, 'scratch', 'cvar_ab')


DRAW_RE = re.compile(r'GPU debug draw (\d+): (.*)')


def frames_differ(a_png, b_png, threshold):
    a = Image.open(a_png).convert('RGB')
    b = Image.open(b_png).convert('RGB')
    if a.size != b.size:
        return True
    values = ImageChops.difference(a, b).convert('L').getdata()
    return any(v > threshold for v in values)


def prefix_search(trace, common, cvars_a, cvars_b, threshold):
    """The first draw of the frame after which A and B differ: both replays
    keep draws 0..N-1 (gpu_debug_skip_draws, resolves are never skipped) and
    N is binary-searched. Prints that draw's line of gpu_debug_log_draws
    (primitive, count, the vertex and pixel shader hashes)."""
    name = os.path.splitext(os.path.basename(trace))[0]
    out = os.path.join(OUT, name + '_prefix')
    png, _ = backend_ab.replay('vulkan', trace, os.path.join(out, 'log'),
                               common + cvars_a + ['gpu_debug_log_draws=true'])
    log = os.path.join(out, 'log', 'dump.log')
    draws = {}
    for line in open(log, encoding='utf-8', errors='replace'):
        m = DRAW_RE.search(line)
        if m:
            draws[int(m.group(1))] = m.group(2).strip()
    total = max(draws) + 1 if draws else 0
    if not total:
        print('%s: no draws logged' % name)
        return 1

    def differs(n):
        skip = ['gpu_debug_skip_draws=%d-999999' % n] if n < total else []
        images = []
        for side, extra in (('a', cvars_a), ('b', cvars_b)):
            probe = os.path.join(out, '%s%d' % (side, n))
            png, _ = backend_ab.replay('vulkan', trace, probe, common + extra + skip)
            images.append(png)
        return all(images) and frames_differ(images[0], images[1], threshold)

    if not differs(total):
        print('%s: the full frames do not differ by more than %d' % (name, threshold))
        return 0
    lo, hi = 0, total  # differs(hi) is true; find the smallest such n.
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if differs(mid):
            hi = mid
        else:
            lo = mid
    print('%s: A and B first differ after draw %d of %d' % (name, hi - 1, total))
    print('  draw %d: %s' % (hi - 1, draws.get(hi - 1, '?')))
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--prefix', action='store_true',
                    help='binary-search the first draw after which A and B differ '
                         '(one trace; names its shaders)')
    ap.add_argument('--threshold', type=int, default=8,
                    help='--prefix: a pixel differs above this')
    ap.add_argument('--a', required=True, help='cvars of replay A')
    ap.add_argument('--b', required=True, help='cvars of replay B')
    ap.add_argument('--cvars', default='', help='cvars of both replays')
    ap.add_argument('--thor-profile', action='store_true',
                    help='both replays with the Thor GPU settings (backend_ab.py)')
    ap.add_argument('--gpu', default='vulkan', choices=('vulkan', 'd3d12'))
    args = ap.parse_args()
    common = [c for c in args.cvars.split() if c]
    if args.thor_profile:
        common += backend_ab.thor_profile_cvars(include_vrs=False)
    if args.prefix:
        return prefix_search(args.traces[0], common, [c for c in args.a.split() if c],
                             [c for c in args.b.split() if c], args.threshold)
    changed = 0
    print('%-28s %10s %10s %6s  %s' % ('trace', 'changed', 'off>8', 'max', 'failed a/b'))
    for trace in args.traces:
        name = os.path.splitext(os.path.basename(trace))[0]
        images = []
        failed = []
        for side, extra in (('a', args.a), ('b', args.b)):
            out = os.path.join(OUT, name, side)
            for f in os.listdir(out) if os.path.isdir(out) else []:
                os.remove(os.path.join(out, f))
            png, fails = backend_ab.replay(args.gpu, trace, out,
                                           common + [c for c in extra.split() if c])
            images.append(png)
            failed.append(fails)
        if not all(images):
            print('%-28s no image (a: %s, b: %s)' % (name, images[0], images[1]))
            changed += 1
            continue
        a = Image.open(images[0]).convert('RGB')
        b = Image.open(images[1]).convert('RGB')
        if a.size != b.size:
            print('%-28s size %s vs %s' % (name, a.size, b.size))
            changed += 1
            continue
        diff = ImageChops.difference(a, b).convert('L')
        values = list(diff.getdata())
        any_off = sum(1 for v in values if v)
        off8 = sum(1 for v in values if v > 8)
        print('%-28s %9.4f%% %9.4f%% %6d  %d/%d' % (
            name, 100.0 * any_off / len(values), 100.0 * off8 / len(values),
            max(values), failed[0], failed[1]))
        if any_off:
            changed += 1
            pair = Image.new('RGB', (a.width, a.height * 2))
            pair.paste(a, (0, 0))
            pair.paste(b, (0, a.height))
            pair.save(os.path.join(OUT, name + '_pair.png'))
    print('%d of %d traces changed' % (changed, len(args.traces)))
    return 1 if changed else 0


if __name__ == '__main__':
    sys.exit(main())
