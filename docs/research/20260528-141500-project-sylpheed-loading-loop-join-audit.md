# Project Sylpheed Loading Loop Join Audit

## Summary

Added a packet-level join audit for the remaining Project Sylpheed loading
state:

```text
tools/thor/thor_project_sylpheed_loading_loop_audit.ps1
```

It joins:

- visual packet classification,
- scoped crash/status rows,
- `VdSwap` presentation rows,
- optional Android `top -H` thread snapshots,
- optional `Xboxkrnl wait trace` rows.

The goal is to distinguish a guest/kernel wait stall from a live guest CPU loop
that keeps presenting near-black frames.

## Current Packet

Packet:

```text
scratch/thor-debug/remote-debug-20260528-134556
```

Added live thread snapshot without clearing logcat:

```text
scratch/thor-debug/remote-debug-20260528-134556/top-threads-live.txt
```

The screen remains the same near-black/loading-style state. The emulator process
is still alive.

## Joined Evidence

Audit output:

```text
classification=project_sylpheed_live_loading_guest_cpu_vdswap_no_crash
decision=add_guest_pc_or_progress_counter_for_cpu_spinning_loading_loop
visual_classification=project_sylpheed_live_loading_vdswap_loop_no_crash
presentation_evidence=vdswap_continuing
guest_execution_evidence=guest_xthreads_consuming_cpu
kernel_wait_evidence=missing_xboxkrnl_thread_wait_trace
vdswap_count=19975
vdswap_span_seconds=157.212
vdswap_rate_per_second=127.058
vdswap_thread_ids=3737
wait_trace_count=0
guest_thread_rows=17
guest_running_rows=4
guest_cpu_sum=126.5
guest_max_cpu=61.5
guest_top_thread=XThread3ACD6CB0
main_xthread_cpu=15.3
gpu_cpu_sum=15.3
audio_cpu_sum=3.8
longjmp_reenter_count=0
heap_release_count=0
native_abort_marker_count=0
invalid_pthread_join_count=0
```

Top guest threads:

```text
4130 R 61.5 XThread3ACD6CB0
3790 R 19.2 XThread4BD8ECB0
3757 R 15.3 Main XThread (truncated by top)
3791 S 11.5 XThread49C85CB0
```

## Interpretation

This is not currently a process crash, reenter abort, pthread abort, or physical
heap free failure. It is also not a pure kernel wait stall from the available
evidence: multiple guest XThreads are consuming CPU while `VdSwap` continues
near-black frames.

The packet lacks `Xboxkrnl wait trace` rows because it was not launched with
`-XboxkrnlThreadWaitTrace true`, so it cannot yet prove whether some guest
threads are simultaneously blocked on kernel waits. The useful next proof is
not another heap/free repro; it is a guest progress or PC/class sampler for the
CPU-consuming XThreads, optionally paired with the existing wait trace cvar.

## Next

Add or enable a focused guest progress sampler for the live loading loop:

```text
-XboxkrnlThreadWaitTrace true
-XboxkrnlThreadWaitTraceBudget 256
```

plus a guest PC/progress counter for the top guest XThreads if available or
newly added. The question is whether the CPU-consuming XThreads are spinning in
a loader/poll loop, advancing guest work that never changes the presented
buffer, or repeatedly submitting the same near-black frame.
