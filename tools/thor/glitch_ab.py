"""Live A/B of GPU cvars against a visible glitch, one launch, one screen.

  python tools/thor/glitch_ab.py [--title banjo] [--route "40:START"] [--settle 50]
                                 [--cvars name=value,...] [--score lower_black]

Launches the title, plays the route to a stable screen, takes the baseline
screenshot, then sets each cvar live through the in-app server, waits 3 s,
screenshots, scores, and restores it. The score is a number a person does
not need to eyeball: "lower_black" is the fraction of near-black pixels in
the lower half of the frame (the Banjo menu glitch of 2026-09-21), "black"
the whole frame. Prints one line per cvar with the score delta, saves every
screenshot to scratch/mcp, and force-stops the title. A cvar that moves
the score is the axis; one that does not is ruled out in three seconds
instead of a five-minute relaunch.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402

DEFAULT_CVARS = [
    'gpu_vrs_foliage_rate=0',
    'vulkan_cache_sampler_parameters=false',
    'vulkan_fast_register_ranges=false',
    'vulkan_skip_redundant_fetch_constant_writes=false',
    'rt_cache_ownership_claim_memo=false',
    'vulkan_mid_frame_submission_draws=0',
]


def score_png(path, mode):
    from PIL import Image
    im = Image.open(path).convert('L')
    w, h = im.size
    if mode == 'lower_black':
        im = im.crop((0, h // 2, w, h))
    px = im.getdata()
    n = len(px)
    dark = sum(1 for v in px if v < 12)
    return dark / n if n else 0.0


def shot(name):
    r = json.loads(m.xenia_screenshot(name))
    return r['path']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--title', default='banjo')
    ap.add_argument('--route', default='40:START', help='"<s>:<BUTTON> ..." after launch')
    ap.add_argument('--settle', type=int, default=50, help='seconds after launch for the baseline')
    ap.add_argument('--cvars', default=','.join(DEFAULT_CVARS))
    ap.add_argument('--score', default='lower_black', choices=['lower_black', 'black'])
    ap.add_argument('--skip-preflight', action='store_true',
                    help='a 90 s menu run at 60 C GPU is safe on a warm case')
    ap.add_argument('--wait-score', type=float, default=0.0,
                    help='after the route, wait (up to --wait-max s) until the score reaches this')
    ap.add_argument('--wait-max', type=int, default=60)
    args = ap.parse_args()
    route = []
    for item in args.route.split():
        at, _, button = item.partition(':')
        route.append((int(at), button))
    r = json.loads(m.xenia_launch(args.title, skip_preflight=args.skip_preflight))
    print('launch', r['launched'], 'pid', r.get('pid'), r.get('preflight', {}).get('reasons', ''))
    if not r['launched']:
        return 2
    t0 = time.time()
    try:
        while time.time() - t0 < args.settle:
            time.sleep(1)
            now = time.time() - t0
            for at, button in list(route):
                if now >= at:
                    print('+%.0f s press %s' % (now, m._api('/press?button=%s&hold_ms=150' % button, 'POST')))
                    route.remove((at, button))
        base = shot('ab-base')
        base_score = score_png(base, args.score)
        # A timed route lands on a transition as often as on the screen; wait
        # for the glitch itself (its score) instead of a clock.
        waited = 0
        while args.wait_score and base_score < args.wait_score and waited < args.wait_max:
            gpu_c = m._temps().get('gpu_c') or 0
            if gpu_c >= 70:
                print('ABORT: GPU %s C' % gpu_c)
                return 3
            time.sleep(5)
            waited += 5
            base = shot('ab-base')
            base_score = score_png(base, args.score)
            print('  +%d s waiting: %s=%.3f GPU %s C' % (waited, args.score, base_score, gpu_c))
        print('baseline %s=%.3f %s' % (args.score, base_score, os.path.basename(base)))
        for item in args.cvars.split(','):
            name, _, value = item.partition('=')
            if not name:
                continue
            before = m._api('/cvar?name=%s' % name, timeout=5).get('value')
            m._api('/cvar?name=%s&value=%s' % (name, value), 'POST', timeout=5)
            time.sleep(3)
            path = shot('ab-' + name)
            s = score_png(path, args.score)
            print('%-48s %s=%.3f (%+.3f) %s' % (item, args.score, s, s - base_score, os.path.basename(path)))
            if before is not None:
                m._api('/cvar?name=%s&value=%s' % (name, before), 'POST', timeout=5)
                time.sleep(1)
    finally:
        m.xenia_force_stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
