"""Which guest shaders still need the program counter switch, and why.

  python tools/pc/cf_census.py [TRACE.xtr ...] [--shaders DIR] [--cvars "a=1"]

The SPIR-V translator writes a shader with jumps as nested selection
constructs when every jump is forward and the skipped regions nest (a
conditional jump skips to its target; an unconditional jump only ends the
"then" part of an if/else, or does nothing when it goes to the next
instruction; a jump past the end of its enclosing region ends at an
unconditional jump to the same target inside that region) - XenosRecomp's
flattened control flow. Any other
shader with labels keeps the loop with the program counter switch, which a
driver compiler optimizes less well (NVIDIA got a fallthrough wrong,
2026-09-29). This tool reads the ucode disassembly that --dump_shaders
writes (*.ucode.vert/.frag; with traces, it replays them first into
scratch/cf_census/shaders) and prints, for the shaders with labels, how many
are structured and the reason for each one that is not (loop or call,
backward jump, crossing regions, an unconditional jump that is not the end
of a "then" part), with an example file per reason.
The rule is the same as SpirvShaderTranslator::AnalyzeStructuredForwardJumps.
"""
import argparse
import collections
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'scratch', 'cf_census')
EXE = os.path.join(ROOT, 'build', 'bin', 'Windows', 'Release', 'xenia-gpu-vulkan-trace-dump.exe')


def parse(text):
    """(cf index, kind, target, unconditional) per jump, loop, call or return."""
    instructions = {}
    for line in text.splitlines():
        m = re.match(r'/\*\s+(\d+)\.(\d)\s+\*/\s*(.*)', line)
        if not m:
            continue
        index = int(m.group(1)) * 2 + int(m.group(2))
        body = m.group(3)
        jump = re.search(r'(\(!?p0\)\s*)?(c?jmp)\s+(!?b\d+,\s*)?L(\d+)', body)
        if jump:
            unconditional = jump.group(2) == 'jmp' and not jump.group(1)
            instructions[index] = ('jmp', int(jump.group(4)), unconditional)
        elif re.search(r'\bloop\b|\bendloop\b|\bcall\b|\bret\b', body):
            instructions[index] = ('loop or call', 0, False)
    return instructions


def classify(text):
    instructions = parse(text)
    last = max(list(instructions) + [0])
    unconditional_jumps = collections.defaultdict(list)
    for index, (kind, target, is_unconditional) in sorted(instructions.items()):
        if kind == 'jmp' and is_unconditional:
            unconditional_jumps[target].append(index)

    def region_end(index, target, end):
        # A jump past the end of a region can end at an unconditional jump to
        # the same target inside that region (the same path).
        return next((j for j in unconditional_jumps[target] if index < j <= end), None)

    regions = []  # innermost last: dict(end, is_else, else_end)
    for index in range(last + 1):
        while regions and regions[-1]['end'] == index:
            region = regions.pop()
            if region['else_end']:
                regions.append(dict(end=region['else_end'], is_else=True, else_end=0))
                break
        if regions and regions[-1]['end'] <= index:
            return 'crossing regions'
        if index not in instructions:
            continue
        kind, target, unconditional = instructions[index]
        if kind != 'jmp':
            return kind
        if target <= index:
            return 'backward jump'
        if unconditional:
            if target == index + 1:
                continue  # a jump to the next instruction does nothing
            if (not regions or regions[-1]['is_else'] or regions[-1]['else_end'] or
                    regions[-1]['end'] != index + 1):
                return 'unconditional jump not at the end of a "then" part'
            if len(regions) >= 2 and target > regions[-2]['end']:
                target = region_end(index, target, regions[-2]['end'])
                if target is None:
                    return 'crossing regions'
            regions[-1]['else_end'] = target
            continue
        if regions and target > regions[-1]['end']:
            target = region_end(index, target, regions[-1]['end'])
            if target is None:
                return 'crossing regions'
        regions.append(dict(end=target, is_else=False, else_end=0))
    return 'structured'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='*')
    ap.add_argument('--shaders', default='')
    ap.add_argument('--cvars', default='')
    args = ap.parse_args()
    shaders = args.shaders or os.path.join(OUT, 'shaders')
    if args.traces:
        os.makedirs(shaders, exist_ok=True)
        for f in glob.glob(os.path.join(shaders, '*')):
            os.remove(f)
        for trace in args.traces:
            subprocess.run([EXE, '--target_trace_file=' + trace,
                            '--trace_dump_path=' + os.path.join(OUT, 'dump') + os.sep,
                            '--log_file=' + os.path.join(OUT, 'dump.log'),
                            '--dump_shaders=' + shaders] +
                           ['--' + c for c in args.cvars.split() if c],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    files = glob.glob(os.path.join(shaders, '*.ucode.vert')) + glob.glob(os.path.join(shaders, '*.ucode.frag'))
    if not files:
        sys.exit('no *.ucode.vert/.frag in %s' % shaders)
    counts = collections.Counter()
    examples = {}
    with_labels = 0
    for f in files:
        text = open(f, encoding='utf-8', errors='replace').read()
        if 'label L' not in text:
            continue
        with_labels += 1
        reason = classify(text)
        counts[reason] += 1
        examples.setdefault(reason, os.path.basename(f))
    print('%d guest shaders, %d with labels' % (len(files), with_labels))
    for reason, count in counts.most_common():
        print('  %-52s %4d  e.g. %s' % (reason, count, examples[reason]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
