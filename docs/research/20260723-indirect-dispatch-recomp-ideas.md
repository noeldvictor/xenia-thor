# Indirect-dispatch + recomp/GameNative ideas (2026-07-23)

Research mined for the next levers, triggered by the finding that BD's AOT
runtime residue (~1,536 fns) is **virtual/function-pointer dispatch**, not switch
tables (the jump-table scan, committed 1729b7bf3, covered those). What the recomp
scene + latest DBT research + GameNative do about it.

## The residue = indirect dispatch; it's inherently a RUNTIME lookup

The residue functions are reached only via `bctr`/`blr`-to-computed-target
(virtual calls, function pointers loaded from data). These cannot be discovered
statically (undecidable) - every recompiler handles them with a **runtime
guest-addr -> host-fn lookup**. So the lever is not more static coverage; it's
making that lookup + the residue's first-compile cheap.

## Lever 1 (CPU, Android-side): direct-indexed dispatch table (Tiaozhuan FAM / XenonRecomp)

- **Tiaozhuan** (ACM TACO, Mar 2025, DOI 10.1145/3703355): "each guest indirect
  branch still translates into ~10 host instructions" with hash-table/stack GPC->
  HPC mapping. Its **Full Address Mapping (FAM)** simplifies the data structure to
  a direct-indexed map so the lookup is a couple of instructions, not ~10. (Plus
  Exception-Assisted Branch Elimination.)
- **XenonRecomp** does exactly this: a "perfect hash table" = the recompiled
  function pointer is found by **dereferencing at `guest_addr * 2`**, with the
  pointer table **placed just past the valid XEX memory region** in the base
  pointer (they dropped an 8 GB virtual alloc for this lighter scheme).
- **xenia today:** the LLVM guest-call path uses an 8192-slot lock-free
  self-validating resolve-cache (hash by low bits + validate) - already a
  fast-path (+25% BD, memory), but it's a HASH with validation, not a
  collision-free direct index.
- **Actionable:** replace/augment the resolve-cache with a **direct-indexed
  guest->host table** keyed by `(guest_addr - base) / 4` (~1.7M entries * 8B =
  ~14 MB for BD's range), placed once, no collision/validation. O(1), ~1-2
  instructions per indirect call. Directly attacks the per-indirect-call dispatch
  cost the resolve-cache already proved is a real BD cost.
  **Location:** the LLVM backend guest-call helper (xe_llvm_resolve_function) =
  Android-compile + device-validate. Deferred while device testing is paused.

## Lever 2 (GPU): the custom Turnip driver is the single biggest variable

- GameNative/GameHub/Winlator consensus: **custom Mesa Turnip drivers = +30-50%
  fps on Adreno**, "the single biggest performance variable." GameNative's speed
  is Turnip + FEX/Box64 + DXVK, with "sensible defaults for compat/heat/FPS."
- **xenia-thor already ships a custom Turnip (26.0 R8/rc08, in-APK).** Actionable:
  track whether a NEWER/better-tuned Turnip (or GameHub's bundled build) lifts
  BD/RE2 - a driver swap + device measure. Device-gated.
- The DXVK-for-360 GPU HLE (the committed native-renderer direction) IS the
  "steal from GameNative" idea already: replace xenia's LLE Xenos with a native
  Vulkan translator, the same shape as GameNative's DXVK.

## What this means for the plan

- **CPU static-recomp is near its device-free ceiling.** AOT coverage is ~92% and
  validated; the jump-table scan (committed) added the statically-discoverable
  switch tables; the remaining residue is virtual dispatch, which is inherently a
  runtime lookup. The next CPU lever is **dispatch SPEED** (Lever 1, direct-indexed
  table) - Android-side + device-validated.
- **The GPU HLE (native Vulkan renderer) is the marquee 30fps path** and where the
  biggest remaining win is - device-gated (the redirect spec'd in
  20260719-bd-native-rt-substrate-and-redirect-spec.md).
- **Both top levers need a device session.** The device-free structural runway
  (AOT coverage, native-RT substrate, capture, metric) is largely built + committed.

Sources: XenonRecomp README (github.com/hedge-dev/XenonRecomp) · N64Recomp README
(github.com/N64Recomp/N64Recomp) · Tiaozhuan (dl.acm.org/doi/10.1145/3703355) ·
GameNative perf (gardinerbryant.com, droix.net Turnip +30-50%).
