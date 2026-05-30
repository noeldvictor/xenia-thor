# UMA Quantification — Inconclusive + Metric Flaw (autonomous iter 7)

## Result: no clean UMA delta measurable with current instrumentation

Attempted a cap-independent A/B of `gpu_uma_direct_shared_memory` off vs on using
`vulkan_trace_perf_counters`, on Gears 3 menu and Burnout highway.

| run | scene @ capture | fps | issue_swaps | barriers/frame |
|-----|-----------------|-----|-------------|----------------|
| Gears off | menu (rendering) | 30 | 2040 | 156.9 |
| Gears on | early boot (crashed) | — | 570 | 16.0 |
| Burnout off | highway (rendering) | 59 | 3660 | 17.65 |
| Burnout on | black/transition | 20 | 2730 | 38.31 |

Two reasons the comparison is invalid:

1. **`buffer_barriers/frame` is not a valid UMA proxy.** The UMA direct-write path
   (`UploadRangesDirect`) removes the staging buffer + the `vkCmdCopyBuffer`
   transfer, but it STILL pushes one `HOST_WRITE -> shader/index read` memory
   barrier per upload batch (just like the staging path's transfer barrier). So
   the barrier count does not isolate the UMA effect; what UMA removes is the
   copy, which no existing perf counter tracks.
2. **Scene/timing variance.** The Start/A movie-skip lands each run in a different
   scene, so the two arms never captured the same state: Gears arm B crashed early
   (see below) and Burnout arm B ended black at a later transition. The barrier and
   fps differences are scene artifacts, not the cvar.

UMA path engaged correctly both times (`unified-memory direct-write path (memory
type 6, host-coherent)`), and an earlier in-session Burnout capture rendered the
UMA-on frame pixel-clean at 58 fps. So there is **no evidence of a UMA regression
or corruption** — just no controlled measurement.

## New finding: XamAlloc assert (candidate quick fix)

Gears 3 arm B aborted on `xam_info.cc:344 XamAlloc_entry: assertion "unk == 0"
failed` (hit intermittently). Same softenable-assert pattern as the Gears stride
fix — a kernel call asserting on an unexpected parameter. Candidate quick fix:
log + handle `unk != 0` instead of aborting (verify what unk carries first).

## Next (for a real UMA number)

1. Add an upload-path perf counter that isolates UMA: in `vulkan_shared_memory.cc`
   count staging `vkCmdCopyBuffer` calls + bytes + transfer microseconds (staging
   path) vs direct-mapped memcpy bytes (UMA path), exposed via the diagnostic
   counters. Then off-vs-on directly shows the removed copy work.
2. Capture a deterministic same-scene route (fixed input script or a save state)
   so the two arms are comparable; the current movie-skip is non-deterministic.
3. Cheaper interim: soften the XamAlloc assert so Gears stops crashing mid-A/B.
