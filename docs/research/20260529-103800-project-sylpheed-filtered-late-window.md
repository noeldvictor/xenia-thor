# 2026-05-29 10:38 EDT - Project Sylpheed Filtered Late Window

## Context

This executes the capture plan from
`20260529-094500-project-sylpheed-late-pc-capture-plan.md` for the remaining
Project Sylpheed New Game black/loading state.

## Changes

- Guarded real launcher game starts with `FLAG_ACTIVITY_NEW_TASK |
  FLAG_ACTIVITY_CLEAR_TASK` so selecting Start game cannot stack a second
  `EmulatorActivity` over a still-live emulator instance. The pre-fix packet
  `scratch/thor-debug/remote-debug-20260529-100300-sylpheed-after-start`
  hit `xe::Memory::Memory(): assertion "(active_memory_) == 0" failed` with
  two Xenia PIDs.
- Made filtered PPC/HIR dumps more reliable for inner-PC filters by rechecking
  `disassemble_function_filter` after scanner end-address discovery.
- Changed PPC decoder misses to return `PPCOpcode::kInvalid` instead of
  asserting. The scanner/HIR path already handles invalid opcodes; the assert
  was killing diagnostic dumps on embedded data in the filtered route.
- Updated `thor_project_sylpheed_late_pc_wait_map.ps1` so a filtered dump is
  credited when the target PC is inside the dumped function range or appears in
  any filtered dump line, not only when the dump label equals the exact PC.

## Proof

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShellDeploy -DeviceSerial c3ca0370 -NoSubst
$errors = $null; [System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1', [ref]$null, [ref]$errors)
```

Fresh route packet:

- `scratch/thor-debug/remote-debug-20260529-103800-sylpheed-filtered-late-window`
- Real game-library launch path with filtered PPC/HIR, wait trace, A64 thread
  snapshots, and physical memory audit.
- Current screen is black with the guest-swap FPS badge at `61.3 FPS`.
- `status-report.txt` classifies
  `project_sylpheed_heap_release_failures_no_crash_marker`.
- `xenia_pid_count=1`, `native_signal_count=0`, `android_runtime_count=0`.
- `base_heap_release_count=19`, `physical_heap_count=19`.

Audit reports:

- `scratch/thor-debug/20260529-103800-sylpheed-late-pc-wait-map.txt`
  now classifies `project_sylpheed_late_pc_wait_map_has_filtered_dumps`.
  Active PCs are present in wait/snapshot evidence and filtered dumps:
  `824AB214` and `824AC578`.
- `scratch/thor-debug/20260529-103800-sylpheed-free-site-flow-join.txt`
  has `ppc_target_match_count=19`, `failed_interior_free_count=19`,
  `wait_trace_after_last_failed_free_count=2048`, and
  `vdswap_after_last_failed_free_count=14880`.
- `scratch/thor-debug/20260529-103800-sylpheed-physical-heap-release-audit.txt`
  keeps the ownership classification:
  `project_sylpheed_single_owner_interior_physical_heap_frees`.
- `scratch/thor-debug/20260529-103800-sylpheed-loading-loop-audit.txt`
  still reports a live VdSwap loop with `vdswap_rate_per_second=60.464`, but
  no top-thread CPU sample was captured in this packet.

## Findings

- The late active wait PCs are small wait/retry wrappers:
  `824AB190-824AB23C` calls `NtWaitForMultipleObjectsEx` and loops on
  `0x101`; `824AC540-824AC5A4` calls `NtWaitForSingleObjectEx` and also has a
  timeout retry shape.
- The failing physical frees are not random. All 19 parsed failed interior frees
  belong to the same owning physical allocation group
  `BC220000:03A80000`, and 19/19 CTR cases have PPC/HIR target matches in the
  `821511D0-821518C0` table.
- VdSwap continues after the last failed free, so the current state is a live
  guest wait/loading loop with heap-release failures, not a scoped Android
  native crash.
- Blind physical-free round-down remains unsafe: the parent region spans
  `1C220000-1FCA0000` / `0x03A80000` bytes.

## Decision

Do not hide the heap errors or round down physical frees. The next compatibility
slice should inspect the `821511D0-821518C0` free table and the
`824AB190`/`824AC540` wait/retry wrappers together, then design an explicit
physical suballocation release/owner-refcount contract or prove the presentation
stall is independent before changing behavior.
