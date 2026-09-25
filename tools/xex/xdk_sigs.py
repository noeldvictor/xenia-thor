"""Library functions shared across titles: the XDK code linked into every XEX.

  python tools/xex/xdk_sigs.py TITLE=path.xex [TITLE=path.xex ...] [--min-insns 8]
      [--top 25] [--out scratch/xdk_sigs/census.json]

On the Xbox 360 the XDK libraries (the C runtime, D3D, XAudio, XMA, XAM
helpers) are linked statically into each title. A title built with the same
XDK version carries the same machine code for them, at other addresses. This
decrypts and decompresses each XEX (xex_disasm.load_xex), splits the image into
functions with its exception directory (.pdata: every function's start and
length), and hashes every function with the address fields masked: the targets
of b/bl, the immediates of lis, and the D-form immediates whose base register a
lis loaded in the same function. Functions with the same hash in several titles
are library code - the candidates for a native host version (a hot memcpy is
the same PPC in every title, so one ARM64 replacement serves all of them).

Prints the census (functions per title, shared by 2, 3, 4 titles) and the
largest shared functions with a guess of what they are (dcbz/dcbt memory
routines, VMX128 math, FPU, system calls). Get default.xex from an ISO with
.agents/skills/xenia-thor-ghidra-game-patch/scripts/gdfx_extract.py.
"""
import argparse
import collections
import contextlib
import hashlib
import io
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xex_disasm  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
# D-form opcodes whose 16-bit immediate can be the low half of an address.
D_FORM = {14, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
          48, 49, 50, 51, 52, 53, 54, 55}


def le16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def le32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def functions(img, base):
    """(start, length in bytes) from the PE exception directory."""
    pe = le32(img, 0x3C)
    assert img[pe:pe + 4] == b'PE\0\0', 'no PE header in the image'
    opt = pe + 24
    magic = le16(img, opt)
    dirs = opt + (96 if magic == 0x10B else 112)
    exc_va, exc_size = le32(img, dirs + 3 * 8), le32(img, dirs + 3 * 8 + 4)
    out = []
    for i in range(exc_size // 8):
        start, bits = struct.unpack_from('>II', img, exc_va + i * 8)
        length = ((bits >> 8) & 0x3FFFFF) * 4
        if base <= start < base + len(img) and length:
            out.append((start, length))
    return sorted(set(out))


def signature(words):
    """sha1 of the words with the address-dependent fields masked, and tags."""
    lis_regs = set()
    masked = []
    tags = collections.Counter()
    for w in words:
        op = w >> 26
        if op == 18:  # b, bl
            w &= 0xFC000003
            if w & 1:
                tags['calls'] += 1
        elif op == 15:  # addis; lis is addis rD, 0, imm
            if (w >> 16) & 0x1F == 0:
                lis_regs.add((w >> 21) & 0x1F)
            w &= 0xFFFF0000
        elif op in D_FORM and (w >> 16) & 0x1F in lis_regs:
            w &= 0xFFFF0000
        elif op == 31:
            xo = (w >> 1) & 0x3FF
            if xo == 1014:
                tags['dcbz'] += 1
            elif xo in (278, 246):
                tags['dcbt'] += 1
            elif xo in (20, 150, 84, 214):
                tags['atomic'] += 1
        elif op == 4:
            tags['vmx'] += 1
        elif op in (59, 63):
            tags['fpu'] += 1
        elif op == 17:
            tags['syscall'] += 1
        masked.append(w)
    digest = hashlib.sha1(struct.pack('>%dI' % len(masked), *masked)).hexdigest()[:16]
    return digest, tags


def guess(tags, insns):
    if tags['dcbz'] or tags['dcbt'] >= 2:
        return 'memory routine (dcbz/dcbt: memcpy/memset class)'
    if tags['syscall']:
        return 'system call wrapper'
    if tags['atomic']:
        return 'atomic / lock primitive'
    if tags['vmx'] > insns // 4:
        return 'VMX128 math'
    if tags['fpu'] > insns // 4:
        return 'FPU math'
    return ''


def load_seed_names(paths):
    """Guest address -> name from recomp tables: '0xADDR = { name = "X" }'
    (renut_funcs.toml, renut_gpu_funcs.toml) and 'X = 0xaddr' (renut_crt.toml)."""
    names = {}
    for path in paths:
        for line in open(path, encoding='utf-8', errors='replace'):
            line = line.split('#', 1)[0].strip()
            m = re.match(r'(0x[0-9A-Fa-f]+)\s*=\s*\{.*name\s*=\s*"([^"]+)"', line)
            if m:
                names[int(m.group(1), 16)] = m.group(2)
                continue
            m = re.match(r'([A-Za-z_][\w]*)\s*=\s*(0x[0-9A-Fa-f]+)$', line)
            if m and not m.group(1).endswith('_address'):
                names[int(m.group(2), 16)] = m.group(1)
    return names


def masked_prefix_word(w):
    op = w >> 26
    if op == 18:
        return w & 0xFC000003
    if op == 15:
        return w & 0xFFFF0000
    return w


def masked_prefix(img, base, address, count=16):
    """The first `count` instructions at address with branch targets and lis
    immediates masked (for functions without a .pdata entry)."""
    off = address - base
    if off < 0 or off + count * 4 > len(img):
        return None
    return tuple(masked_prefix_word(w)
                 for w in struct.unpack_from('>%dI' % count, img, off))


def first_word_index(img):
    """Masked word -> word offsets, for the prefix search."""
    words = struct.unpack_from('>%dI' % (len(img) // 4), img, 0)
    index = collections.defaultdict(list)
    for i, w in enumerate(words):
        index[masked_prefix_word(w)].append(i)
    return words, index


FINGERPRINTS = ('Unreal Engine 3', 'PhysX', 'Havok', 'Bink', 'FaceFX', 'SpeedTree', 'Scaleform',
                'CRI ', 'ADX', 'Wwise', 'FMOD', 'Miles', 'Gamebryo', 'RenderWare',
                'Microcode Compiler', 'deflate 1.')


def xdk_build(path):
    """The kernel import library version of the XEX header: the XDK build."""
    x = open(path, 'rb').read()
    for i in range(struct.unpack_from('>I', x, 0x14)[0]):
        key, value = struct.unpack_from('>II', x, 0x18 + i * 8)
        if key == 0x000103FF:
            str_size = struct.unpack_from('>I', x, value + 4)[0]
            p = value + 12 + str_size
            version = struct.unpack_from('>I', x, p + 28)[0]
            return (version >> 8) & 0xFFFF
    return 0


def fingerprints(img):
    """Engine and middleware names found in the image (2026-09-25: Blue Dragon
    is not Unreal; what it shares with Gears is the XDK's shader compiler)."""
    return [f.strip() for f in FINGERPRINTS if img.find(f.encode()) >= 0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('xex', nargs='+', help='TITLE=path.xex')
    ap.add_argument('--seed', default='',
                    help='TITLE=table.toml[,table.toml]: names of that title\'s functions '
                         '(a recomp\'s tables, such as reNut for Banjo) to find in the others')
    ap.add_argument('--min-insns', type=int, default=8)
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--out', default=os.path.join(ROOT, 'scratch', 'xdk_sigs', 'census.json'))
    args = ap.parse_args()
    by_hash = collections.defaultdict(dict)  # hash -> {title: (start, insns)}
    info = {}
    per_title = {}
    images = {}
    hash_at = collections.defaultdict(dict)  # title -> {start: hash}
    for item in args.xex:
        title, _, path = item.partition('=')
        with contextlib.redirect_stdout(io.StringIO()):
            img, base, _entry = xex_disasm.load_xex(path)
        images[title] = (img, base)
        funcs = functions(img, base)
        count = 0
        for start, length in funcs:
            off = start - base
            n = length // 4
            if n < args.min_insns or off + length > len(img):
                continue
            words = struct.unpack_from('>%dI' % n, img, off)
            digest, tags = signature(words)
            by_hash[digest].setdefault(title, (start, n))
            info.setdefault(digest, (n, tags))
            hash_at[title][start] = digest
            count += 1
        per_title[title] = count
        print('%-6s %6d functions of %d+ instructions (%d in .pdata), XDK %d: %s' % (
            title, count, args.min_insns, len(funcs), xdk_build(path),
            ', '.join(fingerprints(img)) or 'no known engine or middleware'))
    titles = list(per_title)
    shared = collections.Counter(len(v) for v in by_hash.values())
    print('\nshared by: ' + '  '.join('%d titles %d' % (k, shared[k])
                                      for k in range(len(titles), 1, -1)))
    for title in titles:
        mine = [h for h, v in by_hash.items() if title in v]
        common = [h for h in mine if len(by_hash[h]) > 1]
        insns = sum(info[h][0] for h in common)
        print('  %-6s %5d of %5d functions (%4.1f%%) also in another title, %d instructions' % (
            title, len(common), len(mine), 100.0 * len(common) / max(len(mine), 1), insns))
    rows = sorted(((len(v), info[h][0], h) for h, v in by_hash.items() if len(v) > 1),
                  reverse=True)
    print('\nlargest functions shared by the most titles:')
    for n_titles, insns, h in rows[:args.top]:
        where = ' '.join('%s:%08X' % (t, s) for t, (s, _) in sorted(by_hash[h].items()))
        print('  %d titles %5d insns  %-48s %s' % (n_titles, insns, guess(info[h][1], insns),
                                                 where))
    memory = [(len(v), info[h][0], h) for h, v in by_hash.items()
              if len(v) > 1 and info[h][1]['dcbz']]
    print('\nshared memory routines (dcbz): %d' % len(memory))
    for n_titles, insns, h in sorted(memory, reverse=True)[:10]:
        where = ' '.join('%s:%08X' % (t, s) for t, (s, _) in sorted(by_hash[h].items()))
        print('  %d titles %5d insns  %s' % (n_titles, insns, where))
    named = {}
    if args.seed:
        seed_title, _, tables = args.seed.partition('=')
        seed_names = load_seed_names([t for t in tables.split(',') if t])
        seed_img, seed_base = images[seed_title]
        indexes = {t: first_word_index(img) for t, (img, _) in images.items()
                   if t != seed_title}
        print('\nseed: %d named %s functions' % (len(seed_names), seed_title))
        for title in indexes:
            words, index = indexes[title]
            base = images[title][1]
            found = {}
            for address, name in seed_names.items():
                digest = hash_at[seed_title].get(address)
                if digest and title in by_hash.get(digest, {}):
                    found[name] = ('%08X' % by_hash[digest][title][0], 'function')
                    continue
                prefix = masked_prefix(seed_img, seed_base, address)
                if not prefix:
                    continue
                hits = [i for i in index.get(prefix[0], ())
                        if i + 16 <= len(words) and
                        all(masked_prefix_word(words[i + k]) == prefix[k] for k in range(1, 16))]
                if len(hits) == 1:
                    found[name] = ('%08X' % (base + hits[0] * 4), 'prefix')
            named[title] = found
            gpu = sorted(n for n in found if n.startswith(('D3D', 'Direct3D')))
            print('  %-6s %4d of %d names found (%d D3D): %s' % (
                title, len(found), len(seed_names), len(gpu), ' '.join(gpu[:12])))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    json.dump({'titles': per_title, 'named': named,
               'shared': {h: {'insns': info[h][0], 'tags': dict(info[h][1]),
                              'where': {t: '%08X' % s for t, (s, _) in v.items()}}
                          for h, v in by_hash.items() if len(v) > 1}},
              open(args.out, 'w'), indent=1)
    print('\nresult', os.path.relpath(args.out, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
