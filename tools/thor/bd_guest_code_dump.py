#!/usr/bin/env python3
"""Dump the first 8 MB of Blue Dragon's guest code (0x82000000) from the device.

Uses the diagnostic cvar dump_guest_mem_at_ms through the persisted config,
launches through the play-button path, waits for the dump line, pulls the file
with run-as, then restores the config keys and force-stops. The dump is a
derivative of the game and stays under scratch/ (never committed).

Usage: python tools/thor/bd_guest_code_dump.py [--out scratch/blue-dragon/bd_code_82000000.bin]
"""
import argparse
import importlib.util
import json
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MCP_PATH = os.path.join(REPO, 'tools', 'mcp', 'xenia_thor_mcp.py')
TITLE = ('/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/'
         'Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso')
KEYS = {
    'dump_guest_mem_at_ms': '3000',
    'dump_guest_mem_size_mb': '8',
    'dump_guest_mem_path': '"/data/data/jp.xenia.emulator.github.debug/files/gm.bin"',
}
RESTORE = {
    'dump_guest_mem_at_ms': '0',
    'dump_guest_mem_size_mb': '64',
    'dump_guest_mem_path': '"/data/local/tmp/guestmem.bin"',
}


def load_mcp():
    spec = importlib.util.spec_from_file_location('xenia_thor_mcp', MCP_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, 'scratch', 'blue-dragon',
                                                  'bd_code_82000000.bin'))
    ap.add_argument('--limit-s', type=int, default=600)
    args = ap.parse_args()
    m = load_mcp()

    pre = json.loads(m.xenia_preflight())
    print('preflight:', json.dumps(pre))
    if not pre.get('ok'):
        print('ABORT: preflight failed')
        return 2

    for k, v in KEYS.items():
        print('set:', m.xenia_config_set(k, v))
    try:
        m.xenia_logcat_clear()
        launch = json.loads(m.xenia_launch(TITLE))
        print('launch:', json.dumps(launch))
        if not launch.get('launched'):
            return 2
        pid = launch['pid']
        t0 = time.time()
        result = 'time limit'
        while time.time() - t0 < args.limit_s:
            time.sleep(5)
            if m._pid(m.PKG) != pid:
                result = 'process died'
                break
            temp = m._shell('cat /sys/class/kgsl/kgsl-3d0/temp').strip()
            if temp.isdigit() and int(temp) > 70000:
                result = f'ABORT: GPU {int(temp) / 1000:.1f} C'
                break
            log = m._shell(f'logcat -d --pid={pid} -s xenia')
            hit = [l for l in log.splitlines() if 'guest-mem dump' in l]
            if hit:
                print(f'+{time.time() - t0:.0f}s', hit[-1].strip()[:200])
                result = 'dumped'
                break
        print('result:', result)
    finally:
        print('stop:', m.xenia_force_stop())
        for k, v in RESTORE.items():
            print('restore:', m.xenia_config_set(k, v))
    if result != 'dumped':
        return 1
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    # run-as cat through adb exec-out keeps the bytes intact.
    import subprocess
    out = subprocess.run(['adb', '-s', m.SERIAL, 'exec-out',
                          f'run-as {m.PKG} cat files/gm.bin'],
                         capture_output=True, timeout=300).stdout
    with open(args.out, 'wb') as f:
        f.write(out)
    m._run_as('rm -f files/gm.bin')
    print(f'saved {len(out)} bytes to {args.out}')
    return 0 if len(out) == 8 * 1024 * 1024 else 1


if __name__ == '__main__':
    sys.exit(main())
