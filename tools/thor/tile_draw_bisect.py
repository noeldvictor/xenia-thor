"""Find the draw that turns Banjo's lower (window-offset) tile dark, in one launch.

  python tools/thor/tile_draw_bisect.py [--screen menu|start]

Launches with gpu_debug_offset_tile_draw_limit set high (counting, no skip),
routes to the Single Player menu (dark in every frame on the device,
2026-09-22), reads the offset draws per tile pass from the log, then
bisects N with live cvar sets: with only the first N offset draws the
lower band is bright (the scene up to that draw is fine) or dark. The
first N that turns it dark names the culprit draw index. Each step: set,
wait 1.5 s, two screenshots, lower-band luma (56-95% of the height).
Also prints the lower band for N = 0 (no offset draws at all) as a
reference.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'thor'))
import xenia_thor_mcp as m  # noqa: E402
import banjo_split_ab as b  # noqa: E402

START = 'name:puzzle;until:gold>0.35;press:START;settle:3000|name:start;until:gold<0.2;timeout:120'
MENU = START + ';press:A;settle:6000'


def lower_band(tag):
    vals = []
    for i in range(2):
        p = json.loads(m.xenia_screenshot('bisect-%s-%d' % (tag, i))).get('path')
        low, _ = b.split_metrics(p)
        vals.append(low)
        if i == 0:
            keep = p
        else:
            os.remove(p)
        time.sleep(0.4)
    return sum(vals) / len(vals), keep


def set_limit(n):
    m._api('/cvar?name=gpu_debug_offset_tile_draw_limit&value=%d' % n, 'POST')
    time.sleep(1.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--screen', default='menu', choices=['menu', 'start'])
    ap.add_argument('--dark', type=float, default=50.0)
    args = ap.parse_args()
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    m.xenia_launch_cvars(set='gpu_debug_offset_tile_draw_limit=1000000')
    steps = MENU if args.screen == 'menu' else START
    g = json.loads(m.xenia_goto(steps=steps, title='banjo', launch=True, screenshot=False)).get('goto') or {}
    print('reached', g.get('reached'), flush=True)
    time.sleep(3)
    log = subprocess.run(['adb', '-s', m.SERIAL, 'logcat', '-d', '-s', 'xenia'], capture_output=True,
                         text=True, errors='replace').stdout
    counts = [int(x) for x in re.findall(r'OFFSET TILE: (\d+) offset draws', log)]
    total = max(counts[-5:]) if counts else 600
    print('offset draws per tile pass (recent):', counts[-5:], '-> bisect range 0..%d' % total, flush=True)
    full, shot_full = lower_band('full')
    set_limit(0)
    none, shot_none = lower_band('none')
    print('all offset draws: lower band %.0f (%s); none: %.0f (%s)' % (full, shot_full, none, shot_none), flush=True)
    lo, hi = 0, total  # invariant: limit lo is bright, limit hi is dark
    if not (none >= args.dark and full < args.dark):
        print('no bright->dark transition to bisect (none=%.0f full=%.0f)' % (none, full))
    else:
        while hi - lo > 1:
            mid = (lo + hi) // 2
            set_limit(mid)
            v, shot = lower_band('n%d' % mid)
            dark = v < args.dark
            print('  limit %4d: lower band %.0f %s' % (mid, v, 'DARK' if dark else 'bright'), flush=True)
            if dark:
                hi = mid
            else:
                lo = mid
        print('first dark with %d offset draws: draw index %d turns the tile dark' % (hi, hi - 1), flush=True)
        set_limit(hi - 1)
        v1, s1 = lower_band('before')
        set_limit(hi)
        v2, s2 = lower_band('after')
        print('before %.0f %s | after %.0f %s' % (v1, s1, v2, s2), flush=True)
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
