"""Vulkan against D3D12 on the same GPU trace: what does the Thor's path get wrong?

  python tools/pc/backend_ab.py TRACE.xtr [TRACE.xtr ...] [--cvars "a=1 b=2"]

The Thor runs the Vulkan backend; the PC also has the D3D12 backend, which
implements features the SPIR-V path lacks (alpha to mask until 2026-09-23,
tessellation). Each trace replays in both trace-dump tools; the images are
compared (whole frame, top and bottom halves, share of pixels off by more than
8) and a side-by-side PNG (D3D12 above, Vulkan below) is written to
scratch/backend_ab/. A large difference is a Vulkan-only glitch candidate -
look at the pair. "Failed in backend" counts the draws each backend dropped.

2026-09-23: Banjo's grass drew as opaque cards on Vulkan; D3D12 showed the
blades. This comparison named it (alpha to mask) in one replay.
"""
import argparse
import glob
import os
import subprocess
import sys

from PIL import Image, ImageChops, ImageStat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BIN = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release')
OUT = os.path.join(ROOT, 'scratch', 'backend_ab')


def replay(gpu, trace, out, cvars):
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(BIN, 'xenia-gpu-%s-trace-dump.exe' % gpu)
    log = os.path.join(out, 'dump.log')
    subprocess.run([exe, '--target_trace_file=' + trace, '--trace_dump_path=' + out,
                    '--log_file=' + log] + ['--' + c for c in cvars],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    pngs = glob.glob(os.path.join(out, '*.png'))
    failed = 0
    if os.path.exists(log):
        failed = open(log, encoding='utf-8', errors='replace').read().count('Failed in backend')
    return (pngs[0] if pngs else None), failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--cvars', default='')
    args = ap.parse_args()
    cvars = [c for c in args.cvars.split() if c]
    missing = [g for g in ('vulkan', 'd3d12')
               if not os.path.exists(os.path.join(BIN, 'xenia-gpu-%s-trace-dump.exe' % g))]
    if missing:
        print('build the trace dump first: tools/pc/build_pc.py --project xenia-gpu-%s-trace-dump' % missing[0])
        return 1
    print('%-18s %6s %6s %6s %8s  %s' % ('trace', 'diff', 'd_top', 'd_bot', 'off>8', 'failed d3d12/vulkan'))
    for trace in args.traces:
        name = os.path.splitext(os.path.basename(trace))[0]
        d3d, fd = replay('d3d12', trace, os.path.join(OUT, name, 'd3d12'), cvars)
        vk, fv = replay('vulkan', trace, os.path.join(OUT, name, 'vulkan'), cvars)
        if not d3d or not vk:
            print('%-18s no image (d3d12 %s, vulkan %s)' % (name, bool(d3d), bool(vk)))
            continue
        a = Image.open(d3d).convert('RGB')
        b = Image.open(vk).convert('RGB')
        if a.size != b.size:
            print('%-18s size differs %s %s' % (name, a.size, b.size))
            continue
        w, h = a.size
        d = ImageChops.difference(a, b).convert('L')
        whole = ImageStat.Stat(d).mean[0]
        top = ImageStat.Stat(d.crop((0, 0, w, h // 2))).mean[0]
        bot = ImageStat.Stat(d.crop((0, h // 2, w, h))).mean[0]
        hist = d.histogram()
        off = 100.0 * sum(hist[9:]) / (w * h)
        pair = Image.new('RGB', (w, h * 2))
        pair.paste(a, (0, 0))
        pair.paste(b, (0, h))
        pair.resize((w // 2, h)).save(os.path.join(OUT, name + '_pair.png'))
        print('%-18s %6.1f %6.1f %6.1f %7.2f%%  %d/%d' % (name, whole, top, bot, off, fd, fv))
    print('pairs in', os.path.relpath(OUT, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
