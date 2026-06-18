# Stackable small-wins roadmap ("10 × 1% = 10%") — Codex consult, reconciled

Date 2026-06-18. User: "every little gain matters, get them all." Consulted Codex/gpt-5.5 (Gemini removed)
for an exhaustive enumeration of small, SAFE, stackable wins. Codex returned 70 ranked toggles + a
compilation-lock + XMA plan. Full raw list: scratch/consult/codex.md. This is the reconciled campaign tracker
(each win is landed + verified one at a time; never fabricate a banked gain).

## LANDED this session
- **`hir_const_range_fold` (codegen peephole batch #1)** — extends MaxNonzeroBits: folds AND(x,const) with a
  non-overlapping mask + SHR(x,const) that shifts all known bits out → constant 0 (set_zero + Remove, DCE
  cleans up). Bit-exact; 5 host differential tests (2 fold + 3 discriminators); full suite 158 green; host
  x64 + Android a64 clean. Default-off → flip default-on after a device regression-check (with the other folds).
- **Lock-free fast-path on `GetOrCreate` (the REAL resolution path), not just `Get()`** — reconciling Codex's
  #1/#6 with a scout exposed that `Processor::ResolveFunction` goes through `GetOrCreate` (always locked), NOT
  `Get()`, so the shipped lock-free cache was INERT on the hot path (why the A/B was neutral). Now an
  already-READY function resolves with no global lock. Committed. **Validate next on STEADY GAMEPLAY** (the
  Gears-load A/B can't see it — load is compile-bound, not resolution-bound). This is the corrected form of
  `cpu_lockfree_entry_lookup`.

## Reconciliation vs our walls (do NOT pursue — proven dead)
- Codex #10 `jit_stub_first_publish`, anything that lets a worker compile while gameplay runs = the parallel-JIT
  DEADLOCK wall (recursive global lock). Only JOINED load-window precompile is safe.
- Codex #33/#34 inlining = OK ONLY as leaf-inline with NO cross-barrier state elision (the shared-PPCContext
  wall). #32 small-memcpy-inline = the shared-function NO-GO territory; gate hard.
- Codex #45 vmx min/max/select, #44 splat = OK only for EXACT integer VMX classes; NEVER FP16/int8 on guest
  FP32 (black-screen wall). #5/#62 alpha front-to-back + blend reorder = blend-order hazard; title-gated only.
  depth-prepass stays DEAD.

## Top tier to grind next (device-free validatable = host x64 + qemu-a64, NO fire)
These are codegen peepholes — verify each is NOT already emitted by the a64 backend, then implement + add a
host/qemu differential test, default-off → flip after qemu-clean. Each ~0.2-1.5%, they STACK:
- #29 `hir_const_range_fold` — extend my `MaxNonzeroBits` (known-bits) to drop sign/zero-extends beyond known
  masks. Direct extension of the shipped `hir_known_bits_mask_fold`. EASIEST next (infra exists).
- #35 `a64_tbz_tbnz_bit_tests`, #36 `a64_cbz_cbnz_zero_branches`, #28 `ppc_cond_select_fold` (csel/csinc),
  #37 `a64_ccmp_chains`, #39 `a64_movn_movz_min`, #27 `ppc_branch_layout_hot`, #40 `a64_pair_load_store_context`
  (ldp/stp PPCContext fields), #41 `a64_prfm_guest_stream` (prefetch), #42 `a64_crc32_guest_hash` (CRC32 unit).
- #23 `ppc_cmp_branch_fuse2`, #24 `ppc_subf_cmp_zero_fuse`, #25 `ppc_cr_logical_fold`, #26 `ppc_record_bit_lazy`
  — FLAGM2/NZCV branchy-integer wins (Burnout class). #31 `hir_address_common_subexpr` (CSE guest addr calc).
- **Also: `hir_known_bits_mask_fold` → default-on** (shipped, bit-exact, host-tested; one device regression-
  check then flip + XeniaOptimizations toggle).

## Compilation-lock churn (the measured ~13.8% at load) — the safe attack (Codex's plan)
ResolveFunction ALREADY compiles outside the global lock (scouted); the churn is acquisition FREQUENCY +
the COMPILING spin. Safe levers (NOT parallel JIT): #6 `jit_per_bucket_locks` (shard the entry-table lock off
the global recursive mutex by addr hash), #2 `jit_batch_code_protect` (coalesce mprotect/icache during
joined precompile), #8 `jit_compile_metadata_defer`, #13 `jit_slab_allocator` (TL compile arenas), #15
`jit_duplicate_request_merge` (promise/future, not lock re-enter), #11 `jit_lse_refcounts` / #12
`jit_rcpc_metadata_reads`. Plus the shipped `cpu_precompile_guest_functions` A/B with #14 call-graph ordering.

## XMA decoder (the measured 21.5% at load) — host-side, cross-title
First MEASURE whether FFmpeg uses NEON for the active XMA path. Then: #19 `xma_fixed_format_fastpath`,
#18 `xma_ffmpeg_frame_reuse` (buffer reuse), #3 `xma_decode_cache`, #16 `xma_thread_affinity` (pin to A715,
keep X3 for guest main), #20 `xma_neon_imdct_window`. Correctness harness = per-packet PCM CRC + by-ear.

## GPU CPU-side hygiene (BD + CPU-bound GPU paths)
#56 `vk_eds_dynamic_state` (blend/depth/raster → EDS, cut pipeline variants), #54 `vk_push_descriptor_hot`,
#55 `vk_descriptor_index_cache`, #57 `vk_state_shadow_compact`, #58 `vk_mdi_same_state_batch` (multiDrawIndirect),
#53 `uma_upload_ring`, #60 `vk_tile_area_scissor`, #63 `vk_lrz_preserve_state`, #64 `vk_clear_elide`,
#65 `vk_resolve_region_min`. Each 0.3-3%, TBDR/driver-CPU.

## Underused Thor units (Codex) — exploit constantly
LSE/LSE2 (publish-once metadata/refcounts), RCPC `LDAPR` (read-mostly tables), CRC32 (guest hash idioms),
tbz/cbz/ccmp/csel (branchy scalar), Adreno EDS/push-descriptors/MDI (CPU-side Vulkan), UMA (decoded-audio /
upload / dirty-region caches). FP16/int8 stay barred for guest FP32.

## Discipline
Each win: verify-not-already-done → implement cvar-gated default-off → host x64 + qemu-a64 differential test →
flip default-on + XeniaOptimizations toggle (the stacking UI). Device fires ONLY for the perf A/B of GPU/sync/
resolution levers (steady gameplay, matched, thermal-gated). Never fabricate a banked gain.
