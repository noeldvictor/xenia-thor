# Project Sylpheed late-PC wait map

## Context

This continues `20260528-181500-project-sylpheed-free-site-flow-join.md` and
uses the repaired game-library packet:

`scratch/thor-debug/remote-debug-20260528-180311`

The game is still in a live black/loading state with guest CPU, VdSwap, GPU, and
XMA activity. The prior join proved that the interior physical frees are real
guest suballocator slices, but not sufficient by themselves to explain the
black frame. The remaining question was what the late wait/progress PCs mean.

## Tool

Added:

`tools/thor/thor_project_sylpheed_late_pc_wait_map.ps1`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir scratch\thor-debug\remote-debug-20260528-180311 -OutPath scratch\thor-debug\20260528-180311-sylpheed-late-pc-wait-map.txt
```

## Findings

Report:

`scratch/thor-debug/20260528-180311-sylpheed-late-pc-wait-map.txt`

Classification:

`project_sylpheed_late_pc_wait_map_needs_capture`

The target PCs are visible in the packet, but none have filtered PPC/HIR dumps
yet:

- `824AB214`: wait LR for `NtWaitForMultipleObjectsEx` loops, 219 wait rows,
  78 snapshot `last_ret` rows, 18 snapshot rows after the last failed physical
  free.
- `824AC578`: wait LR for `NtWaitForSingleObjectEx`, 11 wait rows, 72 snapshot
  `last_ret` rows, 18 snapshot rows after the last failed physical free.
- `8272D048`: one late snapshot `last_fn` row on main thread, with CTR
  `82705FB4`.
- `82762158`: one late snapshot `last_fn` row on main thread, with CTR
  `8276900C`.

The wait trace still ends before the final failed physical free:

- `wait_trace_last_time=18:02:12.984`
- `last_failed_free_time=18:03:02.018`
- `a64_snapshots_after_last_failed_free=87`

## Decision

Do not patch heap behavior from the current evidence. The next useful runtime
capture should be tightly targeted:

- Use the real game-library route.
- Keep the filtered disassembly/HIR set to
  `824AB214,824AC578,8272D048,82762158`.
- Extend or delay `XboxkrnlThreadWaitTrace` so it covers the black/loading
  window after the failed physical frees.
- Keep the physical-free audit enabled only as supporting evidence, not as the
  behavior target.

If filtered dumps for those PCs still do not resolve the enclosing guest
functions, then move the mapping into the Ghidra guest-PPC lane rather than
guessing from wait trace aggregates.
