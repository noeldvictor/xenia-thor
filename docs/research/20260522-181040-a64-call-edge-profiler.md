# A64 Call-Edge Profiler

Date: 2026-05-22 18:10 EDT

## Question

Can we separate the inclusive `82282490` block body-time wall into direct guest
call edges before choosing the next Blue Dragon speed patch?

## Change

Added a default-off A64 direct-call edge profiler:

- cvar: `arm64_speed_profile_call_edge_filter`
- Thor script flag: `-Arm64SpeedProfileCallEdgeFilter`
- Android extra forwarding in `EmulatorActivity`
- per-edge counters and CNTVCT body ticks in `A64Function`
- speed-log rows shaped as `A64 speed profile call edge top ...`
- `tools/thor/thor_hir_call_path_report.ps1` now reports a
  `Dynamic Call Edge Body-Time Rows` section when those rows exist.

The profiler is intended for filtered caller functions only. For the current
route, the target filter is `82282490`.

## Captures

`scratch/thor-debug/20260522-175432-*`

- APK SHA: `23A6DCBDEBB1CCDDCBD022330F77161A57CA1A77876E38A63C50A9298A4A5362`
- Enabled `arm64_speed_profile_call_edge_filter=82282490`.
- Crashed before the 120s body-time activation.
- Log showed `ARM64 SIGSEGV could not classify access` with `pc` equal to a
  counter-ish address, followed by `Fatal signal 6`.
- Root cause: the first instrumentation sequence loaded the direct callee into
  `x9`, then called the atomic counter helper, which also used `x9` as scratch.
  The generated code branched to the counter address instead of the guest
  callee. The code now starts the edge profiler before loading the callee into
  `x9`.

`scratch/thor-debug/20260522-175951-*`

- APK SHA: `0E6F2CADBA61BAAF16866D1EA10B085C1DDC219570924260A2B292DCD975F311`
- Enabled `arm64_speed_profile_call_edge_filter=82282490`.
- No searched fatal markers.
- The route black-idled before `82282490`: counters went flat after the early
  boot burst, body-time activated at 120s, and no call-edge rows were emitted.
- Screenshot was black with Android system bars.

`scratch/thor-debug/20260522-180335-*`

- Same APK SHA: `0E6F2CADBA61BAAF16866D1EA10B085C1DDC219570924260A2B292DCD975F311`
- Control run with no call-edge filter.
- No searched fatal markers.
- It also black-idled before `82282490` and emitted no body-time target rows.

## Interpretation

The call-edge profiler is not proven to be a speed win, and it has not yet
produced the desired `822825E0 -> 82282490` or `822825C8 -> 8227FEE8` dynamic
rows because the fixed APK's route did not reach the opening-scene target in
either the edge run or the control run.

Do not blame the fixed profiler for this idle yet: the same APK without the
call-edge filter idled in the same early pattern. Treat this as route
instability until a control reaches the opening route again.

## Decision

Keep `arm64_speed_profile_call_edge_filter` default-off. Next step is a
route-stability/control check on the same APK or a small idle-attribution probe.
Only rerun the edge filter after a control reaches the opening route and
`82282490` body-time rows again.

