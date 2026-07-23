# XenonRecomp/ReXGlue optimization map → xenia (faster + lower power)

Deep dive into XenonRecomp's codegen/optimization surface (README + source study),
mapped to xenia's current state, with the concrete implementation + expected
speed/power impact for each lever. Power angle: every lever here reduces
EXECUTED host instructions and/or memory traffic = lower CPU busy time = lower
wattage/heat on the Thor. Source: github.com/hedge-dev/XenonRecomp.

## 1. Register save/restore helper inlining — XenonRecomp's #1 lever

XenonRecomp: "the local variable optimization introduces the MOST improvements,
as the calls to the register restore/save functions can be COMPLETELY REMOVED,
and the redundant stores to the PPC context struct can be eliminated"
(~20 MB smaller binary, several ms/frame in Unleashed Recompiled).

- **xenia today:** detects `__savegprlr/__restgprlr/__savefpr/...` by byte
  signature (xex_module.cc FindSaveRest) and tags them kProlog/kEpilogReturn,
  BUT the call path (ppc_emit_control.cc InstrEmit_branch) inlines only
  `kDefault` leaves + `kExtern` thunks - so `bl __savegprlr_N` (kProlog) still
  emits a real `f.Call()` = a per-non-leaf-function-invocation call + separate
  dispatch of the hot save helper.
- **The lever (verified feasible):** the save helpers are straight-line store
  leaves ending in `blr` (no branches/mtlr), so they PASS ScanInlineLeafCandidate
  (<=64 insts). Extend the inline condition to also inline
  `behavior()==kProlog` (and the tail `kEpilogReturn`) via the existing
  `EmitInlineLeaf` - correct-by-construction (emits the helper's exact store body
  + skips its blr; LR/r1 preserved identically to the call). Gate behind a new
  cvar (e.g. cpu_inline_saverest, default off).
- **Impact:** eliminates the save-helper call/return + dispatch per non-leaf call
  = fewer executed ops = faster + lower power. Under AOT-primary the code-bloat is
  compile-time (amortized); with register promotion (#2) the inlined stores fold
  to near-nothing.
- **⚠️ Coordination:** ppc_emit_control.cc is under active inlining development
  (arm64_jit_inline_leaf "Unit 0/1", "past-the-wall lever") - land this WITH that
  work, not against it.

## 2. Register promotion to locals (residency) — the enabler for #1

XenonRecomp promotes ctr/xer/cr/reserved/non-argument/non-volatile regs from the
context struct to C++ locals (TOML flags ctr_as_local, cr_as_local,
non_volatile_as_local, skip_lr, ...), so Clang keeps them in host registers and
the redundant context stores vanish.
- **xenia:** the LLVM backend's residency-writeback (committed, memory
  [[llvm-jit-backend-build]]) is xenia's version - keeps guest regs resident
  WITHIN a function via mem2reg/GVN. Gap: no CROSS-function residency (the
  WriteBackCtxRegs/ReloadCtxRegs round-trip at every call, the residency-trap;
  the return-trampoline is the fix, Android+device). skip_lr (drop LR maintenance,
  assume no exceptions) is a xenia-applicable micro-lever but exception-risky.

## 3. Indirect-dispatch: perfect-hash / direct-index (Tiaozhuan)

XenonRecomp resolves virtual/fn-ptr calls by dereferencing at `guest_addr*2`
(the recompiled-fn pointer table placed just past guest memory) - O(1),
collision-free, ~1-2 host instructions. Tiaozhuan (TACO 2025) Full Address
Mapping is the same idea (vs ~10 insts for hash-table DBT).
- **xenia:** LLVM guest-call path uses an 8192-slot self-validating resolve-cache
  (hash + validate). Lever: replace with a full direct-indexed
  guest->host-fn table = fewer instructions per indirect call = faster+lower power
  on the hot dispatch. Android-side (LLVM backend) + device-validated.

## 4. Static function discovery (the residue) — DONE in xenia

XenonRecomp: jump-table detection (mtctr;bctr + TOML) + manual `functions`
boundaries + `invalid_instructions` skips. xenia now has, committed this session:
- jump-table scan (1729b7bf3), vtable/pointer-table scan (fa9e447b2),
  drain-frontier + robust exhaustion (703a11606), cpu_aot_maximize (64454050b).
- Result: BD AOT coverage ~92% -> ~97.5%, residue -67%. See
  [[aot-coverage-measured]]. This is the largest committed speed/smoothness (and,
  via fewer runtime JIT-compile spikes, lower peak-power) win of the session.

## 5. VMX/FPU efficiency

XenonRecomp: 16-byte full vector reversal (not per-element swaps) with reversed
component orders; per-instruction denormal flush (VMX flushes, FPU preserves);
volatile memory ops to prevent unsafe reordering.
- **xenia:** VMX NEON helpers already do denorm-flush + positional-NaN + the
  fault-decodable 32-bit vector-mem trick (memory [[llvm-jit-backend-build]]).
  The 16-byte-reversal-with-reversed-lane-order idea could cut per-vector-op swap
  instructions - audit vs xenia's current byteswap-per-lane approach.

## 6. Misc XenonRecomp techniques worth stealing

- **setjmp/longjmp native** using the Xbox 360's abundant vector registers to hold
  guest CPU state alongside host state - relevant if xenia's guest longjmp path is
  a fallback/slow.
- **Mid-asm hooks** (weakly-linked aliasing + per-instruction hooks) = the
  game-patch/HLE injection mechanism, already paralleled by xenia's .patch.toml +
  D3D9-HLE signature trampolines.

## Priority for FASTER + LOWER POWER (given the constraints)

1. #4 static discovery — DONE + committed (biggest landed win).
2. #1 save/restore inlining — highest remaining per-call win; land WITH the active
   inline work (ppc_emit_control.cc), correct-by-construction, gated.
3. #3 direct-index dispatch — the hot-indirect-call win; Android+device.
4. #2 cross-function residency (return-trampoline) — Android+device.
All of #1-#4 reduce executed instructions/memory traffic = the lower-power axis.
