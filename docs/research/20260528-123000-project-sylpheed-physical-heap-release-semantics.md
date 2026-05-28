# Project Sylpheed Physical Heap Release Semantics

## Summary

Added a behavior-neutral packet audit:

```text
tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1
```

It parses `BaseHeap::Release`, `PhysicalHeap::Release`, and
`MmFreePhysicalMemory` failure triplets from Android logcat and computes whether
the requested free is the parent region base, an interior address, repeated, or
page-aligned. It also joins the rows with Android reenter longjmp and `VdSwap`
positions in the same packet.

## Proof

Black-frame packet after the Android reenter longjmp fix:

```text
scratch/thor-debug/remote-debug-20260528-114503
scratch/thor-debug/20260528-123000-sylpheed-physical-heap-release-audit.txt
```

Key result:

```text
classification=project_sylpheed_interior_physical_heap_frees_after_reenter_no_present_swaps
decision=add_source_or_runtime_owner_trace_before_any_physical_free_behavior_patch
longjmp_reenter_count=6
native_abort_marker_count=0
vd_swap_count=9024
vd_swap_after_longjmp=0
parsed_triplet_count=18
parsed_triplets_after_longjmp=1
distinct_parent_address_count=18
repeated_parent_address_occurrences=0
distinct_parent_region_count=1
inside_region_count=18
region_start_count=0
parent_page_aligned_count=0
physical_page_aligned_count=0
round_down_region_key=00000000:1000:1C220000:14976
round_down_region_size_max_hex=03A80000
round_down_region_size_max_bytes=61341696
```

The post-longjmp row is still an interior free:

```text
parent=1DA98C80 physical=BDA98C80 region_base=1C220000 region_end=1FCA0000 offset=01878C80 after_longjmp=1
```

Visible menu control packet:

```text
scratch/thor-debug/remote-debug-20260528-114147
scratch/thor-debug/20260528-123000-sylpheed-menu-physical-heap-control.txt
```

It also has interior physical free failures while the menu is visible:

```text
classification=project_sylpheed_interior_physical_heap_frees
longjmp_reenter_count=0
vd_swap_count=4710
parsed_triplet_count=17
inside_region_count=17
region_start_count=0
```

## Interpretation

`BaseHeap::Release` requires the requested address to equal a region base. In
this packet, every Sylpheed physical free maps to a parent address inside the
same large parent region:

```text
region_base=1C220000
region_end=1FCA0000
region_pages=14976
region_size=0x03A80000
```

None of the failed frees is a parent region start or even page-aligned. The
addresses are distinct, so the packet does not look like a simple repeated
double-free of one stale pointer.

The visible menu control already contains 17 of these failures with thousands
of `VdSwap` rows, so the interior free failures alone are not sufficient to
explain black presentation. The black-frame packet adds one more interior free
after the first Android reenter longjmp, and no `VdSwap` rows appear after that
point. That makes the correlation:

```text
reenter longjmp -> one more interior physical free -> no later VdSwap rows
```

not:

```text
any interior physical free -> presentation immediately dies
```

## Decision

Do not implement a blind physical free round-down. Rounding the post-longjmp
parent address down to `1C220000` would release a `0x03A80000` parent region,
which is much larger than the requested interior address and could invalidate
live memory.

The next useful compatibility slice is one of:

- source/runtime ownership tracing for `MmAllocatePhysicalMemory*` and
  `MmFreePhysicalMemory` around this region, to determine whether Sylpheed is
  passing an expected interior allocation handle that Xenia should understand;
- a post-reenter execution/presentation audit that proves why no `VdSwap` rows
  follow the longjmp path;
- a separate Android audio backend, since `apu=any` still reaches
  `NopAudioSystem`.
