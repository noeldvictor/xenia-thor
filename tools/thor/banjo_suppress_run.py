"""Banjo with the dirty-disc check suppressed live through the device MCP.

Launch, set xam_suppress_dirty_disc_error as soon as the in-app server
answers, press START at +40 s and A at +70 s, screenshot every 30 s, and save
the log ring before the force-stop. Prints the badge fps per interval.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    r = json.loads(m.xenia_launch('banjo'))
    print('launch', r['launched'], 'pid', r['pid'], 'battery', r['battery']['level'])
    if not r['launched']:
        return 2
    t0 = time.time()
    for i in range(60):
        try:
            print('suppress:', m._api('/cvar?name=xam_suppress_dirty_disc_error&value=true', 'POST', timeout=2),
                  'at +%.0f s' % (time.time() - t0))
            break
        except RuntimeError:
            time.sleep(0.5)
    route = {40: 'START', 70: 'A'}
    next_shot = 30
    try:
        while time.time() - t0 < seconds:
            time.sleep(1)
            now = time.time() - t0
            if m._pid(m.PKG) is None:
                print('died at +%.0f s' % now)
                break
            for at, button in list(route.items()):
                if now >= at:
                    print('+%.0f s press %s: %s' % (now, button, m._api(f'/press?button={button}&hold_ms=150', 'POST')))
                    del route[at]
            if now >= next_shot:
                next_shot += 30
                fps = m._api('/fps', timeout=20)
                last = [f['fps'] for f in fps[-20:]]
                med = sorted(last)[len(last) // 2] if last else None
                shot = json.loads(m.xenia_screenshot('suppress-%ds' % int(now)))
                gpu = m._api('/gpu', timeout=20)
                temp = m._temps().get('gpu_c')
                print('+%.0f s fps median %s (last %s) pipelines %s ms %s GPU %s C %s' % (
                    now, med, last[-5:], gpu.get('pipelines_created'), gpu.get('pipeline_create_ms'),
                    temp, os.path.basename(shot['path'])))
                if temp and temp > 70:
                    print('ABORT: GPU %s C' % temp)
                    break
    finally:
        try:
            rows = m._api('/log?lines=8192', timeout=60)
            path = os.path.join(m.SCRATCH, 'ring-banjo-suppress-%s.txt' % time.strftime('%Y%m%d-%H%M%S'))
            with open(path, 'w', encoding='utf-8') as f:
                f.write(chr(10).join(rows))
            marks = [l[:160] for l in rows if 'DirtyDisc' in l or 'suppressed' in l or 'CRASH DIAG' in l]
            print('ring saved', path, 'markers', len(marks))
            for l in marks[:4]:
                print('   ', l)
        except Exception as e:
            print('ring not saved:', e)
        print('stop', m.xenia_force_stop())
    return 0


if __name__ == '__main__':
    sys.exit(main())
