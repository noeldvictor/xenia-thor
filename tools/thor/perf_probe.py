"""Where does a title's frame go? The CPU/GPU split in one device launch.

  python tools/thor/perf_probe.py gears1 [--seconds 12] [--cvar name=value ...] [--no-profile]

Uses the scoreboard's routes (tools/thor/scoreboard.py ENTRIES) to reach the
measured scene, then in the same launch: the live values of the launch
cvars, presented fps over 10 s, the GPU frame time per frame from the pass
timestamps (median and p90), GPU busy, the hottest threads with their CPU
share, a simpleperf sample (the top host symbols and the guest functions)
unless --no-profile, and three screenshots 3 s apart. The verdict line:

  GPU-BOUND  the GPU frame time is >= 85% of the presented frame time
  CPU-BOUND  a thread runs >= 85% of a core and the GPU frame is short
  MIXED      neither

2026-09-22: the user found Gears "blurry, weird and super slow" (15.8 fps on
the scoreboard route); this is the first call for any "slow" report.
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


def gpu_frames(lines_text):
    vals = [int(x) for x in re.findall(r'GPU pass timing: gpu_frame_us=(\d+)', lines_text)]
    return [v for v in vals if v > 0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('entry')
    ap.add_argument('--seconds', type=int, default=12)
    ap.add_argument('--cvar', action='append', default=[])
    ap.add_argument('--no-profile', action='store_true')
    args = ap.parse_args()
    spec = dict(scoreboard.ENTRIES[args.entry])
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    cvars = ['vulkan_trace_pass_timestamps=true', 'vulkan_trace_draw_outcomes_per_frame=true'] + spec.get('cvars', []) + args.cvar
    for c in cvars:
        m.xenia_launch_cvars(set=c)
    temps = scoreboard.wait_cool()
    print('launch cvars:', ' '.join(cvars), '| case %.1f C' % temps.get('case_c', 0), flush=True)
    if spec['kind'] == 'goto':
        g = json.loads(m.xenia_goto(steps=spec['steps'], title=spec.get('title') or spec['path'],
                                    launch=True, screenshot=False)).get('goto') or {}
        print('route: reached=%s seconds=%s' % (g.get('reached'), g.get('seconds')), flush=True)
    else:
        m.xenia_launch(spec['path'], skip_preflight=True)
        t0 = time.time()
        for at, button in spec['presses']:
            time.sleep(max(0.0, at - (time.time() - t0)))
            m.xenia_press(button)
        time.sleep(max(0.0, spec['at'] - (time.time() - t0)))
    live = []
    for c in cvars:
        n = c.split('=', 1)[0]
        try:
            live.append('%s=%s' % (n, m._api('/cvar?name=%s' % n).get('value')))
        except Exception:
            live.append('%s=?' % n)
    print('live:', ' '.join(live), flush=True)
    m._adb('logcat', '-c')
    s0, t0 = scoreboard.swaps(), time.time()
    time.sleep(10)
    s1, t1 = scoreboard.swaps(), time.time()
    fps = (s1 - s0) / (t1 - t0) if (s0 is not None and s1 is not None) else 0.0
    log = m._adb('logcat', '-d', '-s', 'xenia:*', timeout=120)
    frames = gpu_frames(log)
    med = statistics.median(frames) if frames else None
    p90 = sorted(frames)[int(len(frames) * 0.9)] if frames else None
    try:
        st = m._api('/stall', timeout=30)
        hot = [(r.get('comm') or r.get('name'), r.get('cpu_pct')) for r in st.get('hot_threads', [])][:5]
    except Exception as e:
        hot = [('stall unavailable', str(e)[:60])]
    try:
        gpu_busy = m._shell('cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage').strip()
    except Exception:
        gpu_busy = None
    shots = []
    for i in range(3):
        shots.append(json.loads(m.xenia_screenshot('probe-%s-%d' % (args.entry, i))).get('path'))
        time.sleep(3)
    print('presented fps %.1f | gpu_frame_us median %s p90 %s (%d frames) | gpu busy %s' % (
        fps, med, p90, len(frames), gpu_busy), flush=True)
    print('hot threads:', ', '.join('%s %s%%' % (n, p) for n, p in hot), flush=True)
    # The command processor's CPU per frame (vulkan_trace_draw_outcomes_per_frame).
    import re as _re
    cpu = {}
    for k in ('draws', 'issuedraw_us', 'emit_us', 'setup_us', 'bind_us', 'process_us', 'vfres_us', 'breaks'):
        vals = [int(v) for v in _re.findall(r'GPU draw cpu/frame:.*?%s=(\d+)' % k, log)]
        if vals:
            cpu[k] = statistics.median(vals)
    if cpu:
        print('command processor per frame (median):', ' '.join('%s=%s' % kv for kv in cpu.items()), flush=True)
    frame_us = 1e6 / fps if fps > 0 else None
    top_cpu = max([p or 0 for _, p in hot] or [0])
    cp_us = cpu.get('issuedraw_us') if cpu else None
    if cp_us and frame_us and cp_us >= 0.6 * frame_us and (not med or cp_us > med):
        # The command processor's draw submission is the longest part of the
        # frame (Gears of War, 2026-09-22: ~40 of 54 ms, 30 of them vertex
        # residency) - the verdict "MIXED" hid this.
        verdict = ('COMMAND-PROCESSOR-BOUND (draw submission %d us of a %d us frame; vfres %s us, '
                   'emit %s us; gpu %s us) - see the GPU shmem/frame line with gpu_shared_memory_stats' % (
                       cp_us, frame_us, cpu.get('vfres_us'), cpu.get('emit_us'), med))
    elif med and frame_us and med >= 0.85 * frame_us:
        verdict = 'GPU-BOUND (gpu %d us of a %d us frame)' % (med, frame_us)
    elif top_cpu >= 85 and (not med or not frame_us or med < 0.6 * frame_us):
        verdict = 'CPU-BOUND (a thread at %s%% of a core, gpu %s us of %s us)' % (top_cpu, med, frame_us and int(frame_us))
    else:
        verdict = 'MIXED (gpu %s us of %s us, top thread %s%%)' % (med, frame_us and int(frame_us), top_cpu)
    print('verdict:', verdict, flush=True)
    if not args.no_profile:
        prof = json.loads(m.xenia_profile(seconds=args.seconds))
        for row in (prof.get('by_dso') or [])[:8]:
            print('  dso', row.strip()[:120])
        for row in (prof.get('top') or [])[:15]:
            print('  top', str(row).strip()[:140])
    print('screenshots:', ' '.join(p for p in shots if p), flush=True)
    m.xenia_force_stop()
    m.xenia_launch_cvars(clear=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
