#!/usr/bin/env python3
"""Extract a file (e.g. default.xex) from an Xbox 360 GDFX/XGD disc image.

Mirrors the fork's own GDFX parser (src/xenia/vfs/devices/disc_image_device.cc):
find the "MICROSOFT*XBOX*MEDIA" magic at sector 32 of the game partition, read
the root directory sector/size, then walk the on-disc binary tree to locate the
entry by name and copy its bytes out. Random-access (seek/read) so it never
loads the multi-GB image into memory.

Usage:
    python gdfx_extract.py <image.iso> [filename] [out_path]

Defaults: filename=default.xex, out_path=./<filename>. Pass a name like
"default.xex" (root-dir lookup; case-insensitive).
"""

import struct
import sys

SECTOR = 2048
ATTR_DIRECTORY = 0x10
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Same probe offsets the engine's DiscImageDevice::Verify tries.
LIKELY_OFFSETS = [0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000]


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _read_at(f, offset, length):
    f.seek(offset)
    return f.read(length)


def find_game_offset(f, size):
    for game_offset in LIKELY_OFFSETS:
        probe = game_offset + 32 * SECTOR
        if probe + len(MAGIC) > size:
            continue
        if _read_at(f, probe, len(MAGIC)) == MAGIC:
            return game_offset
    return None


def find_entry(f, game_offset, dir_sector, dir_size, want_name):
    """Walk a directory's GDFX tree; return (sector, length) for want_name."""
    base = game_offset + dir_sector * SECTOR
    data = _read_at(f, base, dir_size)
    want = want_name.lower()
    stack = [0]
    seen = set()
    while stack:
        ordinal = stack.pop()
        if ordinal in seen:
            continue
        seen.add(ordinal)
        off = ordinal * 4
        if off + 14 > len(data):
            continue
        node_l = _u16(data, off + 0)
        node_r = _u16(data, off + 2)
        sector = _u32(data, off + 4)
        length = _u32(data, off + 8)
        attributes = data[off + 12]
        name_length = data[off + 13]
        name = data[off + 14:off + 14 + name_length].decode("latin-1", "replace")
        if not (attributes & ATTR_DIRECTORY) and name.lower() == want:
            return sector, length
        if node_l:
            stack.append(node_l)
        if node_r:
            stack.append(node_r)
    return None


def extract(image_path, filename, out_path):
    with open(image_path, "rb") as f:
        f.seek(0, 2)
        size = f.tell()
        game_offset = find_game_offset(f, size)
        if game_offset is None:
            raise SystemExit("No GDFX magic found — not an Xbox 360 disc image?")
        fs = _read_at(f, game_offset + 32 * SECTOR, 32)
        root_sector = _u32(fs, 20)
        root_size = _u32(fs, 24)
        sys.stderr.write(
            "game_offset=0x%X root_sector=%d root_size=%d\n"
            % (game_offset, root_sector, root_size))
        found = find_entry(f, game_offset, root_sector, root_size, filename)
        if not found:
            raise SystemExit("'%s' not found in the disc root." % filename)
        sector, length = found
        data_offset = game_offset + sector * SECTOR
        sys.stderr.write(
            "%s: offset=0x%X length=%d (%.1f MiB)\n"
            % (filename, data_offset, length, length / 1048576.0))
        remaining = length
        with open(out_path, "wb") as out:
            f.seek(data_offset)
            while remaining > 0:
                chunk = f.read(min(1 << 20, remaining))
                if not chunk:
                    raise SystemExit("Unexpected EOF reading file data.")
                out.write(chunk)
                remaining -= len(chunk)
        sys.stderr.write("wrote %s\n" % out_path)


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    image_path = argv[1]
    filename = argv[2] if len(argv) > 2 else "default.xex"
    out_path = argv[3] if len(argv) > 3 else filename
    extract(image_path, filename, out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
