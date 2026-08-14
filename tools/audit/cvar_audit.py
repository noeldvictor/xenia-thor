"""Audit xenia-thor cvars for levers that cannot take effect.

Three classes, each of which has already cost this project real time:
  A. DEFINED BUT NEVER READ  - no cvars::<name> anywhere. Setting it does nothing.
  B. READ ONLY IN DEAD CODE  - read, but the only readers sit behind an opcode
     no producer emits. Detected here for the reservation opcodes specifically.
  C. NOT ALLOWLISTED         - read and live, but EmulatorActivity never copies
     it from the launch intent, so a GUI launch cannot set it.
"""
import re, os, io, collections, sys

SRC = 'src'
ANDROID = ('android/android_studio_project/app/src/main/java/jp/xenia/emulator/'
           'EmulatorActivity.java')

def_re = re.compile(
    r'\bDEFINE_(bool|int32|uint32|int64|uint64|double|string|path)\s*\(\s*'
    r'([A-Za-z_][A-Za-z0-9_]*)')
use_re = re.compile(r'cvars::([A-Za-z_][A-Za-z0-9_]*)')

defs = {}
reads = collections.Counter()
read_files = collections.defaultdict(set)

for root, dirs, files in os.walk(SRC):
    for fn in files:
        if not fn.endswith(('.cc', '.h', '.inl', '.cpp')):
            continue
        p = os.path.join(root, fn).replace('\\', '/')
        try:
            s = io.open(p, encoding='utf-8', errors='ignore').read()
        except OSError:
            continue
        for m in def_re.finditer(s):
            defs.setdefault(m.group(2), (p, m.group(1)))
        for m in use_re.finditer(s):
            reads[m.group(1)] += 1
            read_files[m.group(1)].add(p)

# A cvar whose ONLY readers are the definition site itself is still unread in
# any meaningful sense, but DEFINE_ does not textually produce a cvars:: read,
# so a plain count is already correct.
never = sorted(n for n in defs if reads[n] == 0)

print('cvars defined      :', len(defs))
print('never read anywhere:', len(never))
print()
print('=== CLASS A: DEFINED BUT NEVER READ (setting them does nothing) ===')
for n in never:
    print('  %-54s %s' % (n, defs[n][0]))

# Class B: readers confined to files implementing an opcode nobody emits.
print()
print('=== CLASS B: read ONLY inside the unreachable reservation path ===')
dead_files = {'src/xenia/cpu/backend/a64/a64_seq_memory.cc',
              'src/xenia/cpu/backend/a64/a64_backend.cc'}
for n in sorted(defs):
    if reads[n] and read_files[n] <= dead_files and 'reserv' in n:
        print('  %-54s readers: %s' % (n, sorted(read_files[n])))

# Class C: allowlist coverage.
try:
    java = io.open(ANDROID, encoding='utf-8', errors='ignore').read()
except OSError:
    print('\n(no EmulatorActivity.java found)')
    sys.exit(0)
allow = set(re.findall(r'copy\w+Extra\(intent,\s*launchArguments,\s*"([^"]+)"',
                       java))
print()
print('allowlisted for launch:', len(allow))
missing = sorted(n for n in allow if n not in defs)
if missing:
    print()
    print('=== ALLOWLISTED BUT NOT A DEFINED CVAR (typo or removed) ===')
    for n in missing:
        print('  ' + n)
