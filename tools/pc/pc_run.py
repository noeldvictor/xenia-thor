"""Run any title on the Windows build with the device's settings, and watch it.

  python tools/pc/pc_run.py ISO [--seconds 120] [--every 10]
                            [--from-snapshot scratch/mcp/cvars-<stamp>.json]
                            [--cvars "name=value name=value"] [--presses "40:start 55:a"]
                            [--name gears] [--oracle]

The PC parity run (docs/research/20260922-dev-speed-plan.md): a device-only
bug that is not the Adreno reproduces here in about a minute with no heat.
--from-snapshot applies the device's GPU/Vulkan settings from an xenia_cvars
file (the toggles that caused Banjo's dark lower half, 2026-09-22). --presses
drives the nop HID through its trigger file at the given seconds. --oracle
runs scratch/oracle/xenia_canary.exe instead (the reference when the fork
fails). Every --every seconds the window is captured off-screen (no focus);
each line: time, top/bottom luma, the change from the previous capture.
FROZEN = three captures in a row without change after the first frame. At
the end: the log's marker counts (waits, timeouts, unimplemented, crashes)
and the verdict. Captures stay in scratch/<name>/pcrun/.
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import pc_screens  # noqa: E402
import trace_ab  # noqa: E402
from PIL import Image, ImageChops, ImageStat  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ORACLE = os.path.join(ROOT, 'scratch', 'oracle', 'xenia_canary.exe')
MARKERS = ('timed out', 'Timeout', 'Unimplemented', 'unimplemented', 'guest crash', 'Fatal',
           'SPINLOCK STALL', 'assert', 'Wait', 'XamShowDirtyDiscErrorUI', 'Non-default cvars')

IMPORT_ROW = re.compile(r'^\s+F [0-9A-F]{8} ')


def halves(path):
    im = Image.open(path).convert('L')
    w, h = im.size
    return (ImageStat.Stat(im.crop((0, 0, w, h // 2))).mean[0],
            ImageStat.Stat(im.crop((0, h // 2, w, h))).mean[0])


def change(a, b):
    ia, ib = Image.open(a).convert('L'), Image.open(b).convert('L')
    if ia.size != ib.size:
        return 255.0
    return ImageStat.Stat(ImageChops.difference(ia, ib)).mean[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('iso')
    ap.add_argument('--seconds', type=int, default=120)
    ap.add_argument('--every', type=float, default=10.0)
    ap.add_argument('--from-snapshot', default='')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--presses', default='', help='"seconds:button ..." through the nop HID trigger file')
    ap.add_argument('--hold', type=int, default=150)
    ap.add_argument('--name', default='')
    ap.add_argument('--oracle', action='store_true')
    ap.add_argument('--exe', default=pc_screens.EXE)
    ap.add_argument('--trace-at', default='', help='"seconds ..." at which to trace the frame on screen '
                    '(trace_gpu_request_file); the .xtr files land in <storage>/traces for tools/pc/trace_ab.py')
    args = ap.parse_args()
    name = args.name or os.path.splitext(os.path.basename(args.iso))[0][:24]
    storage = os.path.join(ROOT, 'scratch', name, 'pcrun' + ('-oracle' if args.oracle else ''))
    os.makedirs(storage, exist_ok=True)
    trigger = os.path.abspath(os.path.join(storage, 'trigger.txt'))
    if os.path.exists(trigger):
        os.remove(trigger)
    log = os.path.join(storage, 'xenia.log')
    cvars = []
    if args.from_snapshot:
        cvars += trace_ab.arms_from_snapshot(args.from_snapshot)[1][1]  # the "device" arm
    cvars += [c for c in args.cvars.split() if c]
    exe = ORACLE if args.oracle else args.exe
    cmd = [exe, '--storage_root=' + storage, '--log_file=' + log, '--mount_cache=true',
           '--hid=nop', '--hid_nop_connected=true', '--hid_nop_trigger_file=' + trigger]
    trace_request = os.path.abspath(os.path.join(storage, 'trace_request.txt'))
    trace_dir = os.path.abspath(os.path.join(storage, 'traces')) + os.sep
    trace_times = sorted(float(t) for t in args.trace_at.split() if t)
    if not args.oracle:
        cmd += ['--' + c for c in cvars]
        if trace_times:
            os.makedirs(trace_dir, exist_ok=True)
            cmd += ['--trace_gpu_request_file=' + trace_request, '--trace_gpu_prefix=' + trace_dir]
    cmd.append(args.iso)
    presses = []
    for item in args.presses.split():
        t, _, button = item.partition(':')
        presses.append((float(t), button))
    print('run:', os.path.basename(exe), ' '.join('--' + c for c in cvars) if not args.oracle else '(oracle)',
          flush=True)
    proc = subprocess.Popen(cmd, cwd=ROOT)
    t0 = time.time()
    parked = set()
    prev = None
    still = 0
    first_frame = False
    frozen = False
    shot_i = 0
    next_shot = args.every
    try:
        while time.time() - t0 < args.seconds and proc.poll() is None:
            now = time.time() - t0
            while trace_times and trace_times[0] <= now:
                trace_times.pop(0)
                open(trace_request, 'w').close()
                print('+%5.1f s trace requested (%s)' % (now, trace_dir), flush=True)
            while presses and presses[0][0] <= now:
                _, button = presses.pop(0)
                with open(trigger, 'w') as f:
                    f.write('%s:%d\n' % (button, args.hold))
                print('+%5.1f s press %s' % (now, button), flush=True)
            if now >= next_shot:
                next_shot += args.every
                hwnd = pc_screens.find_window(proc.pid)
                if hwnd:
                    if hwnd not in parked:
                        pc_screens.park_offscreen(hwnd)
                        parked.add(hwnd)
                    path = os.path.join(storage, 'shot-%02d.png' % shot_i)
                    shot_i += 1
                    if pc_screens.capture(hwnd, path):
                        top, bottom = halves(path)
                        d = change(prev, path) if prev else 255.0
                        if top + bottom > 10:
                            first_frame = True
                        still = still + 1 if (first_frame and d < 0.5) else 0
                        frozen = frozen or still >= 3
                        print('+%5.1f s top %5.1f bottom %5.1f change %5.1f%s  %s' % (
                            now, top, bottom, d, '  STILL' if still else '', path), flush=True)
                        prev = path
            time.sleep(0.5)
    finally:
        exited = proc.poll()
        if exited is None:
            proc.kill()
    counts = {}
    try:
        for line in open(log, encoding='utf-8', errors='replace'):
            # The module import tables list export names ("   F 820006F8 ...
            # XamShowDirtyDiscErrorUI"); a listed name is not a call.
            if IMPORT_ROW.match(line):
                continue
            for k in MARKERS:
                if k in line:
                    counts[k] = counts.get(k, 0) + 1
    except OSError:
        pass
    print('log markers:', ', '.join('%s=%d' % kv for kv in sorted(counts.items())) or 'none')
    verdict = ('EXITED %s' % exited) if exited is not None else (
        'FROZEN' if frozen else ('NO FRAME' if not first_frame else 'RUNNING'))
    print('verdict:', verdict, '| log', log)
    return 0 if verdict == 'RUNNING' else 1


if __name__ == '__main__':
    sys.exit(main())
