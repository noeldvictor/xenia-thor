#!/usr/bin/env python3
"""Probe a title on the device: launch through the play-button path, wait for
the load, then take a screenshot timeline while pressing an optional button
route. Reports the presented fps per interval and any crash. For a title with
no documented route yet (2026-09-20: Banjo-Kazooie).

Abort conditions: preflight fails, GPU above 70 C, process dies, time limit.
Force-stop at the end.

Usage:
  python tools/thor/title_probe.py --title banjo --seconds 240 \
      --route "60:START 75:A 90:A 105:A 120:START 135:A"
  route items are "<seconds after load>:<BUTTON>[:hold_ms]".
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
TITLES = {
    'bd': ('/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/'
           'Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'),
    'banjo': ('/storage/2664-21DE/Roms/xbox360/Banjo-Kazooie - Nuts & Bolts (USA) '
              '(En,Ja,Fr,De,Es,It,Nl,Sv,No,Zh,Ko,Pl,Ru,Cs).iso'),
}


def load_mcp():
    spec = importlib.util.spec_from_file_location('xenia_thor_mcp', MCP_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def gpu_c(m):
    t = m._shell('cat /sys/class/kgsl/kgsl-3d0/temp').strip()
    return int(t) / 1000.0 if t.isdigit() else 999.0


def fps_recent(m, seconds):
    # The badge history from the MCP inside the emulator (60 windows); the
    # logcat line stays as the fallback when the app is not reachable.
    vals = []
    try:
        for row in m._api('/fps', timeout=20):
            vals.append((float(row['fps']), int(row['window_ms'])))
    except RuntimeError:
        rows = m._adb('logcat', '-d', '-s', 'xenia-fps:*', timeout=60).splitlines()
        for l in rows:
            mm = re.search(r'fps=([0-9.]+) swaps=(\d+) window_ms=(\d+)', l)
            if mm:
                vals.append((float(mm.group(1)), int(mm.group(3))))
    acc, out = 0, []
    for v, w in reversed(vals):
        out.append(v)
        acc += w
        if acc >= seconds * 1000:
            break
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--title', default='banjo')
    ap.add_argument('--seconds', type=int, default=240, help='probe length after the load')
    ap.add_argument('--shot-every', type=int, default=20)
    ap.add_argument('--route', default='')
    ap.add_argument('--load-limit', type=int, default=900)
    ap.add_argument('--profile-at', type=int, default=0,
                    help='seconds after the load to run xenia_profile (0 = never)')
    ap.add_argument('--profile-seconds', type=int, default=15)
    args = ap.parse_args()
    m = load_mcp()
    title = TITLES.get(args.title, args.title)
    route = []
    for item in args.route.split():
        parts = item.split(':')
        route.append((int(parts[0]), parts[1], int(parts[2]) if len(parts) > 2 else 150))
    route.sort()

    pre = json.loads(m.xenia_preflight())
    print('preflight:', pre['ok'], pre['reasons'], pre['temps'], pre['battery'])
    if not pre['ok']:
        return 2
    m.xenia_logcat_clear()
    launch = json.loads(m.xenia_launch(title))
    pid = launch.get('pid')
    print('launch:', launch.get('launched'), 'pid', pid, 'battery', launch['battery'])
    if not pid:
        return 2
    result = 'time limit'
    try:
        t0 = time.time()
        while time.time() - t0 < args.load_limit:
            time.sleep(5)
            if m._pid(m.PKG) != pid:
                result = 'died during load'; break
            # The load is done when the in-app log ring holds the pre-warm
            # line (the precompile pass ended); the title comes from /status.
            try:
                pw = m._api('/log?lines=4&grep=pre-warmed', timeout=30)
            except RuntimeError:
                pw = []
            if pw:
                print(f'+{time.time() - t0:.0f}s loaded:', pw[0][pw[0].find('pre-warmed'):].strip()[:80])
                st = m._api('/status', timeout=20)
                print(f'    title {st.get("title_id")} {st.get("title_name")}')
                for l in m._api('/log?lines=6&grep=patch', timeout=20) or []:
                    print('   ', l[:150])
                break
        else:
            result = 'load timeout'
        if result == 'time limit':
            l0 = time.time()
            next_shot = l0 + args.shot_every
            ri = 0
            saw_frames = False
            zero_shots = 0
            stall_reported = False
            profiled = False
            while time.time() - l0 < args.seconds:
                time.sleep(1)
                now = time.time() - l0
                if m._pid(m.PKG) != pid:
                    result = f'died at +{now:.0f}s'; break
                temp = gpu_c(m)
                if temp > 70:
                    result = f'ABORT: GPU {temp} C'; break
                if args.profile_at and not profiled and now >= args.profile_at:
                    profiled = True
                    print(f'+{now:4.0f}s profile {args.profile_seconds} s ...')
                    pr = json.loads(m.xenia_profile(args.profile_seconds, True, True))
                    print('    presented fps:', pr.get('presented_fps'), 'perf:', pr.get('perf_data'))
                    for l in pr.get('by_dso', [])[:6]:
                        print('    dso:', l[:150])
                    for l in pr.get('top', [])[:14]:
                        print('    top:', l[:170])
                    gh = pr.get('guest_hot')
                    if isinstance(gh, list):
                        for r in gh[:16]:
                            print('    guest:', r)
                    else:
                        print('    guest:', gh)
                while ri < len(route) and route[ri][0] <= now:
                    _, button, hold = route[ri]; ri += 1
                    print(f'+{now:4.0f}s press {button}: {m.xenia_press(button, hold)}')
                if time.time() >= next_shot:
                    next_shot = time.time() + args.shot_every
                    shot = json.loads(m.xenia_screenshot(f'probe-{args.title}-{int(now)}s'))
                    f = fps_recent(m, args.shot_every)
                    med = f[len(f) // 2] if f else None
                    print(f'+{now:4.0f}s shot {os.path.basename(shot["path"])} fps median {med} '
                          f'(n={len(f)}) GPU {temp:.0f} C')
                    # The stall reflex: frames seen, then two intervals without
                    # a frame, means one stall picture (markers, hot threads,
                    # wait channels) printed here instead of found by hand.
                    if med:
                        saw_frames = True
                        zero_shots = 0
                    else:
                        zero_shots += 1
                    if saw_frames and zero_shots >= 2 and not stall_reported:
                        stall_reported = True
                        st = json.loads(m.xenia_stall(pid))
                        print(f'+{now:4.0f}s STALL: {st["verdict"]}; badge {st["fps_badge"]}; '
                              f'GPU busy {st["gpu_busy"]}')
                        for l in st['markers'][-6:]:
                            print('    marker:', l[:200])
                        for r in st['hot_threads']:
                            print(f'    thread {r["tid"]} {r["cpu_pct"]}%: '
                                  f'{r["comm_state_uticks_sticks_wchan"]}')
        print('result:', result)
        if result.startswith('died'):
            print(m._shell('logcat -b crash -d -t 60')[-3000:])
        log = m._shell(f'logcat -d --pid={pid} -s xenia', timeout=120)
        bad = [l for l in log.splitlines() if any(k in l for k in
               ('guest crash', 'Fatal', 'unimplemented', 'GPU is hung', 'Failed to', 'DbgPrint'))]
        print(f'log lines of interest ({len(bad)}):')
        for l in bad[-15:]:
            print('   ', l[l.find('xenia'):].strip()[:170])
    finally:
        print('stop:', m.xenia_force_stop())
    return 0


if __name__ == '__main__':
    sys.exit(main())
