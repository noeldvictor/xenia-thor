# Project Sylpheed Black Frame Classifier

## Summary

Added a packet-level visual status tool for Android remote-debug captures:

```text
tools/thor/thor_android_packet_visual_status.ps1
```

The tool joins:

- `screen.png` near-black analysis, ignoring the top-left FPS OSD corner
- `status-report.txt` classification
- logcat reenter rows
- heap release rows
- native abort markers
- `VdSwap` rows
- silent audio fallback rows

This is behavior-neutral tooling. It does not change emulator runtime behavior.

## Proof

Black-frame packet:

```text
scratch/thor-debug/remote-debug-20260528-114503
scratch/thor-debug/20260528-121600-sylpheed-black-frame-visual-status.txt
```

Result:

```text
classification=project_sylpheed_black_frame_after_reenter_no_crash
decision=classify_heap_release_and_black_frame_next
near_black_share=1.000000
average_luma=0.000
non_black_bbox=none
black_frame_likely=1
longjmp_reenter_count=6
throws_exception_count=0
vd_swap_count=9023
vd_swap_after_longjmp=0
heap_release_count=54
heap_release_after_longjmp=3
native_abort_marker_count=0
nop_audio_count=1
```

Menu control packet:

```text
scratch/thor-debug/remote-debug-20260528-114147
scratch/thor-debug/20260528-121600-sylpheed-menu-visual-status-control.txt
```

Result:

```text
classification=visual_not_black_frame
decision=packet_is_visual_control_or_non_black_screen
near_black_share=0.554675
average_luma=26.108
black_frame_likely=0
```

The classifier therefore distinguishes the fixed title/menu screen from the
post-New-Game black frame using the captured screenshots, not just logcat.

## Interpretation

The earlier Android process-kill crash class remains fixed: the black-frame
packet has no scoped native abort marker and still shows Android reenter
longjmp rows instead of exception-unwind rows.

The next Project Sylpheed blocker is not the old reenter SIGABRT. It is now:

```text
black frame after reenter, no process crash, with remaining physical heap release failures
```

The packet did not show `VdSwap` rows after the first longjmp row, so do not
claim continued guest presentation from logcat alone. The FPS OSD was visible in
the screenshot, but the durable packet classification should stay conservative.

## Next

Classify the physical heap release semantics before any behavior patch:

- exact region start vs interior free
- parent region base and size
- repeated free vs first free
- whether the failing frees correlate with loss of guest presentation

Do not blindly round the physical free down to the parent region base. The
previous source audit showed the parent region can be much larger than the
interior address being released.

Separately, Android audio still needs a real backend: this packet still reports
`NopAudioSystem created silent audio driver 0`.
