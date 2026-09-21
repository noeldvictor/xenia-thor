"""One call: the guest call chain and the disassembly at a kernel export hit.

  python tools/thor/guest_trap_context.py [title=banjo] [export=XamShowDirtyDiscErrorUI] [seconds=90]

Launches the title through the device MCP, arms the export trap with pause
as soon as the in-app server answers, waits for the hit, and then, with the
guest thread held: prints the registers, the string at r4 when r4 points at
text, walks the PowerPC back chain (frame at [r1], return address at
[frame - 8]... the ABI stores lr at 8 bytes below the caller's frame), and
disassembles 16 instructions before each return address. Saves the whole
context to scratch/mcp/trap-<export>-<stamp>.txt, releases the thread, and
force-stops the title. This is the device-side answer to "which guest code
decided to call this export", the question a log line cannot answer.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402


def mem(addr, length):
    r = m._api('/memory?addr=%08X&len=%d' % (addr, length), timeout=10)
    return bytes.fromhex(r.get('hex', ''))


def word(addr):
    b = mem(addr, 4)
    return int.from_bytes(b, 'big') if len(b) == 4 else None


def disasm(addr, count):
    return m._api('/disasm?addr=%08X&count=%d' % (addr, count), timeout=10)


def text_at(addr, limit=160):
    b = mem(addr, limit)
    if not b:
        return ''
    out = b.split(b'\0', 1)[0]
    if out and all(32 <= c < 127 for c in out):
        return out.decode('ascii')
    # UTF-16BE text?
    if len(b) >= 4 and b[0] == 0 and 32 <= b[1] < 127:
        chars = []
        for i in range(0, len(b) - 1, 2):
            c = (b[i] << 8) | b[i + 1]
            if c == 0:
                break
            chars.append(chr(c) if 32 <= c < 127 else '?')
        return ''.join(chars)
    return ''


def main():
    title = sys.argv[1] if len(sys.argv) > 1 else 'banjo'
    export = sys.argv[2] if len(sys.argv) > 2 else 'XamShowDirtyDiscErrorUI'
    seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 90
    out = []

    def say(s=''):
        print(s)
        out.append(s)

    r = json.loads(m.xenia_launch(title))
    say('launch %s pid %s battery %s' % (r['launched'], r['pid'], r['battery']['level']))
    if not r['launched']:
        return 2
    t0 = time.time()
    armed = None
    while time.time() - t0 < 60:
        try:
            armed = m._api('/trap?name=%s&pause=1' % export, 'POST', timeout=2)
            break
        except RuntimeError:
            time.sleep(0.5)
    say('armed at +%.0f s: %s' % (time.time() - t0, armed))
    report = None
    try:
        while time.time() - t0 < seconds:
            time.sleep(1)
            if m._pid(m.PKG) is None:
                say('died at +%.0f s' % (time.time() - t0))
                break
            rep = m._api('/trap', timeout=5)
            if rep.get('paused'):
                report = rep
                say('HIT at +%.0f s' % (time.time() - t0))
                break
        if not report:
            say('no hit in %d s' % seconds)
            return 1
        regs = [int(x, 16) for x in report['r']]
        say('thread %s lr=%s ctr=%s' % (report.get('tid'), report.get('lr'), report.get('ctr')))
        say('r3..r10: ' + ' '.join('%08X' % v for v in regs[3:11]))
        say('r1=%08X r13=%08X r30=%08X r31=%08X' % (regs[1], regs[13], regs[30], regs[31]))
        for n in (3, 4, 5, 6):
            t = text_at(regs[n]) if 0x1000 <= regs[n] < 0xC0000000 else ''
            if t:
                say('r%d text: %s' % (n, t))
        # Memory at the non-volatile registers that hold heap pointers, and
        # one dereference: a request object and the block it points at.
        say()
        for n in range(24, 32):
            v = regs[n]
            if not (0x10000 <= v < 0x8C000000):
                continue
            b = mem(v, 64)
            if len(b) < 64:
                continue
            words = [int.from_bytes(b[i:i + 4], 'big') for i in range(0, 64, 4)]
            say('r%d [%08X]: %s' % (n, v, ' '.join('%08X' % w for w in words)))
            for k, w in enumerate(words[:8]):
                if 0x10000 <= w < 0x8C000000 and w < 0x82000000:
                    b2 = mem(w, 64)
                    if len(b2) == 64:
                        w2 = [int.from_bytes(b2[i:i + 4], 'big') for i in range(0, 64, 4)]
                        say('   +%02X -> [%08X]: %s' % (k * 4, w, ' '.join('%08X' % x for x in w2)))
        # The back chain.
        say()
        say('call chain (return address <- frame):')
        chain = []
        sp = regs[1]
        ret = int(report['lr'], 16)
        chain.append((ret, sp))
        for _ in range(12):
            prev = word(sp)
            if prev is None or prev <= sp or prev - sp > 0x100000:
                break
            ret = word(prev - 8)
            if ret is None or not (0x82000000 <= ret < 0x8C000000):
                break
            chain.append((ret, prev))
            sp = prev
        for ret, frame in chain:
            say('  %08X <- frame %08X' % (ret, frame))
        for ret, frame in chain[:8]:
            say()
            say('== around %08X' % ret)
            for line in disasm(ret - 64, 20):
                mark = ' <==' if line.startswith('%08X:' % (ret - 4)) else ''
                say('  ' + line + mark)
        # The other guest threads: where each one waits, and the text on its
        # stack. The thread that queued the failing request usually blocks
        # on it with the request name still in its frames.
        say()
        say('other guest threads at the hit:')
        try:
            threads = m._api('/threads', timeout=20)
        except RuntimeError as e:
            threads = []
            say('  threads failed: %s' % e)
        if isinstance(threads, dict):
            say('  threads: %s' % json.dumps(threads)[:300])
            threads = []
        if isinstance(threads, str):
            try:
                threads = json.loads(threads)
            except ValueError:
                say('  threads: unparsed %r' % threads[:200])
                threads = []
        for t in threads:
            if not isinstance(t, dict):
                say('  row: %r' % (t,))
                continue
            if not t.get('guest') or t.get('tid') == report.get('tid'):
                continue
            sp = int(t['r1'], 16)
            if not (0x10000 <= sp < 0x8C000000):
                continue
            say('  thread %s %s lr=%s r1=%08X state=%s wait=%s' % (
                t['tid'], t.get('name', ''), t['lr'], sp, t.get('state'), t.get('wait_reason')))
            tchain = []
            fr = sp
            for _ in range(10):
                prev = word(fr)
                if prev is None or prev <= fr or prev - fr > 0x100000:
                    break
                ret = word(prev - 8)
                if ret is None or not (0x82000000 <= ret < 0x8C000000):
                    break
                tchain.append(ret)
                fr = prev
            say('    chain: ' + ' '.join('%08X' % r for r in tchain))
            b = mem(sp, 2048)
            texts = []
            i = 0
            while i < len(b):
                j = i
                while j < len(b) and 32 <= b[j] < 127:
                    j += 1
                if j - i >= 6:
                    texts.append('+%d:%s' % (i, b[i:j].decode('ascii')))
                i = j + 1
            if texts:
                say('    stack text: ' + ' | '.join(texts[:16]))
    finally:
        try:
            m._api('/trap_release', 'POST', timeout=5)
        except Exception:  # noqa: BLE001
            pass
        path = os.path.join(m.SCRATCH, 'trap-%s-%s.txt' % (export, time.strftime('%Y%m%d-%H%M%S')))
        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(out))
        print('saved', path)
        m.xenia_force_stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
