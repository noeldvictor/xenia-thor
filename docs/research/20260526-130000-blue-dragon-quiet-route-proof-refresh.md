# Blue Dragon Quiet Route Proof Refresh

Date: 2026-05-26 13:00 EDT

## Status

This is a runtime evidence-refresh slice after the A64 micro-lane rerank. It
does not change emulator behavior. It FullDeployed current `master`, ran one
quiet 180-second Blue Dragon route capture with the offset-memory address
fastpath enabled, and added deterministic capture-summary tooling.

No sustained 30 FPS proof exists.

## Build And Capture

- Commit: `a7c855769`
- APK SHA256:
  `5320F722699CE8B9659BC3A942E5E892CCD01622AE2233F4C1E4D86CB7E3158B`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Capture:
  `scratch/thor-debug/20260526-124549-speed-logcat.txt`
- Filtered log:
  `scratch/thor-debug/20260526-124549-speed-logcat-filtered.txt`
- Metadata:
  `scratch/thor-debug/20260526-124549-meta.txt`
- Screenshot:
  `scratch/thor-debug/20260526-124549-screenshot.png`
- Final perf:
  `scratch/thor-debug/20260526-124549-perf-final.txt`
- Summary report:
  `scratch/thor-debug/20260526-124549-speed-proof-summary.txt`

Launch cvars included:

```text
arm64_offset_memory_address_fastpath=true
arm64_speed_profile_body_time_filter=82282490,82281D28,82287788
arm64_speed_profile_body_time_after_ms=120000
```

Audit/disassembly/block tracing was off. The capture reached visible opening
sky / dragon-wing evidence and force-stopped cleanly after the capture.

## Proof-Gate Summary

`tools/thor/thor_blue_dragon_speed_capture_summary.ps1` now summarizes capture
logs, metadata, final perf samples, final delayed body-time rows, fatal markers,
FPS markers, and Choreographer skipped-frame rows.

For this capture it reports:

```text
fatal_marker_count=0
fps_marker_count=0
choreographer_skipped_events=14
choreographer_skipped_max_frames=107
thread_cpu main_thread=96.1
thread_cpu gpu_commands=3.8
thread_cpu draw_thread=0.0
thread_cpu xma_decoder=7.6
wall=main_thread_a64_generated_code
sustained_30fps_proof=false
proof_reason=no_fps_markers
```

Final delayed body-time rows:

```text
body fn=82282490 total=27545622 delta=9317750 entries_delta=62328 ticks_per_entry=149 code_size=86452
body fn=82281D28 total=7174789 delta=2758368 entries_delta=29736 ticks_per_entry=92 code_size=95724
body fn=82287788 total=3120411 delta=1241531 entries_delta=247380 ticks_per_entry=5 code_size=35712
body fn=82282388 total=2078581 delta=801427 entries_delta=5670 ticks_per_entry=141 code_size=87344
body fn=82282410 total=2000853 delta=772010 entries_delta=4158 ticks_per_entry=185 code_size=87344
```

## Decision

The fresh route proof refresh did not change the wall. The route remains
Main Thread / A64 generated-code bound around the same closed wall cluster, not
broad Vulkan bound, and the log has no FPS marker supporting a sustained 30 FPS
claim.

Do not rerun this same proof refresh unchanged. Do not reopen the current
A64/VMX/fastmem/helper/GPU behavior lanes from this capture. A future speed
slice needs either:

- a genuinely new structural class with fresh evidence outside the closed
  `82282490` / `82281D28` / `82287788` micro-lanes; or
- a user-prioritized Android controller/settings UX lane, which is playability
  work and not FPS proof.

Keep `-Arm64OffsetMemoryAddressFastpath true` explicit in future Blue Dragon
speed captures while keeping the global default off.

## Validation

- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- The 180-second route capture completed and force-stopped cleanly.
- Focused fatal-marker search found zero matches.
- Parser check passed for
  `tools/thor/thor_blue_dragon_speed_capture_summary.ps1`.
- The summary tool wrote
  `scratch/thor-debug/20260526-124549-speed-proof-summary.txt`.
