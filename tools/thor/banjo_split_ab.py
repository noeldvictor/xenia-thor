"""How often Banjo's lower frame goes dark, per patch or cvar set.

  python tools/thor/banjo_split_ab.py [--shots 30] ARM ...
  ARM = "label" or "label:patch=No MSAA on the scene targets,cvar=gpu_fp16_shaders=false"

The defect (2026-09-22): below 53% of the height the scene is missing and
only a dark, speckled layer remains, with a sharp horizontal edge at the cut.
It is intermittent, so one screenshot per build proves nothing. Each arm
launches the play path, passes the gold puzzle with START, then takes `shots`
screenshots of the attract fly-through and counts the frames where

  lower band (56-95% of the height) mean luma < 50  and
  the step across the cut row (52.5% -> 54.5%) > 40.

Measured on the first screenshots: bad frames 28-30 / 64-69, good frames
106-111 / 6-20. The arm's patches are reset after the arm; the device cools
between arms. Prints one line per arm: dark frames / shots.
"""
import argparse
import json
import os
import sys
import time

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402

TITLE_ID = '4D5307ED'
ROUTE = 'name:puzzle;until:gold>0.35;press:START;settle:3000|name:start;until:gold<0.2;timeout:60'


def split_metrics(path):
    im = Image.open(path).convert('L')
    w, h = im.size
    px = im.load()

    def band(y0, y1):
        s = n = 0
        for y in range(int(y0 * h), int(y1 * h), 4):
            for x in range(0, w, 8):
                s += px[x, y]
                n += 1
        return s / max(1, n)

    ra, rb = int(0.525 * h), int(0.545 * h)
    step = sum(px[x, ra] - px[x, rb] for x in range(0, w, 4)) / (w / 4)
    return band(0.56, 0.95), step


def is_dark_half(path):
    low, step = split_metrics(path)
    return low < 50 and step > 40, round(low), round(step)


def parse_arm(spec):
    label, _, rest = spec.partition(':')
    patches, cvars = [], []
    for item in [i for i in rest.split(',') if i.strip()]:
        kind, _, value = item.partition('=')
        if kind.strip() == 'patch':
            patches.append(value.strip())
        elif kind.strip() == 'cvar':
            cvars.append(value.strip())
    return label, patches, cvars


def wait_cool(max_case_c=41.0):
    t = {}
    for _ in range(45):
        t = json.loads(m.xenia_preflight()).get('temps', {})
        if t.get('case_c', 99) < max_case_c:
            break
        time.sleep(20)
    return t


def run_arm(label, patches, cvars, shots):
    for p in patches:
        m.xenia_patch_set(TITLE_ID, p, True)
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    for c in cvars:
        m.xenia_launch_cvars(set=c)
    t = wait_cool()
    g = json.loads(m.xenia_goto(steps=ROUTE, title='banjo', launch=True, screenshot=False)).get('goto') or {}
    dark, rows = 0, []
    # The live values: a launch-order override would void the arm silently.
    live = []
    for c in cvars:
        name = c.split('=', 1)[0]
        try:
            live.append('%s=%s' % (name, m._api('/cvar?name=%s' % name).get('value')))
        except Exception:
            live.append('%s=?' % name)
    if live:
        print('  %s live: %s' % (label, ' '.join(live)), flush=True)
    # The arm's full settings snapshot (every non-default cvar, all sources),
    # so each result names the settings it ran with.
    try:
        snap = json.loads(m.xenia_cvars())
        print('  %s settings: %s non-default, %s' % (label, snap.get('count'), snap.get('saved')), flush=True)
    except Exception as e:
        print('  %s settings: unavailable (%s)' % (label, e), flush=True)
    fps = None
    if g.get('reached'):
        # Presented fps over the shot window (the swap counter), so an arm's
        # image result and its speed come from the same run.
        try:
            s0, t0 = json.loads(m.xenia_api('/frame_stats')).get('swaps'), time.time()
        except Exception:
            s0 = None
        for i in range(shots):
            path = json.loads(m.xenia_screenshot('split-%s-%02d' % (label, i))).get('path')
            if path and os.path.exists(path):
                bad, low, step = is_dark_half(path)
                dark += bad
                rows.append('%s%d/%d' % ('X' if bad else '.', low, step))
                if not bad:
                    os.remove(path)  # keep only the evidence frames
            time.sleep(1.0)
        try:
            s1 = json.loads(m.xenia_api('/frame_stats')).get('swaps')
            if s0 is not None and s1 is not None:
                fps = (s1 - s0) / max(0.1, time.time() - t0)
        except Exception:
            pass
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    for p in patches:
        m.xenia_patch_set(TITLE_ID, p, False)
    print('%-28s dark=%d/%d fps=%s reached=%s case=%.1fC  %s' % (
        label, dark, len(rows), ('%.1f' % fps) if fps is not None else '?', g.get('reached'),
        t.get('case_c', 0), ' '.join(rows)), flush=True)
    return dark, len(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--shots', type=int, default=30)
    ap.add_argument('arms', nargs='+')
    args = ap.parse_args()
    for spec in args.arms:
        run_arm(*parse_arm(spec), args.shots)
    return 0


if __name__ == '__main__':
    sys.exit(main())
