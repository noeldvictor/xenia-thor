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
    # PrintWindow with PW_RENDERFULLCONTENT captures the window's own
    # surface without bringing it to the front (the earlier ImageGrab stole
    # the focus from whatever the user was doing).
    from PIL import Image
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right, rect.bottom
    hdc_win = user32.GetDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_win)
    bmp = gdi32.CreateCompatibleBitmap(hdc_win, w, h)
    gdi32.SelectObject(hdc_mem, bmp)
    user32.PrintWindow(hwnd, hdc_mem, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG),
                    ('biPlanes', wt.WORD), ('biBitCount', wt.WORD), ('biCompression', wt.DWORD),
                    ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG),
                    ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]
    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth, bi.biHeight, bi.biPlanes, bi.biBitCount = w, -h, 1, 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc_mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_win)
    im = Image.frombuffer('RGB', (w, h), buf.raw, 'raw', 'BGRX', 0, 1)
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
    ap.add_argument('--exe', default=EXE, help='another build, e.g. the canary oracle')
    ap.add_argument('--keys', default='', help='keyboard route for a build without the nop HID: '
                    '"x@30000:300;oem1@50000:300" (x = Start, oem1 = the ; key = A in the winkey defaults)')
    args = ap.parse_args()
    os.makedirs(args.storage, exist_ok=True)
    log = os.path.join(args.storage, 'xenia.log')
    cmd = [args.exe, '--storage_root=' + args.storage, '--log_file=' + log, '--mount_cache=true',
           '--gpu=' + args.gpu]
    if args.keys:
        cmd += ['--hid=winkey']
    else:
        cmd += ['--hid=nop', '--hid_nop_connected=true', '--hid_nop_button_sequence=' + args.seq]
    cmd += args.extra.split() + [args.iso]
    # Keyboard route: (ms, vk, hold_ms), pressed with keybd_event on the window.
    VK = {'x': 0x58, 'oem1': 0xBA, 'enter': 0x0D, 'space': 0x20, 'z': 0x5A}
    key_route = []
    for item in [k for k in args.keys.split(';') if k]:
        name, _, rest = item.partition('@')
        at, _, hold = rest.partition(':')
        key_route.append((int(at) / 1000.0, VK[name.lower()], int(hold or 200) / 1000.0))
    proc = subprocess.Popen(cmd, cwd=ROOT)
    t0 = time.time()
    shots = sorted(int(s) for s in args.shots.split(',') if s)
    try:
        for at in shots:
            while time.time() - t0 < at:
                if proc.poll() is not None:
                    print('xenia exited with', proc.returncode)
                    return 1
                now = time.time() - t0
                for k in list(key_route):
                    if now >= k[0]:
                        hwnd = find_window(proc.pid)
                        if hwnd:
                            # A keyboard route needs the focus; the nop HID
                            # route (--seq) does not, so prefer it.
                            ctypes.windll.user32.SetForegroundWindow(hwnd)
                            time.sleep(0.1)
                            ctypes.windll.user32.keybd_event(k[1], 0, 0, 0)
                            time.sleep(k[2])
                            ctypes.windll.user32.keybd_event(k[1], 0, 2, 0)
                            print('+%ds key 0x%02X' % (now, k[1]))
                        key_route.remove(k)
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
