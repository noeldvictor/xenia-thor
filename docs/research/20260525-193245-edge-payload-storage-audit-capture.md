# Edge Payload Storage Audit Capture

## Summary

Added a default-off, counter-only `arm64_blue_dragon_edge_payload_storage_audit`
skeleton for the exact Blue Dragon edge `82282490:82282598 -> 82287788`, then
ran a route-clean Thor capture. The audit is behavior-neutral:
`payload_materializations_allowed=0`, `behavior_changed=0`, normal-entry
fallback remains preserved, no `A64Function::machine_code()` replacement is
made, and the global indirection slot is unchanged.

The capture proves there is real payload volume on the edge, but it also proves
that the next step should be a lifetime/barrier design audit, not payload
materialization or a quiet speed A/B.

## Patch

- Added cvar `arm64_blue_dragon_edge_payload_storage_audit`.
- Added Android and Thor launcher plumbing.
- Added compile counters for the exact edge:
  `eligible_edge_compiles`, `variant_codegen_skipped`, `storage_missing`, and
  `normal_entry_owned`.
- Added runtime edge counters:
  `eligible_edge_calls`, `normal_entry_fallbacks`, `indirection_fallbacks`,
  `variant_misses`, `payload_materializations`, `marker_sets`, and
  `marker_clears`.
- Added payload counters for `f[1]`, `fpscr`, and `r[3]`.
- Added active-marker flush counters for helper-preserved calls,
  child-preserved calls, return/exit calls, unknown calls, context barriers,
  exception/trap, and external visibility.

## Validation

- `git diff --check` passed with only existing CRLF warnings.
- PowerShell parser check passed for `tools/thor/thor_xenia_debug.ps1`.
- `tools\thor\thor_build.ps1 -Mode NativeCore` passed.
- `tools\thor\thor_build.ps1 -Mode FullDeploy` passed and installed the APK.

## Thor Capture

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64BlueDragonEdgePayloadStorageAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788,82281D28" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Artifacts:

- `scratch/thor-debug/20260525-192928-speed-logcat.txt`
- `scratch/thor-debug/20260525-192928-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260525-192928-meta.txt`
- `scratch/thor-debug/20260525-192928-screenshot.png`
- `scratch/thor-debug/20260525-192928-perf-final.txt`

APK SHA:
`9DD345DCD8C404E0BDE50D3C67F72EF1CA105A2C9A4A3F7554462EF17BA567DF`.

The screenshot reached the visible opening sky / dragon-wing route. A fatal
marker search for Android fatal exceptions, fatal signals, tombstones, ANRs,
failed checks, assertions, panics, and crash strings was clean.

## Final Counter Rows

Final base row:

- `eligible_edge_compiles=0/1`
- `variant_codegen_skipped=0/1`
- `storage_missing=0/1`
- `normal_entry_owned=0/1`
- `eligible_edge_calls=246624/910159`
- `normal_entry_fallbacks=246624/910159`
- `indirection_fallbacks=246624/910159`
- `variant_misses=246624/910159`
- `payload_materializations=0/0`
- `marker_sets=246624/910159`
- `marker_clears=246624/910159`

Final payload row:

- `f1_seed_candidates=246624/910159`
- `f1_active_reads_covered=736260/2717039`
- `f1_inactive_reads=2108/4810`
- `f1_unknown_kills=0/0`
- `fpscr_seed_candidates=246624/910159`
- `fpscr_dirty_writes=770788/2849260`
- `fpscr_required_writebacks=250026/922692`
- `r3_seed_candidates=246624/910159`
- `r3_mutable_writes=736458/2718234`

Final flush row:

- `helper_preserved=250026/922692`
- `child_preserved=121716/449168`
- `return_exit=246624/910159`
- `unknown_call=0/0`
- `context_barrier=3131147/11561589`
- `exception_or_trap=0/0`
- `external_visibility=12751/48803`

## Body-Time / Perf

Final body-time rows:

- `82282490=51783317`, `entries_delta=62328`, `ticks_per_entry=228`
- `82281D28=10283586`, `entries_delta=29736`, `ticks_per_entry=94`
- `82287788=6836225`, `entries_delta=247636`, `ticks_per_entry=7`

Final thread sample:

- Main Thread: `103%`
- XMA Decoder: `46.1%`
- GPU Commands: `11.5%`
- Draw Thread: `3.8%`

## Decision

The edge payload idea is still alive because the route has:

- 910159 eligible hot-edge calls;
- about 2.99 covered active `f[1]` reads per eligible call;
- matching `f[1]`, `fpscr`, and `r[3]` seed volume;
- zero unknown calls and zero `f[1]` unknown kills;
- zero payload materializations and unchanged behavior.

It is not ready for a behavior patch because:

- `context_barrier=11561589` total is much larger than the edge-call count;
- `fpscr_dirty_writes=2849260` and `fpscr_required_writebacks=922692` mean
  `fpscr` cannot be bundled casually with `f[1]`;
- `r3_mutable_writes=2718234` keeps `r[3]` count-only for now;
- the existing route is still Main Thread / A64 generated-code bound, not a
  GPU/Vulkan wall.

Next: add an offline or counter-only lifetime/segment audit for
`82282490:82282598 -> 82287788` that answers how many `f[1]` / `r[3]` reads are
usable before the first context barrier, external visibility point, return/exit,
or required `fpscr` writeback. Do not materialize payload state or run a quiet
speed A/B until that audit shows useful surviving payload windows.
