"""Disassemble guest code ranges through the in-app debug server.

  python tools/thor/guest_disasm.py <title> <hexaddr>[:count] [<hexaddr>[:count] ...]

Uses the running title when the in-app server answers; otherwise launches
the title, waits for the server, disassembles, and force-stops it. Prints
the PowerPC disassembly of every range (count defaults to 64
instructions) and saves it to scratch/mcp/disasm-<title>-<stamp>.txt.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402


def main():
    title = sys.argv[1]
    ranges = []
    for arg in sys.argv[2:]:
        addr, _, count = arg.partition(':')
        ranges.append((int(addr, 16), int(count) if count else 64))
    launched = False
    if not m._api_up():
        r = json.loads(m.xenia_launch(title))
        if not r['launched']:
            print('launch failed', r)
            return 2
        launched = True
        t0 = time.time()
        while time.time() - t0 < 60 and not m._api_up():
            time.sleep(0.5)
        # The module is mapped when the kernel reports a title.
        while time.time() - t0 < 60:
            try:
                st = m._api('/status', timeout=3)
                if st.get('title_id') or st.get('title'):
                    break
            except RuntimeError:
                pass
            time.sleep(0.5)
    out = []
    try:
        for addr, count in ranges:
            out.append('== %08X (%d)' % (addr, count))
            lines = m._api('/disasm?addr=%08X&count=%d' % (addr, count), timeout=20)
            out.extend('  ' + line for line in lines)
            out.append('')
    finally:
        if launched:
            m.xenia_force_stop()
    text = '\n'.join(out)
    print(text)
    path = os.path.join(m.SCRATCH, 'disasm-%s-%s.txt' % (title, time.strftime('%Y%m%d-%H%M%S')))
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    print('saved', path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
