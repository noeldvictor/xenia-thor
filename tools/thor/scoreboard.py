"""The scoreboard: the same device measurements after every install.

  python tools/thor/scoreboard.py [ENTRY ...] [--note "what changed"]
  ENTRY: banjo_title banjo_story gears1 mc2 (default: all)

Outcome rule 4 (AGENTS.md): each cycle reports its row - fixed, faster, or
nothing. Each entry launches its title the play-button way, drives it to a
fixed point (the screen routes for Banjo; timed presses for Gears and
MagnaCarta 2), then measures for 20 s: presented fps from the swap counter,
the median GPU frame time (vulkan_trace_pass_timestamps), the panel's top and
bottom luma, and keeps one screenshot. One JSON row per entry is appended to
docs/scoreboard.jsonl with the commit and the device's installed build id;
the printout shows the change from that entry's previous row. The device
cools to the preflight gate before each entry.

Heat (AGENTS.md section 5): a guard thread reads the case temperature every
5 s during the whole entry and force-stops the emulator at --max-case-c
(default 44 C) or after --max-seconds (default 300); the row then says
"stopped". --cool N launches with thor_debug_cool=N: an N fps cap, no X3
prime core, Sleep(0) streaks sleep - about half the heat of a 30 fps title
at 15, with a valid GPU frame time (2026-09-29: the plain Gears route took
the case from 34 C to 44.8 C in 47 s).
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402

BOARD = os.path.join(ROOT, 'docs', 'scoreboard.jsonl')
ROMS = '/storage/2664-21DE/Roms/xbox360/'
GEARS = ROMS + 'Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso'
MC2 = ROMS + 'Magna Carta 2.m3u/MagnaCarta 2 (USA) (En,Fr) (Disc 1).iso'
BANJO_START = 'name:puzzle;until:gold>0.35;press:START;settle:3000|name:start;until:gold<0.2;timeout:120'

ENTRIES = {
    # The attract fly-through after the gold puzzle (the dark-half scene).
    'banjo_title': dict(kind='goto', title='banjo', steps=BANJO_START),
    # Single Player -> Start New Game -> the opening story over the world.
    'banjo_story': dict(kind='goto', title='banjo',
                        steps=BANJO_START + '|name:menu;press:A;settle:6000|name:story;press:A;settle:25000'),
    # Title -> menus -> the prison cell. The title (red, "PRESS START") comes
    # later on the device than on the PC, so the route waits for it instead
    # of pressing on a clock (2026-09-22: timed presses left it at the title).
    'gears1': dict(kind='goto', path=GEARS,
                   steps='name:title;until:red>0.2;timeout:400;press:START;settle:12000'
                         '|name:m1;press:A;settle:12000|name:m2;press:A;settle:12000'
                         '|name:m3;press:START;settle:12000|name:m4;press:A;settle:15000'
                         '|name:m5;press:A;settle:60000|name:cell;press:START;settle:15000'
                         '|name:c2;press:A;settle:20000|name:c3;press:A;settle:30000'),
    # Title -> menus -> the castle scene (the PC reached it at 200 s). a64 only.
    'mc2': dict(kind='timed', path=MC2, at=260, cvars=['cpu_backend_llvm=false'],
                presses=[(50, 'START'), (65, 'A'), (80, 'A'), (95, 'START'), (110, 'A'), (125, 'A'),
                         (140, 'A'), (160, 'START'), (175, 'A'), (190, 'A'), (210, 'A'), (230, 'A')]),
}


def swaps():
    try:
        return json.loads(m.xenia_api('/frame_stats')).get('swaps')
    except Exception:
        return None


def wait_cool(max_case_c=41.0):
    # The panel too: a sleeping panel fails every launch (2026-10-01, twice).
    r = json.loads(m.xenia_wait_ready(timeout_s=900, max_case_c=max_case_c))
    if 'screen is asleep' in ' '.join(r.get('waited_on', [])):
        print('waited %d s for the panel (power button)' % r.get('waited_s', 0), flush=True)
    return r.get('temps', {})


def last_rows():
    rows = {}
    if os.path.exists(BOARD):
        for line in open(BOARD, encoding='utf-8'):
            try:
                r = json.loads(line)
                rows[r['entry']] = r
            except Exception:
                pass
    return rows


class HeatGuard(threading.Thread):
    """Force-stops the emulator at the case limit or the time limit."""

    def __init__(self, max_case_c, max_seconds):
        super().__init__(daemon=True)
        self.max_case_c = max_case_c
        self.max_seconds = max_seconds
        self.stop_flag = threading.Event()
        self.stopped = ''
        self.peak_case_c = 0.0

    def run(self):
        start = time.time()
        while not self.stop_flag.is_set():
            try:
                case_c = float(json.loads(m.xenia_device_status()).get('temps', {}).get('case_c') or 0)
            except Exception:
                case_c = 0.0
            self.peak_case_c = max(self.peak_case_c, case_c)
            if case_c >= self.max_case_c or time.time() - start >= self.max_seconds:
                self.stopped = ('case %.1f C' % case_c if case_c >= self.max_case_c
                                else 'time %d s' % self.max_seconds)
                print('heat guard: %s - force stop' % self.stopped, flush=True)
                m.xenia_force_stop()
                return
            self.stop_flag.wait(5)


def run_entry(name, spec, extra_cvars=(), max_case_c=44.0, max_seconds=300.0):
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    m.xenia_launch_cvars(set='vulkan_trace_pass_timestamps=true')
    m.xenia_launch_cvars(set='vulkan_trace_draw_outcomes_per_frame=true')  # the timing line prints inside its block
    for c in list(spec.get('cvars', [])) + list(extra_cvars):
        m.xenia_launch_cvars(set=c)
    temps = wait_cool()
    guard = HeatGuard(max_case_c, max_seconds)
    guard.start()
    reached = False
    if spec['kind'] == 'goto':
        try:
            g = json.loads(m.xenia_goto(steps=spec['steps'], title=spec.get('title') or spec['path'],
                                        launch=True, screenshot=False)).get('goto') or {}
        except Exception as e:  # the heat guard stopped the app mid-route
            if not guard.stopped:
                raise
            print('route ended: %s' % e.__class__.__name__, flush=True)
            g = {}
        reached = bool(g.get('reached'))
    else:
        r = json.loads(m.xenia_launch(spec['path'], skip_preflight=True))
        t0 = time.time()
        for at, button in spec['presses']:
            time.sleep(max(0.0, at - (time.time() - t0)))
            m.xenia_press(button)
        time.sleep(max(0.0, spec['at'] - (time.time() - t0)))
        reached = bool(r.get('launched')) and (swaps() or 0) > 0
    s0, t0 = swaps(), time.time()
    time.sleep(20)
    s1, t1 = swaps(), time.time()
    fps = round((s1 - s0) / (t1 - t0), 1) if (s0 is not None and s1 is not None) else None
    try:
        gpu = json.loads(m.xenia_fps()).get('summary', {}).get('median_gpu_frame_us')
    except Exception:
        gpu = None
    # The driver is a paradigm axis: record the one this row ran on.
    driver = None
    try:
        d = json.loads(m.xenia_gpu_driver())
        driver = d.get('selected') or d.get('active') or d.get('current')
        if driver is None:
            driver = str(d)[:160]
    except Exception:
        pass
    timing_lines = 0
    try:
        timing_lines = m._adb('logcat', '-d', '-s', 'xenia:*', timeout=120).count('GPU pass timing:')
    except Exception:
        pass
    shot = json.loads(m.xenia_screenshot('score-' + name)).get('path')
    top = bottom = None
    if shot and os.path.exists(shot):
        from PIL import Image, ImageStat
        im = Image.open(shot).convert('L')
        w, h = im.size
        top = round(ImageStat.Stat(im.crop((0, 0, w, h // 2))).mean[0], 1)
        bottom = round(ImageStat.Stat(im.crop((0, h // 2, w, h))).mean[0], 1)
    guard.stop_flag.set()
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    return {'entry': name, 'reached': reached, 'fps': fps, 'gpu_frame_us': gpu, 'driver': driver,
            'timing_lines': timing_lines,
            'luma_top': top, 'luma_bottom': bottom, 'case_c': temps.get('case_c'),
            'peak_case_c': round(guard.peak_case_c, 1), 'stopped': guard.stopped, 'shot': shot}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('entries', nargs='*')
    ap.add_argument('--note', default='')
    ap.add_argument('--cool', type=int, default=0,
                    help='launch with thor_debug_cool=N (an N fps cap, no prime core, '
                         'sleeping Sleep(0) streaks): device debugging without overheating')
    ap.add_argument('--max-case-c', type=float, default=44.0)
    ap.add_argument('--max-seconds', type=float, default=300.0)
    ap.add_argument('--cvars', default='',
                    help='extra launch cvars for this run, name=value separated by spaces '
                         '(an A/B arm that cannot switch live, tools/thor/pending_ab.py); '
                         'they are added to the note of the row')
    args = ap.parse_args()
    extra = [c for c in args.cvars.split() if c]
    if args.cool:
        extra.append('thor_debug_cool=%d' % args.cool)
    if extra:
        args.note = (args.note + ' ' if args.note else '') + 'cvars: ' + ' '.join(extra)
    names = args.entries or list(ENTRIES)
    commit = subprocess.run(['git', 'rev-parse', '--short=10', 'HEAD'], cwd=ROOT, capture_output=True,
                            text=True).stdout.strip()
    build = m._installed_app_build_id()[:12]
    prev = last_rows()
    stamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    for name in names:
        row = run_entry(name, ENTRIES[name], extra, args.max_case_c, args.max_seconds)
        row.update({'date': stamp, 'commit': commit, 'build': build, 'note': args.note})
        with open(BOARD, 'a', encoding='utf-8') as f:
            f.write(json.dumps({k: v for k, v in row.items() if k != 'shot'}) + '\n')
        p = prev.get(name)
        delta = ''
        if p and p.get('fps') is not None and row['fps'] is not None:
            delta = ' (was %s fps on %s, %+.1f)' % (p['fps'], p.get('date'), row['fps'] - p['fps'])
        print('%-12s reached=%s fps=%s%s gpu_frame_us=%s luma=%s/%s case=%sC peak=%sC%s  %s' % (
            name, row['reached'], row['fps'], delta, row['gpu_frame_us'], row['luma_top'],
            row['luma_bottom'], row['case_c'], row['peak_case_c'],
            ' STOPPED (%s)' % row['stopped'] if row['stopped'] else '', row['shot']), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
