"""The host timer queue on the Thor's code path, tested on the PC in WSL.

  python tools/pc/timer_queue_test.py [--ref HEAD] [--no-tsan]

Windows does not use src/xenia/base/threading_timer_queue.cc for guest timers
(it has native waitable timers); the POSIX build does - every guest timer and
timed wait on the Thor goes through it. This compiles the file alone with g++
in WSL (shims in tools/pc/timer_queue_test/shim stand in for the cvar,
threading and assert headers, with the Android cvar defaults), once from the
working tree and once from --ref, and runs tools/pc/timer_queue_test/test_tq.cc
on both: idle CPU of the timer thread, one-shot lateness (fire - due) for
timers queued while it sleeps, a 2 ms recurring timer (fires and CPU), disarm,
8 producers x 2000 timers (none missing, none early), and the working tree
again under ThreadSanitizer.

2026-10-01: the 1 ms idle poll spun the disruptor's 4092 pauses on every wake
(idle 13.5% of a core, a full PC core in the Gears profile); the event wait
took it to 0.04% and the new-timer lateness p99 from 777 to 135 us.
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEST_DIR = os.path.join(ROOT, 'tools', 'pc', 'timer_queue_test')
SOURCE = 'src/xenia/base/threading_timer_queue.cc'


def wsl_path(path):
    path = os.path.abspath(path).replace('\\', '/')
    return '/mnt/%s%s' % (path[0].lower(), path[2:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', default='HEAD', help='git ref to compare against')
    ap.add_argument('--no-tsan', action='store_true')
    args = ap.parse_args()

    work = tempfile.mkdtemp(prefix='tq_', dir=os.environ.get('CLAUDE_JOB_DIR') and
                            os.path.join(os.environ['CLAUDE_JOB_DIR'], 'tmp') or None)
    ref_source = os.path.join(work, 'ref_threading_timer_queue.cc')
    with open(ref_source, 'wb') as f:
        f.write(subprocess.run(['git', 'show', '%s:%s' % (args.ref, SOURCE)], cwd=ROOT,
                               capture_output=True, check=True).stdout)
    # The shim headers go first so the real cvar/threading headers are not used;
    # the header under test is the working tree's.
    shim = os.path.join(work, 'shim')
    os.makedirs(os.path.join(shim, 'xenia', 'base'))
    for name in ('cvar.h', 'threading.h', 'assert.h'):
        with open(os.path.join(TEST_DIR, 'shim', 'xenia', 'base', name), 'rb') as src, \
                open(os.path.join(shim, 'xenia', 'base', name), 'wb') as dst:
            dst.write(src.read())
    with open(os.path.join(ROOT, 'src', 'xenia', 'base', 'threading_timer_queue.h'), 'rb') as src, \
            open(os.path.join(shim, 'xenia', 'base', 'threading_timer_queue.h'), 'wb') as dst:
        dst.write(src.read())

    root, test, out = wsl_path(ROOT), wsl_path(os.path.join(TEST_DIR, 'test_tq.cc')), wsl_path(work)
    flags = '-std=c++17 -pthread -I %s/shim -I %s' % (out, root)
    builds = [('working tree', '-O2', '%s/%s' % (root, SOURCE), 'tq_new'),
              (args.ref, '-O2', '%s/ref_threading_timer_queue.cc' % out, 'tq_ref')]
    if not args.no_tsan:
        builds.append(('working tree, ThreadSanitizer', '-O1 -g -fsanitize=thread',
                       '%s/%s' % (root, SOURCE), 'tq_tsan'))
    script = ['set -e']
    for _, opt, source, exe in builds:
        script.append('g++ %s %s -DTQ_SOURCE=\'"%s"\' %s -o %s/%s' % (opt, flags, source, test, out, exe))
    for label, _, _, exe in builds:
        script.append('echo "== %s =="; %s/%s 2>&1 || true' % (label, out, exe))
    script_path = os.path.join(work, 'run.sh')
    with open(script_path, 'w', newline='\n') as f:
        f.write('\n'.join(script) + '\n')
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    result = subprocess.run(['wsl', 'bash', wsl_path(script_path)], env=env,
                            stdin=subprocess.DEVNULL, capture_output=True, text=True)
    lines = [l for l in (result.stdout + result.stderr).splitlines()
             if not l.startswith('wsl: Failed to translate')]
    print('\n'.join(lines))
    return 0 if result.returncode == 0 and 'FAIL' not in result.stdout else 1


if __name__ == '__main__':
    sys.exit(main())
