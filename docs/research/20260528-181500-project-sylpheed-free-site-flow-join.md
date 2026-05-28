# Project Sylpheed free-site flow join

## Context

This follows `20260528-180300-project-sylpheed-library-wait-and-apk-native-guard.md`
and uses packet `scratch/thor-debug/remote-debug-20260528-180311`.

The packet is a real game-library launch after the APK native-lib guard fix. It
shows a black frame with no scoped Android/native crash marker, active guest CPU,
bounded wait trace, A64 thread snapshots, VdSwap rows, and the same
`821507BC` / CTR physical-free cluster.

## Tool

Added:

`tools/thor/thor_project_sylpheed_free_site_flow_join.ps1`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_free_site_flow_join.ps1 -PacketDir scratch\thor-debug\remote-debug-20260528-180311 -OutPath scratch\thor-debug\20260528-180311-sylpheed-free-site-flow-join.txt
```

## Findings

Report:

`scratch/thor-debug/20260528-180311-sylpheed-free-site-flow-join.txt`

Classification:

`project_sylpheed_guest_suballocator_offsets_with_unattributed_live_black_loop`

The filtered PPC/HIR dump now maps all 16 failed interior physical frees:

- `failed_interior_free_count=16`
- `failed_ctr_target_count=16`
- `ppc_target_match_count=16`
- `offset_match_count=16`
- `next_case_offset_match_count=16`

Important nuance: each failed free row's `ctr` points at the previous table case,
while the failed owner offset matches the next PPC case (`ctr + 0x30`). Example:

- failed row `ctr=821514A0`, base `BDB18CC0`
- owner `BC220000:03A80000`
- owner offset `018F8CC0`
- `ctr=821514A0` computes previous offset `01878C80`
- next case `821514D0` computes `018F8CC0`

This repeated for all 16 rows. The dump shape is a table-driven guest
suballocator sequence: compute a fixed `r30` slice, pass `r3=r30` and `r4=r31`,
call one helper, branch nonzero to `82151158`, otherwise call a second helper,
then exit through `82151900`. HIR has the same table blocks for each target:
four target-range blocks, 46 HIR instructions, three call forms, and four
context barriers per failed target.

Wait/progress join:

- wait trace: 256 rows from `18:02:10.655` to `18:02:12.984`
- final failed free: `18:03:02.018`
- wait rows after final failed free: 0
- wait budget exhausted before final failed free: 1
- VdSwap rows: 2444 from `18:02:08.337` to `18:03:01.481`
- A64 snapshots after final failed free: 87

Top early wait groups are `824AB214` timeout loops on threads 9/10 and
`824AC578` waits on thread 6. Late snapshots keep showing wait/progress PCs:
`824AB214`, `824AC578`, `8272D048`, and `82762158`.

## Decision

Do not hide failed physical frees and do not round down to the owner allocation.
The guest is freeing fixed slices inside one large physical owner, and the
current evidence does not prove that changing heap release semantics fixes the
black frame.

Next useful slice: map the late guest PCs (`824AB214`, `824AC578`, `8272D048`,
`82762158`) and rerun a focused capture with delayed/thread-filtered wait trace
so the wait evidence covers the live black-loop window instead of only the early
startup burst.
