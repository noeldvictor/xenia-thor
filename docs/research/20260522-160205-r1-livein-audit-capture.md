# 2026-05-22 16:02 EDT - r[1] Live-In Audit Capture

## Goal

Validate the branch-preserve fix for the default-off `r[1]` live-in probe on
AYN Thor. Success for this slice was replacement coverage, not FPS:

- run the current `master` APK on Thor;
- enable `arm64_context_promotion_gpr_livein_r1` only for `82282490`;
- keep audit on;
- require no searched fatal markers;
- require `loads_replaced` to approach the offline `50+` opportunity before
  doing quiet speed A/B captures.

## Build And Deploy

Current build:

```text
head=3bc08fac2
apk_sha256=A39BDE205BD4D454AEA115DDE6932EA2CC407F02A4C0A698A9819CB97B9DBDBA
target=/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso
```

`FullDeploy` succeeded on device `c3ca0370` (`AYN Thor`, Android SDK `33`).

## Captures

### `scratch/thor-debug/20260522-155225-*`

This 135-second run launched cleanly and had no searched fatal markers, but did
not reach the target function:

- final screenshot: black;
- no `A64 GPR live-in r1 promotion audit` lines;
- no `fn 82282490` body-time lines;
- `entry_delta=0` from `15:53:12` onward;
- final process state mostly sleeping.

Treat this as a route black-idle sample, not evidence about the branch fix.

### `scratch/thor-debug/20260522-155608-*`

This 80-second follow-up stayed active but still did not reach `82282490`:

- no searched fatal markers;
- final screenshot: black;
- final interval still busy;
- no r1 audit lines.

This suggested the previous black-idle was route instability, and a longer
capture was needed.

### `scratch/thor-debug/20260522-155827-*`

This 190-second audited capture reached the loading route and compiled
`82282490`.

Important cvars:

```text
arm64_context_promotion_gpr_livein_r1=true
arm64_context_promotion_gpr_livein_r1_function=0x82282490
arm64_context_promotion_gpr_livein_r1_preserve_barrier=true
arm64_context_promotion_gpr_livein_r1_audit=true
arm64_speed_profile_body_time_filter=82282490
arm64_speed_profile_body_time_after_ms=120000
arm64_speed_profile_thread_snapshot_on_idle=true
```

Fatal scan:

```powershell
rg -n "Fatal signal|AndroidRuntime|FATAL EXCEPTION|signal 11|Guest crash|VK_ERROR_DEVICE_LOST|GPU is hung|CHECK failed|ASSERT" scratch\thor-debug\20260522-155827-speed-logcat.txt scratch\thor-debug\20260522-155827-speed-logcat-filtered.txt
```

No matches.

Audit:

```text
blocks=164 entry_clean_blocks=109 needs_entry_local_blocks=59
loads_attempted=107 loads_replaced=64 loads_seeded=43
local_loads=59 local_stores=17 branch_stores=17 final_stores=0
stores_seen=11 alias_stores=0 call_resets=43 barrier_resets=0
alias_resets=0 exit_resets=0 preserve_barrier=true
skipped dirty_entry=9 after_call=34 after_barrier=0 after_alias=0
after_exit=0 no_value_for_store=0 branch_preserves=153
```

Final screenshot:

```text
scratch/thor-debug/20260522-155827-screenshot.png
```

The screenshot shows the loading spinner, not title/opening/gameplay.

## Result

The branch-preserve fix succeeded for the original audit target:

- before fix: `loads_replaced=16/107`;
- after fix: `loads_replaced=64/107`;
- branch preserves recorded: `153`;
- no searched fatal markers in the successful target capture.

This exceeds the offline `56` first-load opportunity and proves the previous
runtime gap was the conditional-branch volatile kill.

## Decision

Keep `arm64_context_promotion_gpr_livein_r1` default-off for now.

The probe is ready for quiet speed A/B, but this audited run is not FPS proof
because audit logging and route instability contaminate timing.

## Next Action

Run a quiet same-build A/B:

1. Control: `Arm64ContextPromotionGprLiveInR1 false`.
2. Experiment: `Arm64ContextPromotionGprLiveInR1 true`,
   `Arm64ContextPromotionGprLiveInR1Function 0x82282490`,
   `Arm64ContextPromotionGprLiveInR1PreserveBarrier true`,
   `Arm64ContextPromotionGprLiveInR1Audit false`.

Use the same route, same APK hash, no thread snapshot, no audit, and body-time
filter `82282490` after the title/input transition. Judge only matching routes
that reach at least the loading spinner/opening path with no searched fatal
markers.
