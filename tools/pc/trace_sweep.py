"""Find Vulkan-only glitches across titles: trace a run, compare every frame.

  python tools/pc/trace_sweep.py [TITLE ...] [--at "150 180 210 240"]
      [--cvars "a=1"] [--plain]
  TITLE: the pc_matrix.py titles (default: all present)

For each title (one emulator at a time): pc_run.py on its pc_matrix.py press
route with the Android defaults (the Thor's configuration) and a GPU trace at
each --at time (default: five times across the route), then backend_ab.py on
every trace - the Vulkan and the D3D12 trace dumps on the same frame. The rows
are sorted by the share of pixels that differ; look at the pairs in
scratch/backend_ab/ from the top, and name the draws with draw_bisect.py.

2026-09-24: a trace compare over 16 frames found Banjo's missing tessellation,
vertex kill and alpha-to-mask dither; this makes the frame capture one call.
"""
import argparse
import glob
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pc_matrix  # noqa: E402

ROOT = pc_matrix.ROOT
ROW_RE = re.compile(r'^(\S+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)%\s+(\d+)/(\d+)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('titles', nargs='*')
    ap.add_argument('--at', default='')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--plain', action='store_true')
    ap.add_argument('--thor-profile', action='store_true',
                    help='the Android defaults and the default-on app toggles (pc_run.py)')
    ap.add_argument('--deep', action='store_true',
                    help='the deeper gameplay routes of pc_matrix.DEEP, traced at their own '
                         'times inside gameplay')
    args = ap.parse_args()
    entries = pc_matrix.DEEP if args.deep else pc_matrix.ENTRIES
    titles = args.titles or list(entries)
    rows = []
    for title in titles:
        iso, seconds, every, presses = entries[title][:4]
        if not os.path.exists(os.path.join(ROOT, iso)):
            print('%-7s SKIP (no %s)' % (title, iso), flush=True)
            continue
        at = args.at or (entries[title][4] if args.deep else
                         ' '.join(str(int(seconds * f)) for f in (0.5, 0.6, 0.7, 0.8, 0.9)))
        name = ('sweep_deep_%s' if args.deep else 'sweep_%s') % title
        pc_matrix.delete_saves(name)
        trace_dir = os.path.join(ROOT, 'scratch', name, 'pcrun', 'traces')
        for old in glob.glob(os.path.join(trace_dir, '*.xtr')):
            os.remove(old)
        cmd = [sys.executable, os.path.join(ROOT, 'tools', 'pc', 'pc_run.py'), iso,
               '--name', name, '--seconds', str(seconds), '--every', str(every),
               '--presses', presses, '--trace-at', at]
        if args.thor_profile:
            cmd.append('--thor-profile')
        elif not args.plain:
            cmd.append('--android-defaults')
        if args.cvars:
            cmd += ['--cvars', args.cvars]
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                           encoding='utf-8', errors='replace')
        verdict = next((l.split('verdict:', 1)[1].split('|')[0].strip()
                        for l in p.stdout.splitlines() if l.startswith('verdict:')), '?')
        traces = sorted(glob.glob(os.path.join(trace_dir, '*.xtr')))
        print('%-7s %s, %d traces' % (title, verdict, len(traces)), flush=True)
        if not traces:
            continue
        ab = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'pc', 'backend_ab.py')] +
                            traces + (['--cvars', args.cvars] if args.cvars else []) +
                            (['--thor-profile'] if args.thor_profile else []),
                            cwd=ROOT, capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
        for line in ab.stdout.splitlines():
            m = ROW_RE.match(line)
            if m:
                rows.append((float(m.group(5)), title, m.group(1), int(m.group(6)),
                             int(m.group(7)), os.path.join(trace_dir, m.group(1) + '.xtr')))
            elif line.strip() and not line.startswith(('trace ', 'pairs in')):
                print('  ' + line.strip())
    rows.sort(reverse=True)
    print('%-7s %-18s %8s  %s' % ('title', 'trace', 'off>8', 'failed d3d12/vulkan'))
    for off, title, trace, fd, fv, path in rows:
        print('%-7s %-18s %7.2f%%  %d/%d  %s' % (title, trace, off, fd, fv,
                                                  os.path.relpath(path, ROOT)))
    print('pairs in scratch/backend_ab/<trace>_pair.png (D3D12 above, Vulkan below)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
