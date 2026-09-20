#!/usr/bin/env python3
"""Blue Dragon guest-patch A/B on the device (2026-09-20).

Arms are sets of enabled patches in files/patches/4D5307DF-thor.patch.toml
(the bundled re:Blue levers). Each arm: write the enables (the same edit the
Game Patches screen makes), cool to --cool-c, launch through the play-button
path, wait for the warm load and the title (fps log), replay the documented
route (tools/thor/bd_gameplay_route.sh timings, START/A presses through the
debug gamepad broadcast), then read 30 s of presented fps (tag xenia-fps) and
take two screenshots.

Abort conditions: preflight fails, GPU above 70 C between presses, process
dies, or a step times out. Force-stop after every arm.

Usage: python tools/thor/bd_patch_ab.py [--arms control,fsaa_off,rr075,shadow512]
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
ASSET = os.path.join(REPO, 'android', 'android_studio_project', 'app', 'src', 'main',
                     'assets', 'patches', '4D5307DF-thor.patch.toml')
DEVICE_FILE = 'files/patches/4D5307DF-thor.patch.toml'
TITLE = ('/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/'
         'Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso')
PATCH_NAMES = {
    'fsaa_off': 'No anti-aliasing, single pass',
    'rr075': 'Internal resolution 75%',
    'rr050': 'Internal resolution 50%',
    'shadow512': 'Shadow map 512',
}
# Relative to the first START press, from bd_gameplay_route.sh (absolute
# START@20000 ... A@124000, 1500 ms holds).
ROUTE = [('START', 0), ('A', 8), ('A', 14), ('A', 20), ('START', 26), ('A', 32), ('A', 38),
         ('START', 44), ('A', 50), ('A', 56), ('A', 62), ('START', 68), ('A', 74), ('A', 80),
         ('A', 86), ('START', 92), ('A', 98), ('A', 104)]
HOLD_MS = 1500


def load_mcp():
    spec = importlib.util.spec_from_file_location('xenia_thor_mcp', MCP_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def gpu_c(m):
    t = m._shell('cat /sys/class/kgsl/kgsl-3d0/temp').strip()
    return int(t) / 1000.0 if t.isdigit() else 999.0


def write_arm(m, enabled_names):
    text = open(ASSET, encoding='utf-8').read()
    blocks = text.split('[[patch]]')
    out = [blocks[0]]
    for b in blocks[1:]:
        name = re.search(r'name = "([^"]+)"', b).group(1)
        flag = 'true' if name in enabled_names else 'false'
        b = re.sub(r'is_enabled = (true|false)', f'is_enabled = {flag}', b, count=1)
        out.append(b)
    content = '[[patch]]'.join(out)
    local = os.path.join(REPO, 'scratch', 'mcp', 'arm.patch.toml')
    os.makedirs(os.path.dirname(local), exist_ok=True)
    with open(local, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)
    m._adb('push', local, '/data/local/tmp/arm.patch.toml')
    m._run_as(f'cp /data/local/tmp/arm.patch.toml {DEVICE_FILE}')
    m._shell('rm -f /data/local/tmp/arm.patch.toml')
    return m._run_as(f'grep -c "is_enabled = true" {DEVICE_FILE}').strip()


def fps_tail(m, seconds):
    rows = m._adb('logcat', '-d', '-s', 'xenia-fps:*', timeout=60).splitlines()
    vals = []
    for l in rows:
        mm = re.search(r'fps=([0-9.]+) swaps=(\d+) window_ms=(\d+)', l)
        if mm:
            vals.append((float(mm.group(1)), int(mm.group(3))))
    # Walk back until the windows sum to `seconds`.
    acc, out = 0, []
    for v, w in reversed(vals):
        out.append(v)
        acc += w
        if acc >= seconds * 1000:
            break
    return list(reversed(out))


def run_arm(m, arm, cool_c):
    enabled = {PATCH_NAMES[k] for k in arm.split('+') if k in PATCH_NAMES}
    print(f'\n=== arm {arm}: enabled={sorted(enabled)}; device file has {write_arm(m, enabled)} enabled')
    t0 = time.time()
    while gpu_c(m) > cool_c:
        if time.time() - t0 > 600:
            print('ABORT: no cooldown'); return None
        time.sleep(10)
    pre = json.loads(m.xenia_preflight())
    print('preflight:', pre['ok'], pre['reasons'], pre['temps'], pre['battery'])
    if not pre['ok']:
        return None
    m.xenia_logcat_clear()
    launch = json.loads(m.xenia_launch(TITLE))
    pid = launch.get('pid')
    print('launch:', launch.get('launched'), 'pid', pid, 'battery', launch['battery'])
    if not pid:
        return None
    result = {'arm': arm, 'pid': pid}
    try:
        # Warm load, then the title: two fps samples above 20 in a row.
        t0 = time.time(); stage = 'load'
        while time.time() - t0 < 420:
            time.sleep(3)
            if m._pid(m.PKG) != pid:
                print('process died'); return result | {'result': 'died'}
            log = m._shell(f'logcat -d --pid={pid} -s xenia', timeout=120)
            if stage == 'load' and 'pre-warmed' in log:
                stage = 'title'; print(f'+{time.time() - t0:.0f}s loaded')
                applied = [l.split(' - ', 1)[1].strip() for l in log.splitlines()
                           if 'Applying patch for' in l]
                match = [l[l.find('Patcher: title'):].strip() for l in log.splitlines()
                         if 'Patcher: title' in l]
                result['applied'] = applied
                print('patcher:', match[-1] if match else 'NO MATCH LINE', '| applied:', applied)
                if set(enabled) - set(applied):
                    print('WARNING: enabled patch not reported as applied')
            if stage == 'title':
                tail = fps_tail(m, 3)
                if len(tail) >= 2 and min(tail[-2:]) > 20:
                    print(f'+{time.time() - t0:.0f}s title renders at {tail[-1]} fps'); break
        else:
            print('ABORT: no title'); return result | {'result': 'no title'}
        time.sleep(5)
        # Route.
        r0 = time.time()
        for button, at in ROUTE:
            while time.time() - r0 < at:
                time.sleep(0.2)
            temp = gpu_c(m)
            if temp > 70:
                print(f'ABORT: GPU {temp} C'); return result | {'result': f'hot {temp}'}
            print(f'route +{at:3d}s {button} ({temp:.0f} C): {m.xenia_press(button, HOLD_MS)}')
        # Gameplay window.
        time.sleep(12)
        json.loads(m.xenia_screenshot(f'ab-{arm}-1'))
        time.sleep(18)
        shot = json.loads(m.xenia_screenshot(f'ab-{arm}-2'))
        tail = fps_tail(m, 30)
        s = sorted(tail)
        result.update({'result': 'ok', 'samples': len(tail),
                       'median_fps': s[len(s) // 2] if s else None,
                       'min_fps': s[0] if s else None, 'max_fps': s[-1] if s else None,
                       'gpu_c_end': gpu_c(m), 'shot': shot['path']})
        print('result:', json.dumps(result))
        return result
    finally:
        print('stop:', m.xenia_force_stop())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--arms', default='control,fsaa_off,rr075,shadow512')
    ap.add_argument('--cool-c', type=float, default=45.0)
    args = ap.parse_args()
    m = load_mcp()
    results = []
    for arm in args.arms.split(','):
        r = run_arm(m, arm.strip(), args.cool_c)
        results.append(r or {'arm': arm, 'result': 'aborted'})
    # Restore: everything disabled.
    write_arm(m, set())
    print('\n| arm | result | samples | median fps | min | max | GPU C |')
    print('|---|---|---|---|---|---|---|')
    for r in results:
        print(f"| {r.get('arm')} | {r.get('result')} | {r.get('samples', '')} | {r.get('median_fps', '')} | "
              f"{r.get('min_fps', '')} | {r.get('max_fps', '')} | {r.get('gpu_c_end', '')} |")
    return 0


if __name__ == '__main__':
    sys.exit(main())
