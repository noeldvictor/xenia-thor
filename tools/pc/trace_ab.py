"""Which setting breaks this frame? A/B a device GPU trace on the PC, no device.

  python tools/pc/trace_ab.py TRACE.xtr ARM ...
  python tools/pc/trace_ab.py TRACE.xtr --from-snapshot scratch/mcp/cvars-<stamp>.json
  ARM = "label" or "label:name=value,name=value"

Each arm replays the trace with xenia-gpu-vulkan-trace-dump on the PC with its
cvars (about a minute) and the resulting image is compared with the first arm:
mean absolute difference over the whole image and over the top and bottom
halves, plus the mean luma of each half. --from-snapshot builds the arms from
a device settings snapshot (xenia_cvars): "pc" (PC defaults), "device" (every
GPU/Vulkan setting the device runs with), and one arm per such setting alone.
The device-only glitch then names its setting on the PC.

2026-09-22: Banjo's dark lower half came from the device's app toggles, not the
Adreno. This A/B on the device's own trace named the cause in four replays
(the dynamic primitive topology that gpu_cull_compaction turns on), where the
device runs had taken a day.
"""
import argparse
import json
import os
import subprocess
import sys

from PIL import Image, ImageChops, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release', 'xenia-gpu-vulkan-trace-dump.exe')
GPU_PREFIXES = ('gpu_', 'vulkan_', 'render_target', 'rt_', 'draw_', 'edram', 'texture_')
# Paths, device identities and platform plumbing that do not apply on the PC.
SKIP = ('gpu_vulkan_driver', 'gpu_vulkan_driver_hooks_path', 'gpu_vulkan_driver_path',
        'vulkan_pipeline_cache_path', 'gpu', 'gpu_cp_worker_nice', 'gpu_frame_limit_fps',
        'gpu_adpf_performance_hints', 'gpu_adpf_thermal_throttle')


def parse_arm(spec):
    label, _, rest = spec.partition(':')
    cvars = [c.strip() for c in rest.split(',') if c.strip()]
    return label, cvars


def arms_from_snapshot(path):
    snap = json.load(open(path, encoding='utf-8'))
    settings = []
    for r in snap.get('cvars', []):
        name, value = r.get('name', ''), r.get('value')
        if value is None or name in SKIP or not name.startswith(GPU_PREFIXES):
            continue
        settings.append('%s=%s' % (name, str(value).strip('"')))
    arms = [('pc', []), ('device', settings)]
    arms += [(s.split('=', 1)[0], [s]) for s in settings]
    return arms


def replay(trace, label, cvars, out_root):
    out = os.path.join(out_root, label)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith('.png'):
            os.remove(os.path.join(out, f))
    cmd = [EXE, '--target_trace_file=' + trace, '--trace_dump_path=' + out,
           '--log_file=' + os.path.join(out, 'dump.log')] + ['--' + c for c in cvars]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    pngs = [os.path.join(out, f) for f in os.listdir(out) if f.endswith('.png')]
    return max(pngs, key=os.path.getmtime) if pngs else None


def compare(a, b):
    ia, ib = Image.open(a).convert('L'), Image.open(b).convert('L')
    if ia.size != ib.size:
        ib = ib.resize(ia.size)
    w, h = ia.size
    diff = ImageChops.difference(ia, ib)
    top, bottom = (0, 0, w, h // 2), (0, h // 2, w, h)
    return {
        'diff': ImageStat.Stat(diff).mean[0],
        'diff_top': ImageStat.Stat(diff.crop(top)).mean[0],
        'diff_bottom': ImageStat.Stat(diff.crop(bottom)).mean[0],
        'luma_top': ImageStat.Stat(ib.crop(top)).mean[0],
        'luma_bottom': ImageStat.Stat(ib.crop(bottom)).mean[0],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('arms', nargs='*')
    ap.add_argument('--from-snapshot', default='')
    ap.add_argument('--out', default=os.path.join(ROOT, 'scratch', 'traces', 'ab'))
    args = ap.parse_args()
    if not os.path.exists(EXE):
        print('no trace-dump build: MSBuild build\\xenia-gpu-vulkan-trace-dump.vcxproj '
              '"/p:Configuration=Release Windows" /p:Platform=x64')
        return 1
    arms = arms_from_snapshot(args.from_snapshot) if args.from_snapshot else []
    arms += [parse_arm(s) for s in args.arms]
    if not arms:
        print('no arms')
        return 1
    base = None
    print('%-44s %6s %6s %6s %6s %6s' % ('arm', 'diff', 'd_top', 'd_bot', 'l_top', 'l_bot'))
    for label, cvars in arms:
        png = replay(args.trace, label, cvars, args.out)
        if not png:
            print('%-44s no image (the replay failed; see %s)' % (label, os.path.join(args.out, label)))
            continue
        base = base or png
        c = compare(base, png)
        print('%-44s %6.1f %6.1f %6.1f %6.1f %6.1f  %s' % (
            label[:44], c['diff'], c['diff_top'], c['diff_bottom'], c['luma_top'], c['luma_bottom'],
            ' '.join(cvars)[:80]), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
