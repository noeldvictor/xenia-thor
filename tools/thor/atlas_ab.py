"""Device A/B of the Banjo shadow-atlas accumulation: one launch per cvar set.

  python tools/thor/atlas_ab.py "vulkan_gate_rt_update=false" "rt_cache_ownership_claim_memo=false" ...

Each run: launch cvars = the resolve checksum trace (4 MB resolves only) +
the set under test, force-stop, launch Banjo, START on the puzzle, wait for
the green Spiral Mountain title, hold 25 s, then read the log: the largest
not_far24 of the last 60 atlas resolves (the PC stays under 7000; the
device climbed past 600000 on 2026-09-21 = casters piling up frame after
frame) and the frame stats' bottom_luma (the dark lower band). Prints one
line per set; the screenshots go to scratch/mcp. The app is force-stopped at
the end so the device is quiet.
"""
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402

TRACE = 'vulkan_trace_resolve_checksum=true,vulkan_trace_resolve_checksum_budget=-1,vulkan_trace_resolve_checksum_length=4194304'
ROUTE = 'name:puzzle;until:gold>0.35;press:START;settle:4000|name:spiral;until:green>0.03&gold<0.2;timeout:90'


def wait_cool(max_case_c=41.0, max_wait_s=1500):
    """Directive 8: a series measures the throttle unless the case cools
    between runs. Polls the preflight temperatures every 30 s."""
    t0 = time.time()
    while time.time() - t0 < max_wait_s:
        temps = json.loads(m.xenia_preflight()).get('temps', {})
        if temps.get('case_c', 0) < max_case_c:
            return temps
        print('  cooling: case %.1f C gpu %.1f C' % (temps.get('case_c', 0), temps.get('gpu_c', 0)), flush=True)
        time.sleep(30)
    return temps


def run(label, cvars, hold=40):
    m.xenia_force_stop()
    temps = wait_cool()
    m.xenia_launch_cvars(clear=True)
    m.xenia_launch_cvars(set=TRACE + (',' + cvars if cvars else ''))
    m.xenia_force_stop()
    time.sleep(1)
    subprocess.call(['adb', 'logcat', '-c'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = json.loads(m.xenia_launch('banjo', skip_preflight=True))
    if not r.get('launched'):
        return '%s: launch failed %s' % (label, r)
    t0 = time.time()
    while time.time() - t0 < 60 and not m._api_up():
        time.sleep(1)
    g = json.loads(m.xenia_goto(steps=ROUTE, launch=False, screenshot=False)).get('goto') or {}
    reached = all(s.get('holds') for s in g.get('steps', [])) if g.get('steps') else False
    time.sleep(hold)
    stats = json.loads(m.xenia_api('/frame_stats'))
    shot = json.loads(m.xenia_screenshot('atlas-' + re.sub(r'[^A-Za-z0-9]+', '_', label)[:40])).get('path')
    log = subprocess.run(['adb', 'logcat', '-d', '-s', 'xenia'], capture_output=True, text=True, errors='replace').stdout
    vals = [int(x) for x in re.findall(r'shared-memory checksum .*?not_far24=(\d+)', log)]
    # The first resolves are the boot garbage (the whole atlas "not far");
    # after that the PC never exceeds ~7000 at any moment, so the maximum
    # over the whole title stay is the accumulation measure.
    after = vals[40:]
    over = sum(1 for v in after if v > 20000)
    fps = json.loads(m.xenia_fps()).get('presented') or {}
    return '%s: reached=%s resolves=%d max_not_far=%s over20k=%d/%d bottom_luma=%s green=%s swaps=%s fps_median=%s start_case=%.1fC shot=%s' % (
        label, reached, len(vals), max(after) if after else None, over, len(after), stats.get('bottom_luma'),
        stats.get('green'), stats.get('swaps'), fps.get('median_fps'), temps.get('case_c', 0), os.path.basename(shot or ''))


def main():
    sets = sys.argv[1:] or ['']
    for cv in sets:
        print(run(cv or 'baseline', cv), flush=True)
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)


if __name__ == '__main__':
    main()
