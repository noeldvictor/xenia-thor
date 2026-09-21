"""Reach a screen on the device, record one GPU frame, replay it on the PC.

  python tools/thor/trace_at_screen.py [--title banjo] [--route "40:START 60:A"]
                                       [--settle 65] [--wait-score 0.5] [--wait-max 60]

The device trace (.xtr) is pulled to scratch/traces and rendered by the
PC's xenia-gpu-vulkan-trace-dump. The device screenshot and the PC render
of the same command stream sit side by side: when the PC render is right,
the glitch is in the device's execution (Android code or the driver); when
both are wrong, it is the shared GPU logic and the PC bisects it.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402
sys.path.insert(0, os.path.dirname(__file__))
from glitch_ab import score_png, shot  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--title', default='banjo')
    ap.add_argument('--route', default='40:START 60:A')
    ap.add_argument('--settle', type=int, default=65)
    ap.add_argument('--wait-score', type=float, default=0.5)
    ap.add_argument('--wait-max', type=int, default=60)
    ap.add_argument('--skip-preflight', action='store_true')
    args = ap.parse_args()
    route = []
    for item in args.route.split():
        at, _, button = item.partition(':')
        route.append((int(at), button))
    r = json.loads(m.xenia_launch(args.title, skip_preflight=args.skip_preflight))
    print('launch', r['launched'], r.get('preflight', {}).get('reasons', ''))
    if not r['launched']:
        return 2
    t0 = time.time()
    try:
        while time.time() - t0 < args.settle:
            time.sleep(1)
            now = time.time() - t0
            for at, button in list(route):
                if now >= at:
                    m._api('/press?button=%s&hold_ms=150' % button, 'POST')
                    print('+%.0f s press %s' % (now, button))
                    route.remove((at, button))
        path = shot('trace-screen')
        score = score_png(path, 'lower_black')
        waited = 0
        while score < args.wait_score and waited < args.wait_max:
            time.sleep(5)
            waited += 5
            path = shot('trace-screen')
            score = score_png(path, 'lower_black')
            print('  +%d s lower_black=%.3f' % (waited, score))
        print('screen %s lower_black=%.3f' % (os.path.basename(path), score))
        result = json.loads(m.xenia_trace_frame())
        print(json.dumps({k: v for k, v in result.items() if k != 'dump_out'}, indent=1))
        if result.get('dump_out'):
            print('dump tail:', result['dump_out'][-400:])
        if result.get('png') and os.path.exists(result['png']):
            from PIL import Image
            im = Image.open(result['png'])
            g = im.convert('L')
            w, h = g.size
            px = g.crop((0, h // 2, w, h)).getdata()
            print('PC replay %s %dx%d lower_black=%.3f' % (result['png'], w, h,
                  sum(1 for v in px if v < 12) / max(1, len(px))))
    finally:
        m.xenia_force_stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
