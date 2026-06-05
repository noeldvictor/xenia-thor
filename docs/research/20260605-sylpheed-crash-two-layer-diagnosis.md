# Project Sylpheed (4D5307F2) early-boot crash — precise two-layer diagnosis

Date: 2026-06-05. Host-only static analysis (device thermally blocked). Grounds:
`src/xenia/memory.cc` `PhysicalHeap::Release`/`Alloc` (read this turn) + the
device crash log captured 2026-06-04 (see memory `thor-game-state`).

## Symptom
Crashes early in boot (xenia "guest has crashed" dialog, VdSwap=0). Device log:
`PhysicalHeap::Release failed due to parent heap failure (physical_address=BC79C900,
parent_address=1C79C900, heap_base=A0000000, page_size=...)` then `BaseHeap::Alloc
page count too big` + `PhysicalHeap::Alloc` failure, then a guest C++ exception
(0xE06D7363 = MSVC "msc") that is never caught → crash.

## Layer 1 — physical-heap mirror aliasing (xenia issue #1559)
`PhysicalHeap::Release(base)` (memory.cc:1590) computes
`parent_base = GetPhysicalAddress(base)` and calls
`parent_heap_->Release(parent_base)`. For Sylpheed: base=`0xBC79C900` (in the
`0xA0000000` **64K-page** physical mirror), parent=`0x1C79C900`.

`0x1C79C900` is **not 64K-aligned** (`& 0xFFFF = 0xC900`). The region was allocated
through the **`0xE0000000` 4K-page mirror** (where intro-video textures live), but is
being freed through the **`0xA0000000` 64K mirror** (dynamic vertices). The parent
heap's `Release` rejects an address that is not a region base at its granularity →
returns false → cascade.

This is exactly the case the in-code comment at memory.cc:1606 cites: "dynamic
vertices in 4D5307F2 start screen and menu allocated in 0xA0000000 at addresses that
overlap intro video textures in 0xE0000000". The two mirrors map the same physical
memory at different page granularities with **independent allocators**, so a region
allocated via one mirror cannot be cleanly released via the other.

Fix class (DEEP, device-validate): make physical Release/Alloc granularity-aware
across the 0xA0000000/0xE0000000 mirrors (resolve the region base in the heap that
actually owns it, or unify the physical allocator across mirrors). High regression
risk — touches all physical memory. NOT a loop-cycle unit.

## Layer 2 — missing guest SEH unwinding (cross-game)
The Layer-1 failure surfaces to the guest as a thrown C++ exception. The guest's
exception machinery needs `RtlCaptureContext` (0x119), `RtlUnwind` (0x147), and
`__C_specific_handler` (0x1A5) — all three are declared `kFunction` in
`xboxkrnl_table.inc` but **have no `_entry` implementation** (verified this turn).
So the throw cannot unwind/dispatch to a handler → hard crash.

This is **cross-game**: any title that throws+catches a C++ exception on a path we
don't otherwise satisfy will crash here, not just Sylpheed. Implementing guest SEH
(stack walk + unwind-data interpretation + handler dispatch into JIT'd frames) is a
substantial subsystem — also NOT a loop-cycle unit, but it is the higher-leverage of
the two (helps many titles, independent of the memory bug).

## Conclusion
Sylpheed needs BOTH a memory-subsystem fix (Layer 1, game-specific-ish, high risk)
and guest SEH (Layer 2, cross-game, high value). Neither is a safe blind host-only
change; both want on-device A/B. Recommend tackling Layer 2 (SEH) first in a
dedicated effort — it is cross-game and unblocks the "exception → crash" class
broadly, of which Sylpheed is one instance. Remains parked for the autonomous grind;
this doc upgrades it from "deep, vague" to precisely root-caused with a fix roadmap.
