---
name: xenia-snapdragon-profiler-gpu-metrics
description: "Capture Adreno 740 hardware GPU stage metrics (% time shading vertices vs fragments, % shader stalled, primitives/sec, polygon area, texture/ALU/bus busy) for xenia on the AYN Thor using Qualcomm Snapdragon Profiler. Use to confirm WHICH GPU stage is the bottleneck (geometry/binning/vertex vs fragment/fill vs stall) when the headless KGSL busy% says GPU-bound but cannot say which stage. USER runs the GUI; this skill is the exact procedure + how Claude interprets the exported CSV."
---

# Snapdragon Profiler GPU metrics for xenia on AYN Thor (Adreno 740)

## When to use
The headless skill [xenia-thor-gpu-profile] answers "is the frame GPU-bound vs stalled" (KGSL
`gpu_busy_percentage`) and "what are the draws" (xenia's draw-outcomes log). It CANNOT split the
GPU's internal time across stages. Snapdragon Profiler reads the Adreno **hardware performance
counters** and gives that split — the decisive evidence for whether xenia is **vertex/geometry/
binning-bound** (our B60/B61 hypothesis: hundreds-to-1200 tiny draws/frame) vs **fragment/fill-bound**
vs **stalled on memory**. Get this BEFORE building the deep draw-batching change, so the batch
provably targets the real GPU cost.

Snapdragon Profiler is a **desktop GUI** (Windows). Claude cannot install or drive it. The USER runs
the capture; Claude gives the exact steps and interprets the exported CSV.

## Device facts (verified)
- AYN Thor, **USB-connected** (`adb devices -l` → `usb:... product:thor model:Thor`), serial `c3ca0370`.
- GPU: **Adreno 740v2**, max clock 680MHz. xenia package `jp.xenia.emulator.github.debug`.
- The heavy 3D scene that exhibits the bottleneck: Blue Dragon field, ~120-135s after launch.
- ADB: `C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`.

## One-time setup (USER)
1. Create a free Qualcomm account, download **Snapdragon Profiler** (qualcomm.com/developer →
   Snapdragon Profiler) and install on the Windows host.
2. Snapdragon Profiler bundles its own adb. **Close any other adb server first** (`adb kill-server`)
   so it doesn't fight the Android SDK adb — or point SP at the SDK platform-tools adb in its settings.
   Only ONE adb server can own the USB device at a time.
3. Keep the Thor on USB with USB debugging authorized. Confirm `adb devices` shows `c3ca0370 device`.
   No root needed — Adreno GPU metrics are readable on production devices via the KGSL driver SP uses.

## Capture procedure (USER) — Realtime mode (fastest, what we need)
1. In xenia on the Thor, launch Blue Dragon and reach the **heavy 3D field scene** (use the movie-skip
   launch from CLAUDE.md / xenia-thor-build-deploy; ~120-135s). Leave it running on that scene. The
   scene is animated, so metrics will be a steady average there — that's fine for stage attribution.
2. Snapdragon Profiler → **Connect** to the Thor (it auto-detects the USB device) → choose
   **Realtime** mode (a.k.a. "Realtime Metrics"), target process `jp.xenia.emulator.github.debug`.
3. Add the **GPU (Adreno) metric set**. Enable AT LEAST these counters (names are Qualcomm-standard;
   the exact label may vary slightly by SP version — match by meaning):
   - **% GPU Busy** / **GPU % Utilization** (sanity-check vs our KGSL 77-79%)
   - **% Time Shading Vertices**  ← KEY
   - **% Time Shading Fragments** ← KEY
   - **% Shaders Busy** / **% Shader ALU Capacity Utilized** / **% Time ALUs Working**
   - **% Vertex Fetch Stall** and **% Texture Fetch Stall**  ← stall evidence
   - **% Stalled on System Memory** / **GPU % Bus Busy** (memory-bound evidence)
   - **Primitives / Second** and **Average Polygon Area** (in pixels)  ← tiny-draw evidence
   - **% Time Compute** (xenia may run compute for EDRAM/primitive work)
   - **Clocks / Second** (is the GPU at max 680MHz?)
   - **Textures / Vertex**, **% Texture Pipes Busy** (texture-bound check)
4. Let it sample on the steady heavy scene for ~10-20s.
5. **Export the realtime metrics to CSV** (SP realtime view has an export/save-to-CSV button), or take
   a clear screenshot of the metric panel. Pull the CSV onto the host.

### Deeper (optional): Snapshot / Trace Capture
- **Snapshot** capture gives **per-drawcall** GPU counters + pipeline state (like RenderDoc + HW
  counters) — use to see which specific tiny draws cost the most. Heavier; needs the app to be
  capture-ready (xenia is Vulkan, supported).
- **Trace Capture** gives a timeline (CPU+GPU). Use if we need to see GPU idle gaps / submission
  cadence over time.

## What to hand Claude
- The exported **CSV** (preferred — Claude parses it), or a screenshot of the metric panel with the
  KEY counters visible, captured on the confirmed heavy 3D scene (say which scene).

## How Claude interprets it (decision tree)
Read the steady-state averages on the heavy scene:
- **% Time Shading Vertices >> % Time Shading Fragments** (e.g. vertices 50-80%, fragments low), AND
  **Average Polygon Area small** (sub-pixel to a few px), AND **high Primitives/sec** →
  **GEOMETRY/VERTEX/BINNING-BOUND**. CONFIRMS B60/B61 (death by tiny draws). The draw-batching /
  per-draw-cost-reduction work is correctly aimed → build it.
- **% Time Shading Fragments high** / **% Texture Pipes Busy high** → fragment/fill/texture-bound.
  Would contradict our resolution-independence result (B58) — re-examine before batching.
- **% Vertex/Texture Fetch Stall high** or **% Stalled on System Memory high** / **Bus Busy high** →
  memory/bandwidth-bound (the per-draw constant/vertex re-upload or binning parameter-buffer spilling
  to system memory). Fix shifts to reducing per-draw memory traffic, not just draw count.
- **% GPU Busy low** but app slow → not GPU-bound after all (contradicts KGSL 77-79%; re-check scene).
- Cross-check **Clocks/Second**: if not pinned near 680MHz under load, the governor is under-clocking
  (a separate, easy win — force max via KGSL `min_pwrlevel`/`force_clk_on` and re-measure).

Record the verdict + the key numbers in the worklog (next Bxx entry) and update memory
[[autonomous-blue-dragon-fullspeed]] with the confirmed bottleneck stage.

## Relationship to other skills
- [xenia-thor-gpu-profile] — headless busy%/clock + perfetto; the fast triage. Run FIRST.
- [xenia-vulkan-adreno-renderdoc] — per-drawcall structure (pipeline/descriptors/barriers), no HW
  stage counters. Use for "what did the GPU execute / why is it wrong".
- THIS skill — HW stage % split (which stage is the bottleneck). The three are complementary; this one
  is the deciding evidence for the geometry-vs-fragment-vs-stall question.

## Honesty note
Claude has NOT run Snapdragon Profiler itself (GUI, user-driven) and SP's exact menu labels differ
across versions — match counters by meaning, not exact string. Do not invent SP menu paths/CLI flags
in conversation; if a step is uncertain, say so and have the user confirm in their SP version.
