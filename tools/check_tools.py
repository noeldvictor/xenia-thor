"""Check every tool script: it compiles, and it holds no stray control bytes.

  python tools/check_tools.py

A heredoc in the agent's shell turns a backslash escape into a control byte
(2026-09-23: "\\b" in a regex became byte 0x08 in tools/thor/perf_probe.py, so
the command-processor line never matched and the COMMAND-PROCESSOR-BOUND
verdict could not fire - silently, for a day). Run this after any scripted
edit of tools/; the stop hook runs it too. Exit 1 on a finding.
"""
import glob
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
# Tab, LF and CR are normal; every other byte below 0x20 is a finding.
BAD = set(range(0x20)) - {0x09, 0x0A, 0x0D}


def main():
    findings = []
    files = [f for f in glob.glob(os.path.join(ROOT, 'tools', '**', '*.py'), recursive=True)
             if '__pycache__' not in f]
    for f in sorted(files):
        rel = os.path.relpath(f, ROOT).replace('\\', '/')
        data = open(f, 'rb').read()
        for n, line in enumerate(data.split(b'\n'), 1):
            bad = sorted({c for c in line if c in BAD})
            if bad:
                findings.append('%s:%d: control byte(s) %s' % (rel, n, ' '.join('0x%02X' % c for c in bad)))
        try:
            compile(data, f, 'exec')
        except (SyntaxError, ValueError) as e:
            findings.append('%s: does not compile: %s' % (rel, str(e)[:160]))
    for line in findings:
        print(line)
    print('%d tool scripts, %d findings' % (len(files), len(findings)))
    return 1 if findings else 0


if __name__ == '__main__':
    sys.exit(main())
