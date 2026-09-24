"""The PC regression matrix: every title, the Thor's configuration, cdb on crashes.

  python tools/pc/pc_matrix.py [TITLE ...] [--plain] [--cvars "a=1 b=2"]
  TITLE: banjo gears gears2 mc2 bd (default: all)

For each title in turn (never in parallel - one emulator at a time keeps the
PC's memory free): pc_run.py with --android-defaults (the Thor's GPU, kernel
and cache defaults the PC can run; --plain drops them), on a fixed press route
long enough to reach gameplay. A title that crashes (EXITED 0xC0000005) runs
again under --cdb so its stack comes back. Not under cdb from the start: every
write-watch page fault is a debugger event, the emulation slows several
times over, and the press routes stop fitting (2026-09-23: Banjo on its logo
for 150 s).
Collects the verdict, the crash stack, the pipeline compile rate and the
zero-copy hazard line, makes one contact sheet per title of all its captures,
and writes scratch/matrix/summary-<stamp>.json. Look at the sheets: a glitch
is a picture, not a verdict.

2026-09-23: after a day of changes a Banjo crash showed only on its Spiral
Mountain load; one matrix pass after every change set catches that class.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'scratch', 'matrix')

# Routes reach gameplay on the PC with the Android defaults. A press with a
# "+" is sticky (pc_run.py): repeated until the picture changes, and the later
# presses wait for it - titles ignore presses while they load, and the load
# time varies run to run (2026-09-23: timed routes stuck Gears on its menu).
ENTRIES = {
    'banjo': ('scratch/banjo/banjo.iso', 300, 30,
              '70:a+ 85:a 100:start 115:a 130:a 145:a 160:a 175:a 190:a 205:a 220:a'),
    'gears': ('scratch/gears/gears.iso', 300, 30,
              '45:start+ 60:a+ 75:a+ 90:start+ 105:a+ 150:start 170:a'),
    'gears2': ('scratch/gears/gears2.iso', 270, 30,
               '60:start+ 75:a+ 90:a+ 105:start+ 120:a+ 150:a 165:a'),
    'mc2': ('scratch/mc2/mc2_disc1.iso', 250, 25,
            '40:start+ 55:a 70:a 85:start 100:a 115:a 130:a 150:start 165:a 180:a 200:a 220:a'),
    'bd': ('scratch/blue-dragon/bd_disc1.iso', 270, 30,
           '60:start+ 75:a+ 90:a 105:a 120:start 135:a 150:a 165:a 180:a 195:a'),
}


def contact_sheet(shots, path, label):
    if not shots:
        return None
    w, h = 320, 180
    cols = 4
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new('RGB', (w * cols, h * rows))
    for i, shot in enumerate(shots):
        im = Image.open(shot).convert('RGB').resize((w, h))
        ImageDraw.Draw(im).text((4, 4), '%s %s' % (label, os.path.basename(shot)[5:7]), fill=(255, 0, 0))
        sheet.paste(im, ((i % cols) * w, (i // cols) * h))
    sheet.save(path)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('titles', nargs='*')
    ap.add_argument('--plain', action='store_true', help='PC defaults instead of the Android defaults')
    ap.add_argument('--thor-profile', action='store_true',
                    help='the Android defaults and the default-on app toggles (pc_run.py)')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--keep-saves', action='store_true',
                    help='keep the saved games of earlier runs (default: delete them)')
    args = ap.parse_args()
    titles = args.titles or list(ENTRIES)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for title in titles:
        iso, seconds, every, presses = ENTRIES[title]
        if not os.path.exists(os.path.join(ROOT, iso)):
            print('%-7s SKIP (no %s)' % (title, iso), flush=True)
            continue
        name = 'matrix_%s' % title
        if not args.keep_saves:
            # Saved games (content type 00000001) change the route: after a run
            # that reached gameplay, Gears asks to overwrite its checkpoint and
            # the route stops there (2026-09-24). Every run starts clean.
            for saves in glob.glob(os.path.join(ROOT, 'scratch', name, 'pcrun', 'content',
                                                '*', '*', '00000001')):
                shutil.rmtree(saves, ignore_errors=True)
        cmd = [sys.executable, os.path.join(ROOT, 'tools', 'pc', 'pc_run.py'), iso, '--name', name,
               '--seconds', str(seconds), '--every', str(every), '--presses', presses]
        if args.thor_profile:
            cmd.append('--thor-profile')
        elif not args.plain:
            cmd.append('--android-defaults')
        if args.cvars:
            cmd += ['--cvars', args.cvars]
        t0 = time.time()

        def run(extra):
            p = subprocess.run(cmd + extra, cwd=ROOT, capture_output=True, text=True,
                               encoding='utf-8', errors='replace')
            lines = p.stdout.splitlines()
            v = next((l.split('verdict:', 1)[1].split('|')[0].strip() for l in lines
                      if l.startswith('verdict:')), '?')
            return lines, v

        out, verdict = run([])
        stack = []
        if verdict.startswith('EXITED 3221225477'):
            print('%-7s crashed - again under cdb for the stack' % title, flush=True)
            out_cdb, _ = run(['--cdb', '--name', name + '_cdb'])
            stack = [l.strip() for l in out_cdb if re.match(r'\s+[0-9a-f]{2} ', l)][:8]
        pipes = next((l for l in out if l.startswith('pipelines:')), '')
        hazards = next((l for l in out if l.startswith('zero-copy hazards:')), '')
        shots = sorted(glob.glob(os.path.join(ROOT, 'scratch', name, 'pcrun', 'shot-*.png')))
        sheet = contact_sheet(shots, os.path.join(OUT, '%s-%s.png' % (title, stamp)), title)
        row = {'title': title, 'verdict': verdict, 'seconds': round(time.time() - t0),
               'shots': len(shots), 'crash_stack': stack, 'pipelines': pipes, 'hazards': hazards,
               'sheet': os.path.relpath(sheet, ROOT).replace('\\', '/') if sheet else None}
        rows.append(row)
        print('%-7s %-24s shots %2d  %s' % (title, verdict, len(shots), pipes), flush=True)
        for line in stack:
            print('        ' + line[:150], flush=True)
    path = os.path.join(OUT, 'summary-%s.json' % stamp)
    json.dump({'config': 'plain' if args.plain else 'android-defaults', 'cvars': args.cvars,
               'rows': rows}, open(path, 'w'), indent=1)
    print('summary', os.path.relpath(path, ROOT), flush=True)
    return 1 if any(r['verdict'].startswith('EXITED') for r in rows) else 0


if __name__ == '__main__':
    sys.exit(main())
