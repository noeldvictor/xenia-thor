import sys
from capstone import Cs, CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_MODE_32
img = open("bd_image.bin","rb").read()
base = 0x82000000
md = Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN + CS_MODE_32)
for a in [int(x,16) for x in sys.argv[1:]]:
    off = a - base
    print("=== guest_%08X (off %#x) ===" % (a, off))
    if off < 0 or off+88 > len(img):
        print("  (out of range, img size %#x)" % len(img)); continue
    for ins in md.disasm(img[off:off+88], a):
        print("  %08X: %-7s %s" % (ins.address, ins.mnemonic, ins.op_str))
