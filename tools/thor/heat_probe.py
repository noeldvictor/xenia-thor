"""What burns the power: one short launch, the temperatures and the busiest
threads over time.

  python tools/thor/heat_probe.py [gears|mc2|PATH] [--cool 15] [--seconds 75]
      [--max-case-c 42] [--every 15] [--cvars "a=1 b=2"]

Waits until the device is ready (xenia_wait_ready), launches the title the
play-button way with the extra launch cvars (--cool N adds thor_debug_cool=N),
then every --every seconds reads the case and hottest CPU/GPU zone, the GPU
load, the battery (charging or not) and the per-thread CPU ticks of the
emulator (xenia_threads), and prints the threads that used the most CPU in
that interval. It force-stops the emulator at --max-case-c or after --seconds,
whichever comes first, and prints the average CPU % per thread name over the
run and the case slope in C per minute. Uses the device: ask the user first.

2026-10-01: in cool mode (15 fps) the GPU was 25-30% busy and the chip stayed
under 75 C, but the case still rose about 5 C a minute - this names the
threads (or the charger) behind it in one call.
"""
import argparse
import collections
import json
import os
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xenia_thor_mcp as m  # noqa: E402
import scoreboard  # noqa: E402

TITLES = {'gears': scoreboard.GEARS, 'mc2': scoreboard.MC2}
TICKS_PER_S = 100.0  # Linux USER_HZ


def sample():
    status = json.loads(m.xenia_device_status())
    temps = status.get('temps', {})
    battery = json.loads(m.xenia_preflight()).get('battery', {})
    try:
        threads = json.loads(m.xenia_threads(top=64))
    except Exception:
        threads = []
    return temps, battery, threads if isinstance(threads, list) else []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('title', nargs='?', default='gears')
    ap.add_argument('--cool', type=int, default=0)
    ap.add_argument('--seconds', type=float, default=75.0)
    ap.add_argument('--max-case-c', type=float, default=42.0)
    ap.add_argument('--every', type=float, default=15.0)
    ap.add_argument('--cvars', default='')
    ap.add_argument('--wait', type=int, default=900,
                    help='seconds to wait for the preflight (panel awake, case cool)')
    args = ap.parse_args()
    path = TITLES.get(args.title, args.title)

    # Say at once what the wait is for: a sleeping panel needs the user's power
    # button (2026-10-02: an approved probe waited 15 silent minutes on it).
    first = json.loads(m.xenia_preflight())
    if not first.get('ok'):
        print('waiting (up to %d s) for: %s' % (args.wait, '; '.join(
            r.split(chr(10))[0] for r in first.get('reasons', []))), flush=True)
    ready = json.loads(m.xenia_wait_ready(timeout_s=args.wait))
    if not ready.get('ok'):
        print('device not ready:', ready.get('reasons'))
        return 1
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    extra = [c for c in args.cvars.split() if c]
    if args.cool:
        extra.append('thor_debug_cool=%d' % args.cool)
    for c in extra:
        m.xenia_launch_cvars(set=c)
    launched = json.loads(m.xenia_launch(path, skip_preflight=True))
    print('launched', launched.get('launched'), 'cvars', ' '.join(extra) or '-', flush=True)

    start = time.time()
    last_swaps = scoreboard.swaps()
    previous = {}
    totals = collections.Counter()
    samples = []
    stopped = ''
    while True:
        time.sleep(args.every)
        elapsed = time.time() - start
        temps, battery, threads = sample()
        swaps_now = scoreboard.swaps()
        fps = (round((swaps_now - last_swaps) / args.every, 1)
               if swaps_now is not None and last_swaps is not None else None)
        last_swaps = swaps_now
        case_c = float(temps.get('case_c') or 0)
        samples.append((elapsed, case_c))
        interval = []
        for row in threads:
            tid = row.get('host_tid') or row.get('tid') or row.get('id')
            ticks = row.get('cpu_ticks', 0)
            # The guest thread name when the kernel knows it, else the host comm.
            name = row.get('name') or row.get('comm') or str(tid)
            if tid in previous:
                delta = ticks - previous[tid]
                if delta > 0:
                    interval.append((delta, name))
                    totals[name] += delta
            previous[tid] = ticks
        interval.sort(reverse=True)
        top = ', '.join('%s %.0f%%' % (name, 100.0 * d / (TICKS_PER_S * args.every))
                        for d, name in interval[:6])
        print('%4.0f s fps %s case %.1f C hottest %s gpu %s charging %s | %s' % (
            elapsed, fps, case_c, temps.get('hottest_cpu_gpu_zone_c'), temps.get('gpu_busy'),
            battery.get('charging'), top or '(no thread data yet)'), flush=True)
        if case_c >= args.max_case_c:
            stopped = 'case %.1f C' % case_c
        elif elapsed >= args.seconds:
            stopped = 'time %d s' % args.seconds
        if stopped:
            break
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)

    span = samples[-1][0] - samples[0][0] if len(samples) > 1 else 0
    slope = (samples[-1][1] - samples[0][1]) / span * 60 if span else 0
    print('\nstopped: %s; case %.1f -> %.1f C (%+.1f C/min)' % (
        stopped, samples[0][1], samples[-1][1], slope))
    seconds = samples[-1][0] - samples[0][0] if len(samples) > 1 else args.every
    print('average CPU per thread over the run (100% = one core):')
    for name, ticks in totals.most_common(12):
        print('  %-40s %5.0f%%' % (name, 100.0 * ticks / (TICKS_PER_S * max(seconds, 1))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
