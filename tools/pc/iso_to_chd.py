"""Convert Xbox 360 ISOs to CHD (MAME compressed hunks of data) for xenia-thor.

  python tools/pc/iso_to_chd.py GAME.iso [GAME2.iso ...] [--codec zstd|lzma]
      [--out-dir DIR] [--no-verify]

Runs `chdman createdvd` in WSL (Ubuntu: `apt install mame-tools`) for each ISO,
then `chdman verify`, and prints the sizes. The emulator opens a CHD like an
ISO (src/xenia/vfs/devices/disc_image_source.cc, by its "MComprHD" signature):
the game reads decompress the hunks on demand.

The codec decides the load speed on the Thor: zstd (the default here) decodes
several times faster than LZMA for a somewhat larger file; LZMA, chdman's own
default mix, is the smallest. A CHD made with `createcd` (a CD image) or with a
parent is refused by the emulator.
"""
import argparse
import os
import subprocess
import sys


def wsl_path(path):
    path = os.path.abspath(path).replace('\\', '/')
    return '/mnt/%s%s' % (path[0].lower(), path[2:])


def wsl(command):
    return subprocess.run(['wsl', 'bash', '-lc', command], stdin=subprocess.DEVNULL,
                          capture_output=True, text=True,
                          env=dict(os.environ, MSYS_NO_PATHCONV='1'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('isos', nargs='+')
    ap.add_argument('--codec', default='zstd', choices=('zstd', 'lzma'))
    ap.add_argument('--out-dir', default='')
    ap.add_argument('--no-verify', action='store_true')
    args = ap.parse_args()
    if 'chdman' not in wsl('which chdman').stdout:
        print('chdman is not installed in WSL: wsl -u root apt-get install -y mame-tools')
        return 1
    failed = 0
    for iso in args.isos:
        out_dir = args.out_dir or os.path.dirname(os.path.abspath(iso))
        chd = os.path.join(out_dir, os.path.splitext(os.path.basename(iso))[0] + '.chd')
        r = wsl('chdman createdvd -f -i "%s" -o "%s" -c %s' % (wsl_path(iso), wsl_path(chd),
                                                              args.codec))
        if r.returncode or not os.path.exists(chd):
            print('%s: createdvd failed: %s' % (iso, (r.stdout + r.stderr)[-500:]))
            failed += 1
            continue
        if not args.no_verify:
            v = wsl('chdman verify -i "%s"' % wsl_path(chd))
            if v.returncode:
                print('%s: verify failed: %s' % (chd, (v.stdout + v.stderr)[-500:]))
                failed += 1
                continue
        iso_size, chd_size = os.path.getsize(iso), os.path.getsize(chd)
        print('%s -> %s: %.2f GB -> %.2f GB (%.0f%%), %s%s' % (
            os.path.basename(iso), chd, iso_size / 1e9, chd_size / 1e9,
            100.0 * chd_size / iso_size, args.codec, '' if args.no_verify else ', verified'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
