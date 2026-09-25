"""How many Shader Model 3 zero tests could be skipped exactly? (research estimate)

  python tools/pc/zero_rule_coverage.py [SHADER_DIR] [--const-bits 24]
      [--tex-bounded/--no-tex-bounded] [--interp-bounded]

Every Xenos multiply is emulated with "+0 times anything (Inf, NaN) is +0":
min(|a|,|b|), a compare and a select - 27% of the Adreno instructions
(spirv_debug_ieee_multiply, 2026-09-25). If both operands are finite, the IEEE
product differs only in the sign of a zero result, and a*b + 0.0 gives +0
exactly - so the test could go wherever the translator can prove both operands
finite. This reads the microcode disassembly that --dump_shaders writes
(*.ucode.frag / *.ucode.vert) and follows a magnitude bound per register
component, |x| <= 2^e, through each shader:

- saturated results, fixed-point texture results (a draw-time check of the
  fetch formats), frc, sin, cos, compares: e = 0
- constants: e = --const-bits (a draw-time check that the shader's float
  constants are finite and below 2^const-bits)
- vertex fetches: bounded when the fetch format is fixed-point (known in the
  microcode), else unbounded
- interpolated pixel shader inputs, rsq, rcp, log, exp of an unbounded value,
  relative register addressing: unbounded
- mul: e_a + e_b; add: max + 1; mad: e_a + e_b + 1; dot products: + 2

Control flow is ignored (the passes repeat until no bound grows, so a value
changed in a loop becomes unbounded) - an estimate, not the translator's
proof. Prints the zero tests (per component, as the translator emits them)
per shader type and the share that has two bounded operands.
"""
import argparse
import collections
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
UNBOUNDED = 1000
FINITE_LIMIT = 127

LINE_RE = re.compile(r'^/\*\s*[\d.]+\s*\*/\s+(\(!?p0\)\s+)?(\S+)\s+(.*)$')
COISSUE_RE = re.compile(r'^\s+\+\s+(\(!?p0\)\s+)?(\S+)\s+(.*)$')
OPERAND_RE = re.compile(
    r'^(-)?(r|c|r_abs|c_abs|oC\d|oPts|oDepth|eA|eM\d|o|a0|aL|ps)?(\[[^\]]*\]|\d+)?(?:\.([xyzw01_]{1,4}))?$')
COMPONENTS = 'xyzw'

VECTOR_ZERO_RULE = {'mul', 'mad', 'dp4', 'dp3', 'dp2add'}
SCALAR_ZERO_RULE = {'muls', 'muls_prev', 'muls_prev2', 'mulsc'}
FIXED_POINT_VFETCH = ('8_8_8_8', '2_10_10_10', '10_11_11', '11_11_10', '16_16', '16_16_16_16',
                      '8_8', '_8')


def split_operands(text):
    text = text.split('//')[0]
    parts = []
    depth = 0
    current = ''
    for ch in text:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(current.strip())
            current = ''
        else:
            current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


class Analysis:
    def __init__(self, const_bits, tex_bounded, interp_bounded, pixel, taint=False):
        self.taint = taint
        self.const_bits = const_bits
        self.tex_bounded = tex_bounded
        self.bounds = collections.defaultdict(lambda: UNBOUNDED)
        self.ps = UNBOUNDED
        self.sites = 0
        self.bounded_sites = 0
        if not pixel:
            # Vertex shader registers start as 0 except the vertex index in r0.
            for r in range(128):
                for c in range(4):
                    self.bounds[(r, c)] = 0
            for c in range(4):
                self.bounds[(0, c)] = 32
        else:
            # Pixel shader registers start as the interpolators.
            for r in range(128):
                for c in range(4):
                    self.bounds[(r, c)] = 0 if interp_bounded else UNBOUNDED

    def source(self, text):
        """Per-component bounds (4) of a source operand."""
        m = OPERAND_RE.match(text.replace(' ', ''))
        if not m:
            return [UNBOUNDED] * 4
        kind, index, swizzle = m.group(2), m.group(3), m.group(4) or 'xyzw'
        swizzle = (swizzle + swizzle[-1] * 4)[:4]
        if kind in ('c', 'c_abs'):
            return [self.const_bits] * 4
        if kind in ('r', 'r_abs'):
            if index is None or index.startswith('['):
                if index and index[1:-1].isdigit():
                    reg = int(index[1:-1])
                else:
                    return [UNBOUNDED] * 4  # relative addressing
            else:
                reg = int(index)
            out = []
            for ch in swizzle:
                if ch in '01':
                    out.append(0)
                elif ch == '_':
                    out.append(0)
                else:
                    out.append(self.bounds[(reg, COMPONENTS.index(ch))])
            return out
        if kind == 'ps':
            return [self.ps] * 4
        return [UNBOUNDED] * 4

    def write(self, dest, values, saturate):
        if self.taint:
            # Only rcp/rsq/exp/log (and what is computed from them) can be
            # Inf; finite values stay finite (overflow ignored, as DXVK).
            values = [0 if v <= FINITE_LIMIT else UNBOUNDED for v in values]
        m = OPERAND_RE.match(dest.replace(' ', ''))
        if not m or m.group(2) != 'r' or m.group(3) is None or m.group(3).startswith('['):
            return
        reg = int(m.group(3))
        mask = m.group(4) or 'xyzw'
        for i, ch in enumerate(mask[:4]):
            if ch == '_':
                continue
            value = 0 if saturate else values[i]
            self.bounds[(reg, i)] = min(value, UNBOUNDED)

    def site(self, a, b):
        self.sites += 1
        if a <= FINITE_LIMIT and b <= FINITE_LIMIT:
            self.bounded_sites += 1

    def vector(self, op, operands):
        saturate = op.endswith('_sat')
        base = op[:-4] if saturate else op
        dest, srcs = operands[0], [self.source(o) for o in operands[1:]]
        mask = (OPERAND_RE.match(dest.replace(' ', '')) or [None] * 5)
        mask = (mask.group(4) if hasattr(mask, 'group') and mask.group(4) else 'xyzw')
        written = [i for i, ch in enumerate(mask[:4]) if ch != '_']
        def cap(v):
            return v if v <= FINITE_LIMIT else UNBOUNDED
        result = [UNBOUNDED] * 4
        if base == 'mul' and len(srcs) >= 2:
            for i in written:
                self.site(srcs[0][i], srcs[1][i])
            result = [cap(srcs[0][i] + srcs[1][i]) for i in range(4)]
        elif base == 'mad' and len(srcs) >= 3:
            for i in written:
                self.site(srcs[0][i], srcs[1][i])
            result = [cap(max(srcs[0][i] + srcs[1][i], srcs[2][i]) + 1) for i in range(4)]
        elif base in ('dp4', 'dp3', 'dp2add') and len(srcs) >= 2:
            n = {'dp4': 4, 'dp3': 3, 'dp2add': 2}[base]
            for i in range(n):
                self.site(srcs[0][i], srcs[1][i])
            e = max(srcs[0][i] + srcs[1][i] for i in range(n)) + 2
            if base == 'dp2add' and len(srcs) >= 3:
                e = max(e, srcs[2][0] + 1)
            result = [cap(e)] * 4
        elif base in ('add',) and len(srcs) >= 2:
            result = [cap(max(srcs[0][i], srcs[1][i]) + 1) for i in range(4)]
        elif base in ('max', 'min', 'max4') and srcs:
            result = [max(s[i] for s in srcs) for i in range(4)]
        elif base in ('seq', 'sgt', 'sge', 'sne', 'frc', 'kill_eq', 'kill_gt', 'kill_ge',
                      'kill_ne') or base.startswith('setp'):
            result = [0] * 4
        elif base in ('trunc', 'floor') and srcs:
            result = srcs[0]
        elif base in ('cndeq', 'cndge', 'cndgt') and len(srcs) >= 3:
            result = [max(srcs[1][i], srcs[2][i]) for i in range(4)]
        elif base == 'mov' and srcs:
            result = srcs[0]
        self.write(dest, result, saturate)

    def scalar(self, op, operands):
        saturate = op.endswith('_sat')
        base = op[:-4] if saturate else op
        dest, srcs = operands[0], [self.source(o)[0] for o in operands[1:]]
        a = srcs[0] if srcs else UNBOUNDED
        b = srcs[1] if len(srcs) > 1 else UNBOUNDED
        def cap(v):
            return v if v <= FINITE_LIMIT else UNBOUNDED
        if base in ('muls', 'mulsc'):
            self.site(a, b)
            value = cap(a + b)
        elif base in ('muls_prev', 'muls_prev2'):
            self.site(a, self.ps)
            value = cap(a + self.ps)
        elif base in ('adds', 'addsc', 'subs', 'subsc', 'adds_prev', 'subs_prev'):
            value = cap(max(a, b if base != 'adds_prev' else self.ps) + 1)
        elif base in ('maxs', 'mins', 'movs', 'floors', 'truncs', 'retain_prev'):
            value = self.ps if base == 'retain_prev' else a
        elif base in ('frcs', 'sin', 'cos', 'seqs', 'sgts', 'sges', 'snes') or base.startswith(
                ('setp', 'kills', 'kill')):
            value = 0
        elif base == 'sqrt':
            value = a // 2 + 1 if a <= FINITE_LIMIT else UNBOUNDED
        elif base == 'exp':
            value = cap(2 ** a if a < 7 else UNBOUNDED)
        elif base in ('logc', 'rcpc', 'rcpf', 'rsqc', 'rsqf'):
            value = FINITE_LIMIT  # clamped to FLT_MAX, finite
        else:
            value = UNBOUNDED  # rcp, rsq, log, anything else
        if saturate:
            value = 0
        self.ps = value
        self.write(dest, [value] * 4, saturate)

    def fetch(self, op, operands, text):
        if not operands:
            return
        if op.startswith('tfetch') or op.startswith('getWeights') or op.startswith('getCompTexLOD'):
            self.write(operands[0], [0 if self.tex_bounded else UNBOUNDED] * 4, False)
        elif op.startswith('vfetch'):
            fixed = 'format=' in text and any(f in text.split('format=')[1].split(',')[0]
                                              for f in FIXED_POINT_VFETCH) and 'FLOAT' not in text
            self.write(operands[0], [0 if (fixed or self.taint) else UNBOUNDED] * 4, False)


def analyze(path, args):
    pixel = path.endswith('.frag')
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    instructions = []
    for line in lines:
        m = LINE_RE.match(line)
        coissue = False
        if not m:
            m = COISSUE_RE.match(line)
            coissue = True
        if not m:
            continue
        op, rest = m.group(2), m.group(3)
        instructions.append((op, split_operands(rest), rest, coissue))
    analysis = Analysis(0 if args.taint else args.const_bits,
                        args.taint or not args.no_tex_bounded,
                        args.taint or args.interp_bounded, pixel, args.taint)
    # Repeat until no bound grows (loops); a growing bound becomes unbounded.
    previous = None
    for _ in range(4):
        analysis.sites = analysis.bounded_sites = 0
        for op, operands, rest, coissue in instructions:
            if op in ('exec', 'exece', 'alloc', 'loop', 'endloop', 'jmp', 'cjmp', 'call',
                      'ret', 'cexec', 'cexece', 'nop'):
                continue
            if op.startswith(('tfetch', 'vfetch', 'getWeights', 'getCompTexLOD', 'setTex',
                              'getGradients')):
                analysis.fetch(op, operands, rest)
            elif coissue:
                analysis.scalar(op, operands)
            elif op.startswith(('mul', 'mad', 'dp', 'add', 'max', 'min', 'seq', 'sgt', 'sge',
                                'sne', 'frc', 'trunc', 'floor', 'cnd', 'mov', 'kill', 'setp',
                                'cube', 'dst', 'maxa')) and not op.endswith(('s', 'sc')):
                analysis.vector(op, operands)
            else:
                analysis.scalar(op, operands)
        snapshot = dict(analysis.bounds)
        if previous is not None:
            grown = [k for k, v in snapshot.items() if v > previous.get(k, v)]
            for k in grown:
                analysis.bounds[k] = UNBOUNDED
            if not grown:
                break
        previous = snapshot
    return analysis.sites, analysis.bounded_sites


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('shaders', nargs='?',
                    default=os.path.join(ROOT, 'scratch', 'shader_lab', 'shaders'))
    ap.add_argument('--const-bits', type=int, default=24)
    ap.add_argument('--no-tex-bounded', action='store_true')
    ap.add_argument('--taint', action='store_true',
                    help='the hybrid: only values from rcp, rsq, exp, log can be Inf '
                         '(constants, textures, interpolators, vertex data finite; '
                         'overflow ignored) - the tests a hybrid would keep')
    ap.add_argument('--interp-bounded', action='store_true',
                    help='assume interpolated inputs are bounded (not provable)')
    args = ap.parse_args()
    totals = collections.defaultdict(lambda: [0, 0, 0])
    for path in sorted(glob.glob(os.path.join(args.shaders, '*.ucode.frag')) +
                       glob.glob(os.path.join(args.shaders, '*.ucode.vert'))):
        if '.bin.' in path:
            continue
        sites, bounded = analyze(path, args)
        kind = 'pixel' if path.endswith('.frag') else 'vertex'
        totals[kind][0] += 1
        totals[kind][1] += sites
        totals[kind][2] += bounded
    print('assumptions: constants < 2^%d, textures %s, interpolators %s' % (
        args.const_bits, 'unbounded' if args.no_tex_bounded else 'fixed-point (bounded)',
        'bounded' if args.interp_bounded else 'unbounded'))
    for kind, (count, sites, bounded) in totals.items():
        print('%-6s %4d shaders: %6d zero tests, %6d (%.1f%%) with both operands bounded' % (
            kind, count, sites, bounded, 100.0 * bounded / max(sites, 1)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
