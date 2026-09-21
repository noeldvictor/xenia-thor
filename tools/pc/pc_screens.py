"""Run the Windows build on a title, press buttons, and capture its window.

  python tools/pc/pc_screens.py <iso> [--gpu vulkan|d3d12] [--seq "start@40000:300;a@60000:300"]
                                [--shots 30,50,70] [--storage scratch/banjo/pcshots]

The PC is the oracle for the device: the same fork code on a desktop GPU.
A screenshot at the same route point as the device says whether a glitch
is the shared render-target logic or the tiler and driver axis. The
nop HID driver presses the sequence; the window is captured with GDI
at the listed seconds and saved next to the storage root as
pc-<gpu>-<seconds>s.png. Prints the lower-half black fraction of each
capture (the same score as tools/thor/glitch_ab.py).
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release', 'xenia.exe')


def find_window(pid):
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, lparam):
        wpid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and user32.IsWindowVisible(hwnd):
            rect = wt.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            if rect.right - rect.left > 300 and rect.bottom - rect.top > 200:
                found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def capture(hwnd, path):
    from PIL import ImageGrab
    user32 = ctypes.windll.user32
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    rect = wt.RECT()
    # The client area in screen coordinates.
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    box = (pt.x, pt.y, pt.x + rect.right, pt.y + rect.bottom)
    im = ImageGrab.grab(bbox=box, all_screens=True)
    im.save(path)
    return im


def lower_black(im):
    g = im.convert('L')
    w, h = g.size
    px = g.crop((0, h // 2, w, h)).getdata()
    return sum(1 for v in px if v < 12) / max(1, len(px))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('iso')
    ap.add_argument('--gpu', default='vulkan')
    ap.add_argument('--seq', default='start@40000:300')
    ap.add_argument('--shots', default='30,50,70')
    ap.add_argument('--storage', default=os.path.join(ROOT, 'scratch', 'banjo', 'pcshots'))
    ap.add_argument('--extra', default='', help='more xenia arguments, space separated')
    args = ap.parse_args()
    os.makedirs(args.storage, exist_ok=True)
    log = os.path.join(args.storage, 'xenia.log')
    cmd = [EXE, '--storage_root=' + args.storage, '--log_file=' + log, '--mount_cache=true',
           '--gpu=' + args.gpu, '--hid=nop', '--hid_nop_connected=true',
           '--hid_nop_button_sequence=' + args.seq] + args.extra.split() + [args.iso]
    proc = subprocess.Popen(cmd, cwd=ROOT)
    t0 = time.time()
    shots = sorted(int(s) for s in args.shots.split(',') if s)
    try:
        for at in shots:
            while time.time() - t0 < at:
                if proc.poll() is not None:
                    print('xenia exited with', proc.returncode)
                    return 1
                time.sleep(0.5)
            hwnd = find_window(proc.pid)
            if not hwnd:
                print('+%ds no window' % at)
                continue
            path = os.path.join(args.storage, 'pc-%s-%ds.png' % (args.gpu, at))
            im = capture(hwnd, path)
            print('+%ds %s %dx%d lower_black=%.3f' % (at, path, im.size[0], im.size[1], lower_black(im)))
    finally:
        proc.kill()
    return 0


if __name__ == '__main__':
    sys.exit(main())
