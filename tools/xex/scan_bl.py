#!/usr/bin/env python3
"""Scan the whole XEX .text for I-form branches (b/bl/ba/bla) whose target == a given guest addr.
Also resolves bctrl/bctr indirect through a preceding lis/addi+mtctr that loads the thunk addr.
Usage: python scan_bl.py <file.xex> <target_hex> [more targets...]
"""
import sys, struct
import xex_disasm as X
from capstone import Cs, CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_MODE_32

def sext(v, bits):
    m = 1 << (bits - 1)
    return (v ^ m) - m

def main():
    path = sys.argv[1]
    targets = set(int(t, 16) for t in sys.argv[2:])
    img, base, entry = X.load_xex(path)
    n = len(img) & ~3
    hits = []
    for off in range(0, n, 4):
        word = struct.unpack_from('>I', img, off)[0]
        op = word >> 26
        if op != 18:
            continue
        li_field = (word >> 2) & 0xFFFFFF   # 24-bit
        aa = (word >> 1) & 1
        lk = word & 1
        addr = base + off
        disp = sext(li_field << 2, 26)
        if aa:
            tgt = disp & 0xFFFFFFFF
        else:
            tgt = (addr + disp) & 0xFFFFFFFF
        if tgt in targets:
            hits.append((addr, word, tgt, lk, aa))
    md = Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN + CS_MODE_32)
    for addr, word, tgt, lk, aa in hits:
        kind = ('bl' if lk else 'b') + ('a' if aa else '')
        print("%08X  %s 0x%08X   (lk=%d aa=%d word=%08X)" % (addr, kind, tgt, lk, aa, word))
    print("--- %d I-form branch hits to %s ---" % (len(hits), [hex(t) for t in targets]))

if __name__ == '__main__':
    main()
