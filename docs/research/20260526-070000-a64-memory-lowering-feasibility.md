# 2026-05-26 - A64 Memory Lowering Feasibility

## Context

The previous `82281D28` load/store offset audit showed the current hot wall is
ordinary scalar guest memory addressing, dominated by `r1 + constant`
guest-stack forms. This slice checked whether A64 has a reusable legal lowering
opportunity before any generated-code behavior patch.

No runtime behavior changed:

- `payload_materializations_allowed=0`
- `behavior_changed=0`

## Tool

New deterministic source audit:

```powershell
tools\thor\thor_a64_memory_lowering_feasibility.ps1
```

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_memory_lowering_feasibility.ps1
```

Report:

```text
scratch/thor-debug/20260526-070000-a64-memory-lowering-feasibility.txt
```

## Evidence

The route-volume input remains:

- `offset_ops=365`
- `load_offset_ops=253`
- `store_offset_ops=112`
- `body_weighted_offset_ops=450860314`
- `body_weighted_guest_stack_ops=411865334`
- `body_weighted_context_gpr_ops=38994980`

Source checks found:

- A64 uses `ComputeMemoryAddress(...)` plus `AddGuestMemoryOffset(...)` for
  `OPCODE_LOAD_OFFSET` / `OPCODE_STORE_OFFSET`.
- A64 explicitly keeps guest address arithmetic in W registers so it wraps at
  32 bits before applying host membase.
- A64 has MMIO helper and inline-MMIO paths, so any offset fold cannot
  unconditionally bypass exception / MMIO visibility.
- `STORE_OFFSET_I32` store-watch handling expects `x0` to contain the final
  guest address.
- x64 already has a reference `ComputeMemoryAddressOffset(...)` helper and
  handles the large-page physical mapping threshold with
  `0xE0000000 - offset_const`.
- Android / POSIX `allocation_granularity()` returns `page_size()`, but the
  source rule still needs to preserve the existing large-page fallback for
  portability.

## Decision

The fastmem/addressing lane is not closed. There is a defensible reusable A64
backend candidate: an offset-aware address helper modeled on x64
`ComputeMemoryAddressOffset`, first limited to normal non-MMIO
`LOAD_OFFSET` / `STORE_OFFSET` constant-offset paths.

The first legal behavior candidate must keep `x0` as the final 32-bit guest
address, then use `[membase, x0]` for the host memory access. For small constant
offsets this can plausibly replace the current separate `mov w0, Wsrc` plus
`add w0, w0, #imm` with a folded `add w0, Wsrc, #imm`, while preserving 32-bit
guest wrap.

Do not replace this with host pointer arithmetic such as `membase + base` plus
a load/store immediate unless a no-wrap proof exists. That would silently
change `base + offset` from guest 32-bit arithmetic into host 64-bit pointer
math.

For `allocation_granularity() > 0x1000`, either copy the x64 threshold rule
using `0xE0000000 - offset_const` or keep the current two-step path.

## Next Slice

Implement no behavior from this audit alone and do not run a quiet speed A/B.

Next useful slice is a default-off A64 offset-aware address helper prototype or
source-tested codegen audit for constant-offset normal memory paths. It must
preserve:

- 32-bit guest address wrap before membase;
- byte-swap behavior;
- MMIO / exception visibility;
- store-watch `x0` guest-address expectations;
- existing fallback behavior when the helper is disabled or a path is unsafe.
