# A64 Mature Gap Reset Rerank

Date: 2026-05-25

## Prompt

Run one post-reset structural slice for Blue Dragon instead of continuing the
old edge-payload/first-barrier chain. The slice should leave a report or
committed patch and keep the reset plan authoritative.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_mature_backend_gap_report.ps1
```

## Tooling Change

`tools/thor/thor_a64_mature_backend_gap_report.ps1` now defaults to the reset
plan:

- `docs/research/20260525-203000-continual-harness-plan-reset.md`
- `docs/worklogs/20260525.md`
- `docs/research/20260525-143937-mature-a64-emulator-backend-patterns.md`

It now reports reset-specific booleans:

- `route_cpu_wall=True`
- `edge_payload_closed=True`
- `broad_vulkan_blocked_by_current_evidence=True`

It also inspects `src/xenia/cpu/backend/a64/a64_seq_vector.cc` so VMX128/NEON
lowering can rank as a broad structural lane instead of being hidden behind
the old fpscr/edge-carrier work.

## Result

The refreshed report found:

- static host register contract is partial: `x19` backend context, `x20` PPC
  context, `x21` membase, with only `x22-x28` listed for allocator GPRs;
- guest register cache remains weak/negative for immediate behavior work:
  emit-time cache and `r1` live-in probes exist, but prior r1/r11 lanes were
  not speed wins;
- helper ABI and stackpoints are hot structural candidates: direct call site
  exists, `PushStackpoint()` exists, helper inlining sites exist, and the direct
  call ABI returns only guest return in `x0`;
- block linking/dispatch is partial but blocked by singleton normal-entry
  assumptions for edge-specific payload variants;
- fastmem exists through membase load/store and inline MMIO flags, but current
  evidence does not prove memory lowering is the wall;
- vector/fpscr lowering is hot but fpscr behavior remains blocked by CFG
  writeback requirements;
- VMX128/NEON lowering is a broad structural candidate:
  `tbl_sites=15`, `arm64_permute_i32_zip_fastpath` exists, PACK/UNPACK lowering
  exists, `LOAD_VECTOR_SHL` starts at `a64_seq_vector.cc:1107`, and
  `EXTRACT_I32` starts at `a64_seq_vector.cc:185`;
- deterministic backend harness coverage is still missing for mature
  structural changes.

## Reranked Lanes

1. Structural register allocation or guest-state cache.
   This is the largest maturity gap, but it must operate before/with register
   allocation and must avoid stale r1/r11 emit-time caching.

2. VMX128/NEON opcode-family audit.
   This is broad enough to matter and aligned with Thor hardware. Start with
   PERMUTE, LOAD_VECTOR_SHL/SHR, EXTRACT/SPLAT, and PACK/UNPACK source and test
   coverage. Do not reopen one-PC stvewx or three-PC MUL_ADD_V128.

3. Helper ABI and block linking.
   Audit general call/stackpoint/resolver overhead across hot functions before
   any edge-specific payload behavior.

4. Fastmem/memory fallback.
   Only after a hot-route counter shows memory/MMIO/fault fallback cost.

5. GPU/Vulkan offload.
   Not next while current evidence says broad Vulkan is not the wall.

## Decision

Do not return to edge payload storage for
`82282490:82282598 -> 82287788`. The immediate next speed-relevant structural
slice should be either:

- a source/test coverage audit for VMX128/NEON opcode families, because it can
  produce broad backend work without reopening stale payload lanes; or
- a pre-RA/register-allocation design audit that explains how a real
  guest-state cache could exist without repeating the failed r1/r11 emit-time
  probes.

No Thor capture was run in this slice because no runtime behavior changed.
