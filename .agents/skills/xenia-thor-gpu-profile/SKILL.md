---
name: xenia-thor-gpu-profile
description: Capture real GPU activity (busy %, clock, per-process active time, perfetto timeline) for xenia on the AYN Thor (Adreno 740) over ADB, headless — no Snapdragon Profiler / AGI GUI needed. Use to decide whether a slow frame is GPU-compute/throughput-bound vs GPU-stalled/starved, and to localize GPU cost.
---

# xenia-thor GPU profiling (Adreno 740, headless over ADB)

The AYN Thor exposes what Android GPU Inspector / Snapdragon Profiler read, **directly over ADB** — capture and interpret GPU activity without a desktop GUI. Two layers:

1. **KGSL sysfs** — instant GPU busy% + clock (fast triage).
2. **perfetto** (on-device `/system/bin/perfetto`, SDK 33) with `power/gpu_frequency` + `power/gpu_work_period` ftrace events — per-process GPU-active-time timeline.

Device: serial `c3ca0370`, GPU node `/sys/class/kgsl/kgsl-3d0`, **Adreno 740v2**. Freq table (MHz): 680 615 550 475 401 348 295 220 124.8 → **max 680MHz**. Package `jp.xenia.emulator.github.debug`. ADB: `C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`.

## Decisive triage: GPU busy% + clock — READ ON THE LIVE HEAVY SCENE

Reach the heavy 3D scene first (~120–135s after launch with the movie-skip seq) and verify with a screenshot. Reading at a menu is meaningless (idle ≈ 3%).

```bash
adb=".../adb.exe"
for i in $(seq 1 12); do
  busy=$("$adb" -s c3ca0370 shell "cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage" | tr -d '\r')
  clk=$( "$adb" -s c3ca0370 shell "cat /sys/class/kgsl/kgsl-3d0/clock_mhz")
  echo "busy=$busy clk=$clk"; sleep 1
done
```

Fork:
- **busy ≥70%** → GPU-bound on real work. Fix = reduce GPU WORK; sub-localize which kind (below).
- **busy <30% on the live heavy scene** → GPU idle/STALLED/STARVED (CPU submit cadence, mid-frame fences, serialization). Fix = remove the stall.

> MEASURED 2026-05-31 (Blue Dragon live heavy 3D field, ~1-2fps): **busy = 77–79% @ 615MHz (max 680)**, stable across 28 samples (two independent polls). GPU genuinely ~78% busy = GPU-bound. (An earlier 3% read was at a MENU → caused a wrong "stall" conclusion; always read on the verified live heavy scene.)

## Sub-localizing GPU work (content-matched cvar harness; already done for Blue Dragon)

Compare configs ONLY at matched content: match `guest_ms` AND require equal `rendered` (relaunch A/B without this is scene-confounded — it once produced a fake "9× load/store"). Measured shares of GPU:
- load/store (`--ez gpu_edram_passes_dont_care true`): ~3%.
- pass breaks (`--ez gpu_skip_edram_transfers true`): ~12%.
- fill/resolution (`draw_resolution_scale` 1→2 in device TOML = 4× pixels): **0%** → not fill-bound.
- geometry/draws: `gpu_frame_us` corr ~0.88 with `total_vertices`, super-linear → dominant cost is GEOMETRY / per-draw / binning / state front-end (~2000 tiny draws/frame, avg ~136 verts, ~2000 descriptor binds/frame).

## perfetto GPU timeline (AGI-equivalent, verified working headless)

```bash
CFG='
buffers { size_kb: 65536 }
data_sources { config { name: "linux.ftrace" ftrace_config {
  ftrace_events: "power/gpu_frequency" ftrace_events: "power/gpu_work_period" atrace_categories: "gfx" } } }
data_sources { config { name: "linux.sys_stats" sys_stats_config { devfreq_period_ms: 50 } } }
duration_ms: 5000
'
echo "$CFG" | "$adb" -s c3ca0370 shell "perfetto -c - --txt -o /data/misc/perfetto-traces/xenia_gpu.pb"
"$adb" -s c3ca0370 pull /data/misc/perfetto-traces/xenia_gpu.pb ./xenia_gpu.pb
```

Read without a GUI via `pip install perfetto` → TraceProcessor SQL on `gpu_work_period` slices / gpu freq counters.

## Fallback: Snapdragon Profiler / AGI (GUI, USER-driven)

For the binning-vs-rendering per-pass split the on-device render-stages producer does NOT emit, ask the user to run **Snapdragon Profiler** → Trace Capture → GPU Metrics (% GPU/ALU/texture/vertex/fragment busy + % shaders stalled) on the heavy scene, or **AGI** Frame Profile (Vulkan). That capture confirms the geometry/binning hypothesis. Prefer headless busy% first.

## Workflow
1. Launch Blue Dragon, wait ~120–135s, screenshot to confirm heavy 3D scene.
2. busy%+clock triage → bound vs stalled.
3. If bound, sub-localize via the content-matched harness (done: geometry/front-end bound).
4. perfetto / Snapdragon Profiler for the binning-vs-rendering split.
5. Force-stop the package when done.
