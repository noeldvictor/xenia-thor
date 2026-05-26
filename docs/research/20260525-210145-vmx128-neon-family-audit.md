# VMX128 / NEON Opcode-Family Audit

Date: 2026-05-25

## Prompt

Continue from the reset plan with a structural VMX128-to-NEON slice rather than
another one-edge payload or first-barrier audit. The goal was to turn "inline
patching for VMX128" into a source/test decision surface that can guide broad
A64 backend work without reopening closed Blue Dragon micro-lanes.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_vmx128_neon_family_audit.ps1
```

## Tooling Change

Added `tools/thor/thor_a64_vmx128_neon_family_audit.ps1`.

The tool reads:

- `src/xenia/cpu/backend/a64/a64_seq_vector.cc`
- `src/xenia/cpu/backend/x64/x64_seq_vector.cc`
- `src/xenia/cpu/testing`
- `docs/research/20260525-203000-continual-harness-plan-reset.md`
- `docs/research/20260525-203900-a64-mature-gap-reset-rerank.md`
- `docs/worklogs/20260525.md`

It reports whether the current route evidence still says CPU/JIT wall, whether
the stale stvewx / three-PC `MUL_ADD_V128` / edge-payload lanes are closed, then
ranks VMX128/NEON opcode families by source shape and unit-test coverage.

## Result

The audit produced these global source facts:

- `cpu_wall_current=True`
- `edge_payload_closed=True`
- `stvewx_closed=True`
- `mul_add_three_pc_closed=True`
- `a64_tbl_sites=15`
- `a64_load_v128_const_sites=48`
- `a64_vector_mov_x0_sites=28`

Family findings:

- `permute` is a broad candidate but needs route dynamic count first. A64 has
  `15` `tbl` sites and the default-on `arm64_permute_i32_zip_fastpath`, while
  active unit coverage is only `2` permute tests.
- `load_vector_shift` has tests (`2`) and source coverage, but the current
  lowering rebuilds base control vectors with scalar `mov` / `fmov` / `ins`
  work per op. It needs current hot-route volume before a codegen change.
- `extract_splat` remains tempting because dynamic extract builds scalar TBL
  controls, but the exact stvewx fold is closed and SPLAT has `0` active unit
  tests. Do not reopen stvewx from this evidence.
- `pack_unpack` is broad but test coverage is the first blocker. Active tests
  cover only `4/9` PACK modes and `4/9` UNPACK modes:
  `D3DCOLOR`, `FLOAT16_2`, `FLOAT16_4`, and `SHORT_2`. Missing active coverage
  includes `SHORT_4`, `UINT_2101010`, `ULONG_4202020`, `8_IN_16`, and
  `16_IN_32`. There are also `4` commented-out UNPACK-style tests in
  `unpack_test.cc`.
- `lvl_lvr_vector_loads` is only a candidate after a route-shape count shows
  broad volume outside the closed local-only `822824F0` peephole.
- shift / compare / minmax work is lower priority under the reset ranking.

## Ranked VMX128/NEON Next Steps

1. `pack_unpack_test_coverage`
   Widen the VMX128 semantic harness before optimizing this broad family.

2. `permute_route_volume_audit`
   Count route dynamic PERMUTE by function/block before adding more TBL or
   constant-control fastpaths.

3. `load_vector_shift_control_materialization`
   Consider shared constant/control materialization only after route volume is
   proven.

4. `extract_splat_non_stvewx_volume`
   Add SPLAT tests or prove non-stvewx dynamic EXTRACT/SPLAT volume first.

5. `lvl_lvr_family`
   Revisit only if fresh HIR shows broad load-left/right usage outside the
   closed `822824F0` lane.

## Decision

Do not patch VMX128 behavior next without either expanded unit coverage for the
chosen opcode family or fresh route-volume counters proving broad non-closed
usage.

The most useful next VMX128 slice is not another Blue Dragon one-PC cvar. It is
either:

- add missing PACK/UNPACK unit coverage for the currently untested modes, then
  consider broad A64 lowering cleanup; or
- run a fresh HIR vector-shape route-volume audit for PERMUTE /
  LOAD_VECTOR_SHL/SHR / non-stvewx EXTRACT-SPLAT across the current hot
  functions.

No Thor capture was run because this slice changed only tooling and planning,
not runtime behavior.
