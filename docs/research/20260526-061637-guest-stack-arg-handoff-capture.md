# 2026-05-26 - Guest-Stack Argument Handoff Capture

## Context

This slice FullDeployed commit `baf5a6b92` and ran the default-off,
counter-only `arm64_guest_stack_arg_handoff_audit` on Thor for `0x82281D28`.
The goal was to decide whether `LOAD_OFFSET` from guest stack `r1 + constant`
feeding `STORE_CONTEXT r3-r10/lr` before direct guest calls is broad enough for
a guarded handoff or fast-entry behavior design.

No generated behavior changed:

- `payload_materializations_allowed=0`
- `behavior_changed=0`
- `alternate_codegen=0`
- `normal_entry=unchanged`
- `global_indirection=unchanged`

## Capture

- Capture prefix: `scratch/thor-debug/20260526-061637-*`
- Speed log: `scratch/thor-debug/20260526-061637-speed-logcat.txt`
- Join report:
  `scratch/thor-debug/20260526-061637-guest-stack-arg-handoff-join.txt`
- Screenshot: `scratch/thor-debug/20260526-061637-screenshot.png`
- APK SHA256:
  `E543483604CF4EE3B550D5A58A56F56F3505EEFB5E733CB6BE28778C93670BB2`
- Route evidence: visible opening sky / dragon-wing frame.
- Fatal-marker search: no AndroidRuntime, tombstone, assert, backtrace, fatal
  exception, SIGSEGV, SIGABRT, crash, or ANR hits were found.

The launch used:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -DisassembleFunctionFilter "82281D28" -Arm64SpeedProfileBlockFilter "82281D28" -Arm64SpeedProfileBlockBodyTime true -Arm64GuestStackArgHandoffAudit true -Arm64GuestStackArgHandoffAuditFunction 0x82281D28 -Arm64GuestStackArgHandoffAuditBudget 16 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 30 -Arm64SpeedProfileMinDelta 1 -StopAppAfterCapture true
```

## Results

Final perf stayed CPU/JIT-heavy:

- Main Thread: `100%`
- GPU Commands: `11.5%`
- Draw Thread: `7.6%`
- XMA Decoder: `7.6%`

Final delayed body-time rows:

- `82281D28=65318256`
- `82282490=29545224`
- `82287788=3471859`

Runtime stack-argument handoff row for `82281D28`:

- `direct_calls=84`
- `eligible_regular=67`
- `target_rows=32`
- `unresolved_direct_targets=52`
- `helper_blockers=17`
- `normal_entry_fallback=67`
- `stackpoint_sensitive=67`
- `load_offset_instrs=254`
- `stack_load_candidates=119`
- `non_stack_load_offsets=135`
- `stack_arg_store_calls=17`
- `stack_arg_store_fields=87`
- `stack_arg_store_bytes=680`
- `estimated_avoidable_bytes=1360`
- `dirty_flush_points=268`
- `flush_context_barrier=260`
- `parent_pre_call_flush_points=23`
- `already_compiled_targets=32`

The join report still sees broad argument live-in volume from the larger
callee-aware HIR view:

- `argument_store_fields=439`
- `callee_first_load_stores=247`
- `body_weighted_live_in_fields=720712480`
- `body_weighted_missing_fields=31079113`

But the new stack-specific runtime rows show the guest-stack handoff subset is
much narrower and concentrated. The largest stack-specific body-weighted row is
the recursive call target:

- target `0x82281D28`
- `stack_arg_store_calls=1`
- `stack_arg_store_fields=6`
- `body_weighted_stack_arg_fields=279333228`
- `normal_entry_fallback=2`
- `stackpoint_sensitive=2`
- `unresolved=2`

Other stack-specific rows are smaller or still blocked by unresolved targets and
normal-entry fallback. Target `0x826BF770` is already compiled and known, but the
stack-specific candidate is only one field (`estimated_avoidable_bytes=8`).

## Decision

Do not patch guest-stack argument handoff behavior and do not run a quiet speed
A/B from this evidence.

The route-clean capture proves the counter path works, but it does not justify a
behavior change. The stack-specific handoff subset is small in static field
count (`87` fields, `1360` estimated avoidable bytes), not broad across the wall
cluster, and still carries high normal-entry, stackpoint, unresolved-target, and
context-barrier pressure.

Close the current guest-stack argument handoff behavior lane. Reopen it only as
part of a broader guarded-stub / late-bound-entry design that explicitly handles
normal entry, global indirection, stackpoint/debug/exception visibility, dirty
flushes, and unresolved targets.

The next useful structural lane is fastmem/addressing or A64 load/store
codegen-floor work, because `82281D28` still has substantial scalar
`LOAD_OFFSET` traffic and remains the body-time wall in this capture. A useful
next slice would be a default-off counter-only address-mode / fastmem audit for
hot `LOAD_OFFSET` and `STORE_OFFSET` forms in `82281D28`, with target block rows
and body-time weighting before any behavior patch.
