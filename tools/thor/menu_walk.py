"""Walk a title's menus one press at a time and see each screen, in one launch.

  python tools/thor/menu_walk.py [--title banjo] [--start STEPS] [--settle 4000] PRESS ...
  PRESS = a button (A, B, START, DPAD_DOWN, ...), optionally BUTTON:settle_ms

Launches the title, runs the goto steps in --start (default: Banjo's puzzle ->
START), then presses each button in order. After each press it waits the
settle time, takes a screenshot (kept, the path is printed), and reads the
swap counter twice two seconds apart. When the swaps stop it calls
xenia_stall once and prints the verdict and the frames of a thread that
faulted in host code, then stops: a frozen screen is the answer, not a step
to press through (2026-09-22: the Banjo menu froze on a closed file that the
handle cache kept alive, and the walk needed a stall picture by hand).
Each line: step, button, swaps delta, fps over 2 s, screenshot, mean luma of
the top and bottom halves (a black or dark half shows at once).
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

BANJO_START = 'name:puzzle;until:gold>0.35;press:START;settle:3000|name:start;until:gold<0.2;timeout:120'


def swaps():
    try:
        return json.loads(m.xenia_api('/frame_stats')).get('swaps')
    except Exception:
        return None


def halves(path):
    im = Image.open(path).convert('L')
    w, h = im.size
    px = im.load()

    def band(y0, y1):
        s = n = 0
        for y in range(int(y0 * h), int(y1 * h), 6):
            for x in range(0, w, 8):
                s += px[x, y]
                n += 1
        return s / max(1, n)
    return round(band(0.0, 0.5)), round(band(0.5, 1.0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--title', default='banjo')
    ap.add_argument('--start', default=BANJO_START, help='goto steps before the presses ("" = none)')
    ap.add_argument('--settle', type=int, default=4000)
    ap.add_argument('--no-launch', action='store_true', help='walk the running game')
    ap.add_argument('presses', nargs='*')
    args = ap.parse_args()
    if not args.no_launch:
        m.xenia_force_stop()
        m.xenia_launch_cvars(clear=True)
    if args.start:
        g = json.loads(m.xenia_goto(steps=args.start, title=args.title, launch=not args.no_launch,
                                    screenshot=False)).get('goto') or {}
        print('start: reached=%s seconds=%s' % (g.get('reached'), g.get('seconds')), flush=True)
        if not g.get('reached'):
            print(json.dumps(g)[:600])
            return 1
    for i, spec in enumerate(args.presses):
        button, _, settle = spec.partition(':')
        settle_ms = int(settle) if settle else args.settle
        m.xenia_press(button.upper())
        time.sleep(settle_ms / 1000.0)
        s0 = swaps()
        time.sleep(2.0)
        s1 = swaps()
        shot = json.loads(m.xenia_screenshot('walk-%02d-%s' % (i + 1, button))).get('path')
        top, bottom = halves(shot) if shot and os.path.exists(shot) else (None, None)
        d = (s1 - s0) if (s0 is not None and s1 is not None) else None
        print('%2d %-10s swaps %s -> %s (+%s, %.1f fps)  top %s bottom %s  %s' % (
            i + 1, button, s0, s1, d, (d or 0) / 2.0, top, bottom, shot), flush=True)
        if d == 0:
            st = json.loads(m.xenia_stall())
            print('FROZEN after %s: %s' % (button, st.get('verdict')), flush=True)
            for r in (st.get('host_fault_threads') or [])[:1]:
                for f in r.get('frames', [])[:14]:
                    print('   ', f[:200])
            return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
