"""PowerPC disassembly of a title's code from a dump on the PC, with the
function names of its static recompilation. No device, no launch.

  python tools/thor/guest_disasm_offline.py <dump.bin> <base_hex> <addr>[:count] ...
  python tools/thor/guest_disasm_offline.py --name 821E3080 8223FDE0     (names only)

The dump is guest memory from xenia_guest_dump (base = its first address);
names come from reference/reNut/config/renut_funcs.toml (Banjo-Kazooie:
Nuts & Bolts) when present: every line is "addr: bytes  mnemonic operands"
with the enclosing function's name and offset, and branch targets named.
Capstone's PPC engine (pip: capstone) reads the big-endian words as the
console does (2026-09-21).
"""
import bisect
import os
import re
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RENUT_FUNCS = os.path.join(ROOT, '..', 'reference', 'reNut', 'config', 'renut_funcs.toml')


def load_names():
    names = {}
    if not os.path.exists(RENUT_FUNCS):
        return names, []
    for line in open(RENUT_FUNCS, encoding='utf-8', errors='replace'):
        m = re.match(r'\s*0x([0-9A-Fa-f]+)\s*=\s*\{(.*)\}', line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        n = re.search(r'name\s*=\s*"([^"]+)"', m.group(2))
        names[addr] = n.group(1) if n else 'sub_%08X' % addr
    return names, sorted(names)


def name_of(addr, names, starts):
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return '%08X' % addr
    start = starts[i]
    if addr - start > 0x20000:
        return '%08X' % addr
    off = addr - start
    return names[start] + ('+%X' % off if off else '')


def main():
    args = sys.argv[1:]
    names, starts = load_names()
    if args and args[0] == '--name':
        for a in args[1:]:
            addr = int(a.split(':')[0], 16)
            print('%08X %s' % (addr, name_of(addr, names, starts)))
        return 0
    if len(args) < 3:
        print(__doc__)
        return 2
    dump, base = args[0], int(args[1], 16)
    data = open(dump, 'rb').read()
    try:
        from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN
    except ImportError:
        print('pip install capstone')
        return 2
    md = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)
    for spec in args[2:]:
        addr_s, _, count_s = spec.partition(':')
        addr = int(addr_s, 16)
        count = int(count_s) if count_s else 16
        off = addr - base
        if off < 0 or off + count * 4 > len(data):
            print('== %08X: outside the dump' % addr)
            continue
        print('== %08X (%s)' % (addr, name_of(addr, names, starts)))
        code = data[off:off + count * 4]
        # One word at a time: capstone stops at the first word it cannot
        # decode (the VMX128 forms), and a whole-block decode then showed
        # every later instruction as raw data (2026-09-22).
        for i in range(count):
            word_addr = addr + i * 4
            insns = list(md.disasm(code[i * 4:i * 4 + 4], word_addr))
            if not insns:
                word = struct.unpack_from('>I', code, i * 4)[0]
                print('  %08X: %08X  .long   (vmx128?)' % (word_addr, word))
                continue
            insn = insns[0]
            ops = insn.op_str
            m = re.search(r'0x([0-9a-fA-F]{8})', ops)
            note = ''
            if m and insn.mnemonic.startswith('b'):
                note = '   ; ' + name_of(int(m.group(1), 16), names, starts)
            print('  %08X: %s  %-8s %s%s' % (insn.address, insn.bytes.hex().upper(), insn.mnemonic, ops, note))
    return 0


if __name__ == '__main__':
    sys.exit(main())
