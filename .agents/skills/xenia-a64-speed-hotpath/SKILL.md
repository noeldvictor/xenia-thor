---
name: xenia-a64-speed-hotpath
description: Use for Xenia Thor ARM64 performance work on AYN Thor Max, especially Blue Dragon 2 FPS runs, A64 speed-profile counters, hot guest PCs, PPC helper/prologue churn, guest-to-host transitions, direct/indirect call overhead, XMA/audio cost, and Snapdragon 8 Gen 2 profiling. AYN Thor ARM64 is the priority; Windows is only a control lane when it answers a specific semantics question.
---

# Xenia A64 Speed Hotpath

Use this skill when Blue Dragon gets past boot/title/opening but runs too slow,
or when the next question is "CPU or GPU?" on Thor. This is the speed lane for
the imported aX360e/Edge-style A64 backend, not the removed helper mini-JIT.

## Priority

- AYN Thor Max ARM64 truth comes first.
- Windows/x64 is a control microscope only when a suspected emulator semantics
  bug needs a faster comparison.
- Do not spend a speed run on broad GPU traces, shader dumps, disassembly dumps,
  live logcat, or RenderDoc unless the experiment explicitly targets that
  subsystem.
- Keep trace-heavy correctness captures separate from trace-off speed captures.

## Baseline Run

Build and deploy only when the native core changed:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Run the canonical profiled speed capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "45,90" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1
```

Then summarize the latest evidence:

```powershell
rg -n "A64 speed profile|A64 thread snapshot|last_global_owner_sys_tid|ResolvePath\\(cache|__savegprlr|__restgprlr|8246B408|8248B040|827294CC|8272A3A4|Fatal signal|AndroidRuntime|VK_ERROR_DEVICE_LOST|GPU is hung|XMA|top_threads" scratch\thor-debug
```

`tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` reapplies
the debug app-op for `MANAGE_EXTERNAL_STORAGE` after install. If a run shows
`funcs=0` forever, verify `cmd appops get jp.xenia.emulator.github.debug
MANAGE_EXTERNAL_STORAGE` before chasing A64.

The Thor debug launcher defaults `mount_cache=true` for Blue Dragon. Keep it on
for correctness runs; `MountCache false` can keep the game busier but leaves
`ResolvePath(cache:\pack) failed - device not found` noise in the route.

## Title Proof Lane

Use this lane to answer only "does Blue Dragon reach the visible title screen on
Thor?" It avoids the speed lane's auto START/A input and forced Vulkan signed
10:10:10:2 fallback. Default title capture is final-screenshot only; pass
`-TitleScreenshotSeconds "30,35,40,45"` only when the experiment needs timed
screenshots because extra screencaps can perturb this route.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonTitleCapture -DeviceSerial c3ca0370 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshot true -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpFastpath true -Arm64BlueDragonJumpTableFastpath true
```

Known title proof: `scratch\thor-debug\20260520-011006-*` shows `press START`
with APK SHA `EA375B75215C12AC84EB5E121C7FC0AC191B189EAD9BBB97F333CC49B2372EF7`.
This is still timing-sensitive; same-cvar captures can black-idle when snapshot
sampling is disabled. Treat that as a route-determinism bug, not as
compatibility.

## Thread Snapshot Lane

Use the thread snapshot flag when the screen is black or the route's final
interval disagrees with `top -H`. It logs each guest thread's last A64 function,
guest return address, LR/CTR/R1/R3/R4, and retries briefly before giving up on
the processor debug lock. Skip lines include the last observed global critical
owner system TID and PPC global-lock owner breadcrumbs.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 50 -PerfSampleSeconds "45" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 10 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshot true
```

For timing-sensitive black-idle repros, prefer the idle-only snapshot first. It
does not sample during the boot/title transition; it logs one thread snapshot
only after counters have been active and a later interval goes flat.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonTitleCapture -DeviceSerial c3ca0370 -TitleScreenshotSeconds "0" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpFastpath true -Arm64BlueDragonJumpTableFastpath true
```

## Body-Time Lane

Use body-time counters when entry counts are misleading after helper fastpaths.
This is especially useful for Blue Dragon where `827294CC`, `826C5620`, and
`826BF770` can stay high-frequency even when each call is cheap.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -PerfSampleSeconds "70" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4,8272A8E8,826C5620,827294CC,826BF770"
```

## Block Profiler Lane

Use block counters only after body time identifies one concrete function. The
filter is exact-start scoped for single addresses, so `8272A3A4` should only
instrument the function whose start address is `8272A3A4`.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 45 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4" -Arm64SpeedProfileBlockFilter "8272A3A4"
```

Treat block-profiler runs as trace-heavy diagnostics. Harvest the first useful
interval, then return to a clean speed capture before judging progress. The
first `8272A3A4` run found hot guest block PCs `8272A8B4`, `8272AA50`,
`8272A3F4`, `8272A474`, `8272A548`, and `8272A424`.

Do not reintroduce a partial `8272A3A4:8272A8B4` byte-copy splice without a new
full-region proof. The 2026-05-19 attempt either crashed at `8272A8B4` or
black-idled the route after resuming at `8272A8D0`; see
`docs/research/20260519-234533-blue-dragon-copy-fastpath-dead-end.md`.

Do not reintroduce wrapped-immediate `ADD`/`SUB` lowering by default without a
lowering audit first. The 2026-05-20 broad I32/I64 pass
`scratch/thor-debug/20260520-143752-*` and the narrower `ADD_I32`-only pass
`scratch/thor-debug/20260520-144309-*` both black-idled Blue Dragon; restored
default donor lowering `scratch/thor-debug/20260520-144638-*` resumed healthy
entry deltas. See
`docs/research/20260520-144829-a64-wrapped-addi-bisect.md`.

Exception: the audited `ADD_I64 reg, reg, wrapped-small-negative` case is now
route-proven and default-on. Capture `scratch\thor-debug\20260520-150652-*`
confirmed `arm64_add_i64_wrapped_imm_fastpath=true`, active Blue Dragon
counters through 60 seconds, and `8272A3A4 code_size=12772`. Roll it back with
`-Arm64AddI64WrappedImmFastpath false` if a future route regresses. Do not
extend this exception to `ADD_I32`, `SUB_I32`, or `SUB_I64` without a separate
audit proof. See
`docs/research/20260520-151030-a64-add-i64-wrapped-immediate-fastpath.md`.

The 2026-05-20 I64 logical-immediate pass is route-proven and should stay in
the generic A64 backend: `AND_I64`, `AND_NOT_I64` with constant second operand,
`OR_I64`, and `XOR_I64` now emit direct A64 logical immediates when masks are
encodable and keep the old scratch-register fallback otherwise. Capture
`scratch\thor-debug\20260520-152048-*` stayed active through 60 seconds and
shrunk `8272A3A4` to `code_size=12652`. See
`docs/research/20260520-152237-a64-i64-logical-immediate-lowering.md`.

The follow-up `AND_NOT_I8/I16/I32 reg, reg, const` cleanup is route-clean but
not a measured Blue Dragon hot-block win. Capture
`scratch\thor-debug\20260520-152601-*` stayed active through 60 seconds, while
`8272A3A4` and `8272A8E8` remained at `code_size=12652` and `5600`. Keep it as
generic codegen cleanup; use an immediate-lowering hit audit before broadening
more shapes. See
`docs/research/20260520-152733-a64-and-not-logical-immediate-lowering.md`.

Use the logical immediate audit before chasing more constant-materialization
work in a hot function:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 45 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64ImmediateLoweringAudit true -Arm64ImmediateLoweringAuditFunction 0x8272A3A4 -Arm64ImmediateLoweringAuditBudget 256
```

Capture `scratch\thor-debug\20260520-154135-*` proved `8272A3A4` has no
remaining logical-immediate `mov+reg` misses in the audited shapes: 38
`AND_I8` rows and 30 `AND_I64` rows were all `logical-imm`. The final interval
idled with audit logging active, so use this lane for translation evidence, not
speed comparison. For high guest address filters, pass hex; the Thor script
converts to signed Android `--ei` extras. See
`docs/research/20260520-154650-a64-immediate-lowering-audit.md`.

Global audit `scratch\thor-debug\20260520-154815-*` found 44 `OR_I32 imm 0`
fallbacks in the first 256 logical-immediate rows. Those now lower through the
zero fastpath as `action identity`; `AND reg, 0` also lowers as `action zero`.
Post-change audit `scratch\thor-debug\20260520-155154-*` left only two
first-budget `mov+reg` rows, both `AND_I64` masks (`0x6001007D` and `0xEF`).
Quiet proof `scratch\thor-debug\20260520-155321-*` stayed route-clean through
60 seconds. See
`docs/research/20260520-155700-a64-zero-logical-immediate-fastpath.md`.

The 2026-05-20 integer zero-select pass is a small generic cleanup for the
Blue Dragon shift/select pattern in `8272A3A4`: `SELECT_I8/I16/I32/I64` now
uses `wzr` / `xzr` directly when either value operand is constant zero. Repeat
proof `scratch\thor-debug\20260520-160757-*` stayed active through 70 seconds
and shrank `8272A3A4` from `12652` to `12540` bytes and `8272A8E8` from `5600`
to `5592` bytes. The earlier `scratch\thor-debug\20260520-160530-*` attempt
idled later, so keep treating Blue Dragon speed routes as timing-sensitive.
See `docs/research/20260520-161130-a64-zero-select-fastpath.md`.

The 2026-05-20 unsigned zero-compare pass folds integer compares that are
forced by an unsigned zero bound, such as `COMPARE_ULT x, 0 -> false` and
`COMPARE_UGE x, 0 -> true`. Proof
`scratch\thor-debug\20260520-161344-*` stayed active through 70 seconds and
shrunk `8272A3A4` from `12540` to `12432` bytes and `8272A8E8` from `5592` to
`5552` bytes. See
`docs/research/20260520-161650-a64-unsigned-zero-compare-fold.md`.

The 2026-05-20 compare-to-context-store peephole is the next proven CR churn
cleanup. A zero-store-only build `scratch\thor-debug\20260520-162727-*` reached
the Blue Dragon Voice Language menu but did not shrink the hot functions. The
actual win was fusing always-false unsigned zero compares whose only use is the
immediately following `store_context`; proof
`scratch\thor-debug\20260520-163134-*` reached the same menu with no searched
fatal markers and shrank `8272A3A4` from `12432` to `12332` bytes and
`8272A8E8` from `5552` to `5520`. See
`docs/research/20260520-163450-a64-compare-store-context-peephole.md`.

Do not re-add the uncommitted `COMPARE_UGT x, 0` plus `COMPARE_EQ x, 0`
context-store pair peephole without an audit counter first. The local
`scratch\thor-debug\20260520-163945-*` proof reached the Voice Language menu,
but clean hot function sizes stayed at `8272A3A4=12332` and `8272A8E8=5520`.
See `docs/research/20260520-164105-a64-ugt-eq-context-pair-nohit.md`.

The proven successor is the 2026-05-20 UGT/EQ CR branch peephole. It only
matches when `COMPARE_UGT` and same-operand `COMPARE_EQ` store to adjacent PPC
CR `GT`/`EQ` bytes and the EQ result is used only by its store plus an optional
immediate branch. It emits one `cmp`, two `cset`/`strb` pairs, and direct
`b.eq` / `b.ne`. Proof `scratch\thor-debug\20260520-170433-*` had no searched
fatal markers and shrank clean code size from `8272A3A4=12332` to `12296` and
`8272A8E8=5520` to `5508`. Keep it, but do not broaden it to arbitrary compare
pairs without a new exact-offset/use audit. See
`docs/research/20260520-170621-a64-ugt-eq-cr-branch-peephole.md`.

Do not re-add the broad integer compare-branch fusion tried in
`scratch\thor-debug\20260520-171056-*`. It matched `COMPARE_*` followed by a
single-use branch and lowered it as `cmp` plus `b.cond`, but Blue Dragon
black-idled after the early burst with `entry_delta=0` in the final intervals.
The local code was reverted before commit. See
`docs/research/20260520-171256-a64-compare-branch-black-idle.md`.

The 2026-05-20 Edge-style global reservation helper import is default-off. It
adds `TryAcquireReservationHelper`, `ReservedStore32Helper`, and
`ReservedStore64Helper`, plus `-Arm64GlobalReservationHelpers true`, but the
default-on experiment `scratch\thor-debug\20260520-172518-*` black-idled after
the early burst. Keep it as opt-in PPC sync infrastructure, not a Blue Dragon
speed fix. See
`docs/research/20260520-173242-edge-reservation-helper-import.md`.

## External PPC To A64 Reference Lane

Use this lane when the work feels like isolated peepholes instead of backend
maturity. The 2026-05-20 source harvest says the useful public references are:

- Dolphin `JitArm64`: best direct PPC-to-AArch64 JIT structure. Study its
  pinned PPC state/membase registers, GPR/FPR/CR register cache, branch/CTR/LR
  lowering, block linking, and last-use-driven flushes. Do not copy GPL code
  bodies into this fork without a deliberate license decision.
- QEMU TCG: best mature DBT architecture reference for translation blocks,
  direct block chaining, `lookup_and_goto_ptr`, helper global read/write
  metadata, PPC CR globals, and reservation/atomic semantics. Use as a design
  and semantics oracle, not as embedded code.
- RPCS3 PPU/LLVM/AArch64: useful second-backbone reference for function/module
  analysis, local/global PPU state separation, LLVM lowering, AArch64 transform
  passes, and leaf-block handling. Treat LLVM as a parallel research route
  after the direct A64 hot path is instrumented.
- IBM PowerPC branch docs: sanity check for CR/LR/CTR and BO/BI branch
  semantics before broadening compare/branch fusions.

For Blue Dragon, translate this into a state-traffic sprint:

1. First add an audit for `8272A3A4` that counts context loads/stores, CR
   loads/stores, LR/CTR traffic, helper calls, direct exits, indirect exits,
   endian swaps, and dispatcher returns.
2. Then add a Dolphin-style PPC state cache at the HIR/A64 boundary, starting
   with CR bytes and common hot GPR context slots.
3. Flush only at helpers, exits, exceptions, and exact aliasing barriers.
4. Use QEMU-style helper classification to keep state live across helpers that
   are proven not to mutate guest context.
5. Only use LLVM/RPCS3 ideas for a larger hot-function comparison after the
   direct A64 audit gives us the concrete waste map.

See `docs/research/20260520-180132-powerpc-to-arm64-source-harvest.md`.

## Classification

Read the final speed-profile interval first.

- If `__savegprlr_*` or `__restgprlr_*` dominate, prove whether they are guest
  ABI helper leaf routines. Candidate work is guest helper inlining, direct
  link cleanup, or cheaper call/return lowering.
- If direct guest calls roughly track function entries, inspect direct block
  chaining and guest function boundary overhead before GPU work.
- If indirect calls are high, inspect indirect branch lookup, hash/cache shape,
  and branch target metadata.
- If guest-to-host or extern calls are high, inspect HLE, kernel waits, XMA,
  file/device I/O, and MMIO paths.
- If resolves or resolve misses are high, inspect function lookup, code-cache
  registration, and invalidation.
- If `A64 speed profile body top` disagrees with entry-count top rows, trust
  body time for the next optimization target. Entry count means "called often";
  body time means "burned measured generated-code cycles."
- If thread samples show XMA or audio ahead of the guest CPU, use
  `-XmaFastSilence true` only as an A/B cost probe, not as a fix.
- If GPU/composer threads are not hot and the screen is merely slow, stay in
  CPU/A64 until evidence changes.

## Files To Inspect

- `src/xenia/cpu/backend/a64/`
- `src/xenia/cpu/backend/x64/`
- `src/xenia/cpu/hir/`
- `src/xenia/cpu/ppc/`
- `src/xenia/kernel/xboxkrnl/`
- `src/xenia/apu/`
- `tools/thor/thor_xenia_debug.ps1`
- `docs/research/20260519-153016-xbox360-thormax-translation-report.md`
- `docs/research/20260519-162000-a64-speed-profile-counters.md`

## Static Lane

Use Ghidra only after a runtime profile gives a concrete guest PC, helper name,
or guest wait loop. The static question should fit in one sentence, such as:

- "What does guest function `8246B408` wait on?"
- "Are `__savegprlr_29` and `__restgprlr_29` compiler ABI helpers we can
  inline or shortcut safely?"
- "Which caller loop creates this direct-call storm?"

Use `$xenia-ghidra-android-debug` for setup and keep extracted guest content
out of git.

## Acceptance

A speed patch is not a win until the notes include:

- before and after capture directories from the same scene/route;
- commit hash and APK SHA-256;
- profiler cvars and logging mode;
- final A64 counter interval;
- final A64 body-time interval when the body filter was enabled;
- thread sample summary;
- screenshot path or route status;
- whether the run crashed, hung, or changed visible progress;
- next hotpath if the current patch helped only part of the wall.
