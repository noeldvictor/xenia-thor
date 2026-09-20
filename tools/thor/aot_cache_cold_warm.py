#!/usr/bin/env python3
"""Cold-then-warm AOT object cache test on the device (2026-09-20).

Run 1 (cold): launch through the play-button path, wait for the precompile
pass to end, record its duration, save /proc/<pid>/maps every --maps-every
seconds under scratch/mcp/maps/, then force-stop.
Run 2 (warm): launch again, record the pass duration and the object-cache
load count, force-stop.
Then: the object cache directory size and file count.

Abort conditions: preflight fails, GPU above 70 C, the process dies, or the
time limit passes. The emulator is force-stopped after each run.

Usage: python tools/thor/aot_cache_cold_warm.py [--limit-s 900] [--skip-cold]
"""
import argparse
import importlib.util
import json
import os
import re
import sys
import time
from collections import Counter

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MCP_PATH = os.path.join(REPO, 'tools', 'mcp', 'xenia_thor_mcp.py')
TITLES = {
    'bd': ('/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/'
           'Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'),
    'banjo': ('/storage/2664-21DE/Roms/xbox360/Banjo-Kazooie - Nuts & Bolts (USA) '
              '(En,Ja,Fr,De,Es,It,Nl,Sv,No,Zh,Ko,Pl,Ru,Cs).iso'),
}
MAPS_DIR = os.path.join(REPO, 'scratch', 'mcp', 'maps')


def load_mcp():
    spec = importlib.util.spec_from_file_location('xenia_thor_mcp', MCP_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def classify_maps(text):
    """Count VMAs and bytes by (perms, backing) and by size bucket."""
    by_kind = Counter()
    bytes_by_kind = Counter()
    sizes = Counter()
    total = 0
    for line in text.splitlines():
        m = re.match(r'([0-9a-f]+)-([0-9a-f]+) (\S{4}) \S+ \S+ \S+\s*(.*)$', line)
        if not m:
            continue
        size = int(m.group(2), 16) - int(m.group(1), 16)
        perms = m.group(3)
        path = m.group(4).strip()
        backing = 'anon' if not path else ('file' if path.startswith('/') else path.split()[0])
        kind = f'{perms} {backing}'
        by_kind[kind] += 1
        bytes_by_kind[kind] += size
        bucket = '4K' if size <= 4096 else '8K' if size <= 8192 else '<=64K' if size <= 65536 else '<=1M' if size <= 1 << 20 else '>1M'
        sizes[bucket] += 1
        total += 1
    return total, by_kind, bytes_by_kind, sizes


def run_once(m, label, limit_s, maps_every, title):
    pre = json.loads(m.xenia_preflight())
    print(f'[{label}] preflight:', json.dumps(pre))
    if not pre.get('ok'):
        return None
    m.xenia_logcat_clear()
    launch = json.loads(m.xenia_launch(title))
    print(f'[{label}] launch:', launch.get('launched'), 'pid', launch.get('pid'),
          'battery', launch.get('battery'))
    if not launch.get('launched'):
        return None
    pid = launch['pid']
    t0 = time.time()
    next_maps = t0 + maps_every if maps_every else None
    seen = set()
    result = {'label': label, 'pid': pid, 'result': 'time limit', 'maps': []}
    while time.time() - t0 < limit_s:
        time.sleep(5)
        if m._pid(m.PKG) != pid:
            result['result'] = 'process died'
            break
        temp = m._shell('cat /sys/class/kgsl/kgsl-3d0/temp').strip()
        if temp.isdigit() and int(temp) > 70000:
            result['result'] = f'ABORT: GPU {int(temp) / 1000:.1f} C'
            break
        log = m._shell(f'logcat -d --pid={pid} -s xenia xenia-aot')
        for line in log.splitlines():
            key = line[19:]
            if key in seen:
                continue
            if any(k in line for k in ('object cache', 'LLVMobjcache', 'pre-warm', 'precompile pass',
                                       'LLVMobjload', 'objcache', 'AOT precompile progress')):
                seen.add(key)
                if 'AOT precompile progress' not in line or line.endswith('00 functions'):
                    print(f'[{label}] +{time.time() - t0:4.0f}s {line.strip()[:170]}')
        now = time.time()
        if next_maps and now >= next_maps:
            next_maps = now + maps_every
            maps = m._run_as(f'cat /proc/{pid}/maps', timeout=60)
            os.makedirs(MAPS_DIR, exist_ok=True)
            path = os.path.join(MAPS_DIR, f'{label}-{pid}-{int(now - t0)}s.maps')
            with open(path, 'w', encoding='utf-8') as f:
                f.write(maps)
            total, by_kind, bytes_by_kind, sizes = classify_maps(maps)
            rss = m._shell(f'grep VmRSS /proc/{pid}/status').split()
            result['maps'].append((int(now - t0), total, rss[1] if len(rss) > 1 else '?'))
            top = ', '.join(f'{k}: {v} ({bytes_by_kind[k] >> 20} MB)' for k, v in by_kind.most_common(5))
            print(f'[{label}] +{int(now - t0)}s maps: {total} VMAs, RSS {" ".join(rss[1:])}; {top}; '
                  f'sizes {dict(sizes)}')
        if any('pre-warmed' in k for k in seen) and now - t0 > 15:
            result['result'] = 'precompile done'
            break
    print(f'[{label}] result:', result['result'], f'after {time.time() - t0:.0f}s')
    if result['result'] == 'process died':
        print(m._shell('logcat -b crash -d -t 40')[-2500:])
    print(f'[{label}] stop:', m.xenia_force_stop())
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--limit-s', type=int, default=900)
    ap.add_argument('--maps-every', type=int, default=60)
    ap.add_argument('--skip-cold', action='store_true')
    ap.add_argument('--skip-warm', action='store_true')
    ap.add_argument('--title', default='bd', help='bd, banjo, or a device path')
    args = ap.parse_args()
    m = load_mcp()
    title = TITLES.get(args.title, args.title)

    print('objcache before:', m._run_as('sh -c "du -sk files/objcache; find files/objcache -type f | wc -l"'))
    if not args.skip_cold:
        cold = run_once(m, 'cold', args.limit_s, args.maps_every, title)
        if not cold:
            return 2
        print('objcache after cold:', m._run_as('sh -c "du -sk files/objcache; find files/objcache -type f | wc -l; ls files/objcache"'))
        time.sleep(5)
    if args.skip_warm:
        return 0
    warm = run_once(m, 'warm', args.limit_s, 10, title)
    if not warm:
        return 2
    print('objcache after warm:', m._run_as('sh -c "du -sk files/objcache; find files/objcache -type f | wc -l"'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
