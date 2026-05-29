# 2026-05-29 10:43 EDT - Project Sylpheed Heap/Wait Contract

## Context

This follows `20260529-103800-project-sylpheed-filtered-late-window.md`.
The latest Project Sylpheed New Game packet is a live black/loading state with
guest swaps still running, not a scoped Android crash.

## Tooling Fix

`tools/thor/thor_project_sylpheed_free_site_flow_join.ps1` was still reporting
`project_sylpheed_free_site_flow_join_incomplete` even after the fresh packet
had all needed PPC/HIR table matches. The mismatch was that 18/19 owner offsets
match the current or next table case, while the final `821518C0` case has no
next case available in the dump.

The classifier now accepts this as a mapped table with one unmatched offset and
promotes packets with post-free wait, A64 snapshot, and VdSwap coverage to:

```text
classification=project_sylpheed_guest_suballocator_table_with_live_wait_loop
decision=design_explicit_physical_suballocation_release_or_prove_presentation_stall_independence_before_behavior
```

Updated report:

- `scratch/thor-debug/20260529-103800-sylpheed-free-site-flow-join.txt`
- `failed_interior_free_count=19`
- `ppc_target_match_count=19`
- `offset_match_count=18`
- `offset_unmatched_count=1`
- `wait_trace_after_last_failed_free_count=2048`
- `vdswap_after_last_failed_free_count=14880`

## Source Audit

Current behavior is region-start only:

- `MmFreePhysicalMemory_entry` looks up the heap and calls
  `heap->Release(base_address, &released_region_size)`.
- `BaseHeap::Release` rejects an address whose page entry base is not the
  allocation region start.
- `PhysicalHeap::Release` maps the physical address to the parent virtual heap
  address, calls `parent_heap_->Release(parent_base_address, out_region_size)`,
  then triggers physical callbacks and releases the physical heap entry.
- If the parent release fails on an interior address, the physical entry is not
  released and invalidation callbacks are not triggered.

The packet shows this is exactly what Project Sylpheed is doing:

- 110 physical allocation audit rows.
- 46 free requests/results.
- 27 region-start free requests, all successful.
- 19 interior free requests, all failed.
- 26 exact allocation free results, all successful.
- 19 failed interior frees belong to the same owner allocation
  `BC220000:03A80000`, parent `1C220000-1FCA0000`.

The filtered PPC table `821511D0-821518C0` is a guest suballocator/free table:
each case computes an r30 offset, calls one cleanup/free helper, branches to a
shared nonzero-error path at `82151158`, otherwise calls a second helper and
exits at `82151900`. The active wait wrappers are small retry loops:

- `824AB190-824AB23C`: `NtWaitForMultipleObjectsEx`, retrying on `0x101`.
- `824AC540-824AC5A4`: `NtWaitForSingleObjectEx`, also retrying on `0x101`.

Wait trace now covers the post-free black/loading window, and VdSwap continues
after the last failed free, so the remaining issue is a live guest wait/resource
state, not a process abort or presenter stop.

## Compatibility Contract

Do not round down physical frees. The unsafe parent region is
`1C220000-1FCA0000` / `0x03A80000`, and blindly releasing it would invalidate
the whole owner allocation while the guest is freeing interior child ranges.

A guarded behavior design must first make the ownership model explicit:

- Keep exact allocation and region-start frees unchanged.
- Track physical owner allocations by physical base, parent base, size,
  protect, and page size.
- Record interior free requests as child/suballocation offsets under the owner.
- Do not release the parent heap range until a proven owner-refcount or
  full-coverage rule says the owner is fully dead.
- Preserve physical callback invalidation semantics for any child or owner
  release that changes guest-visible memory/GPU state.
- Keep `MmFreePhysicalMemory`'s void guest ABI intact; the fix cannot rely on a
  return code because the guest never receives one.
- Keep the first behavior candidate default-off and title/packet-gated until a
  route proves it leaves menu/title geometry, audio, swaps, and crash markers
  intact.

## Decision

The next useful implementation slice is a default-off physical-suballocation
ownership ledger/audit. It should group interior frees by owner allocation,
infer contiguous child coverage from the Sylpheed table, report whether the
owner would become fully released, and estimate which callbacks/parent releases
would fire. Only after that should we try a guarded behavior path.

## Validation

```powershell
$errors = $null; [void][System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_project_sylpheed_free_site_flow_join.ps1', [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_free_site_flow_join.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-103800-sylpheed-filtered-late-window -OutPath scratch\thor-debug\20260529-103800-sylpheed-free-site-flow-join.txt
```
