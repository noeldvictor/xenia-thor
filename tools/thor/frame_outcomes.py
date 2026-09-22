"""Summarize the per-frame GPU draw-outcome lines of a run: where the frame
time goes, in one table.

  python tools/thor/frame_outcomes.py [logfile] [--last N]

Reads the "GPU draw outcomes/frame:" lines (cvar
vulkan_trace_draw_outcomes_per_frame=true) from a log file, or from the
device's logcat when no file is given, and prints the median and the 90th
percentile of every numeric field over the last N frames (default 60):
rendered draws, pipeline and descriptor binds, render-target transfers and
pass breaks, the command-processor CPU split (cpu_process_us and its parts),
the frame-open wait, and gpu_frame_us. A frame whose gpu_frame_us is near
the frame period is GPU-bound; one whose cpu_process_us is, is CPU-bound on
the command processor; a large fopen wait is the frame pipeline stalling
(2026-09-21).
"""
import re
import subprocess
import sys


def percentile(values, p):
    if not values:
        return None
    values = sorted(values)
    k = int(round((len(values) - 1) * p))
    return values[k]


def main():
    args = sys.argv[1:]
    last = 60
    if '--last' in args:
        i = args.index('--last')
        last = int(args[i + 1])
        del args[i:i + 2]
    if args:
        text = open(args[0], encoding='utf-8', errors='replace').read()
    else:
        text = subprocess.run(['adb', 'logcat', '-d', '-s', 'xenia'], capture_output=True, text=True,
                              errors='replace').stdout
    lines = [l for l in text.splitlines() if 'GPU draw outcomes/frame:' in l][-last:]
    if not lines:
        print('no "GPU draw outcomes/frame" lines (cvar vulkan_trace_draw_outcomes_per_frame=true)')
        return 1
    fields = {}
    order = []
    for line in lines:
        body = line.split('GPU draw outcomes/frame:', 1)[1]
        for m in re.finditer(r'([a-z_0-9\[\]+]+)=(-?\d+(?:\.\d+)?)', body):
            key = m.group(1)
            if key not in fields:
                fields[key] = []
                order.append(key)
            fields[key].append(float(m.group(2)))
    print('%d frames' % len(lines))
    print('%-28s %12s %12s' % ('field', 'median', 'p90'))
    for key in order:
        v = fields[key]
        med = percentile(v, 0.5)
        p90 = percentile(v, 0.9)
        if med == 0 and p90 == 0:
            continue
        print('%-28s %12g %12g' % (key, med, p90))
    return 0


if __name__ == '__main__':
    sys.exit(main())
