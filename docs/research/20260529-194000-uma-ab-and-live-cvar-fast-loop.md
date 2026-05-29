# UMA Direct-Write A/B + Live-Cvar Fast Loop (AYN Thor c3ca0370)

## Status

Device-confirmed. Two results: (1) the unified-memory direct-write shared-memory
path is built, runs, and is visually correct on Burnout; (2) a live-cvar
broadcast + existing perf counters give a ~1-second on-device iteration loop.

## UMA direct-write A/B (gpu_uma_direct_shared_memory)

Built `B7C4A17F...` (NativeCore + ApkShell). Burnout Revenge, same intro/highway
route, cvar off vs on:

| arm | FPS~ | near_black | notes |
|-----|------|-----------|-------|
| off (baseline) | 59.8 | 0.10 | clean, crash=0 |
| on | 58.2 | 0.004 | clean, crash=1 (benign RtlRaiseException, same as baseline) |

- Path confirmed active: `Shared memory: using unified-memory direct-write path
  (memory type 6, host-coherent)`. The Thor exposes a HOST_VISIBLE|DEVICE_LOCAL
  host-coherent type, so it even skips the flush.
- **Correctness: PASS.** cvar-on frame is pixel-identical to baseline; the
  coherency hazard (host write racing in-flight GPU reads) did not manifest in
  this scene. The lone crash marker is the same benign RtlRaiseException baseline
  Burnout emits (no CRASH DUMP, no abort, no native backtrace).
- **Perf: neutral in this scene** because it is vsync-bound. Perf counters show
  active work of ~48 us/frame queue-submit, ~56 us/frame present, ~17 us/frame
  acquire against a 16,600 us frame budget. FPS cannot move when the GPU is ~99%
  idle waiting for vsync.
- **Tradeoff:** the UMA path forces a non-sparse full 512 MB shared-memory buffer
  (logged), a real memory cost on a shared-RAM handheld vs the sparse staging
  buffer.

### How to actually measure the UMA win

FPS is the wrong metric at the vsync cap. Use cap-independent counters from
`vulkan_trace_perf_counters`: `buffer_barriers`/frame and `queue_submits`/frame.
The UMA path removes a staging copy + a barrier per shared-memory upload, so
those counts should drop with the cvar on. Measure on a GPU-bound scene (heavy
gameplay), not the vsync-capped intro.

## Live-cvar fast loop (#1) + frame-time (#7)

Goal: collapse edit->rebuild->install->boot->navigate (minutes, blind) to
broadcast-a-cvar->read-telemetry (~1s, quantified). The dominant recurring cost
is boot+JIT+movies (~80s), not the build; most experiments are cvar toggles that
need no rebuild at all.

Implemented:

- `src/xenia/base/cvar.h` / `cvar.cc`: `ICommandVar::SetValueFromString` (impl in
  `CommandVar<T>` via `Convert` + `SetCommandLineValue`, i.e. command-line
  priority / immediate) and a `cvar::SetCommandVarFromString(name, value)` lookup
  over `ConfigVars`/`CmdVars`.
- `src/xenia/ui/windowed_app_context_android.cc`: JNI
  `nativeSetConfigVar(name, value)` calling the setter, logging applied/unknown.
- `EmulatorActivity.java`: `SET_CVAR` broadcast action wired into the existing
  debug receiver + intent filter + native decl. Also added
  `gpu_uma_direct_shared_memory` to the launch-intent allow-list.
- `tools/thor/thor_cvar.ps1`: `-Name <cvar> -Value <v>` fires the broadcast and
  echoes the result.

Proven on a running Burnout (no relaunch):

```
nativeSetConfigVar: vulkan_trace_perf_counters=true -> applied
SET_CVAR vulkan_trace_perf_counters=true applied
... Vulkan perf counters: reason=issue_swap ... present_us=... (frame-time stream)
nativeSetConfigVar: vulkan_trace_perf_counters=false -> applied   (toggles off too)
```

- **#7 frame-time is now free:** flipping `vulkan_trace_perf_counters` live starts
  the existing submit/present/acquire timing snapshots, revealing the headroom the
  FPS cap hides.
- **Caveat (built into the helper doc):** per-frame cvars apply live; init-only
  cvars read once at startup (e.g. `gpu_uma_direct_shared_memory`, which allocates
  its buffer at init) still need a relaunch -- but no rebuild.

## Next

- Quantify UMA via buffer_barriers/frame on a bound scene.
- Investigate persistent JIT/translation cache + save-state to cut the ~80s boot
  (the remaining big OODA cost).
