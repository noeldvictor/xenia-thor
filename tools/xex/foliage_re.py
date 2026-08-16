import struct
img = open('bd_image.bin','rb').read()
base = 0x82000000
def be32(o): return struct.unpack_from('>I', img, o)[0]
def rd_str(addr):
    o = addr - base
    if o < 0 or o >= len(img): return None
    end = img.find(b'\x00', o)
    if end < 0: return None
    return img[o:end].decode('latin1', 'replace')
def basename(s):
    return s.replace(chr(92), '/').split('/')[-1]
def looks_like_entry(addr):
    vs = be32(addr-base); ps = be32(addr+4-base)
    if not (0x82060000 <= vs <= 0x82090000): return False
    if not (0x82060000 <= ps <= 0x82090000): return False
    s1 = rd_str(vs); s2 = rd_str(ps)
    return bool(s1) and ('vs' in s1 or 'cs' in s1) and bool(s2) and ('ps' in s2)
start = 0x827723dc
while looks_like_entry(start - 20): start -= 20
end = 0x827723dc
while looks_like_entry(end + 20): end += 20
print("effect table: start=%08X end=%08X count=%d" % (start, end+20, (end-start)//20 + 1))
a=start; idx=0
while a <= end:
    vs=be32(a-base); ps=be32(a+4-base)
    s1=rd_str(vs) or ""; s2=rd_str(ps) or ""
    print("  [%2d] %08X cat=%2d var=%d flag=%d  vs=%-26s ps=%s" % (
        idx, a, be32(a+8-base), be32(a+12-base), be32(a+16-base), basename(s1), basename(s2)))
    a += 20; idx+=1
