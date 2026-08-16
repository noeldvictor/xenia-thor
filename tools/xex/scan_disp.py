#!/usr/bin/env python3
"""Scan whole XEX .text for ANY memory access (D-form AND DS-form) to given displacement(s).
D-form: opcode 32-47 (lwz/stw/lbz/stb/lhz/sth/lfs/stfs/lfd/stfd ...), disp = imm16 signed.
DS-form: opcode 58 (ld/ldu/lwa) and 62 (std/stdu/stq), disp = (imm16 & ~3) (low 2 bits = sub-op).
Usage: python scan_disp.py <file.xex> <disp_hex> [more...]"""
import sys, struct
import xex_disasm as X
from capstone import Cs, CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_MODE_32

DFORM = {32:'lwz',33:'lwzu',34:'lbz',35:'lbzu',36:'stw',37:'stwu',38:'stb',39:'stbu',
         40:'lhz',41:'lhzu',42:'lha',43:'lhau',44:'sth',45:'sthu',
         48:'lfs',49:'lfsu',50:'lfd',51:'lfdu',52:'stfs',53:'stfsu',54:'stfd',55:'stfdu'}
DS = {58:'ld/lwa',62:'std/stq'}

def main():
    path = sys.argv[1]
    disps = [int(d,16) for d in sys.argv[2:]]
    img, base, entry = X.load_xex(path)
    n = len(img) & ~3
    md = Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN + CS_MODE_32); md.detail=False
    hits=[]
    for off in range(0,n,4):
        word = struct.unpack_from('>I', img, off)[0]
        op = word >> 26
        if op in DFORM:
            d = word & 0xFFFF
            if d >= 0x8000: d -= 0x10000
            if d in disps:
                hits.append((base+off, word, DFORM[op]))
        elif op in DS:
            d = word & 0xFFFC
            if d >= 0x8000: d -= 0x10000
            if d in disps:
                hits.append((base+off, word, DS[op]))
    for addr,word,fam in hits:
        dec = next(md.disasm(struct.pack('>I',word), addr), None)
        txt = "%s %s" % (dec.mnemonic, dec.op_str) if dec else "(?)"
        print("%08X  [%-7s]  %s" % (addr, fam, txt))
    print("--- %d hits for disps %s ---" % (len(hits), [hex(d) for d in disps]))

if __name__=='__main__':
    main()
