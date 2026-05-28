# Project Sylpheed Physical Ownership Route Audit

## Summary

Fresh launcher/game-library evidence keeps Project Sylpheed alive and presenting
while narrowing the remaining heap issue to repeated interior physical frees
inside one owning allocation.

This is not a native crash in the current packet, and it is not proof that
rounding the free down to the parent region is safe.

## Packet

```text
packet=scratch/thor-debug/remote-debug-20260528-163432
commit=d0895fbc3
route=real LauncherActivity Launch Last Game card
screen=Project Sylpheed opening route, FPS badge visible, no crash dialog
status_classification=project_sylpheed_heap_release_failures_no_crash_marker
android_runtime_count=0
native_signal_count=0
guest_crash_count=0
base_heap_release_count=15
physical_heap_count=15
vulkan_swapchain_count=2
```

Audio is still on the good path in this packet:

```text
AndroidAudioDriver opened AAudio output: sample_rate=48000 channels=2
```

## Updated Audit Tool

`tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1` now parses
the behavior-neutral `Xboxkrnl physical memory audit free` rows and joins them
back to the `BaseHeap` / `PhysicalHeap` / `MmFreePhysicalMemory` failure
triplets.

New fields include:

```text
physical_memory_audit_parsed_free_request_count
physical_memory_audit_parsed_free_result_count
physical_memory_audit_request_region_start_count
physical_memory_audit_request_interior_count
physical_memory_audit_result_success_count
physical_memory_audit_result_failure_count
failed_interior_owner_group_count
top_failed_interior_owner_group
audit_physical_allocation_base
audit_physical_allocation_size
audit_physical_region_start
audit_result
```

## Fresh Result

Report:

```text
scratch/thor-debug/20260528-163432-sylpheed-physical-ownership-audit.txt
```

Key rows:

```text
classification=project_sylpheed_single_owner_interior_physical_heap_frees
decision=design_explicit_physical_suballocation_free_contract_or_owner_refcount; do_not_round_down_parent_region
physical_memory_audit_allocate_count=43
physical_memory_audit_parsed_allocate_count=43
physical_memory_audit_allocation_result_count=42
physical_memory_audit_parsed_free_request_count=20
physical_memory_audit_parsed_free_result_count=20
physical_memory_audit_request_region_start_count=5
physical_memory_audit_request_interior_count=15
physical_memory_audit_result_success_count=5
physical_memory_audit_result_failure_count=15
physical_memory_audit_failed_interior_result_count=15
physical_memory_audit_free_exact_allocation_result_count=4
physical_memory_audit_failed_interior_exact_allocation_result_count=0
parsed_triplet_count=15
failed_interior_owner_group_count=1
top_failed_interior_owner_group=BC220000:03A80000
top_failed_interior_owner_group_count=15
top_failed_interior_owner_allocation=line_index=886 thread=00000006 lr=824A88B4 ctr=8280C420 requested_size=03A723D0 adjusted_size=03A80000 protect_bits=20000004 result=BC220000 parent=1C220000 physical_allocation_size=03A80000 behavior_changed=0
round_down_region_key=00000000:1000:1C220000:14976
round_down_region_size_max_hex=03A80000
```

The important contrast is:

- region-start physical frees exist and succeed in the same packet;
- all 15 failures are non-page-aligned interior frees;
- all 15 failures belong to physical allocation `BC220000` size `03A80000`;
- none of the 15 failed interior frees is an exact allocation result;
- the owner allocation itself is the `MmAllocatePhysicalMemoryEx` result
  `BC220000`, requested as `03A723D0` and rounded to `03A80000`;
- the translated parent addresses are all inside `1C220000-1FCA0000`;
- `behavior_changed=0` for all audit rows.

Example joined row:

```text
physical=BDB18CC0
parent=1DB18CC0
audit_lr=821507BC
audit_ctr=821514A0
audit_physical_allocation_base=BC220000
audit_physical_allocation_size=03A80000
audit_physical_region_start=0
audit_result=0
```

## Decision

Do not round these frees down to the parent region. That would release the whole
`03A80000` byte owner region and would also contradict the successful strict
region-start frees in the same packet.

The next compatibility slice should be source design for an explicit physical
suballocation ownership/free contract or a conservative owner-refcount rule.
The design needs to prove:

- what guest API contract permits `MmFreePhysicalMemory` on an interior physical
  address;
- whether the owning `BC220000:03A80000` allocation should remain live until all
  interior objects are freed;
- whether interior free results should be reported as success without changing
  the parent allocation;
- how to avoid double-free, leak, and stale GPU memory hazards.

## Validation

```text
PowerShell parser check: tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1
fresh packet audit: scratch/thor-debug/remote-debug-20260528-163432
old packet audit smoke: scratch/thor-debug/remote-debug-20260528-114503
```
