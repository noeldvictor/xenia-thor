#!/usr/bin/env python3
"""Scan the whole XEX .text for D-form stores (and any instr) to a given displacement.
Reuses xex_disasm.load_xex(). Reports guest addr + decoded instr for each hit.
Usage: python scan_stores.py <file.xex> <disp_hex> [more disps...]
"""
import sys, struct
import xex_disasm as X
from capstone import Cs, CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_MODE_32

# D-form store primary opcodes
STORE_OPS = {36:'stw',37:'stwu',38:'stb',39:'stbu',44:'sth',45:'sthu'}
# D-form load primary opcodes (to see who reads too, optional)
LOAD_OPS  = {32:'lwz',33:'lwzu',34:'lbz',35:'lbzu',40:'lhz',41:'lhzu',42:'lha',43:'lhau'}

def main():
    path = sys.argv[1]
    disps = [int(d,16) for d in sys.argv[2:]]
    img, base, entry = X.load_xex(path)
    n = len(img) & ~3
    md = Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN + CS_MODE_32)
    md.detail = False
    hits = []
    for off in range(0, n, 4):
        word = struct.unpack_from('>I', img, off)[0]
        op = word >> 26
        disp = word & 0xFFFF
        if disp in disps and (op in STORE_OPS or op in LOAD_OPS):
            kind = STORE_OPS.get(op) or LOAD_OPS.get(op)
            is_store = op in STORE_OPS
            addr = base + off
            hits.append((addr, word, kind, is_store, disp))
    # decode each hit
    for addr, word, kind, is_store, disp in hits:
        code = struct.pack('>I', word)
        dec = next(md.disasm(code, addr), None)
        txt = "%s %s" % (dec.mnemonic, dec.op_str) if dec else "(?)"
        tag = "STORE" if is_store else "load "
        print("%08X  %-5s disp=%#06x  %s" % (addr, tag, disp, txt))
    print("--- %d hits across disps %s ---" % (len(hits), [hex(d) for d in disps]))

if __name__ == '__main__':
    main()
