"""Drive the Windows build to a Banjo screen by what is on its window.

  python tools/pc/pc_goto.py <iso> --screen title|menu [--gpu vulkan] [--exe ...]
                             [--storage dir] [--extra "--cvar=value ..."] [--hold 120]

The PC side of xenia_goto: the nop HID driver polls a trigger file
(hid_nop_trigger_file) and this script writes "start:300" into it when the
window shows the screen the step waits for, using tools/pc/grass_score.py
statistics of PrintWindow captures (no window focus, no clock). Steps for
Banjo: wait for the gold puzzle, press START, wait for the title logo
(Spiral Mountain); "menu" adds A. When the screen is reached the capture
is scored (grass_score) and saved as <storage>/goto-<screen>.png; the
verdict line is the last line: GOOD, BAD (foliage), or TIMEOUT. Exit 0
GOOD, 1 BAD, 2 TIMEOUT or no title.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import grass_score  # noqa: E402
import pc_screens  # noqa: E402
from PIL import Image  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RENDERDOCCMD = os.path.join(os.environ.get("ProgramFiles", "C:/Program Files"), "RenderDoc", "renderdoccmd.exe")


def gold(im):
    px = list(im.convert('RGB').resize((192, 108)).getdata())
    return sum(1 for r, g, b in px if r > 140 and g > 80 and b < 90 and r > b + 80) / len(px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('iso')
    ap.add_argument('--screen', default='title', choices=['puzzle', 'title', 'menu'])
    ap.add_argument('--gpu', default='vulkan')
    ap.add_argument('--exe', default=pc_screens.EXE)
    ap.add_argument('--storage', default=os.path.join(ROOT, 'scratch', 'banjo', 'pcgoto'))
    ap.add_argument('--extra', default='')
    ap.add_argument('--hold', type=int, default=150)
    ap.add_argument('--timeout', type=int, default=180)
    ap.add_argument('--settle', type=float, default=4.0, help='seconds after the screen holds before scoring')
    ap.add_argument('--at-screen', default='', help='cvar name=value pairs (comma list) set live once the screen is reached, before the settle')
    ap.add_argument('--after', type=float, default=0.0, help='seconds to keep running after the score (a trace budget drains)')
    ap.add_argument('--renderdoc', default='', help='run under renderdoccmd and capture one frame at the screen into this .rdc path template')
    args = ap.parse_args()
    os.makedirs(args.storage, exist_ok=True)
    trigger = os.path.abspath(os.path.join(args.storage, 'trigger.txt'))
    if os.path.exists(trigger):
        os.remove(trigger)
    log = os.path.join(args.storage, 'xenia.log')
    cmd = [args.exe, '--storage_root=' + args.storage, '--log_file=' + log, '--mount_cache=true',
           '--gpu=' + args.gpu, '--hid=nop', '--hid_nop_connected=true',
           '--hid_nop_trigger_file=' + trigger] + args.extra.split() + [args.iso]
    if args.renderdoc:
        # renderdoccmd starts xenia as its child; the window is then found
        # by its title, and the capture is requested from inside xenia
        # (renderdoc_trigger_capture) - no F12, no focus.
        rd_dir = os.path.dirname(os.path.abspath(args.renderdoc))
        os.makedirs(rd_dir, exist_ok=True)
        for old in os.listdir(rd_dir):
            if old.endswith('.rdc') and old.startswith(os.path.basename(args.renderdoc)):
                os.remove(os.path.join(rd_dir, old))
        cmd = [RENDERDOCCMD, 'capture', '--wait-for-exit', '--working-dir', ROOT,
               '--capture-file', os.path.abspath(args.renderdoc)] + cmd
    proc = subprocess.Popen(cmd, cwd=ROOT)
    t0 = time.time()

    def press(buttons):
        with open(trigger, 'w') as f:
            f.write('%s:%d\n' % (buttons, args.hold))
        print('+%.0f s press %s' % (time.time() - t0, buttons), flush=True)

    def grab():
        hwnd = pc_screens.find_window(proc.pid, 'Xenia' if args.renderdoc else None)
        if not hwnd:
            return None
        path = os.path.join(args.storage, 'live.png')
        return pc_screens.capture(hwnd, path)

    def is_title(im):
        return grass_score.stats(os.path.join(args.storage, 'live.png'))[0]

    def is_menu(im):
        # The SINGLE PLAYER house: warm, no gold puzzle, no logo, a bright
        # header band at the top.
        px = list(im.convert('RGB').resize((160, 90)).getdata())
        top = px[:160 * 12]
        bright = sum(1 for r, g, b in top if r > 200 and g > 200 and b > 200) / len(top)
        return gold(im) < 0.25 and not is_title(im) and bright > 0.04

    # START during the puzzle animation goes to the title screen; START on
    # the static puzzle goes straight to the menu, and B at the menu goes
    # back to the title. Both paths end on the requested screen.
    steps = [('puzzle', lambda im: gold(im) > 0.35, 'start'),
             ('title-or-menu', lambda im: is_title(im) or is_menu(im), None)]
    if args.screen == 'title':
        steps.append(('title', is_title, ''))
    elif args.screen == 'menu':
        steps.append(('menu', is_menu, ''))
    verdict = 'TIMEOUT'
    try:
        last_button = ''
        last_press = 0.0
        for name, pred, button in steps:
            reached = False
            while time.time() - t0 < args.timeout:
                if proc.poll() is not None:
                    print('xenia exited with', proc.returncode)
                    return 2
                im = grab()
                if im is not None and pred(im):
                    reached = True
                    break
                # The previous step's press can be lost (the first puzzle
                # frames take no input): repeat it every 5 s while its own
                # screen is still up.
                if last_button == 'start' and im is not None and time.time() - last_press > 5 and gold(im) > 0.35:
                    press(last_button)
                    last_press = time.time()
                time.sleep(1.5)
            if not reached:
                print('TIMEOUT waiting for %s at +%.0f s' % (name, time.time() - t0))
                return 2
            print('+%.0f s %s' % (time.time() - t0, name), flush=True)
            if button is None:
                # Decide from where we landed.
                if is_menu(im) and args.screen == 'title':
                    button = 'b'
                elif is_title(im) and args.screen == 'menu':
                    button = 'a'
                else:
                    button = ''
            if button:
                press(button)
                last_button, last_press = button, time.time()
                time.sleep(3)
            else:
                last_button = ''
        for item in [x for x in args.at_screen.split(',') if x.strip()]:
            with open(trigger, 'w') as f:
                f.write('cvar:%s' % item.strip() + chr(10))
            time.sleep(0.4)
            print('+%.0f s cvar %s' % (time.time() - t0, item.strip()), flush=True)
        time.sleep(args.settle)
        if args.renderdoc:
            with open(trigger, 'w') as f:
                f.write('cvar:renderdoc_trigger_capture=1' + chr(10))
            print('+%.0f s renderdoc capture requested' % (time.time() - t0), flush=True)
            for _ in range(60):
                time.sleep(1)
                caps = [x for x in os.listdir(rd_dir) if x.endswith('.rdc')]
                if caps:
                    print('capture %s' % os.path.join(rd_dir, caps[0]), flush=True)
                    break
            else:
                print('no capture appeared in 60 s', flush=True)
        im = grab()
        out = os.path.join(args.storage, 'goto-%s.png' % args.screen)
        im.save(out)
        title, black, gray = grass_score.stats(out)
        bad = black > 0.15 or gray > 0.30
        verdict = 'BAD' if bad else 'GOOD'
        print('%s %s title=%s lower_black=%.3f gray=%.3f' % (verdict, out, title, black, gray))
        if args.after > 0:
            time.sleep(args.after)
        return 1 if bad else 0
    finally:
        proc.kill()
        if args.renderdoc:
            # xenia is renderdoccmd's child; killing the parent leaves it.
            subprocess.call(['taskkill', '/IM', os.path.basename(args.exe), '/F'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == '__main__':
    sys.exit(main())
