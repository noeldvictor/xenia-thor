"""One call: does Banjo reach the dirty-disc dialog on the device, and when?

  python tools/thor/banjo_dialog_check.py [seconds=75] [label]

Launches Banjo through the device MCP, polls the in-process log ring for
XamShowDirtyDiscErrorUI, and prints the dialog time or "no dialog". It also
prints the kernel lines that name a cause (a sector-id collision, a failed
file open, the content lookup status) and saves the log ring to scratch
before the force-stop. Exit 0 = no dialog, 1 = dialog, 2 = launch failed or
the process died.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402

DIALOG = 'XamShowDirtyDiscErrorUI'
CAUSES = ['collision', 'not found', 'C0000034', 'C0000001', 'kernel trap']


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 75
    label = sys.argv[2] if len(sys.argv) > 2 else 'run'
    r = json.loads(m.xenia_launch('banjo'))
    print('launch', r['launched'], 'pid', r['pid'], 'battery', r['battery']['level'])
    if not r['launched']:
        return 2
    t0 = time.time()
    result = 2
    try:
        while time.time() - t0 < seconds:
            time.sleep(2)
            now = time.time() - t0
            if m._pid(m.PKG) is None:
                print('died at +%.0f s' % now)
                break
            try:
                rows = m._api('/log?lines=8192&grep=' + DIALOG, timeout=5)
            except RuntimeError:
                continue
            if rows:
                print('DIALOG at +%.0f s: %s' % (now, rows[0][:160]))
                result = 1
                break
        else:
            print('no dialog in %d s' % seconds)
            result = 0
    finally:
        try:
            rows = m._api('/log?lines=8192', timeout=60)
            path = os.path.join(m.SCRATCH, 'ring-banjo-%s-%s.txt' % (label, time.strftime('%Y%m%d-%H%M%S')))
            with open(path, 'w', encoding='utf-8') as f:
                f.write('\n'.join(rows))
            print('log ring:', path, len(rows), 'lines')
            for cause in CAUSES:
                hits = [row for row in rows if cause in row]
                if hits:
                    print('%s: %d lines; first: %s' % (cause, len(hits), hits[0][:200]))
        except Exception as e:  # noqa: BLE001
            print('log ring failed:', e)
        m.xenia_force_stop()
    return result


if __name__ == '__main__':
    sys.exit(main())
