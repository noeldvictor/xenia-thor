# 2026-05-29 15:10 EDT - Project Sylpheed Presenter/GPU Stop Join

## Context

This follows `20260529-141500-project-sylpheed-handle-lifecycle-join.md`.
The same real-library Project Sylpheed packet still shows black `0.0 FPS` after
`VdSwap` stops, while delayed post-free wait rows continue after the final failed
physical free.

## Route

Re-ran the late-PC wait map on:

```text
scratch/thor-debug/remote-debug-20260529-113736
```

The report now also emits:

- last `VdSwap` caller counts and same-caller status rows,
- presenter/GPU log rows before/after the final `VdSwap`,
- snapshot `r3` values for the target wait PCs.

Generated proof:

```text
scratch/thor-debug/20260529-151000-sylpheed-presenter-gpu-stop-join.txt
```

## Findings

The new presentation-stop context is:

```text
presentation_stop_context=last_vdswap_has_no_same_caller_status_and_no_later_presenter_gpu_rows
vdswap_last_time=11:35:19.199
vdswap_last_caller=F8000084
vdswap_caller_counts=F8000084:2548,F8000028:1
vdswap_last_caller_rows=rows=2548 first=11:34:30.104:4685 last=11:35:19.199:14938
vdswap_last_caller_status_count=0
```

The nearby `xeRtlNtStatusToDosError` rows are still same-host-thread context, not
same-caller return proof for `F8000084`:

```text
vdswap_last_status_before=caller=F800025C status=00000103 mapped=000003E5 line=14930 time=11:35:19.117 delta_ms=82.0
vdswap_last_status_after=caller=F8000028 status=C0000001 mapped=0000001F line=14941 time=11:35:19.214 delta_ms=15.0
```

The first wait after the final swap remains tied to the main timeout object:

```text
seconds_from_last_vdswap_to_first_wait=29.62
vdswap_to_first_wait=pc=824AC578 handle=F8000260 guest_object=00121018 status=00000000 thread=00000006 api=NtWaitForSingleObjectEx line=15414 time=11:35:48.819
```

Presenter/GPU logging is absent after the final swap in this packet:

```text
gpu_presenter_event_count=14
gpu_presenter_after_last_vdswap_count=0
gpu_presenter_last_before_vdswap=kind=Gpu line=3286 time=11:34:29.565 delta_ms=-49634.0 text=xenia : w> F8000004 GPU: Write to unknown register (1E4E = 00000000)
```

The target PC snapshot map now also shows `824AC578` snapshots carrying the same
wait handle in `r3`, though less frequently than the other waiter handles:

```text
target_pc[1]=824AC578 ... snapshot_r3s=F80000BC:76,F8000040:38,F80000F8:38,F800002C:37,F8000060:29,F8000260:26,F800007C:8,F8000204:1 post_failed_snapshot_r3s=F80000BC:58,F8000040:29,F80000F8:29,F800002C:28,F8000060:28,F8000260:26,F800007C:1
target_pc[1]_wait_handle[0]=handle=F8000260 rows=1764 ... post_failed_rows=1764 lifecycle_rows=3 ...
```

## Decision

Do not patch physical frees. The current packet proves the presentation stream
stops without a same-caller `VdSwap` status row or later presenter/GPU log row,
while the guest wait loop continues on `824AC578`/`824AB214` timeout objects.

The next useful slice is source instrumentation, not behavior: add a default-off
present-stop sampler that logs `VdSwap` enter/exit status and a compact
presenter/GPU state marker after the last guest swap path. That should make the
next device packet distinguish host presenter/present failure from guest wait
progression without changing physical-memory semantics.

## Validation

```powershell
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1'), [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-113736' -OutPath 'scratch\thor-debug\20260529-151000-sylpheed-presenter-gpu-stop-join.txt'
```
