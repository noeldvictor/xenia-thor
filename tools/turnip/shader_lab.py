"""The Adreno shader lab: the Thor's shader compiler on the PC, no device.

  python tools/turnip/shader_lab.py [--shaders DIR] [--traces T.xtr ...]
      [--cvars "a=1"] [--label NAME] [--baseline NAME] [--top 15] [--ir]
      [--color-format 37]

Takes the translated SPIR-V modules (*.vulkan.bin.vert/.frag) that
tools/pc/spirv_validate.py dumps (default dir scratch/spirv_validate/shaders;
--traces replays traces first, with --cvars, for example the Thor profile's
translator cvars) and builds one graphics pipeline per module on host Turnip
over the freedreno drm-shim as an Adreno 740 (tools/turnip/build_host_shim.sh,
FD_GPU_ID=740). Turnip runs its full NIR and ir3 pipeline, as on the Thor, and
VK_KHR_pipeline_executable_properties gives the statistics: instructions,
registers, waves per core, nops, sync stalls.

A module gets a generated partner stage: a vertex shader that writes every
fragment input from its own attribute (an unwritten input would fold to
undefined and hide cost), or a fragment shader that reads every vertex output
(an unread output is dead code). Descriptor layouts come from the modules'
own bindings.

Writes scratch/shader_lab/<label>.json and prints the costliest shaders per
stage and the totals. --baseline compares with an earlier label: the per-shader
change of instructions and registers (a translator change measured on the
Thor's compiler in one call). --ir writes the NIR and ir3 assembly of every
shader to scratch/shader_lab/<label>-ir/.
"""
import argparse
import array
import collections
import glob
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'scratch', 'shader_lab')
MESA = '/root/mesa-host'
SHIM = MESA + '/build/src/freedreno/drm-shim/libfreedreno_noop_drm_shim.so'
ICD = MESA + '/build/src/freedreno/vulkan/freedreno_devenv_icd.x86_64.json'
LAB = MESA + '/shader_lab'

# VkDescriptorType
SAMPLER, COMBINED, SAMPLED_IMAGE, STORAGE_IMAGE = 0, 1, 2, 3
UNIFORM_TEXEL, STORAGE_TEXEL, UNIFORM_BUFFER, STORAGE_BUFFER = 4, 5, 6, 7
STAGE_BITS = {0: 0x1, 4: 0x10}  # SPIR-V execution model -> VkShaderStageFlagBits
# Statistics shown in the tables, in this order (Turnip's names).
KEY_STATS = ('Instruction Count', 'Max Waves Per Core', 'Registers used',
             'Half-registers used', 'NOPs Count', '(sy) Count', '(ss) Count',
             'Preamble Instruction Count')


def to_wsl(path):
    path = os.path.abspath(path).replace('\\', '/')
    return '/mnt/' + path[0].lower() + path[2:]


def wsl(script, **kw):
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    return subprocess.run(['wsl', '-e', 'bash', '-c', script], capture_output=True,
                          text=True, encoding='utf-8', errors='replace', env=env, **kw)


def reflect(path):
    """The execution model, descriptor bindings, located inputs and outputs and
    push constants of a SPIR-V module."""
    words = array.array('I', open(path, 'rb').read())
    if words[0] != 0x07230203:
        words.byteswap()
    decos = collections.defaultdict(dict)
    types, constants, variables = {}, {}, []
    model = None
    i = 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 0xFFFF
        ops = words[i + 1:i + count]
        if op == 15:  # OpEntryPoint
            model = ops[0]
        elif op == 71:  # OpDecorate
            decos[ops[0]][ops[1]] = list(ops[2:])
        elif 19 <= op <= 32:  # OpType*
            types[ops[0]] = (op, list(ops[1:]))
        elif op == 43:  # OpConstant
            constants[ops[1]] = ops[2]
        elif op == 59:  # OpVariable
            variables.append((ops[0], ops[1], ops[2]))
        if count == 0:
            break
        i += count

    def unwrap(type_id):
        count = 1
        while types[type_id][0] in (28, 29):  # OpTypeArray, OpTypeRuntimeArray
            op, operands = types[type_id]
            count *= constants.get(operands[1], 1) if op == 28 else 64
            type_id = operands[0]
        return type_id, count

    def scalar(type_id):
        """('float'|'int'|'uint', components, array length) of an interface type."""
        length = 0
        if types[type_id][0] == 28:
            length = constants.get(types[type_id][1][1], 1)
            type_id = types[type_id][1][0]
        comps = 1
        if types[type_id][0] == 23:  # OpTypeVector
            comps = types[type_id][1][1]
            type_id = types[type_id][1][0]
        op, operands = types[type_id]
        base = 'float' if op == 22 else ('int' if operands[1] else 'uint')
        return base, comps, length

    info = {'model': model, 'bindings': [], 'inputs': [], 'outputs': [], 'push': 0,
            'depth': False}
    for type_id, var, storage in variables:
        pointee = types[type_id][1][1]
        d = decos.get(var, {})
        if 34 in d and 33 in d:  # DescriptorSet, Binding
            base, count = unwrap(pointee)
            op, operands = types[base]
            if storage == 12:
                kind = STORAGE_BUFFER
            elif storage == 2:
                kind = STORAGE_BUFFER if 3 in decos.get(base, {}) else UNIFORM_BUFFER
            elif op == 26:
                kind = SAMPLER
            elif op == 27:
                kind = COMBINED
            elif op == 25:  # OpTypeImage: dim, depth, arrayed, ms, sampled
                buffer = operands[1] == 5
                if operands[5] == 2:
                    kind = STORAGE_TEXEL if buffer else STORAGE_IMAGE
                else:
                    kind = UNIFORM_TEXEL if buffer else SAMPLED_IMAGE
            else:
                continue
            info['bindings'].append((d[34][0], d[33][0], kind, count))
        elif storage == 9:  # PushConstant
            info['push'] = 128
        elif storage in (1, 3) and 30 in d:  # Input/Output with a Location
            base, comps, length = scalar(pointee)
            entry = {'location': d[30][0], 'component': d.get(31, [0])[0], 'base': base,
                     'comps': comps, 'length': length,
                     'interp': [q for n, q in ((14, 'flat'), (13, 'noperspective'),
                                               (16, 'centroid'), (17, 'sample')) if n in d]}
            info['inputs' if storage == 1 else 'outputs'].append(entry)
        elif storage == 3 and d.get(11) == [22]:  # BuiltIn FragDepth
            info['depth'] = True
    return info


def glsl_type(v):
    if v['comps'] == 1:
        return v['base']
    return {'float': 'vec', 'int': 'ivec', 'uint': 'uvec'}[v['base']] + str(v['comps'])


def qualifiers(v, direction):
    layout = 'location=%d' % v['location']
    if v['component']:
        layout += ', component=%d' % v['component']
    interp = list(v['interp'])
    if v['base'] != 'float' and 'flat' not in interp:
        interp.insert(0, 'flat')
    return 'layout(%s) %s%s' % (layout, ''.join(q + ' ' for q in interp), direction)


def partner_vertex(inputs):
    """A vertex shader that writes every fragment input with a distinct,
    non-constant value (Turnip links the stages: a constant or unwritten
    varying would fold into the fragment shader)."""
    attrs = max(1, min(16, len(inputs)))
    lines = ['#version 450']
    lines += ['layout(location=%d) in vec4 a%d;' % (i, i) for i in range(attrs)]
    body = ['  gl_Position = a0;']
    for n, v in enumerate(inputs):
        name = 'o%d' % n
        array = '[%d]' % v['length'] if v['length'] else ''
        lines.append('%s %s %s%s;' % (qualifiers(v, 'out'), glsl_type(v), name, array))
        for e in range(max(1, v['length'])):
            src = 'a%d + vec4(%d.0)' % ((n + e) % attrs, n * 8 + e + 1)
            swz = '' if v['comps'] == 4 else '.' + 'xyzw'[:v['comps']]
            target = name + ('[%d]' % e if v['length'] else '')
            body.append('  %s = %s((%s)%s);' % (target, glsl_type(v), src, swz))
    return '\n'.join(lines + ['void main() {'] + body + ['}', '']), attrs


def partner_fragment(outputs):
    """A fragment shader that reads every vertex output (an unread output is
    removed from the vertex shader)."""
    lines = ['#version 450', 'layout(location=0) out vec4 xe_lab_color;']
    body = ['  vec4 s = vec4(0.0);']
    for n, v in enumerate(outputs):
        name = 'i%d' % n
        array = '[%d]' % v['length'] if v['length'] else ''
        lines.append('%s %s %s%s;' % (qualifiers(v, 'in'), glsl_type(v), name, array))
        for e in range(max(1, v['length'])):
            src = name + ('[%d]' % e if v['length'] else '')
            pad = ', 0.0' * (4 - v['comps']) if v['comps'] > 1 else ''
            body.append('  s += vec4(%s%s);' % (src, pad))
    body.append('  xe_lab_color = s;')
    return '\n'.join(lines + ['void main() {'] + body + ['}', ''])


def dump_from_traces(traces, cvars, shaders):
    exe = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release', 'xenia-gpu-vulkan-trace-dump.exe')
    os.makedirs(shaders, exist_ok=True)
    for f in glob.glob(os.path.join(shaders, '*')):
        os.remove(f)
    for trace in traces:
        try:
            subprocess.run([exe, '--target_trace_file=' + trace,
                            '--trace_dump_path=' + os.path.join(OUT, 'dump') + os.sep,
                            '--log_file=' + os.path.join(OUT, 'dump.log'),
                            '--dump_shaders=' + shaders] + ['--' + c for c in cvars.split() if c],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        except subprocess.TimeoutExpired:
            print('%s: replay hung (300 s) - killed' % trace)


def ensure_lab():
    src = to_wsl(os.path.join(ROOT, 'tools', 'turnip', 'shader_lab.c'))
    r = wsl('set -e; test -f %s || { echo "no host Turnip: wsl -d Ubuntu -- bash '
            'tools/turnip/build_host_shim.sh"; exit 1; }; '
            'if [ ! -x %s ] || [ %s -nt %s ]; then gcc -O1 -Wall -I %s/include -o %s %s -ldl; fi'
            % (ICD, LAB, src, LAB, MESA, LAB, src))
    if r.returncode:
        sys.exit(r.stdout + r.stderr)


def run_jobs(blocks, work, ir_dir, no_robust=False):
    """Runs the lab over the job blocks (name -> text); a job that crashes the
    driver is recorded and the run goes on from the next job."""
    results = collections.OrderedDict()
    errors = {}
    remaining = list(blocks)
    while remaining:
        part = os.path.join(work, 'jobs.txt')
        with open(part, 'w', encoding='utf-8', newline='\n') as f:
            f.write(''.join(blocks[n] for n in remaining))
        cmd = (('LAB_NO_ROBUST=1 ' if no_robust else '') +
               'MESA_SHADER_CACHE_DISABLE=true FD_GPU_ID=740 LD_PRELOAD=%s VK_DRIVER_FILES=%s '
               'VK_ICD_FILENAMES=%s %s %s %s' % (SHIM, ICD, ICD, LAB, to_wsl(part),
                                                 to_wsl(ir_dir) if ir_dir else ''))
        r = wsl(cmd)
        current = None
        done = set()
        for line in r.stdout.splitlines():
            f = line.split('\t')
            if f[0] == 'begin':
                current = f[1]
            elif f[0] == 'end':
                done.add(f[1])
                current = None
            elif f[0] == 'stat' and len(f) == 5:
                results.setdefault(f[1], {}).setdefault(f[2], {})[f[3]] = float(f[4])
            elif f[0] == 'error':
                errors[f[1]] = f[2]
        if not done and current is None:
            sys.exit('shader lab failed to start:\n' + r.stderr[-2000:])
        if current:
            errors[current] = 'driver crash (exit %d): %s' % (
                r.returncode, (r.stderr.strip().splitlines() or [''])[-1][:200])
            done.add(current)
        remaining = [n for n in remaining if n not in done]
    return results, errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--shaders', default=os.path.join(ROOT, 'scratch', 'spirv_validate', 'shaders'))
    ap.add_argument('--traces', nargs='*', default=[])
    ap.add_argument('--cvars', default='')
    ap.add_argument('--label', default='latest')
    ap.add_argument('--baseline', default='')
    ap.add_argument('--top', type=int, default=15)
    ap.add_argument('--ir', action='store_true')
    ap.add_argument('--no-robust', action='store_true',
                    help='device without robustBufferAccess and robustImageAccess '
                         '(xenia enables them): the cost of robustness')
    ap.add_argument('--only', default='', help='comma-separated name parts (shader hashes)')
    ap.add_argument('--color-format', type=int, default=37,
                    help='VkFormat of every color output (default R8G8B8A8_UNORM)')
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    if args.traces:
        args.shaders = os.path.join(OUT, 'shaders')
        dump_from_traces(args.traces, args.cvars, args.shaders)
    modules = sorted(glob.glob(os.path.join(args.shaders, '*.vulkan.bin.*')))
    if not modules:
        sys.exit('no *.vulkan.bin.* modules in %s' % args.shaders)
    ensure_lab()

    work = os.path.join(OUT, 'work')
    os.makedirs(work, exist_ok=True)
    partners = {}  # glsl text -> (path, stage)
    jobs, meta, skipped = collections.OrderedDict(), {}, []
    for path in modules:
        name = os.path.basename(path).replace('shader_', '').replace('.vulkan.bin', '')
        if args.only and not any(o in name for o in args.only.split(',')):
            continue
        info = reflect(path)
        if info['model'] not in STAGE_BITS:
            skipped.append(name)
            continue
        bindings = {}
        for s, b, kind, count in info['bindings']:
            bindings[(s, b)] = (kind, count)
        if info['model'] == 4:
            text, attrs = partner_vertex(info['inputs'])
            ext, lab_stage, own = 'vert', 'v', 'f'
            colors = [0] * (max([v['location'] for v in info['outputs']] + [-1]) + 1)
            for v in info['outputs']:
                colors[v['location']] = {'float': args.color_format, 'int': 108,
                                         'uint': 107}[v['base']]
        else:
            text, attrs = partner_fragment(info['outputs']), 0
            ext, lab_stage, own = 'frag', 'f', 'v'
            colors = [args.color_format]
        if text not in partners:
            h = hashlib.sha1(text.encode()).hexdigest()[:12]
            glsl = os.path.join(work, 'partner_%s.%s' % (h, ext))
            open(glsl, 'w', newline='\n').write(text)
            partners[text] = glsl
        partner = partners[text] + '.spv'
        lines = ['J\t' + name]
        stages = [(own, path), (lab_stage, partner)]
        stages.sort(key=lambda s: 'vf'.index(s[0]))
        lines += ['S\t%s\t%s' % (st, to_wsl(p)) for st, p in stages]
        lines += ['B\t%d\t%d\t%d\t%d\t%d' % (s, b, kind, count, 0x11)
                  for (s, b), (kind, count) in sorted(bindings.items())]
        if info['push']:
            lines.append('P\t%d' % info['push'])
        lines.append('A\t%d' % attrs)
        lines += ['C\t%d' % c for c in colors]
        jobs[name] = '\n'.join(lines) + '\nE\n'
        meta[name] = {'stage': 'vertex' if info['model'] == 0 else 'fragment',
                      'module': os.path.relpath(path, ROOT).replace('\\', '/'),
                      'inputs': len(info['inputs']), 'outputs': len(info['outputs']),
                      'bindings': len(bindings), 'depth': info['depth']}
    # One WSL call compiles every partner stage.
    r = wsl('cd %s && for f in partner_*.vert partner_*.frag; do [ -f "$f" ] || continue; '
            '[ -f "$f.spv" ] && [ "$f.spv" -nt "$f" ] && continue; '
            'glslangValidator -V --target-env vulkan1.1 -o "$f.spv" "$f" >/dev/null || echo "FAIL $f"; done'
            % to_wsl(work))
    if 'FAIL' in r.stdout:
        sys.exit('partner stage compile failed:\n' + r.stdout[-2000:])
    ir_dir = os.path.join(OUT, args.label + '-ir') if args.ir else None
    if ir_dir:
        os.makedirs(ir_dir, exist_ok=True)
    results, errors = run_jobs(jobs, work, ir_dir, args.no_robust)

    # The module's own executable: Turnip names them by stage; the vertex
    # stage also has a binning-pass variant.
    rows = []
    for name, m in meta.items():
        exes = results.get(name, {})
        own = {k: v for k, v in exes.items()
               if (m['stage'] == 'fragment') == k.upper().startswith(('FRAG', 'FS', 'PIXEL'))}
        main_exe = next((k for k in own if 'BINNING' not in k.upper()), None)
        row = dict(m, name=name, error=errors.get(name),
                   stats=own.get(main_exe, {}) if main_exe else {},
                   executables=sorted(exes))
        binning = next((k for k in own if 'BINNING' in k.upper()), None)
        if binning:
            row['binning_stats'] = own[binning]
        rows.append(row)
    result_path = os.path.join(OUT, args.label + '.json')
    json.dump({'shaders': args.shaders, 'cvars': args.cvars, 'color_format': args.color_format,
               'skipped': skipped, 'rows': rows}, open(result_path, 'w'), indent=1)

    ok = [r for r in rows if r['stats']]
    print('%d modules: %d compiled on the Adreno 740 compiler, %d errors, %d skipped (stage)'
          % (len(rows), len(ok), sum(1 for r in rows if r['error']), len(skipped)))
    if not ok and rows:
        print('executables seen:', sorted({e for r in rows for e in r['executables']})[:10])
    for rule in sorted({r['error'] for r in rows if r['error']})[:8]:
        print('  error: %s (%d)' % (rule, sum(1 for r in rows if r['error'] == rule)))
    names = [s for s in KEY_STATS if any(s in r['stats'] for r in ok)]
    short = {'Instruction Count': 'instr', 'Max Waves Per Core': 'waves',
             'Registers used': 'regs', 'Half-registers used': 'hregs', 'NOPs Count': 'nops',
             '(sy) Count': 'sy', '(ss) Count': 'ss', 'Preamble Instruction Count': 'pre'}
    for stage in ('vertex', 'fragment'):
        group = [r for r in ok if r['stage'] == stage]
        if not group:
            continue
        instr = [r['stats'].get('Instruction Count', 0) for r in group]
        waves = [r['stats'].get('Max Waves Per Core', 0) for r in group]
        print('\n%s: %d shaders, instructions total %d, mean %.0f, max %d; waves min %d'
              % (stage, len(group), sum(instr), sum(instr) / len(group), max(instr),
                 min(waves) if waves else 0))
        print('  %-36s %s' % ('shader', ' '.join('%6s' % short[n] for n in names)))
        for r in sorted(group, key=lambda r: -r['stats'].get('Instruction Count', 0))[:args.top]:
            print('  %-36s %s' % (r['name'], ' '.join('%6d' % r['stats'].get(n, 0) for n in names)))
    if args.baseline:
        base = json.load(open(os.path.join(OUT, args.baseline + '.json')))
        before = {r['name']: r['stats'] for r in base['rows'] if r['stats']}

        # A change that sets a modification bit renames the variant
        # (<hash>_<modification>.<stage>): then match on the hash and stage
        # when both runs have one variant of it.
        def by_hash(names_):
            groups = collections.defaultdict(list)
            for name in names_:
                groups[(name.split('_')[0], name.rsplit('.', 1)[-1])].append(name)
            return {k: v[0] for k, v in groups.items() if len(v) == 1}
        base_by_hash = by_hash(before)
        new_by_hash = by_hash([r['name'] for r in ok])
        diffs = []
        common = 0
        for r in ok:
            b = before.get(r['name'])
            if b is None:
                key = (r['name'].split('_')[0], r['name'].rsplit('.', 1)[-1])
                if new_by_hash.get(key) == r['name'] and key in base_by_hash:
                    b = before[base_by_hash[key]]
            if b is None:
                continue
            common += 1
            d = {n: r['stats'].get(n, 0) - b.get(n, 0) for n in names}
            if any(d.values()):
                diffs.append((r['name'], d))
        print('\nagainst %s: %d shaders in both, %d changed' % (args.baseline, common, len(diffs)))
        for n in names:
            total = sum(d[n] for _, d in diffs)
            if total:
                print('  %-26s %+d' % (n, total))
        for name, d in sorted(diffs, key=lambda x: -abs(x[1].get('Instruction Count', 0)))[:args.top]:
            print('  %-36s %s' % (name, ' '.join('%s %+d' % (short[n], v) for n, v in d.items() if v)))
    print('\nresult', os.path.relpath(result_path, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
