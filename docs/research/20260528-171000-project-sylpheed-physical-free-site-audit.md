# Project Sylpheed Physical Free-Site Audit

## Summary

Added a focused packet audit for Project Sylpheed's physical free sites:

```text
tools/thor/thor_project_sylpheed_physical_free_site_audit.ps1
```

It parses behavior-neutral `Xboxkrnl physical memory audit` allocation/free
rows and reports the guest runtime LR/CTR contexts for failed interior physical
frees.

## Packet

```text
packet=scratch/thor-debug/remote-debug-20260528-163432
report=scratch/thor-debug/20260528-170900-sylpheed-physical-free-site-audit.txt
route=real LauncherActivity Launch Last Game card
```

## Result

```text
classification=project_sylpheed_single_owner_guest_suballocator_free_sites
reason=all failed interior frees share one real owner allocation and zero failed frees match allocation results
decision=map_guest_free_site_821507bc_and_ctr_targets; no_heap_behavior_patch_without_size_lifetime
allocate_row_count=43
allocation_result_count=42
free_request_row_count=20
free_result_row_count=20
region_start_free_request_count=5
exact_allocation_free_request_count=4
successful_free_result_count=5
failed_interior_free_count=15
failed_interior_exact_allocation_result_count=0
failed_free_site_count=15
failed_owner_group_count=1
top_owner_group=BC220000:03A80000
top_owner_group_count=15
```

Owner allocation:

```text
top_owner_allocation=line_index=886
thread=00000006
lr=824A88B4
ctr=8280C420
requested_size=03A723D0
adjusted_size=03A80000
result=BC220000
parent=1C220000
protect_bits=20000004
behavior_changed=0
```

## Runtime Free Sites

All failed interior frees share runtime LR `821507BC`. The observed CTR values
are:

```text
821511D0
821512F0
82151320
821513B0
82151470
821514A0
82151530
82151560
821515C0
82151680
821516B0
82151740
82151770
821517A0
821517D0
```

Each row is a distinct interior offset into owner allocation `BC220000:03A80000`.
Observed offsets span:

```text
0057C900 -> 0314A100
```

Representative row:

```text
free_site[5]=lr=821507BC ctr=821514A0 count=1 owner_count=1 owners=BC220000:03A80000 offset_min=018F8CC0 offset_max=018F8CC0 first_bases=BDB18CC0
```

## Interpretation

The fresh evidence now separates three cases:

- region-start free requests exist and succeed;
- exact allocation-result free requests exist and mostly line up with normal
  frees;
- the 15 Project Sylpheed failures are interior owner-pool frees, not kernel
  allocation-result frees.

Since `MmFreePhysicalMemory` is `void`, hiding the failure would be only
diagnostic. Since the free call has no size argument, releasing the interior
address as a range is not safe without guest-side size/lifetime data. Since all
failed frees share owner allocation `BC220000:03A80000`, rounding down would
free the whole 58.5 MiB owner region.

## Next

Use Ghidra/XEX or another guest-code mapping path to inspect LR `821507BC` and
the CTR cluster above. The question is whether the guest free-site knows an
interior allocation size or is freeing objects from a private physical pool.

Do not patch heap behavior until that size/lifetime contract is proven. If the
guest-code mapping cannot produce the contract, shift to live loading /
presentation evidence because the current packet has heap failures without a
scoped crash marker.

## Validation

```text
PowerShell parser check: tools/thor/thor_project_sylpheed_physical_free_site_audit.ps1
fresh packet audit: scratch/thor-debug/remote-debug-20260528-163432
old packet smoke: scratch/thor-debug/remote-debug-20260528-114503
```
