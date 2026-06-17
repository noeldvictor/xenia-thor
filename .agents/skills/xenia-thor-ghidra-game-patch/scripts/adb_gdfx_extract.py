#!/usr/bin/env python3
"""Extract default.xex from an Xbox 360 GDFX ISO that lives ON THE DEVICE, via
adb dd byte-range reads (no 7.8GB pull). Backs the gdfx_extract.py parser with an
AdbFile that reads aligned superset blocks through `adb shell dd ... > /sdcard/tmp`
+ `adb pull` (clean binary), then trims. Only the GDFX directory sectors + the
target file's bytes are transferred (~few MB total).

Usage: python adb_gdfx_extract.py <adb_serial> "<device_iso_path>" <out_path>
"""
import struct, subprocess, sys, os, math

SECTOR = 2048
ATTR_DIRECTORY = 0x10
MAGIC = b"MICROSOFT*XBOX*MEDIA"
LIKELY_OFFSETS = [0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000]
ADB = r"C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe"
ALIGN = 4096
DEV_TMP = "/sdcard/_gdfx_read.bin"


class AdbFile:
    def __init__(self, serial, dev_path):
        self.serial = serial
        self.dev_path = dev_path
        self.pos = 0
        out = subprocess.check_output(
            [ADB, "-s", serial, "shell", "stat", "-c", "%s",
             _q(dev_path)]).decode().strip()
        self.size = int(out)

    def seek(self, off, whence=0):
        self.pos = self.size + off if whence == 2 else off

    def tell(self):
        return self.pos

    def read(self, length):
        if length <= 0:
            return b""
        aligned = (self.pos // ALIGN) * ALIGN
        lead = self.pos - aligned
        nblocks = math.ceil((lead + length) / ALIGN)
        cmd = ("dd if=%s of=%s bs=%d skip=%d count=%d 2>/dev/null"
               % (_q(self.dev_path), DEV_TMP, ALIGN, aligned // ALIGN, nblocks))
        subprocess.check_call([ADB, "-s", self.serial, "shell", cmd])
        local = os.path.join(os.path.dirname(os.path.abspath(sys.argv[3])),
                             "_gdfx_pull.bin")
        subprocess.check_call([ADB, "-s", self.serial, "pull", DEV_TMP, local],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(local, "rb") as fh:
            raw = fh.read()
        self.pos += length
        return raw[lead:lead + length]


def _q(p):
    return "'" + p.replace("'", "'\\''") + "'"


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _read_at(f, offset, length):
    f.seek(offset)
    return f.read(length)


def find_game_offset(f, size):
    for go in LIKELY_OFFSETS:
        probe = go + 32 * SECTOR
        if probe + len(MAGIC) > size:
            continue
        if _read_at(f, probe, len(MAGIC)) == MAGIC:
            return go
    return None


def find_entry(f, game_offset, dir_sector, dir_size, want):
    data = _read_at(f, game_offset + dir_sector * SECTOR, dir_size)
    want = want.lower()
    stack, seen = [0], set()
    while stack:
        o = stack.pop()
        if o in seen:
            continue
        seen.add(o)
        off = o * 4
        if off + 14 > len(data):
            continue
        nl, nr = _u16(data, off + 0), _u16(data, off + 2)
        sector, length = _u32(data, off + 4), _u32(data, off + 8)
        attr, nlen = data[off + 12], data[off + 13]
        name = data[off + 14:off + 14 + nlen].decode("latin-1", "replace")
        if not (attr & ATTR_DIRECTORY) and name.lower() == want:
            return sector, length
        if nl:
            stack.append(nl)
        if nr:
            stack.append(nr)
    return None


def main():
    serial, dev_iso, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    f = AdbFile(serial, dev_iso)
    sys.stderr.write("ISO size on device: %d bytes\n" % f.size)
    go = find_game_offset(f, f.size)
    if go is None:
        raise SystemExit("No GDFX magic — not an Xbox 360 disc image?")
    fs = _read_at(f, go + 32 * SECTOR, 32)
    root_sector, root_size = _u32(fs, 20), _u32(fs, 24)
    sys.stderr.write("game_offset=0x%X root_sector=%d root_size=%d\n"
                     % (go, root_sector, root_size))
    found = find_entry(f, go, root_sector, root_size, "default.xex")
    if not found:
        raise SystemExit("default.xex not found in disc root.")
    sector, length = found
    sys.stderr.write("default.xex: sector=%d length=%d (%.2f MiB)\n"
                     % (sector, length, length / 1048576.0))
    f.seek(go + sector * SECTOR)
    data = f.read(length)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as out:
        out.write(data)
    sys.stderr.write("wrote %s (%d bytes)\n" % (out_path, len(data)))


if __name__ == "__main__":
    main()
