---
name: xenia-a64-speed-hotpath
description: Use for Xenia Thor ARM64 performance work on AYN Thor Max when Blue Dragon is CPU/JIT-bound, including A64 backend maturity, guest-state traffic, helper ABI, block linking, VMX128/NEON lowering, fastmem/addressing, and speed-profile evidence.
---

# Xenia A64 Speed Hotpath

Use this when the measured wall is Main Thread / generated A64 code. Keep this
skill procedural; detailed chronology lives in research notes.

## Current Speed Read

No sustained 30 FPS proof exists. Recent Blue Dragon route captures remain
CPU/JIT-bound, with `82282490`, `82281D28`, and `82287788` recurring in
body-time rows while GPU Commands stays well below the main thread. Do not make
Vulkan the main sprint lane until counters show present/submit/pipeline/GPU
work overtaking A64.

## Mature Backend Gate

Before another title-specific micro-fastpath, answer these from source,
profiler, and HIR evidence:

- **Register allocation/state:** are hot guest GPR/FPR/VMX/FPSCR values kept in
  host registers with explicit dirty, spill, helper, call, branch, and external
  visibility rules?
- **Helper ABI:** are calls forcing avoidable stackpoint, FPCR/FPSCR, scratch
  save/restore, or static-register churn?
- **Block linking:** are direct exits, recursive edges, and indirection paths
  linked or patched where safe?
- **Fastmem/addressing:** are hot guest loads/stores using the cheapest legal
  A64 path with slow fallback coverage?
- **VMX/FP lowering:** can a general opcode lowering remove broad NEON work
  while preserving NaN, denormal, FPCR/FPSCR, endian, and VMX128 semantics?
- **Harness:** can the hypothesis be audited offline before a Thor route run?

Use `docs/research/20260525-143937-mature-a64-emulator-backend-patterns.md`
and `docs/research/20260525-150348-82282490-82287788-register-cache-gap.md`
as the design baseline.

## Closed Immediate Lanes

Do not repeat these without new evidence:

- `82282490:82282598 -> 82287788` edge payload behavior; lifetime audit found
  no surviving `f[1]`/`r[3]` window.
- Pre/post-promotion `r1`/`r11` register-cache behavior from the 2026-05-25
  counters.
- Narrow stvewx lane folds, three-PC `MUL_ADD_V128`, standalone `f[1]`, fpscr
  dirty-cache behavior, local-only `822824F0` peepholes, broad CR fusion, and
  old emit-time context caches.

## Better A64 Bets

Favor changes that can affect many dynamic instructions:

- source-level register allocation or guest-state cache improvements with
  explicit CFG/lifetime rules;
- direct-call/block-linking dispatch cost reductions with normal fallback;
- helper ABI cleanup that removes repeated scratch/FPCR/FPSCR/state churn;
- general VMX128/NEON lowering for high-volume opcodes, not a single PC;
- fastmem/addressing improvements with correctness fallback;
- mixed/static hot-function variant experiments only when guest-visible state is
  explicit and normal entry remains correct.

Current structural decision:
`docs/research/20260526-001500-a64-register-allocation-audit-capture.md`
records FullDeploy plus a route-clean `0x82282490`
`arm64_register_allocation_audit` capture. It found no material allocator spill
pressure: `blocks_with_spills=0`, `local_slots_added=0`, and INT/FLOAT/VEC
`spill_requests=0`. Do not patch allocator spill behavior or run a quiet speed
A/B from this audit. `docs/research/20260526-004800-a64-helper-block-link-audit.md`
then found material dispatch volume in the warmed route:
`dispatch_delta_total=4216370`, `dispatch_delta_per_entry_delta=1.10`,
`resolve_misses_total=0`, and only `resolves_delta=5`. Do not target resolver
behavior next. Use existing call-edge profiling on
`82282490,82281D28,82287788` before any helper ABI or block-linking behavior
patch. `docs/research/20260526-011000-a64-call-edge-recursion-capture.md`
ran that capture and found `82282490` and `82281D28` dominated by
self-recursive child body time, not caller-side dispatch overhead. Do not rerun
unchanged call-edge captures or generic recursive-call/stackpoint probes.
`docs/research/20260526-012000-vmx128-route-stabilized-counters.md` then ran
the current VMX128 route counters and closed broad PERMUTE /
LOAD_VECTOR_SHL / LOAD_VECTOR_SHR behavior for now: PACK stayed absent,
UNPACK stayed zero weighted, `82282490` / `82281D28` vector volume sat in
closed stvewx/vmaddfp shapes, and `82287788` vector work was not the dominant
body-time wall. `docs/research/20260526-014000-82281d28-hir-block-profile-join.md`
then recovered a one-function `82281D28` OptHIR dump and found the current
block-profile/HIR join unsafe: runtime profile guests such as `8228233C` are
not printed HIR labels/comments, and ordinal fallback maps hot rows to the
wrong HIR label. Do not patch scalar/context-barrier/helper ABI or direct-call
behavior from weighted `82281D28` HIR joins until a deterministic mapper or
metadata dump proves the join safe.

`docs/research/20260526-015900-a64-block-profile-metadata-mapper.md` adds the
metadata surface and disables ordinal fallback by default in the vector,
block-mix, and call-path reports. It passed `NativeCore`; it is not speed proof.
The deployed follow-up in
`docs/research/20260526-022000-82281d28-metadata-capture-blocker.md` found
metadata rows for `82281D28`, but all active rows remained unmappable to
printed OptHIR. The top active unmappable row is block `20`, guest
`8228233C`, total `47409053`, source span `8228233C-82282370`.

Next structural tooling target:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -DisassembleFunctionFilter "82281D28" -Arm64SpeedProfileBlockFilter "82281D28" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 30 -Arm64SpeedProfileMinDelta 1 -StopAppAfterCapture true
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_block_profile_join_audit.ps1 -LogPath <new-speed-logcat> -Function 82281D28 -Phase OptHIR -BlockProfileLog <new-speed-logcat> -ProfileKind Body -Top 20
```

`docs/research/20260526-023500-hir-block-profile-stamps.md` added the
behavior-neutral HIR block stamps and join-audit parsing. The follow-up
`docs/research/20260526-030200-82281d28-block-map-capture.md` FullDeployed it
and proved the join usable after raising Thor logcat buffers to 64 MiB:
`metadata_rows=88`, `metadata_mappable_rows=88`,
`hir_block_metadata_rows=88`, `hir_block_mappable_rows=88`,
`active_metadata_unmappable_rows=0`, `join_status=metadata_required`.

The mapped `82281D28:block20 guest=8228233C` hot block is scalar call setup:
`context_loads=5`, `context_stores=13`, `memory_loads=6`, `memory_stores=1`,
`calls=2`, `context_barriers=2`, and call targets `0x826BF770` plus recursive
`0x82281D28`. Treat the mapper lane as complete for now. Do not rerun the
unchanged mapper capture, and do not use `-AllowOrdinalFallback` for behavior
decisions.

Next structural target: offline/source audit
`82281D28:8228233C-82282370` to classify `r3-r10`/`lr` context stores around
`0x826BF770` and the recursive call, compare against stack-sync and memcpy
fastpaths, and decide whether a reusable helper ABI, direct-call, or
stack-argument rule exists before any behavior patch. Large one-function HIR
captures need either file-backed output or `adb -s c3ca0370 logcat -G 64M`
first.

`docs/research/20260526-032000-82281d28-call-setup-audit.md` closes the local
behavior patch: the `826BF770` call's `r3/r4/r5/lr` stores and the recursive
`82281D28` call's `r3-r10/lr` stores are all `callee_live_in`; strict local
promotion has zero safe wins. Do not patch local store elision, rerun this
unchanged call/setup audit, or inline only this `826BF770` caller. The next
useful structural slice is broader evidence for the generic guest-call
argument handoff lane, not a behavior patch.

`docs/research/20260526-033000-guest-call-arg-handoff-audit.md` adds
`tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1`. First pass:
`direct_ppc_bl_calls=81`, `argument_store_fields=439`,
`callee_first_load_stores=62`, `callee_hir_missing_stores=364`,
`normal_entry_fallback_required=81`. The known hot call boundaries are
ABI-live, and most candidate stores need missing callee HIR before a fast-entry
design is defensible. Next run this tool on broader route or file-backed HIR
coverage for missing direct-call-heavy callees.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1 -LogPath <route-speed-logcat> -ExtraLogPath <callee-hir-log> -Function 82281D28 -Phase OptHIR -Top 24
```

`docs/research/20260526-034500-guest-call-hir-coverage-capture.md` improves
the lane. A targeted Thor capture with broader callee HIR plus the corrected
audit reports `callee_first_load_stores=247`, `callee_hir_missing_stores=126`,
and `normal_entry_fallback_required=81`. The body-weighted top call boundaries
are ABI-live.

`docs/research/20260526-040500-a64-guest-call-fast-entry-feasibility.md` adds
`tools/thor/thor_a64_guest_call_fast_entry_feasibility.ps1`. It proves
fast-entry is feasible only as a separate guarded entry path or stub: normal
entry and global indirection must stay unchanged, direct callsites need explicit
guards, and any payload for `r3-r10/lr` needs explicit dirty flushes before
barriers, helpers, host calls, debug/trap visibility, tail calls, returns, and
exceptions. Next step is a default-off counter-only
`arm64_guest_call_fast_entry_audit` skeleton; do not patch runtime behavior or
run a quiet speed A/B yet. The skeleton now exists in
`docs/research/20260526-041500-a64-guest-call-fast-entry-audit-skeleton.md`
and only logs compile-time HIR summaries. Next work is FullDeploy plus a
route-clean capture with `-Arm64GuestCallFastEntryAudit true
-Arm64GuestCallFastEntryAuditFunction 0x82281D28
-Arm64GuestCallFastEntryAuditBudget 16`.

For the helper ABI / block-linking lane, run this offline audit before deciding
whether a Thor call-edge capture is justified:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_helper_block_link_audit.ps1
```

Current helper/block-link decision: the call-edge capture is complete for this
route and should not be repeated unchanged.

For the VMX128/NEON lane, run this before behavior work:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_vmx128_neon_family_audit.ps1
```

Current decision: PACK/UNPACK unit coverage is complete, and the route-volume
audit in `docs/research/20260525-233000-vmx128-route-volume-audit.md` closes
PACK/UNPACK for immediate behavior work: PACK is absent in the checked hot
functions, and UNPACK has zero weighted hot-block volume. The current
route-stabilized counter pass in
`docs/research/20260526-012000-vmx128-route-stabilized-counters.md` also closes
broad PERMUTE / LOAD_VECTOR_SHL / LOAD_VECTOR_SHR behavior for now. Reopen
VMX128 behavior only with fresh body-dominant route volume outside the closed
stvewx/vmaddfp local shapes, plus explicit fallback/correctness coverage.
Non-stvewx EXTRACT/SPLAT needs either SPLAT tests or broad route volume. Do not
reopen exact stvewx or three-PC `MUL_ADD_V128` from this lane.

## Thor Hardware

The Thor lane can use NEON/Advanced SIMD, dot-product, I8MM, BF16/FHM/FCMA,
CRC32, and LSE-style atomics when the opcode/source evidence matches. Do not
assume SVE/SVE2. GPU offload is for Xenos-like bulk work that can stay
GPU-resident: resolves, format conversion, deswizzle/tile transforms, vertex
fetch unpack, clears, copies, and postprocess.

## Capture Discipline

Use delayed body-time for opening-route CPU work:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Keep audit/disassembly/body-block captures separate from quiet speed A/Bs.
Speed A/Bs require matched APK, route, cvars, audit-off settings, and clean
fatal-marker searches.

## Acceptance

For a behavior patch, require:

- default-off or clearly safe default behavior;
- route-clean Thor proof;
- matched quiet A/B or control sandwich if claiming speed;
- worklog plus research note naming expected FPS mechanism;
- commit/push on `master`.

If the expected win is below noise or only affects one tiny PC, close the lane
instead of adding another cvar.
