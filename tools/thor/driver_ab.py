"""Which Turnip build and which TU_DEBUG options make a scene faster? Short, heat-aware.

  python tools/thor/driver_ab.py gears1 ARM ... [--seconds 12] [--max-case-c 44]
  ARM = "label:zip=path/to/turnip.zip"          install + select this driver
      | "label:driver=<installed id>"            select an installed driver
      | "label:tu=perf,gmem_warmup"              TU_DEBUG for this arm (gpu_vulkan_driver_debug)
      | "label:cvars=gpu_uma_zero_copy=true;x=1" extra launch cvars for this arm (';' between)
      (they combine: "zc:zip=scratch/tools/turnip/...-xe2.zip,cvars=gpu_uma_zero_copy=true")

The custom Turnip is required for this emulator (README); a driver is a
paradigm axis, so each arm names the build it ran. Per arm: select the
driver, launch the scoreboard entry's scene (tools/thor/scoreboard.py), wait
for the scene, measure presented fps and the median GPU frame time for
--seconds, collect Turnip's own log lines (with tu=perf: the reasons it
disabled concurrent binning, VSC overflows, sysmem/gmem choices), stop the
app. Each row also names the unified-memory state the log reports (zero-copy
ACTIVE, or the reason it fell back). Heat: the run ends before an arm when the
case is above --max-case-c (user, 2026-09-22: "we cannot let the emulator fry
the device") or when the session passed --max-minutes (user, 2026-09-23: 5
minute sessions, stop at 44 C). Ask the user before every run (device rule,
user 2026-09-23: "always ask me before you use thor").
"""
import argparse
import json
import os
import re
import statistics
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'thor'))
import xenia_thor_mcp as m  # noqa: E402
import scoreboard  # noqa: E402


def parse_arm(spec):
    label, _, rest = spec.partition(':')
    opts = {}
    # Split only before the arm keys: a cvars value holds '=' itself.
    for item in [i for i in re.split(r',(?=(?:zip|driver|tu|cvars)=)', rest) if i.strip()]:
        k, _, v = item.partition('=')
        opts[k.strip()] = v.strip()
    return label, opts


def zero_copy_state(log):
    """The unified-memory line of this run: ACTIVE, the fallback reason, or off."""
    lines = [l for l in log.splitlines() if 'gpu_uma_zero_copy' in l and 'Shared memory' in l]
    if any('ACTIVE' in l for l in lines):
        return 'ACTIVE'
    if lines:
        return 'fallback: ' + lines[-1][lines[-1].find('gpu_uma_zero_copy'):][:120]
    return 'off'


def case_c():
    return json.loads(m.xenia_preflight()).get('temps', {}).get('case_c', 99)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('entry')
    ap.add_argument('arms', nargs='+')
    ap.add_argument('--seconds', type=float, default=12.0)
    ap.add_argument('--max-case-c', type=float, default=44.0)
    ap.add_argument('--max-minutes', type=float, default=5.0)
    args = ap.parse_args()
    spec = dict(scoreboard.ENTRIES[args.entry])
    rows = []
    t_session = time.time()
    for label, opts in [parse_arm(a) for a in args.arms]:
        c = case_c()
        if c > args.max_case_c:
            print('STOP before %s: case %.1f C > %.1f C' % (label, c, args.max_case_c), flush=True)
            break
        if time.time() - t_session > args.max_minutes * 60:
            print('STOP before %s: session passed %.1f min' % (label, args.max_minutes), flush=True)
            break
        m.xenia_force_stop()
        m.xenia_launch_cvars(clear=True)
        m.xenia_launch_cvars(set='vulkan_trace_pass_timestamps=true')
        m.xenia_launch_cvars(set='vulkan_trace_draw_outcomes_per_frame=true')
        for cv in spec.get('cvars', []):
            m.xenia_launch_cvars(set=cv)
        if opts.get('tu'):
            m.xenia_launch_cvars(set='gpu_vulkan_driver_debug=' + opts['tu'])
        for cv in [c.strip() for c in opts.get('cvars', '').split(';') if c.strip()]:
            m.xenia_launch_cvars(set=cv)
        m._adb('logcat', '-c')
        # The driver manager lives in the app: launch the scene first, then
        # install/select, and relaunch so the selection applies.
        if opts.get('zip') or opts.get('driver'):
            m.xenia_launch(spec.get('title') or spec['path'], skip_preflight=True)
            time.sleep(8)
            if opts.get('zip'):
                r = json.loads(m.xenia_gpu_driver(install_zip=opts['zip']))
            else:
                r = json.loads(m.xenia_gpu_driver(use=opts['driver']))
            print('  %s driver: %s' % (label, json.dumps(r)[:160]), flush=True)
            m.xenia_force_stop()
        if spec['kind'] == 'goto':
            g = json.loads(m.xenia_goto(steps=spec['steps'], title=spec.get('title') or spec['path'],
                                        launch=True, screenshot=False)).get('goto') or {}
        else:
            m.xenia_launch(spec['path'], skip_preflight=True)
            t_launch = time.time()
            for at, button in spec['presses']:
                time.sleep(max(0.0, at - (time.time() - t_launch)))
                m.xenia_press(button)
            time.sleep(max(0.0, spec['at'] - (time.time() - t_launch)))
            g = {'reached': (scoreboard.swaps() or 0) > 0}
        s0, t0 = scoreboard.swaps(), time.time()
        time.sleep(args.seconds)
        s1, t1 = scoreboard.swaps(), time.time()
        fps = (s1 - s0) / (t1 - t0) if (s0 is not None and s1 is not None) else 0.0
        log = m._adb('logcat', '-d', timeout=120)
        gpu = [int(x) for x in re.findall(r'GPU pass timing: gpu_frame_us=(\d+)', log) if int(x) > 0]
        tu_lines = [l[l.find('TU'):][:160] for l in log.splitlines()
                    if ('TU:' in l or 'tu_' in l or 'turnip' in l.lower()) and 'perf' in l.lower()]
        driver_line = next((l[l.find('driverInfo'):][:100] for l in log.splitlines() if 'driverInfo' in l), '?')
        zero_copy = zero_copy_state(log)
        m.xenia_force_stop()
        row = {'arm': label, 'reached': g.get('reached'), 'fps': round(fps, 1),
               'gpu_frame_us': statistics.median(gpu) if gpu else None, 'driver': driver_line,
               'tu_perf_lines': len(tu_lines), 'case_c': c, 'zero_copy': zero_copy,
               'cvars': opts.get('cvars', '')}
        rows.append(row)
        print('%-12s fps %5.1f gpu %s us  %s  case %.1f C  uma: %s' % (
            label, fps, row['gpu_frame_us'], driver_line, c, zero_copy), flush=True)
        # The most frequent Turnip perf reasons (concurrent binning off, VSC overflow, ...).
        counts = {}
        for l in tu_lines:
            key = re.sub(r'\d+', 'N', l)[:120]
            counts[key] = counts.get(key, 0) + 1
        for k, v in sorted(counts.items(), key=lambda kv: -kv[1])[:8]:
            print('    %5d x %s' % (v, k), flush=True)
    m.xenia_launch_cvars(clear=True)
    out = os.path.join(ROOT, 'scratch', 'mcp', 'driver_ab-%s.json' % time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(rows, open(out, 'w'), indent=1)
    print('saved', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
