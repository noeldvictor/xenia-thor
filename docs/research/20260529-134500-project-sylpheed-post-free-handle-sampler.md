# 2026-05-29 13:45 EDT - Project Sylpheed Post-Free Handle Sampler

## Context

The active lane remains from `20260529-113736-project-sylpheed-delayed-wait-no-present.md`: 
Project Sylpheed stalls with black `0.0 FPS` after `VdSwap` ends and physical frees stay behavior-blocking (`21` interior child frees under owner `BC220000:03A80000`, no owner-full release).

## Route

Using packet `scratch/thor-debug/remote-debug-20260529-113736`, I ran:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-113736' -OutPath 'scratch\thor-debug\20260529-134500-sylpheed-late-pc-wait-map-post-free.txt'
```

Output:

```text
scratch/thor-debug/20260529-134500-sylpheed-late-pc-wait-map-post-free.txt
```

## Findings

`post_failed_target_wait_handle_count=3`, with all three handles present only after the final failed free:

- `F8000260` (main thread): `rows=1764`, statuses `00000000:882,00000102:882`, phase loop `BEGIN,END`.
- `F8000054`: `rows=142`, statuses `00000000:71,00000102:71`, phase loop `BEGIN,END`.
- `F8000068`: `rows=142`, statuses `00000000:71,00000102:71`, phase loop `BEGIN,END`.

Sample rows show every sampled cycle alternates timeout+success wait-phase pairs, with line windows bounded at `11:35:48.819` through `11:35:49.969`, and no status transition evidence beyond repeated timeout pacing.

No physical-free-row shaping changes were made in this slice.

## Interpretation

Targeted post-free sampling confirms that all three handles that survive the final failed free are locked into tight wait loops (mostly timeout `00000102` / zero `00000000` pairs) rather than immediately entering different wait modes or exit paths. This gives a concrete next capture target for presenter-state association: prioritize tracing the same threads while correlating `VdSwap` caller-chain and GPU/presenter state around this handle cluster.

## Validation

```powershell
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1'), [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-113736' -OutPath 'scratch\thor-debug\20260529-134500-sylpheed-late-pc-wait-map-post-free.txt'
```
