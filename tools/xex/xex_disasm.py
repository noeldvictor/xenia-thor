#!/usr/bin/env python3
"""Standalone XEX2 decompressor + PPC disassembler (device-free guest RE).
Handles BASIC-compressed + AES-128-CBC-encrypted retail XEX2 (the Mistwalker
titles BD/LO). Replicates xenia src/xenia/cpu/xex_module.cc exactly.
Usage: python xex_disasm.py <file.xex> <guest_hex_addr> [num_insts]
"""
import sys, struct, os, subprocess
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend
from capstone import Cs, CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_MODE_32

XEXLZX = os.path.join(os.path.dirname(__file__), "xexlzx.exe")

RETAIL_KEY = bytes([0x20,0xB1,0x85,0xA5,0x9D,0x28,0xFD,0xC3,0x40,0x58,0x3F,0xBB,0x08,0x96,0xBF,0x91])
DEVKIT_KEY = bytes(16)

def be32(b,o): return struct.unpack_from('>I', b, o)[0]
def be16(b,o): return struct.unpack_from('>H', b, o)[0]

def aes_ecb_dec(key, data):
    d = Cipher(algorithms.AES(key), modes.ECB(), backend=default_backend()).decryptor()
    return d.update(data) + d.finalize()

def aes_cbc_dec(key, data, iv=bytes(16)):
    d = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend()).decryptor()
    return d.update(data) + d.finalize()

def load_xex(path):
    xex = open(path,'rb').read()
    assert xex[:4]==b'XEX2', "not XEX2: %r" % xex[:4]
    header_size = be32(xex, 0x08)
    sec_off     = be32(xex, 0x10)
    hdr_count   = be32(xex, 0x14)
    base_addr, ffi_off, entry = 0x82000000, None, None
    for i in range(hdr_count):
        k = be32(xex, 0x18 + i*8); v = be32(xex, 0x18 + i*8 + 4)
        if k == 0x000003FF: ffi_off = v
        if k == 0x00010201: base_addr = v
        if k == 0x00010100: entry = v
    aes_key_enc = xex[sec_off+0x150 : sec_off+0x150+16]
    info_size = be32(xex, ffi_off); enc = be16(xex, ffi_off+4); comp = be16(xex, ffi_off+6)
    # decompressed image size = sum(page_count * 4096) over page descriptors
    pd_count = be32(xex, sec_off+0x180)
    image_size = 0
    for i in range(pd_count):
        v = be32(xex, sec_off+0x184 + i*24)
        image_size += (v & 0x0FFFFFFF) * 4096
    src = xex[header_size:]
    print("XEX: header_size=%#x sec_off=%#x base=%#x entry=%s enc=%d comp=%d image_size=%#x"
          % (header_size, sec_off, base_addr, hex(entry) if entry else "?", enc, comp, image_size))
    session = aes_ecb_dec(RETAIL_KEY, aes_key_enc)
    if comp in (0, 1):
        nblk = (info_size - 8)//8
        blocks = [(be32(xex, ffi_off+8+n*8), be32(xex, ffi_off+8+n*8+4)) for n in range(nblk)]
        img = build_basic(src, blocks, enc, comp, session)
    elif comp == 2:  # LZX (NORMAL)
        window_size = be32(xex, ffi_off+8)
        first_size = be32(xex, ffi_off+12)
        img = build_lzx(src, enc, session, first_size, window_size, image_size)
    else:
        raise SystemExit("comp_type %d unsupported" % comp)
    print("  session=%s img_len=%#x head=%s" % (session.hex(), len(img), img[:8].hex()))
    return img, base_addr, entry

def build_basic(src, blocks, enc, comp, session):
    image = bytearray()
    if comp == 1:
        total = sum(ds for ds,_ in blocks)
        plain = aes_cbc_dec(session, src[:total]) if enc == 1 else src[:total]
        pp = 0
        for ds, zs in blocks:
            image += plain[pp:pp+ds]; image += b'\x00'*zs; pp += ds
    else:
        image += aes_cbc_dec(session, src) if enc == 1 else src
    return bytes(image)

def build_lzx(src, enc, session, first_size, window_size, image_size):
    # 1. decrypt whole stream
    declen = len(src) & ~15
    dec = aes_cbc_dec(session, src[:declen]) if enc == 1 else src
    # 2. de-block: each block = [4b next_size][20b next_hash][chunks: 2b size + data ... 0]
    lzx = bytearray()
    p = 0; cur = first_size
    while cur:
        block_start = p
        next_size = be32(dec, p)
        pnext = block_start + cur
        p += 24
        while True:
            chunk = (dec[p] << 8) | dec[p+1]; p += 2
            if chunk == 0: break
            lzx += dec[p:p+chunk]; p += chunk
        p = pnext; cur = next_size
    # 3. LZX decompress via the standalone tool
    tmp_in = os.path.join(os.path.dirname(__file__), "_lzx_stream.bin")
    tmp_out = os.path.join(os.path.dirname(__file__), "_lzx_image.bin")
    open(tmp_in, "wb").write(bytes(lzx))
    r = subprocess.run([XEXLZX, tmp_in, tmp_out, str(window_size), str(image_size)],
                       capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    return open(tmp_out, "rb").read()

def disasm(img, base, addr, n):
    off = addr - base
    code = img[off:off + n*4]
    md = Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN + CS_MODE_32)
    print("--- disasm guest %#x (img off %#x) ---" % (addr, off))
    for ins in md.disasm(code, addr):
        print("  %08X: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))

if __name__ == '__main__':
    path = sys.argv[1]; addr = int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv)>3 else 40
    img, base, entry = load_xex(path)
    if img is None: raise SystemExit("decompress failed")
    disasm(img, base, addr, n)
