# Blue Dragon Edge Payload Lifetime Audit

## Summary

This slice extends the default-off
`arm64_blue_dragon_edge_payload_storage_audit` with lifetime counters for exact
edge `82282490:82282598 -> 82287788`. It stays counter-only:
`payload_materializations_allowed=0`, `behavior_changed=0`, normal-entry
fallback remains preserved, and no payload state is materialized.

The Thor route was clean, but the lifetime result blocks a payload-storage
speed patch: every observed hot-edge segment is first-killed by
`CONTEXT_BARRIER` before any useful `f[1]` or `r[3]` read/write can be covered.

## Code Changes

- Added `blue_dragon_edge_payload_storage_lifetime_live` to
  `A64BackendContext`.
- Added audit counters for:
  - `r3_active_reads_covered` and `r3_inactive_reads`;
  - lifetime segments started and survived;
  - `f[1]` reads before/after first kill;
  - `r[3]` reads and writes before/after first kill;
  - first-kill reasons: context barrier, external visibility, return/exit,
    required `fpscr` writeback, unknown call, and exception/trap.
- Set the lifetime-live marker on the exact hot edge marker set, clear it on
  marker clear, and count no-kill survivors.
- Split active `f[1]` and `r[3]` traffic by lifetime-live state.
- Mark first kills from `CONTEXT_BARRIER`, external `fpscr` stores, required
  `fpscr` writeback calls, and unknown calls.

Touched files:

- `src/xenia/cpu/backend/a64/a64_backend.h`
- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `src/xenia/cpu/backend/a64/a64_sequences.cc`

## Validation

- `git diff --check` passed with only existing CRLF warnings.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore`
  passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
  passed and installed successfully.

## Thor Capture

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64BlueDragonEdgePayloadStorageAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788,82281D28" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture paths:

- `scratch/thor-debug/20260525-195142-speed-logcat.txt`
- `scratch/thor-debug/20260525-195142-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260525-195142-meta.txt`
- `scratch/thor-debug/20260525-195142-screenshot.png`
- `scratch/thor-debug/20260525-195142-perf-final.txt`

APK SHA:
`F19476F6E279449C5F155045F0662124941BACD66F60BC05809BF95D304BB72E`.

The meta file records head `13ba6d03d` because the capture used the
just-built working tree before this audit patch was committed. The screenshot
reached the visible opening sky / dragon-wing route. Fatal-marker search across
the full and filtered logcat returned no matches.

## Final Observed Audit Rows

Last cumulative edge-payload row at `19:54:44`:

- `eligible_edge_compiles=0/1`
- `variant_codegen_skipped=0/1`
- `storage_missing=0/1`
- `normal_entry_owned=0/1`
- `eligible_edge_calls=240752/698767`
- `normal_entry_fallbacks=240752/698767`
- `indirection_fallbacks=240752/698767`
- `variant_misses=240752/698767`
- `payload_materializations=0/0`
- `marker_sets=240752/698767`
- `marker_clears=240752/698767`
- `payload_materializations_allowed=0`
- `behavior_changed=0`

Last cumulative payload row:

- `f1_seed_candidates=240752/698767`
- `f1_active_reads_covered=718730/2085964`
- `f1_inactive_reads=2004/3322`
- `f1_unknown_kills=0/0`
- `fpscr_seed_candidates=240752/698767`
- `fpscr_dirty_writes=753034/2188472`
- `fpscr_required_writebacks=244073/708385`
- `r3_seed_candidates=240752/698767`
- `r3_active_reads_covered=491467/1426326`
- `r3_inactive_reads=2045/3440`
- `r3_mutable_writes=718894/2086964`

Last cumulative flush row:

- `helper_preserved=244073/708385`
- `child_preserved=118818/344841`
- `return_exit=240752/698767`
- `unknown_call=0/0`
- `context_barrier=3057580/8877656`
- `exception_or_trap=0/0`
- `external_visibility=12368/37789`

Last cumulative lifetime row:

- `segments_started=240752/698767`
- `segments_survived_no_kill=0/0`
- `f1_reads_before_kill=0/0`
- `f1_reads_after_kill=718730/2085964`
- `r3_reads_before_kill=0/0`
- `r3_reads_after_kill=491467/1426326`
- `r3_writes_before_kill=0/0`
- `r3_writes_after_kill=718894/2086964`

Last cumulative first-kill row:

- `first_context_barrier=240752/698767`
- `first_external_visibility=0/0`
- `first_return_exit=0/0`
- `first_fpscr_writeback=0/0`
- `first_unknown_call=0/0`
- `first_exception_or_trap=0/0`

Last body-time row stayed CPU/JIT-heavy:

- `82282490=39940278`
- `82281D28=8099644`
- `82287788=6069873`
- `82282388=2344420`
- `82282410=2253213`

Final perf sample:

- Main Thread `96.1%`
- XMA Decoder `42.3%`
- GPU Commands `11.5%`
- Draw Thread `3.8%`

## Decision

Do not materialize edge payload state and do not run a quiet speed A/B from the
current payload-storage lane. The useful payload volume is real, but the first
context barrier kills every observed segment before `f[1]` or `r[3]` can be
served from a payload.

Next useful slice: offline HIR/source audit of the first `CONTEXT_BARRIER` in
`82287788`, including its exact guest PC and surrounding state traffic. Decide
whether the barrier is a conservative artifact that can safely preserve a
read-only `f[1]` payload, or a required guest-visible boundary that blocks this
payload-storage strategy.

