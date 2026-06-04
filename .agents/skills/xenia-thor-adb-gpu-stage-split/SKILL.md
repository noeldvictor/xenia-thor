---
name: xenia-thor-adb-gpu-stage-split
description: "Get the per-stage Adreno 740 GPU split (binning/vertex vs fragment vs stall, and per-DRAW cost) for xenia on the AYN Thor over FULL ADB on the debuggable app — Mesa/Turnip freedreno perfetto counter producer, gfxreconstruct .gfxr capture+replay profiling, in-engine per-pass Vulkan timestamps, AGI CLI. NO Snapdragon Profiler / AGI GUI, NO root. Use this to answer the one load-bearing Blue Dragon unknown: is the ~333us/DRAW binning cost a removable per-draw stall/context-roll or an irreducible per-primitive binning floor."
---

# Per-stage Adreno GPU split over FULL ADB (no GUI, no root)

## The gap this fills (and why the GUI punt was wrong)
- `xenia-thor-gpu-profile` (KGSL `gpu_busy_percentage`) answers **bound vs stalled**, not which stage.
- `xenia-snapdragon-profiler-gpu-metrics` gets the **vertex-vs-fragment split** but PUNTS to a USER-run
  desktop GUI. That punt is unnecessary: the xenia package is **debuggable**
  (`jp.xenia.emulator.github.debug`) and **we ship Turnip ourselves** (libadrenotools), so full ADB
  reaches the GPU hardware counters through the **same per-app driver path AGI uses on production
  devices** — no root. This skill is the headless ADB methodology; the GUI is LAST resort, not first.

## The honest constraint (don't repeat the perfetto-empty trap)
Headless `adb shell perfetto` with **KGSL ftrace events** (`kgsl/adreno_cmdbatch_*`, `power/gpu_*`)
returns **empty** on the retail Thor: shell is uid 2000, can READ tracefs but `echo 1 > events/*/enable`
= Permission denied, no `su` (verified, CLAUDE.md). So the KERNEL-ftrace route is dead headless.
The routes below DON'T use kernel ftrace — they use the **GPU driver's own per-context counter access**
(which a debuggable app's GPU context is allowed) or **offline replay** of a captured command stream.
**VERIFICATION GATE for every route: confirm the artifact has NON-ZERO events/draws/counters before
trusting it** (a 782–1026 B "trace" is metadata-only = empty = discard).

Device: serial `c3ca0370`, `/sys/class/kgsl/kgsl-3d0`, **Adreno 740v2**, max 680MHz. Turnip lib in the
app at `/data/data/<pkg>/files/turnip/vulkan.ad07xx.so`. ADB:
`C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`. Heavy BD scene ~120–135s
after launch (verify with a screenshot). **Thermal gate (NEVER thrash): only launch via
`tools/thor/thor_gpu_capture.ps1` — refuses ≥55°C, 64°C watchdog, ONE launch/fire, force-stops when hot.**

## Route A — in-engine per-PASS Vulkan timestamps (ours; always works; partial split)
We own the command buffers, so add `vkCmdWriteTimestamp` brackets around each logical region xenia
already submits and log the deltas alongside `gpu_frame_us`:
- binning/EDRAM-tile setup vs the main color pass vs the EDRAM resolve/copy vs any compute.
- Emit as new fields on the existing `GPU draw outcomes/frame` line (cvar-gated, default-off, read over
  ADB logcat). No external tool.
- **Limit:** Vulkan timestamps bracket COMMAND-BUFFER regions, not the driver's *internal* per-tile
  binning-vs-fragment phases — on a TBDR the bin pass + fragment pass happen INSIDE one render pass.
  So Route A attributes time ACROSS passes (decisive for "is it the resolve? the EDRAM copy? a stall
  between submits?") but CANNOT split binning-VS-fragment within a pass. For that, Route B/C.

## Route B — Mesa/Turnip freedreno Perfetto counter producer (the real HW per-stage split) ⭐
We ship Turnip, and Mesa has a **freedreno Perfetto producer** (`pps-producer`, datasource
`gpu.counters.msm` / `gpu.renderstages.msm`) that exposes Adreno HW perf counters + GPU render-stage
slices as a Perfetto data source — **this is the engine AGI uses for freedreno**. It reads counters via
the driver's own per-context path, not kernel ftrace, so it can work without root for OUR debuggable
app's GPU context.
- Build/enable: Turnip must be built with `-Dperfetto=true` (Mesa option) so the producer is compiled
  in; the producer connects to the on-device `traced`. Enable per-stage tracing via Mesa env the app
  reads (`TU_DEBUG`, `MESA_GPU_TRACES=...`) — wire these through the existing
  `gpu_vulkan_driver_debug` / extra-args plumbing in `thor_gpu_capture.ps1`.
- Capture: run `perfetto` with a `gpu.renderstages.msm` / `gpu.counters.msm` data source (NOT
  `linux.ftrace`), pull the `.pb`, read offline with `pip install perfetto` TraceProcessor SQL on the
  `gpu_slice` / counter tables.
- **VERIFY first:** confirm our shipped Turnip `.so` was built with the perfetto producer (check Mesa
  build flags / `strings vulkan.ad07xx.so | grep -i perfetto`); if not, this route needs a Turnip
  rebuild with `-Dperfetto=true` before it can produce data. Don't claim it works until a capture shows
  non-zero render-stage slices.

## Route C — gfxreconstruct .gfxr capture, then OFFLINE replay-profiling (per-DRAW GPU cost) ⭐
Enable the gfxreconstruct Vulkan layer for the debuggable app (no root), capture one heavy frame, pull
the `.gfxr`, replay it offline with per-draw timing:
```
adb shell settings put global enable_gpu_debug_layers 1
adb shell settings put global gpu_debug_app jp.xenia.emulator.github.debug
adb shell settings put global gpu_debug_layers VK_LAYER_LUNARG_gfxreconstruct
adb shell setprop debug.gfxrecon.capture_file /sdcard/Download/bd.gfxr
adb shell setprop debug.gfxrecon.capture_trigger_frames 1   # arm a 1-frame trigger on the heavy scene
# launch via the thermal-gated tool, reach the heavy scene, trigger, then:
adb pull /sdcard/Download/bd.gfxr
gfxrecon-replay -m rewrite bd.gfxr     # or --measurement-file for per-frame GPU timing
```
- The layer `.so` (`libVkLayer_gfxreconstruct.so`) must be loadable by the app — ship it in the APK
  `lib/arm64` (like the libadrenotools hook) or push into the app data dir; debuggable apps load layers
  the `gpu_debug_layers` setting names.
- Gives draw-by-draw replay you can profile (which specific draws dominate, whether collapsing them
  changes GPU time) — decisive for "is the per-draw cost removable". Heaviest setup; highest signal.
- **VERIFY:** `gfxrecon-info bd.gfxr` shows a real draw count (~1100–2180 for BD) before trusting it.

## Route D — AGI command-line (gapit/agi CLI), headless
AGI (Android GPU Inspector) drives a debuggable app over ADB and its CLI can capture a system profile /
frame profile non-interactively (the GUI is optional). If the AGI CLI is installed on the host, use it
for the per-stage frame profile on freedreno; same counter engine as Route B. Verify the capture has
GPU stage data before trusting.

## Route E — Snapdragon Profiler / AGI GUI (LAST resort only)
Only if A–D are all blocked on this device. See `xenia-snapdragon-profiler-gpu-metrics`. Treat as the
fallback, not the plan.

## Decision tree — answer the load-bearing BD question
Goal: classify the ~333us/DRAW (~140us fixed) BD binning cost.
- **% time shading vertices >> fragments, tiny avg polygon area, high primitives/sec** (Route B/D), or
  **per-draw GPU time ~flat regardless of vertex count** (Route C) → **per-DRAW-FIXED binning setup.**
  Then test: does collapsing draws (draw-concat / MDI, already toggles) cut GPU time in replay? If YES
  → it's a removable per-draw relatch/context-roll → revive dynamic-state collapse + draw-coalescing.
  If NO even in offline replay → **irreducible per-primitive binning floor → BD heavy vista is
  structurally capped (~1.5fps); stop spending on draw-count levers.**
- **Time dominated by the EDRAM resolve / a between-submit stall** (Route A) → not binning at all; fix
  the resolve/serialization instead.
- **Fragment/texture high** → contradicts the resolution-independence result; re-examine before any
  geometry work.
Record the verdict + numbers in the worklog and update `[[major-refactor-build-progress]]` /
`[[autonomous-blue-dragon-fullspeed]]`.

## Discipline
ONE launch per fire via the thermal-gated tool; read the PNG every device fire to confirm the matched
heavy scene; compare only at matched `guest_ms` AND equal `rendered`; never fabricate (numbers from the
output you read THIS turn); force-stop the package when done. If a route produces an empty/metadata-only
artifact, say so and move to the next route — do NOT report an empty capture as a result.
