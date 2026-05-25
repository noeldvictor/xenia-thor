# Blue Dragon Edge-Variant Payload-Scope Audit

## Summary

This slice extends the default-off `arm64_blue_dragon_edge_variant_audit` from a
storage-missing counter probe into a behavior-neutral payload-scope probe for
the exact hot edge `82282490:82282598 -> 82287788`.

The patch does not change generated guest behavior. It only stores a backend
context marker while the hot-edge call is active, clears it immediately after
return, and counts `f[1]` context reads plus call-kill pressure while that
marker is active. `payload_materializations` remains zero, normal entry fallback
remains the only execution behavior, and the global callee entry / indirection
slot remains untouched.

## Patch Shape

- Added `A64BackendContext::blue_dragon_edge_variant_payload_active`.
- Added audit counters for marker sets, marker clears, active/inactive `f[1]`
  reads, and active call kills.
- Set and clear the marker only for non-tail calls from
  `82282490:82282598 -> 82287788`.
- Counted active `f[1]` reads only inside `LOAD_CONTEXT_F64` for
  function `82287788`, offset `296`.
- Extended `tools/thor/thor_a64_edge_variant_storage_plan.ps1` to parse the new
  optional counters while staying compatible with older logs.

## Validation

- `NativeCore` passed.
- `FullDeploy` passed and installed the working tree based on commit
  `ef0521239`.
- PowerShell parser validation passed for
  `tools/thor/thor_a64_edge_variant_storage_plan.ps1`.
- `git diff --check` passed.
- The updated storage-plan report parsed both the old route log and the new
  payload-scope route log.

## Thor Capture

- Command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64BlueDragonEdgeVariantAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788,82281D28" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true`
- Capture prefix: `scratch/thor-debug/20260525-172734-*`.
- APK SHA:
  `12E4BC4F69BA1D0C4A4A249F64C74BD81FB802569B46122644C9242A57DA8CB1`.
- Cvars: `arm64_blue_dragon_edge_variant_audit=true`,
  `arm64_speed_profile_body_time_filter=82282490,82287788,82281D28`,
  `arm64_speed_profile_body_time_after_ms=120000`,
  `arm64_speed_profile_interval_ms=15000`.
- Route reached the visible opening sky / dragon-wing frame.
- Fatal-marker search was clean.

## Edge Counters

Final storage-plan row:

```text
edge parent=82282490 call_pc=82282598 callee=82287788 eligible_calls=687023 delta_calls=205520 normal_fallbacks=687023 normal_fallback_share=100.00% indirection_fallbacks=687023 variant_misses=687023 payload_materializations=0 storage_missing=1 call_kills=1726956 call_kills_per_call=2.51
payload_scope marker_sets=687023 marker_clears=687023 active_f1_reads=2050899 inactive_f1_reads=3250 active_call_kills=1722545 active_f1_reads_per_call=2.99 active_call_kills_per_call=2.51
```

Interpretation:

- The marker is balanced: `marker_sets == marker_clears == 687023`.
- The exact edge has real payload-scope read volume:
  `active_f1_reads_per_call=2.99`.
- The call-kill pressure is also real:
  `active_call_kills_per_call=2.51`.
- `payload_materializations=0`, so this remains audit-only evidence.
- Because `normal_fallback_share=100.00%`, this capture is not a speed verdict.

## Body-Time And Perf

Final body rows stayed CPU/JIT-heavy:

- `82282490 body_ticks_total=34497956`, `ticks_per_entry=177`,
  `code_size=87348`.
- `82281D28 body_ticks_total=7587167`, `ticks_per_entry=93`,
  `code_size=96908`.
- `82287788 body_ticks_total=4153275`, `ticks_per_entry=6`,
  `code_size=36796`.

Final thread sample:

- Main Thread `96.2%`.
- XMA Decoder `11.1%`.
- Audio Worker `7.4%`.
- GPU Commands `7.4%`.
- Draw Thread `0.0%`.

## Decision

The caller-local payload-scope marker is route-clean and proves the hot edge can
be scoped without touching the normal callee entry or global indirection slot.
It also shows a behavior patch must be careful: roughly three active `f[1]`
reads per call are available, but the active scope also sees roughly 2.5 call
kills per call.

Do not run a quiet speed A/B from this audit-only patch. Do not materialize a
payload yet. The next useful slice is either:

- add per-PC active `f[1]` read and kill-window attribution for the same marker,
  then decide if a tiny caller-local payload is worth a behavior prototype; or
- return to broader `82282490` / `82287788` state-roundtrip work with this
  marker as the storage-safe boundary proof.

Keep broad Vulkan/GPU work secondary until a capture shows GPU Commands,
present/submit waits, pipeline stalls, or CPU-side graphics transforms overtaking
the Main Thread/JIT wall.
