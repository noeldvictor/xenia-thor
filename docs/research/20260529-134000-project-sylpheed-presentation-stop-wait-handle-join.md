# 2026-05-29 13:40 EDT - Project Sylpheed Presentation Stop Join

## Context

Follow-on from `20260529-113736-project-sylpheed-delayed-wait-no-present.md`.
The packet `scratch/thor-debug/remote-debug-20260529-113736` was captured through the
real Project Sylpheed launch path and already had:
- final `VdSwap` at `11:35:19.199`,
- screenshot later showing black frame with `0.0 FPS`,
- delayed wait trace (`2048` rows) after the last failed physical free,
- unsafely-behaved interior frees under `BC220000:03A80000`.

## Route

- Keep focus on joining the presentation stop to post-free wait handles and status
  context.
- Re-run late-PC map with per-handle summary output.

## Findings

`tools/thor/thor_project_sylpheed_late_pc_wait_map.ps1` now reports:

```text
classification=project_sylpheed_late_pc_wait_map_has_filtered_dumps
reason=all target PCs are visible in wait/snapshot evidence, and filtered PPC/HIR dumps are present.

actionable VdSwap join evidence:
vdswap_last_time=11:35:19.199
vdswap_last_caller=F8000084
vdswap_last_status_before=caller=F800025C status=00000103 mapped=000003E5 line=14930 time=11:35:19.117 delta_ms=82.0
vdswap_last_status_after=caller=F8000028 status=C0000001 mapped=0000001F line=14941 time=11:35:19.214 delta_ms=15.0
seconds_from_last_vdswap_to_first_wait=29.62
vdswap_to_first_wait=pc=824AC578 handle=F8000260 guest_object=00121018 status=00000000 thread=00000006 api=NtWaitForSingleObjectEx line=15414 time=11:35:48.819
```

Wait-handle joins for the two active PCs:

```text
target_pc[0]_wait_handle[0]=handle=F8000054 rows=142 guest_objects=00051018:142 statuses=00000000:71,00000102:71 threads=00000009:142 names=XTHREAD5AC0ECB0 (F800005C):142 post_failed_rows=142
target_pc[0]_wait_handle[1]=handle=F8000068 rows=142 guest_objects=0005C018:142 statuses=00000000:71,00000102:71 threads=0000000A:142 names=XTHREAD58B05CB0 (F8000070):142 post_failed_rows=142
target_pc[1]_wait_handle[0]=handle=F8000260 rows=1764 guest_objects=00121018:1764 statuses=00000000:882,00000102:882 threads=00000006:1764 names=MAIN XTHREAD (F8000028):1764 post_failed_rows=1764
```

Interpretation:

- The last successful `VdSwap` stopped at `11:35:19.199` and the first mapped wait
  after that is `NtWait...` on `pc=824AC578` at `11:35:48.819` using
  handle `F8000260`.
- `VdSwap`-adjacent `xeRtlNtStatusToDosError` rows are not from the same caller
  (`F8000084`) immediately; the parser sees `F800025C` then `F8000028` conversions
  82 ms / 15 ms apart, so we still do not have a clean same-caller return-status
  trace.
- Waits remain in short timeout-style rows (`status=00000102`/`00000000`) and are
  consistently tied to timeout handle set {`F8000054`, `F8000068`, `F8000260`}.

## Validation

```powershell
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools/thor/thor_project_sylpheed_late_pc_wait_map.ps1'), [ref]$null, [ref]$errors)

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch/thor-debug/remote-debug-20260529-113736' -OutPath 'scratch/thor-debug/20260529-113736-sylpheed-late-pc-wait-map.txt'
```

## Decision

Keep physical-free behavior untouched per prior proof: no owner-release, no child
release, and no callback-unsafe row removals in this lane.

Next useful slice:
- target a short targeted capture around the `824AC578`/`824AB214` wait handles
  (`F8000260`, `F8000054`, `F8000068`) using trace-after capture to include present
  shutdown + wait transition.
