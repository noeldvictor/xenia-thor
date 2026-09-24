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

--d3d12-skip CVAR (for example d3d12_debug_skip_tessellated_draws=true) also
replays D3D12 with that cvar: the pixels it changes are what that class of
draws renders (their share, the bounding box, and a crop - D3D12 above, D3D12
without the draws in the middle, Vulkan below - in scratch/backend_ab/). The
"within" numbers compare Vulkan with D3D12 on those pixels only. 2026-09-24:
Banjo's two failed Vulkan draws per frame were the chasm and the cliff walls
(tessellation); Vulkan now draws them.
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


# Thor-side settings that are not GPU state or cannot run in a trace replay.
THOR_PROFILE_SKIP = ('gpu_frame_limit_fps', 'vulkan_present_refresh_capped',
                     'vulkan_persistent_pipeline_cache', 'gpu_adpf_', 'thor_',
                     'gpu_cp_worker_nice', 'gpu_uma_direct_shared_memory')


def thor_profile_cvars(include_vrs=True):
    """The GPU cvars the Thor runs with: the Android build defaults and the app
    toggles that are on by default (XeniaOptimizations.java), as name=value.
    gpu_vrs_foliage_rate is a deliberate quality trade (include_vrs=False to
    leave it out)."""
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
    import xenia_thor_mcp  # noqa: E402
    out = {}
    for name, (android, _desktop) in xenia_thor_mcp._android_defaults().items():
        out[name] = android
    for toggle in xenia_thor_mcp._toggle_catalog():
        if not toggle['default']:
            continue
        for cvar in toggle['cvars']:
            name, _, value = cvar.partition('=')
            out[name] = value or 'true'
    return ['%s=%s' % (k, v) for k, v in sorted(out.items())
            if k.startswith(('gpu_', 'vulkan_', 'rt_', 'spirv_', 'render_'))
            and not k.startswith(THOR_PROFILE_SKIP)
            and (include_vrs or not k.startswith('gpu_vrs_'))]


def backend_cvars(gpu, cvars):
    """The cvars for one trace dump: vulkan_* only for Vulkan, d3d12_* only
    for D3D12 - the other dump does not know them and stops on an error
    dialog (a hung replay, 2026-09-24)."""
    other = 'd3d12_' if gpu == 'vulkan' else 'vulkan_'
    return [c for c in cvars if not c.startswith(other)]


def replay(gpu, trace, out, cvars):
    cvars = backend_cvars(gpu, cvars)
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(BIN, 'xenia-gpu-%s-trace-dump.exe' % gpu)
    log = os.path.join(out, 'dump.log')
    try:
        subprocess.run([exe, '--target_trace_file=' + trace, '--trace_dump_path=' + out,
                        '--log_file=' + log] + ['--' + c for c in cvars],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    except subprocess.TimeoutExpired:
        # A replay takes well under a minute; a hang is usually an error dialog.
        print('  %s replay hung (300 s) - killed' % gpu)
        return None, 0
    pngs = glob.glob(os.path.join(out, '*.png'))
    failed = 0
    if os.path.exists(log):
        failed = open(log, encoding='utf-8', errors='replace').read().count('Failed in backend')
    return (pngs[0] if pngs else None), failed


def skip_report(name, trace, cvars, skip_cvar, d3d, vk):
    """What the draws that skip_cvar removes render on D3D12, and whether
    Vulkan draws the same pixels."""
    skip, _ = replay('d3d12', trace, os.path.join(OUT, name, 'd3d12_skip'),
                     cvars + [skip_cvar])
    if not skip:
        return '  skip: no image'
    a = Image.open(d3d).convert('RGB')
    s = Image.open(skip).convert('RGB')
    b = Image.open(vk).convert('RGB')
    mask = ImageChops.difference(a, s).convert('L').point(
        lambda v: 255 if v > 8 else 0)
    bbox = mask.getbbox()
    w, h = a.size
    count = sum(1 for p in mask.getdata() if p)
    if not count:
        return '  skip %s: no pixel changes' % skip_cvar
    vd = ImageChops.difference(a, b).convert('L')
    within_off = sum(1 for p, m in zip(vd.getdata(), mask.getdata())
                     if m and p > 8)
    crop = Image.new('RGB', (bbox[2] - bbox[0], (bbox[3] - bbox[1]) * 3))
    for i, im in enumerate((a, s, b)):
        crop.paste(im.crop(bbox), (0, i * (bbox[3] - bbox[1])))
    crop.save(os.path.join(OUT, name + '_skip_crop.png'))
    return ('  skip %s: %.2f%% of pixels, bbox %s; Vulkan differs on %.1f%% '
            'of them (100%% = Vulkan lacks the draws)' %
            (skip_cvar, 100.0 * count / (w * h), bbox,
             100.0 * within_off / count))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--thor-profile', action='store_true',
                    help='Vulkan with the Thor GPU settings (Android defaults and '
                         'the default-on app toggles, without VRS)')
    ap.add_argument('--d3d12-skip', default='',
                    help='a D3D12 cvar that skips a class of draws, such as '
                         'd3d12_debug_skip_tessellated_draws=true')
    args = ap.parse_args()
    cvars = [c for c in args.cvars.split() if c]
    if args.thor_profile:
        # Only the Vulkan dump knows vulkan_*; gpu_* ones apply to both, the
        # same as on the Thor's shared front end.
        cvars += thor_profile_cvars(include_vrs=False)
        print('Thor profile: ' + ' '.join(thor_profile_cvars(include_vrs=False)))
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
        if args.d3d12_skip:
            print(skip_report(name, trace, cvars, args.d3d12_skip, d3d, vk))
    print('pairs in', os.path.relpath(OUT, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
