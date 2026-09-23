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

The live captures are for "which screen / is it frozen", not for pixels: the
parked window's capture shows a ~16 px strip of stale content at the left
edge (MagnaCarta 2, 2026-09-22: luma 52-62 in the strip against 8-10 beside
it, while the trace replay of the same frame is continuous). Check pixels
with --trace-at and tools/pc/trace_ab.py.
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
HAZARDS = re.compile(r'hazards=(\d+)/(\d+)/(\d+)/(\d+)')
PIPES = re.compile(r'VulkanPipelineCache: (\d+) pipelines created, (\d+) ms in creation \(last 64: (\d+) ms')
# XE_ANDROID_DEFAULT settings that cannot apply to the Windows build: the ARM64
# and LLVM CPU backends (Windows runs x64), the Android thermal API, and the
# unified-memory direct-write buffer (a desktop GPU has no 512 MB host-visible
# device-local heap: the app exits with "Failed to allocate 512 MB", 2026-09-23).
# Every other Android default (GPU, kernel, caches) applies with
# --android-defaults.
ANDROID_ONLY = {
    'arm64_jit_inline_extern_thunk', 'cpu_aot_maximize', 'cpu_backend_llvm',
    'cpu_backend_llvm_context_residency', 'cpu_backend_llvm_residency_writeback',
    'cpu_drop_redundant_atomic_release_barrier', 'cpu_llvm_object_cache',
    'cpu_llvm_object_cache_skip_lowering', 'gpu_adpf_thermal_throttle',
    'gpu_uma_direct_shared_memory',
}


def android_default_cvars():
    """name=value for every XE_ANDROID_DEFAULT cvar the PC can run, at its
    Android value (the settings the device changes by build, not by toggle)."""
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
    import xenia_thor_mcp  # noqa: E402
    return ['%s=%s' % (name, android) for name, (android, _desktop)
            in sorted(xenia_thor_mcp._android_defaults().items()) if name not in ANDROID_ONLY]
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
    ap.add_argument('--android-defaults', action='store_true',
                    help='run with the Android build defaults (GPU, kernel, caches) - the '
                         'device configuration for PC visual and CPU checks')
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
    if args.android_defaults:
        cvars += android_default_cvars()
    if args.from_snapshot:
        cvars += trace_ab.arms_from_snapshot(args.from_snapshot)[1][1]  # the "device" arm
    cvars += [c for c in args.cvars.split() if c]
    exe = ORACLE if args.oracle else args.exe
    # --gpu=vulkan: on Windows Xenia picks D3D12 by default, and the device
    # runs Vulkan - without this the device's GPU settings never apply
    # (2026-09-22: the first Gears and MC2 parity runs were D3D12).
    cmd = [exe, '--storage_root=' + storage, '--log_file=' + log, '--mount_cache=true',
           '--gpu=vulkan', '--hid=nop', '--hid_nop_connected=true', '--hid_nop_trigger_file=' + trigger]
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
    pipes = None
    hazard_frames = []
    try:
        for line in open(log, encoding='utf-8', errors='replace'):
            # The module import tables list export names ("   F 820006F8 ...
            # XamShowDirtyDiscErrorUI"); a listed name is not a call.
            if IMPORT_ROW.match(line):
                continue
            for k in MARKERS:
                if k in line:
                    counts[k] = counts.get(k, 0) + 1
            m_ = PIPES.search(line)
            if m_:
                pipes = tuple(int(g) for g in m_.groups())
            m_ = HAZARDS.search(line)
            if m_:
                hazard_frames.append(tuple(int(g) for g in m_.groups()))
    except OSError:
        pass
    print('log markers:', ', '.join('%s=%d' % kv for kv in sorted(counts.items())) or 'none')
    cold = False
    if pipes:
        count, total_ms, last64_ms = pipes
        cold = last64_ms / 64.0 >= 50.0
        # 2026-09-23: a new setting that changes the SPIR-V (here the rounding
        # lever) compiles every pipeline cold in the NVIDIA driver cache, about
        # 200 ms each - Banjo's jigsaw sat still for a minute and read as a
        # freeze. The second run of the same settings took 5 ms per pipeline.
        print('pipelines: %d created, %d ms (last 64: %.0f ms each)%s' % (
            count, total_ms, last64_ms / 64.0,
            '  COLD DRIVER CACHE - a still screen may be compiles; run again before calling it a freeze'
            if cold else ''))
    if hazard_frames:
        # gpu_uma_hazard_check (2026-09-23): CPU writes to pages a GPU
        # submission still reads. With zero-copy the GPU reads them late; the
        # copy path took a snapshot. MagnaCarta 2 tore a cutscene frame this
        # way. Zero or near zero over the run = zero-copy is safe for this title.
        bad = [f for f in hazard_frames if any(f)]
        med = [sorted(f[i] for f in hazard_frames)[len(hazard_frames) // 2] for i in range(4)]
        print('zero-copy hazards: %d of %d frames (buffer definite/possible, other definite/possible '
              'median %d/%d/%d/%d) -> %s' % (
                  len(bad), len(hazard_frames), med[0], med[1], med[2], med[3],
                  'SAFE for this title' if len(bad) * 100 <= len(hazard_frames) else
                  'UNSAFE: the GPU sees data the game changed after the draw was recorded'))
    verdict = ('EXITED %s' % exited) if exited is not None else (
        ('FROZEN (cold compiles)' if cold else 'FROZEN') if frozen else
        ('NO FRAME' if not first_frame else 'RUNNING'))
    print('verdict:', verdict, '| log', log)
    return 0 if verdict == 'RUNNING' else 1


if __name__ == '__main__':
    sys.exit(main())
