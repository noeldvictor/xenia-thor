"""One launch, the two open Banjo questions answered together:

  python tools/thor/title_probe.py [seconds] [--callgraph] [--cvar=name=value ...]

1. Where the guest CPU goes in the scene after the puzzle: a simpleperf
   sample (xenia_profile) with the hottest guest functions named from the
   recomp's table, since the hottest host threads are the game's own
   worker XThreads (2026-09-22: 62% + 59% + 29% of a core at 6.7 fps while
   the GPU takes 1-13 ms and IssueDraw 14-23 ms per frame).
2. Who allocates on the null heap: the export trap on RtlEnterCriticalSection
   with r3 == 0 is armed before the route, and the last hit's registers and
   guest chain are printed with the recomp's names (the "Null critical
   section" lines appear on the main thread even in runs that do not stall;
   the stall's garbage object pointer is the downstream symptom).

The device cools first. The app is force-stopped at the end.
"""
import json
import subprocess
import os
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, ROOT + '/tools/mcp')
sys.path.insert(0, ROOT + '/tools/thor')
import xenia_thor_mcp as m  # noqa: E402
import guest_disasm_offline as g  # noqa: E402

ROUTE = 'name:puzzle;until:gold>0.35;press:START;settle:3000|name:after;until:gold<0.2;timeout:60'


def wait_cool(max_case_c=41.0):
    for _ in range(50):
        t = json.loads(m.xenia_preflight()).get('temps', {})
        if t.get('case_c', 0) < max_case_c:
            return t
        time.sleep(30)
    return t


def name(addr, names, starts):
    try:
        return g.name_of(int(addr, 16) if isinstance(addr, str) else addr, names, starts)
    except Exception:
        return '?'


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    callgraph = '--callgraph' in sys.argv
    # --cvar name=value: a launch cvar for this run (a STARTUP lever A/B).
    extra_cvars = [a[len('--cvar='):] for a in sys.argv[1:] if a.startswith('--cvar=')]
    seconds = int(args[0]) if args else 15
    names, starts = g.load_names()
    m.xenia_force_stop()
    t = wait_cool()
    m.xenia_launch_cvars(clear=True)
    # The JITSYM host->guest map lines for the LLVM-compiled functions.
    m.xenia_launch_cvars(set='cpu_emit_jit_perf_map=true')
    for c in extra_cvars:
        m.xenia_launch_cvars(set=c)
        print('launch cvar:', c, flush=True)
    r = json.loads(m.xenia_launch('banjo', skip_preflight=True))
    if not r.get('launched'):
        print('launch failed', r)
        return 1
    t0 = time.time()
    while time.time() - t0 < 60 and not m._api_up():
        time.sleep(1)
    # r5 is the block RtlFreeHeap was given (r3 = the null heap): 32 bytes
    # before it (the heap entry and the me header) and 160 after.
    armed = m._api('/trap?name=RtlEnterCriticalSection&r3=0&pause=0&dump=r5-32:192', 'POST')
    print('trap armed:', json.dumps(armed)[:200], flush=True)
    goto = json.loads(m.xenia_goto(steps=ROUTE, launch=False, screenshot=False)).get('goto') or {}
    print('route: reached=%s seconds=%s start_case=%.1fC' % (
        goto.get('reached'), goto.get('seconds'), t.get('case_c', 0)), flush=True)
    if goto.get('reached'):
        time.sleep(5)
        prof = json.loads(m.xenia_profile(seconds=seconds, callgraph=callgraph))
        print('== profile: presented', prof.get('presented_fps'))
        if callgraph and prof.get('perf_data'):
            # Who takes the mutex and who enters the kernel: the callee
            # graphs, written next to the data for reading with head/grep.
            for sym in ('pthread_mutex_lock', 'syscall', '__aarch64_cas2_acq', 'xe_llvm_resolve_cached'):
                out = prof['perf_data'] + '.callee-' + sym + '.txt'
                subprocess.call([m.HOST_SIMPLEPERF, 'report', '-i', prof['perf_data'], '-g', 'callee',
                                 '--symbols', sym, '-n'], stdout=open(out, 'w'), stderr=subprocess.DEVNULL)
                print('  callee graph:', out)
            out = prof['perf_data'] + '.kernel-callers.txt'
            subprocess.call([m.HOST_SIMPLEPERF, 'report', '-i', prof['perf_data'], '-g', 'callee',
                             '--dsos', '[kernel.kallsyms]', '-n'], stdout=open(out, 'w'), stderr=subprocess.DEVNULL)
            print('  kernel callers:', out)
        for row in prof.get('by_dso') or []:
            print('  dso', row.strip()[:120])
        for row in (prof.get('top') or [])[:18]:
            print('  top', row.strip()[:140])
        for row in (prof.get('guest_hot') or [])[:20]:
            if 'guest' in row:
                row = dict(row)
                row['name'] = name(row['guest'], names, starts) if row['guest'] != 'a64 or unmapped' else ''
            print('  guest', json.dumps(row)[:160])
        if prof.get('symbolize'):
            print('  symbolize:', prof['symbolize'])
    trap = m._api('/trap')
    print('== trap: hits=%s export=%s tid=%s lr=%s (%s) ctr=%s' % (
        trap.get('hits'), trap.get('export'), trap.get('tid'), trap.get('lr'),
        name(trap.get('lr') or '0', names, starts), trap.get('ctr')))
    regs = trap.get('r') or []
    if regs:
        print('  r1=%s r3=%s r4=%s r5=%s r6=%s r7=%s' % tuple(x[-8:] for x in (regs[1], regs[3], regs[4], regs[5], regs[6], regs[7])))
        print('  r28=%s r29=%s r30=%s r31=%s' % tuple(x[-8:] for x in regs[28:32]))
    for a in trap.get('chain') or []:
        print('  chain %s %s' % (a, name(a, names, starts)))
    for k, v in (trap.get('mem') or {}).items():
        print('  mem %s: %s' % (k, json.dumps(v)[:200]))
    # The dump ranges of the arm call go to the app log as hex lines.
    try:
        rows = m._api('/log?lines=6000&grep=trap')
        rows = rows.get('lines', rows) if isinstance(rows, dict) else rows
        for l in [str(x) for x in rows if x][-6:]:
            print('  log', l[:700])
    except Exception as e:
        print('  log unavailable:', e)
    m.xenia_force_stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
