"""Bisect the guest address window the LLVM backend compiles until the Banjo
puzzle-transition stall names one function.

  python tools/thor/llvm_bisect.py [--runs 2] [--lo 82000000] [--hi 83000000] [--min 0x1000]

Each step launches the title `runs` times with `cpu_backend_llvm_range_lo`
and `_range_hi` set to one half of the window (the other half runs on a64)
and counts frozen-swap stalls with stall_study.one_launch. A half that
stalls at least once keeps the defect; when neither half stalls in `runs`
launches the step is repeated once with the runs doubled before the window
is declared clean (the stall shows in about 3 of 4 launches, 2026-09-22).
Every step is appended to scratch/banjo/rd/llvm_bisect.log so a stopped
run resumes from the last window (pass --lo/--hi from the log).

Background: with the LLVM backend 3 of 4 launches froze at swap 836 to 838
(a call through a garbage function pointer parks a thread); with a64 only,
0 of 4. The residency options off still froze 2 of 3.
"""
import argparse
import os
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'thor'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
import stall_study  # noqa: E402

LOG = os.path.join(ROOT, 'scratch', 'banjo', 'rd', 'llvm_bisect.log')


def log(line):
    print(line, flush=True)
    with open(LOG, 'a', encoding='utf-8') as f:
        f.write(time.strftime('%H:%M:%S ') + line + '\n')


def stalls_in(lo, hi, runs):
    cv = 'cpu_backend_llvm_range_lo=%08X,cpu_backend_llvm_range_hi=%08X' % (lo, hi)
    n = 0
    for i in range(runs):
        v, secs = stall_study.one_launch(cv, 'bisect-%08X-%08X-%d' % (lo, hi, i))
        log('  [%08X,%08X) run %d: %s in %d s' % (lo, hi, i + 1, v, secs))
        if v == 'STALL':
            n += 1
            break  # one frozen run is enough to keep this half
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', type=int, default=2)
    ap.add_argument('--lo', default='82000000')
    ap.add_argument('--hi', default='83000000')
    ap.add_argument('--min', default='0x1000', help='stop when the window is this small')
    args = ap.parse_args()
    lo, hi, min_size = int(args.lo, 16), int(args.hi, 16), int(args.min, 0)
    log('bisect start [%08X,%08X) runs=%d' % (lo, hi, args.runs))
    while hi - lo > min_size:
        mid = (lo + (hi - lo) // 2) & ~0xF
        runs = args.runs
        while True:
            if stalls_in(lo, mid, runs):
                hi = mid
                break
            if stalls_in(mid, hi, runs):
                lo = mid
                break
            if runs >= args.runs * 4:
                log('neither half stalled in %d runs each: the defect needs both halves '
                    '(two functions) or is not in [%08X,%08X); stopping' % (runs, lo, hi))
                return 1
            runs *= 2
            log('  neither half stalled; repeating the step with %d runs' % runs)
        log('window [%08X,%08X) (%d KB)' % (lo, hi, (hi - lo) // 1024))
    log('done: [%08X,%08X)' % (lo, hi))
    return 0


if __name__ == '__main__':
    sys.exit(main())
