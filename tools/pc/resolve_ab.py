"""Which resolve first differs between Vulkan and D3D12? Compare every resolve.

  python tools/pc/resolve_ab.py TRACE.xtr [--cvars "a=1"] [--threshold 0.5]
      [--images all|first|none] [--prefix resolve_000_004]

Replays the trace on the Vulkan and the D3D12 trace dumps with
gpu_debug_dump_resolves: every resolve (EDRAM to guest memory - the render
targets that later draws sample, and the final frame) is read back and
dumped with its state. The resolves are paired by their order and compared:
exact 32-bit words that differ, and "large" differences (a color byte by more
than 8, a 24-bit depth by more than 64 steps). The first resolve with large
differences over --threshold percent is where the backends diverge - look at
the draws before it (the "draw" column is the index of the copy draw packet
that ran the resolve; name the draws with draw_bisect.py). For k_8_8_8_8 and the depth formats the untiled images
(D3D12 left, Vulkan middle, the difference right; depth as gray over its range,
the difference white where it exceeds 64 steps) land in
scratch/resolve_ab/<trace>/.

--prefix RESOLVE: then find the first draw that makes that resolve differ -
every draw after N is skipped on both backends and a binary search over N
(up to the resolve's draw) compares only that resolve. The partial render
target is real (the resolve still runs), so this names the draw, not a clear
or a background draw.

2026-09-24: the final-frame comparisons (backend_ab.py, draw_bisect.py prefix
mode) only reached the last composite of Banjo and Blue Dragon; this reaches
the render target the composite sampled (Blue Dragon: the sun shadow map).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

OUT = os.path.join(backend_ab.ROOT, 'scratch', 'resolve_ab')
LINE_RE = re.compile(r'(\w+)=(\S+)')


def dump(gpu, trace, out, cvars, tag=''):
    folder = os.path.join(out, gpu + tag)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    exe = os.path.join(backend_ab.BIN, 'xenia-gpu-%s-trace-dump.exe' % gpu)
    try:
        subprocess.run([exe, '--target_trace_file=' + trace,
                        '--trace_dump_path=' + folder + os.sep,
                        '--log_file=' + os.path.join(folder, 'dump.log'),
                        '--gpu_debug_dump_resolves=' + os.path.join(folder, 'resolves')] +
                       ['--' + c for c in backend_ab.backend_cvars(gpu, cvars)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    except subprocess.TimeoutExpired:
        print('%s replay hung (600 s) - killed' % gpu)
    index = os.path.join(folder, 'resolves', 'resolves.txt')
    rows = []
    if os.path.exists(index):
        for line in open(index, encoding='utf-8', errors='replace'):
            parts = line.split()
            if not parts:
                continue
            row = dict(LINE_RE.findall(line))
            row['name'] = parts[0]
            row['path'] = os.path.join(folder, 'resolves', parts[0] + '.bin')
            rows.append(row)
    return rows


def tiled2d(x, y, pitch_aligned, bpb_log2):
    """texture_address::Tiled2D (Xenos 2D tiling, 32x32 macro tiles)."""
    outer = ((y >> 5) * (pitch_aligned >> 5) + (x >> 5)) << 6
    inner = (((y >> 1) & 7) << 3) | (x & 7)
    oi = (outer | inner) << bpb_log2
    bank = (y >> 4) & 1
    pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1)
    return (((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oi & 0xF) |
            (((oi >> 4) & 1) << 5) | (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12))


def untile32(data, row):
    """The resolved rectangle as H x W x 4 bytes, from the tiled 32bpp data."""
    width = int(row['width8']) * 8
    height = int(row['height8']) * 8
    pitch = int(row['pitch32']) * 32
    base = int(row['base'], 16) - int(row['address'], 16)
    ox = int(row['offx8']) * 8
    oy = int(row['offy8']) * 8
    ys, xs = np.mgrid[0:height, 0:width]
    offsets = base + tiled2d(xs + ox, ys + oy, pitch, 2)
    ok = (offsets >= 0) & (offsets + 4 <= len(data))
    words = np.frombuffer(data[:len(data) // 4 * 4], dtype=np.uint8)
    img = np.zeros((height, width, 4), dtype=np.uint8)
    for c in range(4):
        img[..., c] = np.where(ok, words[np.clip(offsets + c, 0, len(words) - 1)], 0)
    if row.get('endian') == '2':  # 8in32: the bytes of each word reversed
        img = img[..., ::-1]
    return img


def compare(a, b, row):
    n = min(len(a), len(b)) // 4 * 4
    wa = np.frombuffer(a[:n], dtype='<u4')
    wb = np.frombuffer(b[:n], dtype='<u4')
    exact = float(np.count_nonzero(wa != wb)) / max(1, len(wa))
    if row.get('depth') == '1':
        # 24-bit depth in the upper bits after the 8in32 swap.
        da = (wa.byteswap() >> 8).astype(np.int64)
        db = (wb.byteswap() >> 8).astype(np.int64)
        diff = np.abs(da - db)
        large = float(np.count_nonzero(diff > 64)) / max(1, len(wa))
        worst = int(diff.max()) if len(diff) else 0
    else:
        ba = np.frombuffer(a[:n], dtype=np.uint8).astype(np.int16).reshape(-1, 4)
        bb = np.frombuffer(b[:n], dtype=np.uint8).astype(np.int16).reshape(-1, 4)
        diff = np.abs(ba - bb).max(axis=1)
        large = float(np.count_nonzero(diff > 8)) / max(1, len(diff))
        worst = int(diff.max()) if len(diff) else 0
    return exact, large, worst


def depth_gray(img):
    """24-bit depth as the compare reads it. untile32 already reversed the
    bytes of each 8in32 word, so img[..., 0] is the most significant byte."""
    word = ((img[..., 3].astype(np.uint32) << 24) | (img[..., 2].astype(np.uint32) << 16) |
            (img[..., 1].astype(np.uint32) << 8) | img[..., 0].astype(np.uint32))
    return (word >> 8).astype(np.int64)


def save_images(out, row, a, b):
    ua = untile32(a, row)
    ub = untile32(b, row)
    if row.get('depth') == '1':
        da = depth_gray(ua)
        db = depth_gray(ub)
        lo = min(da.min(), db.min())
        span = max(1, max(da.max(), db.max()) - lo)
        ia = np.stack([((da - lo) * 255 // span).astype(np.uint8)] * 3, axis=2)
        ib = np.stack([((db - lo) * 255 // span).astype(np.uint8)] * 3, axis=2)
        d = np.where(np.abs(da - db) > 64, 255, 0).astype(np.uint8)
    else:
        # k_8_8_8_8 after the 8in32 swap is BGRA in these bytes (Banjo's
        # yellow fence came out cyan as RGB).
        ia = ua[..., 2::-1]
        ib = ub[..., 2::-1]
        d = np.clip(np.abs(ia.astype(np.int16) - ib.astype(np.int16)).max(axis=2) * 8,
                    0, 255).astype(np.uint8)
    h, w = ia.shape[:2]
    pair = Image.new('RGB', (w * 3, h))
    pair.paste(Image.fromarray(ia), (0, 0))
    pair.paste(Image.fromarray(ib), (w, 0))
    pair.paste(Image.fromarray(np.stack([d] * 3, axis=2)), (w * 2, 0))
    path = os.path.join(out, row['name'] + '_d3d12_vulkan_diff.png')
    pair.save(path)
    return path


def resolve_large(gpu_rows_d3d, gpu_rows_vk, name):
    rd = next((r for r in gpu_rows_d3d if r['name'] == name), None)
    rv = next((r for r in gpu_rows_vk if r['name'] == name), None)
    if not rd or not rv:
        return None
    a = open(rd['path'], 'rb').read()
    b = open(rv['path'], 'rb').read()
    return compare(a, b, rd)[1]


def prefix_search(args, out, cvars, rows):
    row = next((r for r in rows if r['name'] == args.prefix), None)
    if not row:
        print('no resolve named %s' % args.prefix)
        return 1
    last = int(row['draw']) - 1  # the draw before the copy
    cache = {}

    def large(n):
        if n not in cache:
            skip = ['gpu_debug_skip_draws=%d-999999' % (n + 1)]
            tag = '_prefix_%d' % n
            value = resolve_large(dump('d3d12', args.trace, out, cvars + skip, tag),
                                  dump('vulkan', args.trace, out, cvars + skip, tag),
                                  args.prefix)
            if value is None:
                # Copies are never skipped (gpu_debug_skip_draws), so a missing
                # resolve means the replay failed.
                print('  draws 0-%d: %s missing - stopping' % (n, args.prefix))
                raise SystemExit(1)
            cache[n] = value
            print('  draws 0-%d: %s %.2f%% large' % (n, args.prefix, cache[n] * 100))
        return cache[n]

    need = args.threshold / 100.0
    lo, hi = 0, last
    if large(lo) >= need:
        hi = lo
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if large(mid) >= need:
            hi = mid
        else:
            lo = mid
    print('first draw that makes %s differ: %d (the draw log: draw_bisect.py or '
          'gpu_debug_log_draws)' % (args.prefix, hi))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--threshold', type=float, default=0.5)
    ap.add_argument('--images', choices=('all', 'first', 'none'), default='first')
    ap.add_argument('--prefix', default='')
    ap.add_argument('--thor-profile', action='store_true',
                    help='Vulkan with the Thor GPU settings (backend_ab.thor_profile_cvars)')
    args = ap.parse_args()
    cvars = [c for c in args.cvars.split() if c]
    if args.thor_profile:
        cvars += backend_ab.thor_profile_cvars(include_vrs=False)
    name = os.path.splitext(os.path.basename(args.trace))[0]
    out = os.path.join(OUT, name)
    os.makedirs(out, exist_ok=True)
    d3d = dump('d3d12', args.trace, out, cvars)
    vk = dump('vulkan', args.trace, out, cvars)
    print('%s: %d resolves on D3D12, %d on Vulkan' % (name, len(d3d), len(vk)))
    if not d3d or not vk:
        print('no resolves dumped - rebuild the trace dumps (gpu_debug_dump_resolves)')
        return 1
    print('%-16s %5s %-18s %9s %8s %8s %5s' % ('resolve', 'draw', 'format', 'size',
                                              'exact%', 'large%', 'max'))
    first = None
    for rd, rv in zip(d3d, vk):
        if (rd.get('address'), rd.get('length')) != (rv.get('address'), rv.get('length')):
            print('%-16s the resolve sequence differs (d3d12 %s+%s, vulkan %s+%s)' % (
                rd['name'], rd.get('address'), rd.get('length'), rv.get('address'),
                rv.get('length')))
            break
        a = open(rd['path'], 'rb').read()
        b = open(rv['path'], 'rb').read()
        exact, large, worst = compare(a, b, rd)
        size = '%dx%d' % (int(rd['width8']) * 8, int(rd['height8']) * 8)
        mark = ''
        if large * 100 >= args.threshold and first is None:
            first = rd['name']
            mark = '  <- first divergent'
        print('%-16s %5s %-18s %9s %7.2f%% %7.2f%% %5d%s' % (
            rd['name'], rd.get('draw'), rd.get('format'), size, exact * 100,
            large * 100, worst, mark))
        want = args.images == 'all' or (args.images == 'first' and mark)
        if want and rd.get('bpp') == '32' and (
                rd.get('depth') == '1' or rd.get('format') == 'k_8_8_8_8'):
            print('    ' + os.path.relpath(save_images(out, rd, a, b), backend_ab.ROOT))
    if first is None:
        print('no resolve differs by more than %.2f%% large' % args.threshold)
    if args.prefix:
        return prefix_search(args, out, cvars, d3d)
    return 0


if __name__ == '__main__':
    sys.exit(main())
