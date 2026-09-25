"""The Khronos validation layer over GPU trace replays: API misuse Turnip may not forgive.

  python tools/pc/vk_validate.py TRACE.xtr [TRACE.xtr ...] [--cvars "a=1"]
      [--thor-profile] [--layer DIR] [--examples 1]

Replays each trace on the Vulkan trace dump with --vulkan_validation=true
(VK_LAYER_KHRONOS_validation with synchronization validation; xenia logs every
message with its ID) and groups the messages by ID over all traces: count,
severity, and an example message. The desktop driver runs many invalid uses
without a visible effect; the Thor's Turnip is strict, and a hazard that
NVIDIA forgives can be a glitch or a hang on the tiler.

The layer is built from source once (no SDK installer): KhronosGroup/
Vulkan-ValidationLayers at the tag of the Android release, in
scratch/tools/vvl (cmake -D UPDATE_DEPS=ON; build with /m:2 - the compiler
runs out of heap with more jobs next to another build). --layer: the folder
with VkLayer_khronos_validation.json. Exit code 1 if there are errors.
"""
import argparse
import collections
import glob
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backend_ab  # noqa: E402

ROOT = backend_ab.ROOT
OUT = os.path.join(ROOT, 'scratch', 'vk_validate')
DEFAULT_LAYER = os.path.join(ROOT, 'scratch', 'tools', 'vvl', 'build', 'layers', 'Release')
# "!> ... Vulkan Error (Validation, ID 123: VUID-...): message" and the
# warning form; xenia prefixes error lines with "!>" and warnings with "w>".
MSG_RE = re.compile(r'^(.)> [0-9A-F]+ Vulkan (\w+) \(([^,]+), ID (-?\d+)(?:: ([^)]+))?\)(?:: (.*))?$')


def find_layer_dir(path):
    for candidate in (path, os.path.join(path, 'Release'), os.path.dirname(path)):
        if glob.glob(os.path.join(candidate, 'VkLayer_khronos_validation.json')):
            return candidate
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('--cvars', default='')
    ap.add_argument('--thor-profile', action='store_true')
    ap.add_argument('--layer', default=DEFAULT_LAYER)
    ap.add_argument('--examples', type=int, default=1)
    ap.add_argument('--best-practices', action='store_true',
                    help='also the best-practices checks with the ARM tiler set (load and '
                         'store ops, clears, render pass breaks - the Adreno is a tiler too)')
    args = ap.parse_args()
    layer_dir = find_layer_dir(args.layer)
    if not layer_dir:
        print('no VkLayer_khronos_validation.json under %s - build the layer '
              '(see the docstring)' % args.layer)
        return 1
    cvars = [c for c in args.cvars.split() if c]
    if args.thor_profile:
        cvars += backend_ab.thor_profile_cvars(include_vrs=False)
    cvars.append('vulkan_validation=true')
    # The layer's own settings (vk_layer_settings): synchronization validation
    # on regardless of what the app chains.
    env = dict(os.environ, VK_LAYER_PATH=layer_dir, VK_ADD_LAYER_PATH=layer_dir,
               VK_KHRONOS_VALIDATION_VALIDATE_SYNC='true')
    if args.best_practices:
        env['VK_KHRONOS_VALIDATION_VALIDATE_BEST_PRACTICES'] = 'true'
        env['VK_KHRONOS_VALIDATION_VALIDATE_BEST_PRACTICES_ARM'] = 'true'
    exe = os.path.join(backend_ab.BIN, 'xenia-gpu-vulkan-trace-dump.exe')
    groups = collections.OrderedDict()
    loaded = 0
    for trace in args.traces:
        name = os.path.splitext(os.path.basename(trace))[0]
        out = os.path.join(OUT, name)
        os.makedirs(out, exist_ok=True)
        log = os.path.join(out, 'dump.log')
        if os.path.exists(log):
            os.remove(log)
        try:
            subprocess.run([exe, '--target_trace_file=' + trace,
                            '--trace_dump_path=' + out + os.sep, '--log_file=' + log] +
                           ['--' + c for c in backend_ab.backend_cvars('vulkan', cvars)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env,
                           timeout=900)
        except subprocess.TimeoutExpired:
            print('%s: replay hung (900 s) - killed' % name)
        text = open(log, encoding='utf-8', errors='replace').read() if os.path.exists(log) else ''
        if 'VK_LAYER_KHRONOS_validation' in text:
            loaded += 1
        count = 0
        for line in text.splitlines():
            m = MSG_RE.match(line)
            if not m:
                continue
            count += 1
            severity, id_name, message = m.group(2), m.group(5) or m.group(4), m.group(6) or ''
            g = groups.setdefault(id_name, {'severity': severity, 'count': 0, 'traces': set(),
                                            'examples': []})
            g['count'] += 1
            g['traces'].add(name)
            if len(g['examples']) < args.examples:
                g['examples'].append(message[:400])
        print('%s: %d messages' % (name, count))
    if not loaded:
        print('the log never names VK_LAYER_KHRONOS_validation - the layer did not load '
              '(VK_LAYER_PATH=%s)' % layer_dir)
    errors = sum(g['count'] for g in groups.values() if g['severity'].lower().startswith('error'))
    print('\n%d message IDs, %d error messages, over %d traces' % (len(groups), errors,
                                                                   len(args.traces)))
    order = sorted(groups.items(), key=lambda kv: (not kv[1]['severity'].lower().startswith('error'),
                                                   -kv[1]['count']))
    for id_name, g in order:
        print('  %-7s %5d  %-60s (%d traces)' % (g['severity'][:7], g['count'], id_name[:60],
                                               len(g['traces'])))
        for example in g['examples']:
            print('          ' + example)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
