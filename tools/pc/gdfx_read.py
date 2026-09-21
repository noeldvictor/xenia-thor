"""Read a file straight from an Xbox 360 disc image (GDFX), no emulator.

  python tools/pc/gdfx_read.py <iso> <path like bundle/50/685374> [offset] [length]

Prints the file's data offset, size, the first 32 bytes, and the same FNV-style
hash the NtReadFile trace logs, so a "wrong data" theory is a comparison of
two hashes. The GDFX layout: the volume descriptor at sector 32 of the game
partition ("MICROSOFT*XBOX*MEDIA"), a root directory (sector, size), and a
binary tree of entries per directory: left, right (uint16 each, in 4-byte
units), sector, size (uint32), attributes, name length, name.
"""
import struct
import sys

SECTOR = 2048
PARTITION_OFFSETS = [0, 0xFD90000, 0x2080000, 0x30600000, 0x10000]  # known XGD offsets


def find_partition(f):
    for base in PARTITION_OFFSETS:
        f.seek(base + 32 * SECTOR)
        magic = f.read(20)
        if magic == b'MICROSOFT*XBOX*MEDIA':
            return base
    raise SystemExit('no GDFX volume descriptor found')


def read_dir(f, base, sector, size):
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    entries = {}

    def walk(off):
        if off + 14 > len(data):
            return
        left, right, esector, esize, attrs, nlen = struct.unpack_from('<HHIIBB', data, off)
        if left == 0xFFFF:
            return
        name = data[off + 14: off + 14 + nlen].decode('latin-1')
        entries[name.lower()] = (esector, esize, attrs)
        if left:
            walk(left * 4)
        if right:
            walk(right * 4)

    walk(0)
    return entries


def resolve(f, base, path):
    f.seek(base + 32 * SECTOR + 20)
    root_sector, root_size = struct.unpack('<II', f.read(8))
    sector, size, attrs = root_sector, root_size, 0x10
    for part in [p for p in path.replace('\\', '/').split('/') if p]:
        entries = read_dir(f, base, sector, size)
        if part.lower() not in entries:
            raise SystemExit('not found: %s (have %d entries)' % (part, len(entries)))
        sector, size, attrs = entries[part.lower()]
    return sector, size, attrs


def fnv(data):
    h = 0
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    iso, path = sys.argv[1], sys.argv[2]
    offset = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    length = int(sys.argv[4]) if len(sys.argv) > 4 else 2048
    with open(iso, 'rb') as f:
        base = find_partition(f)
        sector, size, attrs = resolve(f, base, path)
        data_offset = base + sector * SECTOR
        f.seek(data_offset + offset)
        data = f.read(length)
    print('partition base %d, file sector %d size %d attrs %02X data_offset %d' % (base, sector, size, attrs, data_offset))
    print('read %d bytes at %d: head %s hash %016X' % (len(data), offset, data[:32].hex().upper(), fnv(data)))


if __name__ == '__main__':
    main()
