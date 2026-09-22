"""Count the Banjo puzzle-transition stall over N launches per cvar set.

  python tools/thor/stall_study.py --runs 6 "" "cpu_drop_redundant_atomic_release_barrier=false" ...

Each launch: wait for the debug server, wait for the gold puzzle, press
START, wait up to 60 s for the green Spiral Mountain title; then watch the
swap counter for 8 s. STALL = the swaps stop (the frame stats' swaps do not
advance) or the title never comes while the swaps have stopped; OK = the
title arrived with the swaps running. The stall record (hot threads, the
crash log tail) goes to scratch/mcp/stall-<n>.json for the r3 trap work.
The device cools under 41 C between launches (directive 8). Prints one line
per set: stalls/runs and the seconds to the title of each OK run.
"""
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402

DELAY_MS = 0
ROUTE = 'name:puzzle;until:gold>0.35;%spress:START;settle:4000|name:spiral;until:green>0.03&gold<0.2;timeout:60'


def wait_cool(max_case_c=41.0, max_wait_s=1500):
    t0 = time.time()
    while time.time() - t0 < max_wait_s:
        temps = json.loads(m.xenia_preflight()).get('temps', {})
        if temps.get('case_c', 0) < max_case_c:
            return temps
        time.sleep(30)
    return temps


def swaps():
    try:
        return json.loads(m.xenia_api('/frame_stats')).get('swaps')
    except Exception:
        return None


def one_launch(cvars, tag):
    m.xenia_force_stop()
    wait_cool()
    m.xenia_launch_cvars(clear=True)
    if cvars:
        m.xenia_launch_cvars(set=cvars)
    subprocess.call(['adb', 'logcat', '-c'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    r = json.loads(m.xenia_launch('banjo', skip_preflight=True))
    if not r.get('launched'):
        return 'LAUNCH_FAIL', 0
    while time.time() - t0 < 60 and not m._api_up():
        time.sleep(1)
    # A settle:0 step returns no steps at all; omit it without a delay.
    route = ROUTE % ('settle:%d;' % DELAY_MS if DELAY_MS else '')
    g = json.loads(m.xenia_goto(steps=route, launch=False, screenshot=False)).get('goto') or {}
    if not g.get('steps'):
        print('  goto returned no steps: %s' % json.dumps(g)[:300], flush=True)
    steps = g.get('steps', [])
    title = bool(steps) and all(s.get('holds') for s in steps)
    s0 = swaps()
    time.sleep(8)
    s1 = swaps()
    running = s0 is not None and s1 is not None and s1 > s0
    # ALIVE: the title colours were not seen but the swaps advance (the a64
    # backend is slower and the check timed out four times on 2026-09-22 while
    # every run had passed the freeze point); only STALL counts as a stall.
    verdict = 'OK' if (title and running) else ('STALL' if not running else 'ALIVE')
    if not s1:
        # No frame at all inside the window: a boot that did not finish (an
        # LLVM cache being rebuilt takes minutes), not a stall. Six "stalls"
        # of 2026-09-22 were this.
        verdict = 'NO_BOOT'
    if verdict != 'OK':
        rec = {'cvars': cvars, 'verdict': verdict, 'swaps': [s0, s1], 'steps': steps}
        try:
            rec['stall'] = json.loads(m.xenia_stall())
            rec['crash'] = json.loads(m.xenia_crash())
            # The parked thread's registers and chain (the fault record).
            rec['trap'] = m._api('/trap')
        except Exception as e:
            rec['error'] = repr(e)
        path = os.path.join(ROOT, 'scratch', 'mcp', 'stall-%s.json' % tag)
        with open(path, 'w') as f:
            json.dump(rec, f, indent=1)
    m.xenia_force_stop()
    return verdict, round(time.time() - t0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', type=int, default=6)
    ap.add_argument('--delay-ms', type=int, default=0, help='hold this long on the gold puzzle before START')
    ap.add_argument('sets', nargs='*')
    args = ap.parse_args()
    global DELAY_MS
    DELAY_MS = args.delay_ms
    for cv in args.sets or ['']:
        results = []
        for i in range(args.runs):
            v, secs = one_launch(cv, '%s-%d' % (time.strftime('%H%M%S'), i))
            results.append((v, secs))
            print('  %s run %d: %s in %d s' % (cv or 'baseline', i + 1, v, secs), flush=True)
        stalls = sum(1 for v, _ in results if v == 'STALL')
        alive = sum(1 for v, _ in results if v == 'ALIVE')
        noboot = sum(1 for v, _ in results if v == 'NO_BOOT')
        print('%s: %d/%d stalled, %d alive without the title, %d did not boot in the window; ok seconds %s' % (
            cv or 'baseline', stalls, args.runs, alive, noboot,
            [s for v, s in results if v == 'OK']), flush=True)
    m.xenia_launch_cvars(clear=True)


if __name__ == '__main__':
    main()
