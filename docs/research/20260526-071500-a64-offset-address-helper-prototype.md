# A64 Offset Address Helper Prototype

## Context

The current Blue Dragon Thor wall is still CPU/JIT generated-code bound around
`82282490`, `82281D28`, and `82287788`, not broad Vulkan bound. The latest
`82281D28` offset audit showed scalar guest-stack addressing dominates the
mapped hot block:

- `offset_ops=365`
- `load_offset_ops=253`
- `store_offset_ops=112`
- `body_weighted_offset_ops=450860314`
- `body_weighted_guest_stack_ops=411865334`
- dominant block `82281D28:8228233C-82282370`

The source-level feasibility audit concluded that a reusable A64 lowering is
legal only if it preserves guest-address semantics: 32-bit wrap before membase,
byte swap behavior, MMIO / exception visibility, store-watch `x0`, and fallback
behavior.

## Patch

Added default-off `arm64_offset_memory_address_fastpath`.

Implementation details:

- `src/xenia/cpu/backend/a64/a64_seq_util.h` adds
  `ComputeMemoryAddressOffset`.
- `src/xenia/cpu/backend/a64/a64_seq_memory.cc` routes normal
  `LOAD_OFFSET` / `STORE_OFFSET` lowering through `ComputeOffsetMemoryAddress`.
- Android launch plumbing forwards `arm64_offset_memory_address_fastpath`.
- `tools/thor/thor_xenia_debug.ps1` exposes
  `-Arm64OffsetMemoryAddressFastpath` and records it in capture metadata.
- `tools/thor/thor_a64_memory_lowering_feasibility.ps1` now recognizes either
  the old two-step lowering or the new default-off prototype with fallback.

The enabled path folds constant offset address math before membase while keeping
`x0` as the final 32-bit guest address. Non-constant offsets and
`allocation_granularity() > 0x1000` fall back to the old two-step path.

This is not a host-pointer plus immediate-addressing fold. That remains blocked
without a no-wrap proof.

## Validation

- `tools/thor/thor_a64_memory_lowering_feasibility.ps1` passed and reports:
  `legal_offset_helper_candidate=1`,
  `default_off_offset_helper_prototype=1`, and
  `behavior_changed=default_off_only`.
- `tools/thor/thor_xenia_debug.ps1` parser check passed.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.

## Decision

This is a behavior prototype, not speed proof.

Next useful slice:

1. FullDeploy.
2. Run a route-clean Thor capture with
   `-Arm64OffsetMemoryAddressFastpath true` and delayed body-time comparators
   `82282490,82281D28,82287788`.
3. Treat that as route-safety proof only. Do not run a quiet speed A/B until
   the enabled path reaches visible opening sky / dragon-wing with clean fatal
   markers.
