"""Audit xenia-thor cvars for levers that cannot take effect.

Three classes, each of which has already cost this project real time:
  A. DEFINED BUT NEVER READ  - no cvars::<name> anywhere. Setting it does nothing.
  B. READ ONLY IN DEAD CODE  - read, but the only readers sit behind an opcode
     no producer emits.
  C. NOT A CVAR AT ALL       - allowlisted for launch by EmulatorActivity but
     not DEFINEd anywhere, e.g. left behind by a deleted feature.

Run after adding a cvar:  python tools/audit/cvar_audit.py
"""
import re
import os
import io
import collections
import sys

SRC = 'src'
ANDROID = ('android/android_studio_project/app/src/main/java/jp/xenia/emulator/'
           'EmulatorActivity.java')

def_re = re.compile(
    r'\bDEFINE_(bool|int32|uint32|int64|uint64|double|string|path)\s*\(\s*'
    r'([A-Za-z_][A-Za-z0-9_]*)')
use_re = re.compile(r'cvars::([A-Za-z_][A-Za-z0-9_]*)')


def strip_if_zero(text):
    """Blank out `#if 0` ... `#endif` regions.

    Without this the audit reports cvars that are not compiled at all as
    "defined but never read", which is a false positive. It produced exactly
    two: stack_size_multiplier_hack and main_xthread_stack_size_multiplier_hack
    both sit inside `#if 0` in kernel/xthread.cc, so they are deliberately
    disabled rather than broken. Nesting is tracked so an inner #if inside a
    disabled block does not end it early.
    """
    out = []
    disabled = False
    nest = 0
    for line in text.split('\n'):
        stripped = line.lstrip()
        if not disabled:
            if re.match(r'#\s*if\s+0\b', stripped):
                disabled = True
                nest = 0
                out.append('')
                continue
            out.append(line)
            continue
        if re.match(r'#\s*if', stripped):
            nest += 1
        elif re.match(r'#\s*endif', stripped):
            if nest:
                nest -= 1
            else:
                disabled = False
        out.append('')
    return '\n'.join(out)


defs = {}
reads = collections.Counter()
read_files = collections.defaultdict(set)

for root, dirs, files in os.walk(SRC):
    for fn in files:
        if not fn.endswith(('.cc', '.h', '.inl', '.cpp')):
            continue
        p = os.path.join(root, fn).replace('\\', '/')
        try:
            raw = io.open(p, encoding='utf-8', errors='ignore').read()
        except OSError:
            continue
        s = strip_if_zero(raw)
        for m in def_re.finditer(s):
            defs.setdefault(m.group(2), (p, m.group(1)))
        for m in use_re.finditer(s):
            reads[m.group(1)] += 1
            read_files[m.group(1)].add(p)

never = sorted(n for n in defs if reads[n] == 0)

print('cvars defined      :', len(defs))
print('never read anywhere:', len(never))
print()
print('=== CLASS A: DEFINED BUT NEVER READ (setting them does nothing) ===')
for n in never:
    print('  %-54s %s' % (n, defs[n][0]))

print()
print('=== CLASS B: read ONLY inside the unreachable reservation path ===')
dead_files = {'src/xenia/cpu/backend/a64/a64_seq_memory.cc',
              'src/xenia/cpu/backend/a64/a64_backend.cc'}
for n in sorted(defs):
    if reads[n] and read_files[n] <= dead_files and 'reserv' in n:
        print('  %-54s readers: %s' % (n, sorted(read_files[n])))

try:
    java = io.open(ANDROID, encoding='utf-8', errors='ignore').read()
except OSError:
    print()
    print('(no EmulatorActivity.java found)')
    sys.exit(0)
allow = set(re.findall(r'copy\w+Extra\(intent,\s*launchArguments,\s*"([^"]+)"',
                       java))
print()
print('allowlisted for launch:', len(allow))
# target / android_hide_osd / android_show_fps are Java-side launch arguments,
# not cvars, and are expected here.
missing = sorted(n for n in allow if n not in defs)
if missing:
    print()
    print('=== ALLOWLISTED BUT NOT A DEFINED CVAR (typo, or removed feature) ===')
    for n in missing:
        print('  ' + n)
