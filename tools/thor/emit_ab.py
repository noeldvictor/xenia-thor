"""One device launch, many answers: live cvar A/B of the frame cost in the
scene after the Banjo puzzle.

  python tools/thor/emit_ab.py "vulkan_cache_vertex_residency=true" ...            (live cvars, one launch)
  python tools/thor/emit_ab.py --launch "vulkan_merge_draws_indirect=true" ...      (STARTUP cvars, one launch each)

Launches Banjo with the per-frame draw-outcome lines and the GPU pass
timestamps on, presses START on the gold puzzle, waits for the puzzle to
leave (the title or the menu, both heavy 3D scenes; the comparison is within
the one launch), then for the baseline and for each cvar setting in turn:
sets it live through the app's cvar_set, waits 12 s, and reads

  fps       presented frames: the sum of the FPS badge's swaps over the sum
            of its windows (0.1 fps resolution; the badge's own fps value is
            quantized to swaps per half second, 7.9 or 9.9, and hides a gain)
  draws, issue_us, emit_us, vfres_us, breaks
            medians of the last 40 "GPU draw outcomes/frame" lines
  gpu_us    median gpu_frame_us of the "GPU pass timing" lines
  cpu       the hottest host threads (the /stall probe's one-second sample
            at the end of the window)

Each setting is restored before the next. One launch instead of one per
setting; the device cools first (directive 8). Prints one line per setting.
A lever already at its target value is skipped: the first run of this
harness read six "wins" from six no-ops (the attract camera moved).
"""
import json
import re
import subprocess
import sys
import time

ROOT = __import__('os').path.abspath(__import__('os').path.join(__import__('os').path.dirname(__file__), '..', '..'))
sys.path.insert(0, ROOT + '/tools/mcp')
import xenia_thor_mcp as m  # noqa: E402

ROUTE = 'name:puzzle;until:gold>0.35;press:START;settle:3000|name:after;until:gold<0.2;timeout:60'
FIELDS = ('rendered', 'cpu_issuedraw_us', 'cpu_emit_us', 'cpu_vfres_us', 'pass_break_barrier')
LAUNCH_CVARS = ('vulkan_trace_draw_outcomes_per_frame=true', 'vulkan_trace_pass_timestamps=true')
FAULT_MARKS = ('CRASH DIAG', 'fault STORM', 'Null critical', 'kernel trap hit')


def wait_cool(max_case_c=41.0):
    for _ in range(50):
        t = json.loads(m.xenia_preflight()).get('temps', {})
        if t.get('case_c', 0) < max_case_c:
            return t
        time.sleep(30)
    return t


def cvar_set(name, value):
    return m._api('/cvar?name=%s&value=%s' % (name, value), 'POST')


def cvar_get(name):
    try:
        return m._api('/cvar?name=%s' % name).get('value')
    except Exception:
        return None


def median(vals):
    vals = sorted(vals)
    return vals[len(vals) // 2] if vals else None


def logcat(*tags):
    return subprocess.run(['adb', 'logcat', '-d', '-s'] + list(tags), capture_output=True,
                          text=True, errors='replace').stdout


def save_faults():
    """Keep the fault evidence before the log is cleared: a stalled route is
    a parked thread whose one diag line lived in this buffer (2026-09-22)."""
    faults = [l for l in logcat('xenia').splitlines() if any(k in l for k in FAULT_MARKS)]
    if faults:
        with open(ROOT + '/scratch/banjo/rd/faults.txt', 'a', encoding='utf-8') as f:
            f.write('\n'.join(faults[-6:]) + '\n')
        print('  (%d fault lines saved to scratch/banjo/rd/faults.txt)' % len(faults), flush=True)


def measure(label, seconds=12, top=4):
    save_faults()
    subprocess.call(['adb', 'logcat', '-c'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(seconds)
    log = logcat('xenia', 'xenia-fps')
    lines = [l for l in log.splitlines() if 'GPU draw outcomes/frame:' in l][-40:]
    med = {}
    for key in FIELDS:
        med[key] = median(float(x) for x in re.findall(r'\b%s=(-?\d+(?:\.\d+)?)' % re.escape(key), '\n'.join(lines)))
    gpu = median(int(x) for x in re.findall(r'GPU pass timing: gpu_frame_us=(\d+)', log))
    swaps = [(int(a), int(b)) for a, b in re.findall(r'xenia-fps: fps=[0-9.]+ swaps=(\d+) window_ms=(\d+)', log)]
    total_ms = sum(w for _, w in swaps)
    fps = round(1000.0 * sum(s for s, _ in swaps) / total_ms, 1) if total_ms else None
    # The hottest host threads over the probe's own one-second sample.
    cpu = []
    try:
        for t in (m._api('/stall').get('hot_threads') or [])[:top]:
            cpu.append('%s=%s%%' % (str(t.get('comm', ''))[:14], t.get('cpu_pct')))
    except Exception:
        pass
    print('%-40s fps=%s draws=%s issue_us=%s emit_us=%s vfres_us=%s breaks=%s gpu_us=%s cpu[%s]' % (
        label, fps, med['rendered'], med['cpu_issuedraw_us'], med['cpu_emit_us'],
        med['cpu_vfres_us'], med['pass_break_barrier'], gpu, ' '.join(cpu)), flush=True)
    return {'label': label, 'fps': fps, 'gpu_us': gpu, 'cpu': cpu, **med}


def launch(extra=()):
    """Force-stop, cool, launch Banjo with the trace cvars plus extra, run the
    route. Returns True when the route reached the scene after the puzzle."""
    m.xenia_force_stop()
    t = wait_cool()
    m.xenia_launch_cvars(clear=True)
    for c in tuple(LAUNCH_CVARS) + tuple(extra):
        m.xenia_launch_cvars(set=c)
    r = json.loads(m.xenia_launch('banjo', skip_preflight=True))
    if not r.get('launched'):
        print('launch failed', r)
        return False
    t0 = time.time()
    while time.time() - t0 < 60 and not m._api_up():
        time.sleep(1)
    g = json.loads(m.xenia_goto(steps=ROUTE, launch=False, screenshot=False)).get('goto') or {}
    print('route: reached=%s seconds=%s start_case=%.1fC extra=%s' % (
        g.get('reached'), g.get('seconds'), t.get('case_c', 0), ' '.join(extra) or '-'), flush=True)
    if not g.get('reached'):
        save_faults()
        try:
            trap = m._api('/trap')
            if trap.get('hits'):
                print('  trap record: %s tid=%s lr=%s ctr=%s chain=%s' % (
                    trap.get('export'), trap.get('tid'), trap.get('lr'), trap.get('ctr'),
                    ' '.join(trap.get('chain') or [])[:200]), flush=True)
        except Exception:
            pass
    time.sleep(6)
    return bool(g.get('reached'))


def main():
    settings = sys.argv[1:]
    if settings and settings[0] == '--launch':
        # STARTUP cvars (the draw-merge levers, the pass-timestamp trace):
        # one launch per setting, the baseline launch first and last.
        for extra in [()] + [(s,) for s in settings[1:]] + [()]:
            if launch(extra):
                measure(' '.join(extra) or 'baseline')
        m.xenia_force_stop()
        m.xenia_launch_cvars(clear=True)
        return 0
    if not launch():
        return 1
    measure('baseline')
    for setting in settings:
        name, _, value = setting.partition('=')
        before = cvar_get(name)
        if str(before).lower() == value.lower():
            print('%-40s SKIPPED: already %s' % (setting, before), flush=True)
            continue
        cvar_set(name, value)
        measure('%s (was %s)' % (setting, before))
        if before is not None:
            cvar_set(name, before)
    measure('baseline again')
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
