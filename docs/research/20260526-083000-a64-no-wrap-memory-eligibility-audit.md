# A64 No-Wrap Memory Eligibility Audit

## Status

Fastmem/addressing is closed for the current Blue Dragon sprint lane. The
existing `arm64_offset_memory_address_fastpath` remains useful and should stay
explicitly enabled in future Blue Dragon speed captures, but there is no
defensible next behavior patch that changes memory addressing to host pointer +
immediate addressing.

## Inputs

New deterministic tool:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_no_wrap_memory_eligibility_audit.ps1
```

Output:

`scratch/thor-debug/20260526-083000-a64-no-wrap-memory-eligibility-audit.txt`

The tool joins the current route-backed offset reports:

- `scratch/thor-debug/20260526-063600-82281d28-load-store-offset-audit.txt`
- `scratch/thor-debug/20260526-080921-82282490-load-store-offset-audit.txt`
- `scratch/thor-debug/20260526-080921-82287788-load-store-offset-audit.txt`

It also checks the A64 and x64 memory-lowering source:

- `src/xenia/cpu/backend/a64/a64_seq_util.h`
- `src/xenia/cpu/backend/a64/a64_seq_memory.cc`
- `src/xenia/cpu/backend/x64/x64_seq_memory.cc`

## Findings

Source checks confirm the current helper is the legal endpoint for the already
implemented behavior:

- `compute_memory_address_offset=1`
- `offset_helper_keeps_x0_guest_address=1`
- `offset_helper_uses_w_register_add=1`
- `offset_helper_large_page_fallback=1`
- `load_offset_uses_membase_plus_x0=1`
- `store_watch_uses_final_x0=1`
- `inline_mmio_path_present=1`
- `mmio_aware_path_present=1`
- `x64_offset_reference_present=1`
- `x64_high_address_threshold_present=1`

Route volume is material:

- `offset_ops_total=609`
- `load_offset_ops_total=409`
- `store_offset_ops_total=200`
- `body_weighted_offset_ops_total=585213030`
- `body_weighted_host_pointer_shape_candidates=505659826`

But no-wrap proof is absent:

- `runtime_no_wrap_counter_present=0`
- `static_range_analysis_present=0`
- `static_no_wrap_provable_rows=0`
- `runtime_no_wrap_proven_rows=0`

Top weighted bases are guest-GPR derived:

- `guest_stack:r1=358124989`
- `context_gpr:r31=91625178`
- `context_gpr:r27=15467751`
- `context_gpr:r29=15467751`
- `context_gpr:r28=14805354`

The hottest rows are still known closed shapes:

- `82282490:822825E0-822825F0` scalar context/CR traffic.
- `82281D28:8228233C-82282370` guest-stack argument/call setup.
- `82282490:8228252C-822825C4` closed stvewx / MUL_ADD_V128 / fpscr local
  shape.

## Decision

Do not implement host pointer + immediate A64 addressing from this evidence.

The optimization would need to prove that `guest_address + offset` never wraps
32-bit guest address space. Current HIR/profile evidence only says the bases are
guest GPR-derived (`r1`, `r31`, `r27`, `r29`, `r28`, etc.); it does not contain
range facts. Current source also has no runtime no-wrap counter or static range
analysis surface. Changing the addressing form without that proof risks
breaking:

- 32-bit guest wrap semantics before applying membase;
- `x0` as the final guest address for store-watch/debug paths;
- inline-MMIO and recorded exception visibility;
- byte-swap paths;
- large-page `allocation_granularity()` threshold behavior.

Close the current fastmem/addressing lane. Reopen only if a future slice is
explicitly about adding route counters or static range analysis that proves
material, route-wide no-wrap candidates beyond the closed local shapes.

## Next

Move to a different structural A64 lane. Acceptable next lanes are:

- a guarded-stub / late-bound-entry source design only if it handles normal
  entry, global indirection, stackpoint/debug/exception visibility, dirty
  flushes, and unresolved targets;
- guest-state cache work only if it avoids the closed `r1`/`r11`/`f1`/fpscr /
  barrier lanes;
- helper/block-linking only with a reusable static rule;
- measured GPU offload only after counters show GPU/present/submit/pipeline
  work overtaking CPU/JIT.
