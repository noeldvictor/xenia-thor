"""Which shaders cost the Thor's GPU the most in a real frame?

  python tools/pc/frame_cost.py TRACE.xtr [--cvars "a=1"] [--top 20]
      [--label NAME] [--no-thor-profile] [--reuse]

Replays the trace on the Vulkan trace dump under renderdoccmd (with the Thor's
GPU settings, backend_ab.thor_profile_cvars, unless --no-thor-profile; --cvars
adds or overrides), then in headless RenderDoc (tools/renderdoc/rd_frame_cost.py)
reads per draw the vertex and fragment shader invocations (pipeline
statistics) and the SPIR-V of both stages, and compiles every module on the
Adreno 740 compiler (tools/turnip/shader_lab.py). Cost of a draw in
instruction-invocations: vertex invocations x (vertex + binning-pass vertex
instructions - the tiler runs the position part twice) + fragment invocations
x fragment instructions; texture samples (cat5) are counted the same way.

Prints the frame totals and the shaders and draws with the largest share - the
ALU and sampler work the Adreno does for the frame, by shader. The PC's own
GPU time per draw (RenderDoc) is listed but is an NVIDIA number. --reuse skips
the capture and the RenderDoc pass when scratch/frame_cost/<label>/ has them.

Each draw's specialization constants (the zero rule interpolators, bool and
texture sign words) are baked into a copy of its modules (spirv-opt
--set-spec-const-default-value, in WSL) before the shader lab compiles them,
so a shader is costed as the pipeline the draw used.

Transfer shaders that only write the stencil bit by bit (no output variable;
NVIDIA has no VK_EXT_shader_stencil_export) are left out and listed apart:
Turnip has the extension, so the Thor writes the stencil in the depth transfer
draw. --no-thor-profile keeps them.

2026-10-01: Gears 21135 - 150 M fragment invocations a frame on the PC, half of
the fragment work in stencil-bit draws the Thor does not run.
"""
import argparse
import collections
import hashlib
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

ROOT = backend_ab.ROOT
OUT = os.path.join(ROOT, 'scratch', 'frame_cost')
RENDERDOCCMD = r'C:\Program Files\RenderDoc\renderdoccmd.exe'


def module_kind(path):
    """'guest' for a translated guest shader; for xenia's EDRAM transfer
    shaders (OpName xe_transfer_*) 'transfer', or 'stencil bit' when the
    module has no output variable - one of the 8 draws per depth transfer that
    write the stencil bit by bit without VK_EXT_shader_stencil_export. NVIDIA
    lacks the extension; Turnip has it, so the Thor writes the stencil in the
    depth transfer draw and these draws do not exist there."""
    raw = open(path, 'rb').read()
    if b'xe_transfer_' not in raw:
        return 'guest'
    words = struct.unpack('<%dI' % (len(raw) // 4), raw[:len(raw) // 4 * 4])
    i = 5
    while i < len(words):
        opcode, count = words[i] & 0xFFFF, words[i] >> 16
        if opcode == 59 and count >= 4 and words[i + 3] == 3:  # OpVariable, Output
            return 'transfer'
        i += max(count, 1)
    return 'stencil bit'


def to_wsl(path):
    path = os.path.abspath(path).replace(chr(92), '/')
    return '/mnt/%s%s' % (path[0].lower(), path[2:])


def bake_specializations(shaders, pairs):
    """For each (module, "id=value;...") a copy of the module with those
    specialization constant values as the defaults (spirv-opt in WSL), named
    <module>_S<hash>: the shader lab compiles it as the pipeline the draw used
    (the zero rule, bool and texture sign constants fold). Returns
    {(module, spec): variant name}."""
    names, script = {}, []
    for module, spec in sorted(pairs):
        tag = hashlib.sha1(spec.encode()).hexdigest()[:8].upper()
        variant = module.replace('.vulkan.bin.', '_S%s.vulkan.bin.' % tag)
        names[(module, spec)] = variant
        if not os.path.exists(os.path.join(shaders, variant)):
            values = ' '.join(kv.replace('=', ':') for kv in spec.split(';'))
            script.append('spirv-opt --set-spec-const-default-value="%s" %s -o %s' % (
                values, to_wsl(os.path.join(shaders, module)),
                to_wsl(os.path.join(shaders, variant))))
    if script:
        path = os.path.join(shaders, 'bake.sh')
        open(path, 'w', newline=chr(10)).write(chr(10).join(script) + chr(10))
        subprocess.run(['wsl', 'bash', to_wsl(path)], stdin=subprocess.DEVNULL,
                       capture_output=True, env=dict(os.environ, MSYS_NO_PATHCONV='1'))
    return {k: v for k, v in names.items() if os.path.exists(os.path.join(shaders, v))}


def capture(trace, out, cvars):
    exe = os.path.join(backend_ab.BIN, 'xenia-gpu-vulkan-trace-dump.exe')
    for f in os.listdir(out):
        if f.endswith('.rdc'):
            os.remove(os.path.join(out, f))
    subprocess.run([RENDERDOCCMD, 'capture', '-w', '-c', os.path.join(out, 'cap'), exe,
                    '--target_trace_file=' + os.path.abspath(trace),
                    '--trace_dump_path=' + out, '--log_file=' + os.path.join(out, 'dump.log')] +
                   ['--' + c for c in cvars], stdin=subprocess.DEVNULL,
                   capture_output=True, timeout=900)
    rdcs = [f for f in os.listdir(out) if f.endswith('.rdc')]
    if not rdcs:
        sys.exit('no RenderDoc capture in %s' % out)
    return os.path.join(out, rdcs[0])


def renderdoc_pass(rdc, shaders):
    script = os.path.join(ROOT, 'tools', 'renderdoc', 'rd_frame_cost.py')
    r = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                        '-File', os.path.join(ROOT, 'tools', 'renderdoc', 'run.ps1'),
                        '-Script', script, '-Rdc', rdc, shaders, '-TimeoutSec', '1200'],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=1500)
    if 'ERROR' in r.stdout or not os.path.exists(os.path.join(shaders, 'draws.tsv')):
        sys.exit('RenderDoc pass failed:\n' + r.stdout[-3000:])


def shader_lab(shaders, label):
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'turnip', 'shader_lab.py'),
                        '--shaders', shaders, '--label', label],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=3600)
    path = os.path.join(ROOT, 'scratch', 'shader_lab', label + '.json')
    if not os.path.exists(path):
        sys.exit('shader lab failed:\n' + (r.stdout + r.stderr)[-3000:])
    return json.load(open(path))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--top', type=int, default=20)
    ap.add_argument('--label', default='')
    ap.add_argument('--no-thor-profile', action='store_true')
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    label = args.label or os.path.splitext(os.path.basename(args.trace))[0]
    out = os.path.join(OUT, label)
    shaders = os.path.join(out, 'shaders')
    os.makedirs(shaders, exist_ok=True)
    cvars = [] if args.no_thor_profile else backend_ab.thor_profile_cvars(include_vrs=False)
    extra = [c for c in args.cvars.split() if c]
    names = {c.split('=')[0] for c in extra}
    cvars = backend_ab.backend_cvars('vulkan', [c for c in cvars if c.split('=')[0] not in names] + extra)

    draws_tsv = os.path.join(shaders, 'draws.tsv')
    if not (args.reuse and os.path.exists(draws_tsv)):
        rdc = capture(args.trace, out, cvars)
        renderdoc_pass(rdc, shaders)
    draw_lines = open(draws_tsv).read().splitlines()[1:]
    pairs = set()
    for line in draw_lines:
        cols = line.split(chr(9))
        if len(cols) >= 10:
            for module, spec in ((cols[1], cols[8]), (cols[2], cols[9])):
                if module != '-' and spec != '-':
                    pairs.add((module, spec))
    baked = bake_specializations(shaders, pairs) if pairs else {}
    lab = shader_lab(shaders, 'fc_' + label)
    stats = {}
    for row in lab['rows']:
        if row['stats']:
            stats[row['name']] = row

    def instr(module, key='Instruction Count'):
        if module == '-':
            return 0, 0
        name = module.replace('shader_', '').replace('.vulkan.bin', '')
        row = stats.get(name)
        if not row:
            return None, None
        own = row['stats'].get(key, 0)
        binning = row.get('binning_stats', {}).get(key, 0)
        return own, binning

    kinds = {}

    def kind(module):
        if module == '-':
            return 'guest'
        if module not in kinds:
            kinds[module] = module_kind(os.path.join(shaders, module))
        return kinds[module]

    per_shader = collections.defaultdict(lambda: collections.Counter())
    rows, missing = [], set()
    totals = collections.Counter()
    by_kind = collections.Counter()
    pc_only = collections.Counter()
    for line in draw_lines:
        cols = line.split(chr(9))
        eid, vs, ps, vs_inv, ps_inv, indices, instances, pc_us = cols[:8]
        if len(cols) >= 10:
            vs = baked.get((vs, cols[8]), vs)
            ps = baked.get((ps, cols[9]), ps)
        vs_inv, ps_inv = max(int(vs_inv), 0), max(int(ps_inv), 0)
        vi, vb = instr(vs)
        pi, _ = instr(ps)
        vt, vtb = instr(vs, 'cat5 instructions')
        pt, _ = instr(ps, 'cat5 instructions')
        if vi is None:
            missing.add(vs)
            vi = vb = vt = vtb = 0
        if pi is None:
            missing.add(ps)
            pi = pt = 0
        v_cost = vs_inv * (vi + vb)
        p_cost = ps_inv * pi
        tex = vs_inv * (vt + vtb) + ps_inv * pt
        k = kind(ps)
        if k == 'stencil bit' and not args.no_thor_profile:
            # Not on the Thor (stencil export): listed apart, not in the totals.
            pc_only.update(draws=1, cost=v_cost + p_cost, ps_inv=ps_inv, pc_us=float(pc_us))
            continue
        by_kind[k] += v_cost + p_cost
        totals.update(vs_inv=vs_inv, ps_inv=ps_inv, v_cost=v_cost, p_cost=p_cost, tex=tex,
                      pc_us=float(pc_us), draws=1)
        rows.append((v_cost + p_cost, int(eid), vs, ps, vs_inv, ps_inv, vi, pi, float(pc_us)))
        if vs != '-':
            per_shader[vs].update(cost=v_cost, inv=vs_inv, tex=vs_inv * (vt + vtb), draws=1)
        if ps != '-':
            per_shader[ps].update(cost=p_cost, inv=ps_inv, tex=ps_inv * pt, draws=1)
    total = totals['v_cost'] + totals['p_cost']
    print('%s: %d draws, %.1f M vertex and %.1f M fragment invocations; %.2f G instruction-'
          'invocations (vertex %.0f%%, fragment %.0f%%), %.2f G texture samples; PC GPU %.1f ms' % (
              label, totals['draws'], totals['vs_inv'] / 1e6, totals['ps_inv'] / 1e6, total / 1e9,
              100.0 * totals['v_cost'] / max(total, 1), 100.0 * totals['p_cost'] / max(total, 1),
              totals['tex'] / 1e9, totals['pc_us'] / 1000.0))
    print('by kind: %s' % ', '.join('%s %.0f%%' % (k, 100.0 * v / max(total, 1))
                                    for k, v in by_kind.most_common()))
    if pc_only['draws']:
        print('left out (PC only): %d stencil-bit transfer draws, %.1f M fragment invocations, '
              '%.2f G instruction-invocations, PC GPU %.1f ms - the Thor writes the stencil in '
              'the depth transfer draw (VK_EXT_shader_stencil_export)' % (
                  pc_only['draws'], pc_only['ps_inv'] / 1e6, pc_only['cost'] / 1e9,
                  pc_only['pc_us'] / 1000.0))
    if missing:
        print('modules the shader lab did not compile (cost 0): %d' % len(missing))
    print('\nshaders by share of the instruction-invocations:')
    print('  %-28s %6s %6s %9s %7s %7s %6s  %s' % ('module', 'share', 'draws', 'inv M', 'instr',
                                                     'tex', 'waves', 'kind'))
    for module, c in sorted(per_shader.items(), key=lambda kv: -kv[1]['cost'])[:args.top]:
        name = module.replace('shader_', '').replace('.vulkan.bin', '')
        row = stats.get(name, {})
        s = row.get('stats', {})
        print('  %-28s %5.1f%% %6d %9.2f %7s %7s %6s  %s' % (
            name, 100.0 * c['cost'] / max(total, 1), c['draws'], c['inv'] / 1e6,
            int(s.get('Instruction Count', 0)) if s else '?',
            int(s.get('cat5 instructions', 0)) if s else '?',
            int(s.get('Max Waves Per Core', 0)) if s else '?', kind(module)))
    print('\ndraws by share:')
    for cost, eid, vs, ps, vs_inv, ps_inv, vi, pi, pc_us in sorted(rows, reverse=True)[:args.top]:
        print('  eid %5d %5.1f%%  ps %-24s %9d frag x %4d  vs %-24s %7d vert x %4d  pc %.0f us' % (
            eid, 100.0 * cost / max(total, 1), ps.replace('shader_', '').replace('.vulkan.bin', ''),
            ps_inv, pi, vs.replace('shader_', '').replace('.vulkan.bin', ''), vs_inv, vi, pc_us))
    json.dump({'label': label, 'totals': dict(totals), 'by_kind': dict(by_kind),
               'pc_only_stencil_bit': dict(pc_only),
               'shaders': {k: dict(v) for k, v in per_shader.items()}},
              open(os.path.join(out, 'frame_cost.json'), 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
