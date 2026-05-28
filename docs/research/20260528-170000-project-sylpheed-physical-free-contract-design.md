# Project Sylpheed Physical Free Contract Design

## Summary

The fresh ownership join closes the simplest compatibility theory:

```text
"Project Sylpheed is freeing addresses returned by MmAllocatePhysicalMemoryEx,
and Xenia merely fails to recognize them."
```

That is not what the route packet shows. The failing interior addresses are not
exact allocation results. They are offsets inside one large physical allocation.

## Evidence

Packet and report:

```text
packet=scratch/thor-debug/remote-debug-20260528-163432
report=scratch/thor-debug/20260528-163432-sylpheed-physical-ownership-audit.txt
```

The audit now parses both allocation and free rows:

```text
physical_memory_audit_parsed_allocate_count=43
physical_memory_audit_allocation_result_count=42
physical_memory_audit_parsed_free_request_count=20
physical_memory_audit_request_region_start_count=5
physical_memory_audit_request_interior_count=15
physical_memory_audit_result_success_count=5
physical_memory_audit_result_failure_count=15
physical_memory_audit_free_exact_allocation_result_count=4
physical_memory_audit_failed_interior_exact_allocation_result_count=0
```

All 15 failed frees are interior addresses under a single owner:

```text
top_failed_interior_owner_group=BC220000:03A80000
top_failed_interior_owner_group_count=15
```

The owner allocation is real:

```text
top_failed_interior_owner_allocation=line_index=886
thread=00000006
lr=824A88B4
ctr=8280C420
requested_size=03A723D0
adjusted_size=03A80000
protect_bits=20000004
result=BC220000
parent=1C220000
physical_allocation_size=03A80000
behavior_changed=0
```

Example failed free:

```text
base_address=BDB18CC0
parent_address=1DB18CC0
audit_lr=821507BC
audit_ctr=821514A0
audit_physical_allocation_base=BC220000
audit_physical_allocation_size=03A80000
audit_physical_region_start=0
audit_result=0
```

## Source Contract

Current source behavior:

- `MmFreePhysicalMemory_entry` is `void`, so the guest does not receive a
  status code from this export.
- `MmFreePhysicalMemory_entry` looks up the heap for the physical address and
  calls `heap->Release(base_address, &released_region_size)`.
- `PhysicalHeap::Release` translates the physical address to the parent heap
  address and calls `parent_heap_->Release(parent_base_address, ...)` first.
- `BaseHeap::Release` requires the requested address to be the region start.
- If parent release fails, `PhysicalHeap::Release` returns before physical heap
  release or callback invalidation.

That source contract is correct for exact region starts, and the fresh packet
proves exact region-start frees still succeed.

## Rejected Behavior Patches

Do not round interior frees down to the owner. For Project Sylpheed, that would
release `BC220000:03A80000` / parent `1C220000-1FCA0000`, a 58.5 MiB live
region.

Do not merely suppress the log or treat the export as "success". Since the
export is already `void`, that would only change diagnostics, not reclaim any
memory or explain the later loading/presentation behavior.

Do not release the interior address as a page range without a size. The API
call carries no size, and the current packet proves the failed interior frees
are not direct kernel allocation results.

## Candidate Contract

A safe future behavior path needs an explicit physical suballocation model:

1. Track owner physical allocations returned by `MmAllocatePhysicalMemoryEx`.
2. Track interior free requests by owner allocation, free-site LR/CTR, and
   physical offset.
3. Only free or tombstone an interior range if a separate source of truth gives
   its exact size and lifetime.
4. Keep the owner allocation live until all known interior objects are released
   and no GPU/shared-memory callbacks still depend on it.
5. Treat duplicate interior frees as diagnostics until double-free semantics are
   understood.

Without a size/lifetime source, the only safe runtime behavior is to keep the
owner allocation live and continue execution.

## Next

The next useful slice is not a heap behavior patch yet. It should identify the
guest free-site contract:

- map LR `821507BC` and the top CTR targets such as `821514A0`, `821511D0`,
  `821512F0`, `821513B0`, and `82151530`;
- determine whether those functions know the interior block size;
- determine whether a surrounding guest allocator owns the `BC220000:03A80000`
  pool;
- only then add a guarded compatibility path.

If the free-site cannot provide size/lifetime data, shift back to the live
loading/presentation stall lane because the current heap failures are nonfatal
in the library packet.

## Validation

```text
PowerShell parser check: tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1
fresh packet audit: scratch/thor-debug/remote-debug-20260528-163432
old packet audit smoke: scratch/thor-debug/remote-debug-20260528-114503
```
