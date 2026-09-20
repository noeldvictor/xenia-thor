#!/usr/bin/env python3
"""Device test for the "Compiling game code" overlay (2026-09-20).

Launches a title through the launcher's VIEW intent (the play button path),
then watches the compile from outside the app:
  - the Java watcher's "xenia-aot" log lines (pass running, pass done),
  - the native "AOT precompile progress" lines,
  - the UI watchdog line, which must NOT appear during the compile,
  - a screenshot every `--shot-every` seconds, so the overlay text is on record,
  - the largest free gap in /proc/<pid>/maps every `--maps-every` seconds,
    which is the Scudo AOT OOM test the archive owes (2026-08-10, 2026-08-17).

Abort conditions: preflight fails, GPU temperature above 70 C, the process
dies, or the time limit passes. The emulator is force-stopped at the end and
the stop is verified with pidof.

Usage:
  python tools/thor/aot_overlay_test.py [--title PATH] [--limit-s 900]
"""
import argparse
import importlib.util
import json
import os
import re
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MCP_PATH = os.path.join(REPO, 'tools', 'mcp', 'xenia_thor_mcp.py')
DEFAULT_TITLE = ('/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/'
                 'Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso')


def load_mcp():
    spec = importlib.util.spec_from_file_location('xenia_thor_mcp', MCP_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def largest_free_gap(maps_text):
    """Largest hole between consecutive mappings, in bytes, and where it is."""
    ranges = []
    for line in maps_text.splitlines():
        m = re.match(r'([0-9a-f]+)-([0-9a-f]+) ', line)
        if m:
            ranges.append((int(m.group(1), 16), int(m.group(2), 16)))
    ranges.sort()
    best = (0, 0)
    prev_end = None
    for start, end in ranges:
        if prev_end is not None and start - prev_end > best[0]:
            best = (start - prev_end, prev_end)
        prev_end = max(prev_end or 0, end)
    return best[0], best[1], len(ranges)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--title', default=DEFAULT_TITLE)
    ap.add_argument('--limit-s', type=int, default=900)
    ap.add_argument('--shot-every', type=int, default=60)
    ap.add_argument('--maps-every', type=int, default=30)
    args = ap.parse_args()

    m = load_mcp()
    pre = json.loads(m.xenia_preflight())
    print('preflight:', json.dumps(pre))
    if not pre.get('ok'):
        print('ABORT: preflight failed')
        return 2

    m.xenia_logcat_clear()
    launch = json.loads(m.xenia_launch(args.title))
    print('launch:', json.dumps(launch))
    if not launch.get('launched'):
        print('ABORT: launch failed')
        return 2
    pid = launch['pid']
    print(f'battery at launch: {launch.get("battery")}')

    t0 = time.time()
    next_shot = t0 + 10
    next_maps = t0 + args.maps_every
    seen = set()
    result = 'time limit'
    gap_log = []
    while time.time() - t0 < args.limit_s:
        time.sleep(5)
        if m._pid(m.PKG) != pid:
            result = 'process died'
            break
        temp = m._shell('cat /sys/class/kgsl/kgsl-3d0/temp').strip()
        if temp.isdigit() and int(temp) > 70000:
            result = f'ABORT: GPU {int(temp) / 1000:.1f} C'
            break
        log = m._shell(f'logcat -d --pid={pid} -s xenia-aot xenia-uiwatchdog xenia')
        for line in log.splitlines():
            if ('xenia-aot' in line or 'xenia-uiwatchdog' in line
                    or 'AOT precompile progress' in line or 'pre-warmed' in line):
                key = line[19:]
                if key not in seen:
                    seen.add(key)
                    print(f'+{time.time() - t0:5.0f}s {line.strip()[:160]}')
        now = time.time()
        if now >= next_shot:
            next_shot = now + args.shot_every
            print('shot:', m.xenia_screenshot(f'aot-overlay-{int(now - t0)}s'))
        if now >= next_maps:
            next_maps = now + args.maps_every
            maps = m._run_as(f'cat /proc/{pid}/maps', timeout=60)
            gap, at, count = largest_free_gap(maps)
            rss = m._shell(f'grep VmRSS /proc/{pid}/status').strip()
            entry = (int(now - t0), gap, at, count, rss)
            gap_log.append(entry)
            print(f'+{entry[0]:5d}s maps: {count} mappings, largest gap '
                  f'{gap / (1 << 30):.1f} GiB at 0x{at:x}, {rss}')
        if any('pre-warmed' in k for k in seen) and now - t0 > 20:
            # Give the overlay its 900 ms fade and the game a few seconds.
            time.sleep(8)
            print('shot:', m.xenia_screenshot('aot-overlay-done'))
            result = 'precompile done'
            break

    print('result:', result)
    if result == 'process died':
        crash = m._shell('logcat -b crash -d -t 80')
        print(crash[-3000:])
    print('stop:', m.xenia_force_stop())
    print('gap log:', gap_log)
    return 0 if result == 'precompile done' else 1


if __name__ == '__main__':
    sys.exit(main())
