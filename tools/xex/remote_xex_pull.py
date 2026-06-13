#!/usr/bin/env python3
"""Pull just default.xex from a device-resident GDFX ISO over `adb exec-out dd`
(random-access, never transfers the multi-GB image). Reuses gdfx_extract's tree
walk via a RemoteFile that serves 512-aligned dd reads.
Usage: python remote_xex_pull.py <device_iso_path> <out_path> [serial]
"""
import sys, os, math, subprocess, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__),
    '..', '.agents', 'skills', 'xenia-thor-ghidra-game-patch', 'scripts'))
import gdfx_extract as gx

ADB = r"C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe"

class RemoteFile:
    def __init__(self, iso, serial, size=7835492352):
        self.iso = iso; self.serial = serial; self.pos = 0
        self.size = size
    def seek(self, off, whence=0):
        if whence == 0: self.pos = off
        elif whence == 1: self.pos += off
        elif whence == 2: self.pos = self.size + off
        return self.pos
    def tell(self): return self.pos
    def read(self, length):
        if length <= 0: return b""
        aligned = self.pos & ~511
        delta = self.pos - aligned
        nblocks = math.ceil((delta + length) / 512)
        cmd = "dd if='%s' bs=512 skip=%d count=%d 2>/dev/null" % (
            self.iso, aligned // 512, nblocks)
        want = min(delta + length, nblocks * 512)
        raw = b""
        for attempt in range(10):
            r = subprocess.run([ADB,"-s",self.serial,"exec-out",cmd], capture_output=True)
            raw = r.stdout
            if len(raw) >= want:
                break
            time.sleep(0.3)
        if len(raw) < want:
            sys.stderr.write("WARN short read @%#x want=%d got=%d\n" % (self.pos, want, len(raw)))
        data = raw[delta:delta+length]
        self.pos += length
        return data

def main():
    iso = sys.argv[1]; out_path = sys.argv[2]
    serial = sys.argv[3] if len(sys.argv) > 3 else "c3ca0370"
    subprocess.run([ADB,"-s",serial,"shell","echo","ok"], capture_output=True)  # warm adb server
    f = RemoteFile(iso, serial)
    sys.stderr.write("iso size=%d\n" % f.size)
    game_offset = gx.find_game_offset(f, f.size)
    if game_offset is None: raise SystemExit("no GDFX magic")
    fsdata = gx._read_at(f, game_offset + 32*gx.SECTOR, 32)
    root_sector = gx._u32(fsdata, 20); root_size = gx._u32(fsdata, 24)
    sys.stderr.write("game_offset=0x%X root_sector=%d root_size=%d\n" % (game_offset, root_sector, root_size))
    found = gx.find_entry(f, game_offset, root_sector, root_size, "default.xex")
    if not found: raise SystemExit("default.xex not found")
    sector, length = found
    data_off = game_offset + sector*gx.SECTOR
    sys.stderr.write("default.xex: offset=0x%X length=%d (%.1f MiB)\n" % (data_off, length, length/1048576.0))
    # one efficient sector-aligned dd for the whole file
    nblk = math.ceil(length / 2048)
    cmd = "dd if='%s' bs=2048 skip=%d count=%d 2>/dev/null" % (iso, data_off//2048, nblk)
    with open(out_path, "wb") as o:
        r = subprocess.run([ADB,"-s",serial,"exec-out",cmd], stdout=o)
    # truncate to exact length
    with open(out_path, "r+b") as o:
        o.truncate(length)
    sys.stderr.write("wrote %s (%d bytes)\n" % (out_path, os.path.getsize(out_path)))

if __name__ == "__main__":
    main()
