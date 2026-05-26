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

`docs/research/20260526-043900-a64-guest-call-fast-entry-audit-capture.md`
ran that route-clean Thor capture. It proves material caller traffic
(`direct_calls=84`, `eligible_regular=67`, `arg_store_fields=421`,
`already_compiled_targets=32`) but blocks behavior for now:
`callee_first_use_known=0`, `callee_first_use_missing=421`,
`normal_entry_fallback=67`, and `flush_context_barrier=260`. Do not rerun the
same capture unchanged, do not run a quiet speed A/B, and do not patch
fast-entry behavior.

`docs/research/20260526-045000-guest-call-callee-aware-audit.md` adds that
callee-aware offline report by extending
`tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1` with
`-FastEntryAuditLogPath` and per-target body-weighted summaries. It reports
real known live-in volume for `82281D28`:
`callee_first_load_stores=247`,
`body_weighted_live_in_fields=690421033`, and
`body_weighted_missing_fields=30011199`, with top known targets
`0x82281D28` and `0x826BF770`. It still blocks behavior because the runtime
compile audit has only summary knowledge (`callee_first_use_known=0`) and high
flush pressure (`dirty_flush_points=268`, `flush_context_barrier=260`). If this
lane continues, the next slice is a default-off runtime per-target row audit for
direct guest calls. It must close the fast-entry behavior lane if target rows do
not show broad known first-use traffic with manageable flush pressure.

`docs/research/20260526-051000-a64-guest-call-fast-entry-target-rows.md` adds
that default-off runtime target-row audit plus parser support. It is not speed
proof and changes no generated behavior: no alternate codegen, no normal-entry
replacement, no global indirection replacement, and no payload materialization.
Next work is FullDeploy plus a route-clean `0x82281D28` target-row capture using
the existing fast-entry audit cvars, followed by a join with the offline
callee-aware HIR audit. Close this lane if the target rows do not show broad
known first-use traffic with manageable flush pressure.

`docs/research/20260526-053600-a64-guest-call-fast-entry-target-row-capture.md`
records that route-clean Thor capture. The joined HIR result shows real broad
known live-in volume (`callee_first_load_stores=247`,
`body_weighted_live_in_fields=690421033`), but the runtime target rows do not
meet the behavior threshold: `unresolved_direct_targets=52`,
`normal_entry_fallback=67`, `stackpoint_sensitive=67`,
`dirty_flush_points=268`, `flush_context_barrier=260`, and
`parent_pre_call_flush_points=195`. Do not patch fast-entry behavior, run a
quiet speed A/B, or rerun the same target-row capture unchanged. Reopen this
lane only with a source-level guarded-stub / late-bound-entry design that
explicitly preserves normal entry, global indirection,
stackpoint/debug/exception visibility, and dirty flushes. Otherwise prefer a
hot-block A64 codegen-floor/disassembly audit for the mapped body-dominant
blocks, starting with `82281D28:8228233C-82282370`.

`docs/research/20260526-054200-82281d28-hot-block-codegen-floor.md` completes
that audit for `82281D28:8228233C-82282370`. The hot block is scalar call /
guest-stack argument setup: `store_context=13`, `load_offset.1=6`,
`load_context=5`, `calls=2`, and `context_barriers=2`. It calls `0x826BF770`
and recursive `0x82281D28`. ARM64 Capstone disassembly was unavailable in the
capture, so this is HIR/source codegen-floor evidence, not machine-code
disassembly. Do not patch local store elision, fast-entry behavior, VMX128, or
GPU/Vulkan from this block. The next useful slice is a default-off counter-only
guest-stack argument handoff audit for direct guest calls: count `LOAD_OFFSET`
from `r1 + constant` feeding `STORE_CONTEXT r3-r10/lr` immediately before a
direct call, with target/callsite rows, body-time weighting, resolved/compiled
state, normal-entry fallback pressure, blocker classes, and estimated avoidable
guest stack load / context store traffic.

`docs/research/20260526-055500-guest-stack-arg-handoff-audit-skeleton.md` adds
that default-off counter-only audit plus Android/Thor launch plumbing and
parser support. It changes no generated behavior and is not speed proof.

`docs/research/20260526-061637-guest-stack-arg-handoff-capture.md` FullDeploys
and captures that audit on Thor. The route was clean, but the stack-specific
subset is too narrow and blocked for behavior: `stack_arg_store_fields=87`,
`estimated_avoidable_bytes=1360`, `unresolved_direct_targets=52`,
`normal_entry_fallback=67`, `stackpoint_sensitive=67`,
`dirty_flush_points=268`, and `flush_context_barrier=260`. Do not patch
guest-stack argument handoff behavior or run a quiet speed A/B from this lane.
Reopen only inside a broader guarded-stub / late-bound-entry design that handles
normal entry, global indirection, stackpoint/debug/exception visibility, dirty
flushes, and unresolved targets. Prefer fastmem/addressing or A64 load/store
codegen-floor counters for hot `LOAD_OFFSET` / `STORE_OFFSET` forms in
`82281D28`.

`docs/research/20260526-063600-82281d28-load-store-offset-audit.md` adds
`tools/thor/thor_hir_load_store_offset_audit.ps1` and runs it against the latest
route-clean `82281D28` log. The wall is scalar guest-stack offset traffic:
`offset_ops=365`, `load_offset_ops=253`, `store_offset_ops=112`,
`body_weighted_offset_ops=450860314`, `body_weighted_guest_stack_ops=411865334`,
and `body_weighted_context_gpr_ops=38994980`. The dominant block remains
`82281D28:8228233C-82282370`, led by `stw r11,0x64(r1)` and
`ld r5-r10,0x170-0x198(r1)`. Do not patch behavior or run a quiet speed A/B
from this offline audit. Next useful work is a source-level A64 memory-lowering
feasibility audit for `ComputeMemoryAddress`, `AddGuestMemoryOffset`, and
`OPCODE_LOAD_OFFSET` / `OPCODE_STORE_OFFSET`, especially `r1 + small constant`.
Any candidate must preserve 32-bit guest address wrap, membase, byte-swap,
MMIO/exception visibility, and fallback behavior. If no reusable legal lowering
exists, close fastmem/addressing for the current `82281D28` lane.

`docs/research/20260526-070000-a64-memory-lowering-feasibility.md` adds that
source audit and `tools/thor/thor_a64_memory_lowering_feasibility.ps1`. It
finds the fastmem/addressing lane is still viable as a constrained backend
helper: model A64 on x64 `ComputeMemoryAddressOffset`, keep `x0` as the final
32-bit guest address, apply membase only after guest address math, preserve
byte swap and store-watch behavior, and leave MMIO / inline-MMIO paths on the
existing safe path unless equivalence is proven. Do not use host pointer plus
load/store immediate addressing without a no-wrap proof. Next useful work is a
default-off offset-aware helper prototype or source-tested codegen audit for
constant-offset normal `LOAD_OFFSET` / `STORE_OFFSET` paths, not a quiet speed
A/B.

`docs/research/20260526-071500-a64-offset-address-helper-prototype.md` adds the
default-off `arm64_offset_memory_address_fastpath` prototype. It introduces
`ComputeMemoryAddressOffset`, keeps `x0` as the final 32-bit guest address,
falls back for non-constant offsets and `allocation_granularity() > 0x1000`,
and wires Android/Thor launch metadata. `NativeCore`, `ApkShell`, parser checks,
and the updated feasibility audit pass. This is not speed proof. Next work is
FullDeploy plus a route-clean capture with
`-Arm64OffsetMemoryAddressFastpath true` and delayed body-time comparators
`82282490,82281D28,82287788`; no quiet speed A/B until that enabled route is
clean.

`docs/research/20260526-073440-a64-offset-address-fastpath-route-capture.md`
records that route-clean enabled capture on commit `1ed1dec66` / APK SHA
`3195B62E66ABCE289187F6A0C4A67E621AE2A32931BBD98B790CD32CB0E33AB9`.
It reached visible opening sky / dragon-wing with clean fatal markers and
shrunk generated code sizes in the wall cluster, but this is not speed proof.

`docs/research/20260526-075500-a64-offset-address-fastpath-quiet-ab.md`
records the same-APK ON/OFF/ON sandwich on commit `5ffe7d20b` with audit,
disassembly, block body-time, and call-edge tracing off. The authoritative
control deltas are `82282490 87168 -> 86452`, `82281D28 96908 -> 95724`, and
`82287788 35732 -> 35712`; both ON captures had lower final body ticks than
OFF. Main Thread still stayed near a full core, so this is local codegen/body
evidence, not sustained 30 FPS proof. Keep the cvar globally default-off, use
`-Arm64OffsetMemoryAddressFastpath true` explicitly in future Blue Dragon speed
captures, and do not repeat this unchanged A/B.

`docs/research/20260526-081300-82282490-82287788-codegen-floor-capture.md`
records the follow-up mapped OptHIR / block-body capture with the offset
fastpath enabled for `82282490` and `82287788`. It is route-clean but blocks an
immediate second addressing patch: the dominant `82282490:822825E0-822825F0`
block is scalar context/CR work with one offset load, the next `82282490`
weight is the already-closed stvewx / MUL_ADD_V128 / fpscr local shape, and
`82287788` has only `3` offset ops. Do not rerun this unchanged codegen-floor
capture. Continue fastmem/addressing only with a broader no-wrap /
normal-memory eligibility rule for hot `LOAD_OFFSET` / `STORE_OFFSET` route
blocks; otherwise close the lane.

`docs/research/20260526-083000-a64-no-wrap-memory-eligibility-audit.md` closes
the current fastmem/addressing lane. The joined route reports show material
offset traffic (`offset_ops_total=609`,
`body_weighted_offset_ops_total=585213030`) but zero no-wrap proof:
`runtime_no_wrap_counter_present=0`, `static_range_analysis_present=0`,
`static_no_wrap_provable_rows=0`, and `runtime_no_wrap_proven_rows=0`. Do not
implement host pointer + immediate addressing from guest-GPR bases without a
new range proof surface. Keep the existing offset helper for future Blue Dragon
captures, but move the sprint to a different structural A64 lane.

`docs/research/20260526-084500-a64-guarded-stub-entry-design.md` closes the
current guest-call fast-entry behavior lane. The source audit confirms there is
still only one normal `A64Function::machine_code` entry and one global
indirection target; normal entry receives only `x0=guest_return_address`;
direct calls branch to `fn->machine_code()`; and unresolved/indirection,
stackpoint, debug/exception, and host-call visibility all require normal-entry
fallback. It also confirms no separate fast-entry pointer/offset, no generic
`r3-r10/lr` payload ABI, and no behavior codegen path exists. Combined with the
prior runtime blockers (`unresolved_direct_targets=52`,
`normal_entry_fallback=67`, `stackpoint_sensitive=67`,
`dirty_flush_points=268`, `callee_first_use_known=0`), do not patch fast-entry
behavior. Reopen this lane only as an explicit source/data-model patch for
alternate-entry storage plus dirty-flush payload contracts, with generated
behavior still unchanged.

`docs/research/20260526-085500-a64-nonclosed-guest-state-cache-audit.md` keeps
the guest-state/cache lane alive without reopening closed `r1`/`r11` or
fast-entry slots. The strict non-closed ranking is `r31=237`, `r30=175`,
`r29=168`, `r28=152`, `r27=94`, and `r26=58` across the wall cluster. Source
checks still block behavior: same-block context promotion already exists, the
A64 emit-time cache resets per block, register allocation is per-block, and
host GPR pressure is real. Next valid slice is a default-off, counter-only
post-promotion non-closed GPR audit for `r31,r30,r29,r28,r27`; no store
elision, load replacement, or payload materialization until route-clean
counters prove material post-promotion volume with manageable flush/spill
pressure.

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
