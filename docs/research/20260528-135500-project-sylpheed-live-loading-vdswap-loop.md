# Project Sylpheed Live Loading VdSwap Loop

## Summary

After the Android `pthread_join` abort fix, Project Sylpheed no longer shows a
scoped native crash marker in the current live packet. The remaining observed
state is a near-black loading-style screen with the emulator process still
alive and `VdSwap` continuing.

Current packet:

```text
scratch/thor-debug/remote-debug-20260528-134556
```

Screen/log/status were captured before clearing the running app.

## Evidence

`tools/thor/thor_android_game_status_report.ps1`:

```text
classification=unknown
reason=no decisive markers
android_runtime_count=0
native_signal_count=0
guest_crash_count=0
base_heap_release_count=0
physical_heap_count=0
invalid_pthread_join_count=0
```

`tools/thor/thor_android_packet_visual_status.ps1` now classifies this packet
as:

```text
classification=project_sylpheed_live_loading_vdswap_loop_no_crash
decision=join_guest_execution_kernel_wait_and_present_next
near_black_share=0.998900
average_luma=0.103
non_black_bbox=152,952,304,984
longjmp_reenter_count=0
throws_exception_count=0
vd_swap_count=19975
heap_release_count=0
native_abort_marker_count=0
invalid_pthread_join_count=0
```

The previous post-fix packet has the same class:

```text
scratch/thor-debug/remote-debug-20260528-133426
classification=project_sylpheed_live_loading_vdswap_loop_no_crash
vd_swap_count=19473
```

The physical heap audit for the current packet reports no Project Sylpheed heap
release rows:

```text
classification=no_project_sylpheed_heap_release_rows
longjmp_reenter_count=0
native_abort_marker_count=0
vd_swap_count=19975
physical_memory_audit_allocate_count=0
physical_memory_audit_free_request_count=0
physical_memory_audit_free_result_count=0
```

## Interpretation

This packet is not the older uncaught Android reenter abort, not the
`invalid pthread_t ... pthread_join` abort, and not the earlier heap-release /
post-reenter black-frame packet. It is a live loading/presentation stall: the
guest is still presenting through `VdSwap`, but the screen remains nearly black
apart from the loading-style bottom-left content and FPS OSD.

## Next

Do not rerun the same crash lane or round down physical frees from this packet.
The next useful slice is to join guest execution, kernel wait, and presentation
evidence for this live loading loop: identify whether XThreads are waiting,
spinning, exiting, or continuing guest code while `VdSwap` swaps near-black
frames.
