"""Live A/B of cvars inside one device launch, at a scoreboard scene.

  python tools/thor/live_ab.py gears1 "base:" "novrs:gpu_vrs_foliage_rate=0" \
      "nocull:gpu_cull_compaction=false,gpu_whole_draw_only=false" [--rounds 2] [--seconds 8]

Reaches the entry's scene (tools/thor/scoreboard.py ENTRIES) once, then for
each round and each arm: sets the arm's cvars live (/cvar POST; the other
arms' cvars go back to the values read at the start), waits 3 s, measures
presented fps over --seconds, GPU busy, and takes a screenshot. Two rounds
in alternating order cancel scene drift. One launch, no relaunch per arm,
no cool-down between arms (the outcome rules: split in one launch). Prints a
table per arm: mean fps, gpu busy, and the screenshot paths.
"""
import argparse
import json
import os
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'thor'))
import xenia_thor_mcp as m  # noqa: E402
import scoreboard  # noqa: E402


def parse_arm(spec):
    label, _, rest = spec.partition(':')
    pairs = [p.split('=', 1) for p in rest.split(',') if '=' in p]
    return label, [(k.strip(), v.strip()) for k, v in pairs]


def set_cvar(name, value):
    return m._api('/cvar?name=%s&value=%s' % (name, value), 'POST').get('applied')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('entry')
    ap.add_argument('arms', nargs='+')
    ap.add_argument('--rounds', type=int, default=2)
    ap.add_argument('--seconds', type=float, default=8.0)
    ap.add_argument('--max-case-c', type=float, default=46.0,
                    help='stop the run when the case passes this temperature')
    args = ap.parse_args()
    arms = [parse_arm(a) for a in args.arms]
    spec = dict(scoreboard.ENTRIES[args.entry])
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    for c in spec.get('cvars', []):
        m.xenia_launch_cvars(set=c)
    scoreboard.wait_cool()
    if spec['kind'] == 'goto':
        g = json.loads(m.xenia_goto(steps=spec['steps'], title=spec.get('title') or spec['path'],
                                    launch=True, screenshot=False)).get('goto') or {}
        print('route: reached=%s seconds=%s' % (g.get('reached'), g.get('seconds')), flush=True)
    else:
        m.xenia_launch(spec['path'], skip_preflight=True)
        t0 = time.time()
        for at, button in spec['presses']:
            time.sleep(max(0.0, at - (time.time() - t0)))
            m.xenia_press(button)
        time.sleep(max(0.0, spec['at'] - (time.time() - t0)))
    names = sorted({k for _, pairs in arms for k, _ in pairs})
    start = {n: m._api('/cvar?name=%s' % n).get('value') for n in names}
    print('start values:', ' '.join('%s=%s' % kv for kv in start.items()), flush=True)
    results = {label: [] for label, _ in arms}
    aborted = False
    for rnd in range(args.rounds):
        if aborted:
            break
        order = arms if rnd % 2 == 0 else list(reversed(arms))
        for label, pairs in order:
            # Heat guard (user, 2026-09-22: "we cannot let the emulator fry the
            # device"): stop the whole run, not just wait, when the case passes
            # the limit mid-run.
            case_c = json.loads(m.xenia_preflight()).get('temps', {}).get('case_c', 0)
            if case_c > args.max_case_c:
                print('  STOP: case %.1f C > %.1f C - the device is too hot; the run ends here'
                      % (case_c, args.max_case_c), flush=True)
                aborted = True
                break
            for n in names:
                set_cvar(n, start[n])
            for k, v in pairs:
                set_cvar(k, v)
            time.sleep(3)
            s0, t0 = scoreboard.swaps(), time.time()
            time.sleep(args.seconds)
            s1, t1 = scoreboard.swaps(), time.time()
            fps = (s1 - s0) / (t1 - t0) if (s0 is not None and s1 is not None) else 0.0
            busy = m._shell('cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage').strip()
            shot = json.loads(m.xenia_screenshot('liveab-%s-%s-r%d' % (args.entry, label, rnd))).get('path')
            results[label].append((fps, busy, shot))
            print('  round %d %-10s fps %5.1f  gpu busy %s  %s' % (rnd, label, fps, busy, shot), flush=True)
    if not aborted:
        for n in names:
            set_cvar(n, start[n])
    print('arm        mean fps')
    for label, rows in results.items():
        print('%-10s %6.1f  %s' % (label, sum(r[0] for r in rows) / max(1, len(rows)),
                                   ' '.join(r[1] for r in rows)))
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
