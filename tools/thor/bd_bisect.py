"""Bisect the Blue Dragon gameplay-scene regression with git bisect and the
route as the metric.

  python tools/thor/bd_bisect.py --good 4614144aeb --bad HEAD [--warm 480] [--threshold-good 15.5 --threshold-bad 13]

The 2026-08-09 build (4614144aeb) holds the route's 3D gameplay scene at 17
to 20 fps; today's master holds it at 11.3 (2026-09-22, same scene gate:
about 1,200 draws and 263,000 vertices per frame). Each step: check out the
bisect commit, regenerate the Android makefiles (premake androidndk; the
generated files list the sources of the tree that generated them), build and
install with the commit's own thor_build.ps1 -Mode FullDeploy, warm the LLVM
object cache with one launch of `warm` seconds, cool the device, run the
route (the copy of today's bd_gameplay_route.sh kept next to this driver's
log, so the metric never changes with the checkout), take the median fps of
the rows at t >= 120 s, and call git bisect good/bad (skip when the build
fails or the scene gate fails). Ends with git bisect reset and a FullDeploy
of master so the device is back on the shipped build. The log is
scratch/banjo/rd/bd_bisect.log; the route outputs are next to it.

The device rules hold: one route per cooldown (gpu < 47 C, case < 41 C), the
route's own 70 C abort, force-stop after every arm.
"""
import argparse
import json
import os
import re
import importlib.util
import shutil
import statistics
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'scratch', 'banjo', 'rd')
LOG = os.path.join(OUT, 'bd_bisect.log')
ROUTE_COPY = os.path.join(OUT, 'bd_gameplay_route.bisect.sh')
MCP_COPY = os.path.join(OUT, 'xenia_thor_mcp.bisect.py')
SERIAL = 'c3ca0370'


def log(line):
    print(line, flush=True)
    with open(LOG, 'a', encoding='utf-8') as f:
        f.write(time.strftime('%H:%M:%S ') + line + '\n')


def sh(cmd, timeout=3600, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, shell=isinstance(cmd, str), capture_output=True,
                          text=True, errors='replace', timeout=timeout)


def git(*args):
    r = sh(['git', *args], timeout=120)
    return (r.stdout + r.stderr).strip()


def mcp():
    sys.path.insert(0, OUT)

    spec = importlib.util.spec_from_file_location('xenia_thor_mcp_bisect', MCP_COPY)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def build_and_install():
    r = sh([os.path.join(ROOT, 'tools', 'build', 'bin', 'premake5.exe'), '--file=premake5.lua',
            '--os=android', 'androidndk'], timeout=300)
    if r.returncode != 0:
        log('  premake failed: ' + (r.stdout + r.stderr)[-300:])
        return False
    r = sh(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            os.path.join(ROOT, 'tools', 'thor', 'thor_build.ps1'), '-Mode', 'FullDeploy',
            '-DeviceSerial', SERIAL], timeout=3000)
    text = r.stdout + r.stderr
    ok = 'BUILD SUCCESSFUL' in text and 'Success' in text and 'FAILED' not in text
    log('  build %s (%s)' % ('ok' if ok else 'FAILED', [l for l in text.splitlines() if 'BUILD' in l or 'error' in l.lower()][-2:]))
    return ok


def warm_and_cool(m, warm_s):
    m.xenia_force_stop()
    try:
        m.xenia_launch_cvars(clear=True)
    except Exception:
        pass
    r = json.loads(m.xenia_launch('bd', skip_preflight=True))
    # Until the first guest frames (the AOT window is over) plus 90 s for the
    # runtime-phase compiles, at most warm_s + 600 s. A fixed wait left two
    # commits with a partly warm cache and a void route (2026-09-22 09:34).
    t0 = time.time()
    swaps = None
    while time.time() - t0 < warm_s + 600:
        time.sleep(10)
        try:
            if m._api_up():
                swaps = json.loads(m.xenia_api('/frame_stats')).get('swaps')
        except Exception:
            pass
        if swaps:
            break
    log('  warm-up launched %s: first frames after %d s (swaps=%s)' % (r.get('launched'), time.time() - t0, swaps))
    time.sleep(90)
    m.xenia_force_stop()
    t = {}
    for _ in range(45):
        t = json.loads(m.xenia_preflight()).get('temps', {})
        if t.get('gpu_c', 99) < 47 and t.get('case_c', 99) < 41:
            break
        time.sleep(20)
    log('  cooled: gpu %s case %s' % (t.get('gpu_c'), t.get('case_c')))


def run_route(tag):
    out = os.path.join(OUT, 'bd_bisect_%s.txt' % tag)
    with open(out, 'w', encoding='utf-8') as f:
        subprocess.call(['bash', ROUTE_COPY], stdout=f, stderr=subprocess.STDOUT, cwd=ROOT)
    text = open(out, encoding='utf-8', errors='replace').read()
    rows = [(int(a), float(b)) for a, b in re.findall(r'^\s*(\d+)\s+\d+\s+([0-9.]+)\s+\d+C', text, re.M)]
    late = [f for t, f in rows if t >= 120]
    gate = 'gameplay-tier scene confirmed' in text
    fps = statistics.median(late) if late else None
    log('  route: rows=%d late_median_fps=%s gate=%s abort=%s' % (
        len(rows), fps, gate, 'ABORT' in text))
    return fps if gate else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--good', default='4614144aeb')
    ap.add_argument('--bad', default='HEAD')
    ap.add_argument('--warm', type=int, default=480)
    ap.add_argument('--threshold-good', type=float, default=15.5)
    ap.add_argument('--threshold-bad', type=float, default=13.0)
    ap.add_argument('--max-steps', type=int, default=12)
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    shutil.copy(os.path.join(ROOT, 'tools', 'thor', 'bd_gameplay_route.sh'), ROUTE_COPY)
    shutil.copy(os.path.join(ROOT, 'tools', 'mcp', 'xenia_thor_mcp.py'), MCP_COPY)
    m = mcp()
    if git('status', '--porcelain', '--', 'src', 'tools', 'android').strip():
        log('tree not clean; stop'); return 1
    log('bisect start good=%s bad=%s' % (args.good, args.bad))
    log(git('bisect', 'start', args.bad, args.good))
    steps = 0
    try:
        while steps < args.max_steps:
            head = git('rev-parse', '--short', 'HEAD')
            subject = git('log', '-1', '--format=%ad %s', '--date=short')[:90]
            log('step %d: %s %s' % (steps + 1, head, subject))
            verdict = 'skip'
            if build_and_install():
                warm_and_cool(m, args.warm)
                fps = run_route(head)
                if fps is None:
                    # The scene gate failed (a cold cache fires the presses into
                    # the loading screen): warm once more, then one retry.
                    log('  gate failed; warming again and retrying the route once')
                    warm_and_cool(m, args.warm)
                    fps = run_route(head + '-retry')
                if fps is None:
                    verdict = 'skip'
                elif fps >= args.threshold_good:
                    verdict = 'good'
                elif fps <= args.threshold_bad:
                    verdict = 'bad'
                else:
                    log('  between thresholds; one more route')
                    warm_and_cool(m, 60)
                    fps2 = run_route(head + '-2')
                    both = [f for f in (fps, fps2) if f is not None]
                    mean = statistics.mean(both) if both else None
                    verdict = 'good' if mean and mean >= (args.threshold_good + args.threshold_bad) / 2 else 'bad'
            out = git('bisect', verdict)
            log('  git bisect %s -> %s' % (verdict, out.splitlines()[0][:120] if out else ''))
            steps += 1
            if 'is the first bad commit' in out:
                log('FIRST BAD COMMIT: ' + out[:600])
                break
    finally:
        log(git('bisect', 'reset'))
        log('master rebuild: %s' % ('ok' if build_and_install() else 'FAILED'))
        m.xenia_force_stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
