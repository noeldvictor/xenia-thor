#!/usr/bin/env python3
"""List a directory inside an Xbox 360 XDVDFS (GDFX) ISO, reading only the few
sectors it needs, over adb.

WHY THIS EXISTS: "NtCreateFile failed ... OBJECT_NAME_NOT_FOUND" cannot tell you
whether the file is missing from the disc or merely invisible to our VFS, and
those two need opposite work. This answers it from the RAW IMAGE, independently
of the emulator - so it is ground truth rather than a second opinion from the
same code that already failed.

It reads a handful of 2 KB sectors, not the 7.8 GB image.

XDVDFS layout, for the next reader:
  * volume descriptor sector holds "MICROSOFT*XBOX*MEDIA" at offset 0, then
    root directory sector (u32) and root directory size (u32) at 0x14/0x18.
  * the descriptor sits at image offset (base + 32*2048). `base` is 0 for a
    plain .iso, but Xbox 360 images are often "video partition" images where
    the game partition starts at 0xFD90000.
  * a directory is a tree of entries, each: left u16, right u16 (offsets in
    4-byte units within the directory), sector u32, size u32, attrs u8,
    name_len u8, name bytes. Entries are padded to 4-byte alignment.
"""
import subprocess, struct, sys

ADB = "C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe"
DEV = "192.168.1.33:5555"
SECTOR = 2048
BASES = [0, 0xFD90000]  # plain image, then the video-partition offset


def read_sectors(path, sector, size, base):
    """Read `size` bytes starting at `sector` within the partition at `base`.

    Sector-aligned by construction, which is why this does not slice: an
    earlier version computed a byte offset and sliced, and got it wrong.
    """
    off = base + sector * SECTOR
    skip = off // SECTOR
    count = max(1, (size + SECTOR - 1) // SECTOR)
    cmd = [ADB, "-s", DEV, "exec-out",
           "dd if='%s' bs=%d skip=%d count=%d 2>/dev/null" % (path, SECTOR, skip, count)]
    return subprocess.run(cmd, capture_output=True, timeout=180).stdout


def find_base(path):
    # Sector 32 of the partition holds the volume descriptor. Xbox 360 images
    # are usually "video partition" images where the game partition starts at
    # 0xFD90000 - Gears of War is one of those.
    for base in BASES:
        d = read_sectors(path, 32, 64, base)
        if d[:20] == b"MICROSOFT*XBOX*MEDIA":
            return base, struct.unpack_from("<II", d, 0x14)
    return None, None


def read_dir(path, base, sector, size):
    """Walk a directory's entry tree, returning [(name, sector, size, attrs)]."""
    raw = read_sectors(path, sector, size, base)
    out = []
    stack = [0]
    seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off * 4 + 14 > len(raw):
            continue
        seen.add(off)
        p = off * 4
        left, right, sec, sz = struct.unpack_from("<HHII", raw, p)
        attrs = raw[p + 12]
        nlen = raw[p + 13]
        name = raw[p + 14:p + 14 + nlen].decode("latin-1", "replace")
        if name:
            out.append((name, sec, sz, attrs))
        for nxt in (left, right):
            if nxt and nxt != 0xFFFF:
                stack.append(nxt)
    return out


def main():
    iso = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) > 2 else ""
    base, root = find_base(iso)
    if base is None:
        print("NOT an XDVDFS image (no MICROSOFT*XBOX*MEDIA at either base)")
        return 1
    print("base=0x%X root_sector=%d root_size=%d" % (base, root[0], root[1]))
    sector, size = root
    walked = []
    for part in [p for p in want.split("/") if p]:
        entries = read_dir(iso, base, sector, size)
        match = [e for e in entries if e[0].lower() == part.lower()]
        if not match:
            print("  '%s' NOT FOUND under /%s" % (part, "/".join(walked)))
            print("  siblings:", ", ".join(sorted(e[0] for e in entries))[:400])
            return 2
        walked.append(match[0][0])
        sector, size = match[0][1], match[0][2]
    entries = read_dir(iso, base, sector, size)
    print("/%s contains %d entries:" % ("/".join(walked), len(entries)))
    for name, sec, sz, attrs in sorted(entries):
        print("   %-40s %10d bytes%s" % (name, sz, "  <DIR>" if attrs & 0x10 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
