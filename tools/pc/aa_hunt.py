"""Which setting makes a Vulkan replay nondeterministic? An A/A hunt.

  python tools/pc/aa_hunt.py TRACE.xtr --flips "vulkan_push_descriptors=false a=b ..."
      [--runs 3] [--cvars "common=1"] [--thor-profile] [--region x0,y0,x1,y1]
      [--devices 0,1]

Replays the trace --runs times with the common cvars (the baseline), then
--runs times for each flip (one cvar=value added to the common ones), and
prints per row the largest number of pixels (inside --region, or the frame)
that differ by more than 8 between any two of its replays. A row that goes to
0 where the baseline varies names a setting the nondeterminism needs. A
replay that differs from itself is a read of undefined data or a race on the
GPU - the kind of glitch that also flickers in the live game.

--devices: the baseline once per Vulkan device (vulkan_device=N; the PC has
the NVIDIA card as 0 and the Intel iGPU as 1). A device that stays at 0 where
another varies names a driver-specific fault in one call.

2026-09-29: Gears' black light-shaft wedges (the deep route, near the weapon
pickup) - Vulkan replays of one trace differ from draw 270 on, D3D12 replays
(RTV and ROV) never do, Intel Vulkan replays never do: the NVIDIA compiler
mishandles a switch case fallthrough in divergent invocations (the
translator reaches every label through its loop since).
"""
import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

OUT = os.path.join(backend_ab.ROOT, 'scratch', 'aa_hunt')


def replays(trace, cvars, runs, label):
    images = []
    for i in range(runs):
        out = os.path.join(OUT, label.replace('=', '_'), str(i))
        for f in glob.glob(os.path.join(out, '*')):
            if os.path.isfile(f):
                os.remove(f)
        png, _ = backend_ab.replay('vulkan', trace, out, cvars)
        if png:
            images.append(np.array(Image.open(png).convert('RGB')).astype(int))
    return images


def spread(images, region):
    worst = 0
    for i in range(len(images)):
        for j in range(i + 1, len(images)):
            a, b = images[i], images[j]
            if region:
                x0, y0, x1, y1 = region
                a, b = a[y0:y1, x0:x1], b[y0:y1, x0:x1]
            worst = max(worst, int((np.abs(a - b).max(axis=2) > 8).sum()))
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--flips', default='', help='cvar=value settings to try, separated by spaces')
    ap.add_argument('--runs', type=int, default=3)
    ap.add_argument('--cvars', default='')
    ap.add_argument('--thor-profile', action='store_true')
    ap.add_argument('--region', default='')
    ap.add_argument('--devices', default='',
                    help='Vulkan device indices for extra baselines, e.g. 0,1')
    args = ap.parse_args()
    common = [c for c in args.cvars.split() if c]
    if args.thor_profile:
        common += backend_ab.thor_profile_cvars(include_vrs=False)
    region = tuple(int(v) for v in args.region.split(',')) if args.region else None
    images = replays(args.trace, common, args.runs, 'baseline')
    base = spread(images, region)
    print('%-48s %8d pixels differ between replays (%d of %d replays)' % (
        'baseline', base, len(images), args.runs), flush=True)
    for device in [d for d in args.devices.split(',') if d.strip()]:
        label = 'vulkan_device=%s' % device.strip()
        images = replays(args.trace, common + [label], args.runs, label)
        if len(images) < 2:
            print('%-48s   FAILED (%d of %d replays wrote an image)' % (
                'baseline ' + label, len(images), args.runs), flush=True)
            continue
        print('%-48s %8d' % ('baseline ' + label, spread(images, region)),
              flush=True)
    for flip in [f for f in args.flips.split() if f]:
        images = replays(args.trace, common + [flip], args.runs, flip)
        if len(images) < 2:
            # No image: the replay failed (device lost, error dialog) - never
            # "deterministic" (2026-09-29: a broken lever read as the fix).
            print('%-48s   FAILED (%d of %d replays wrote an image)' % (
                flip, len(images), args.runs), flush=True)
            continue
        s = spread(images, region)
        mark = '  <- deterministic' if base and s == 0 else ''
        print('%-48s %8d%s' % (flip, s, mark), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
