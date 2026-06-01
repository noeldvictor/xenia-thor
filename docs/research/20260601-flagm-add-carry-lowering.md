# FEAT_FlagM ADD_CARRY lowering — design of record (2026-06-01)

From the design+adversarial-verify workflow (tools/thor/wf_flagm_lostodyssey.mjs, run
wf_25f8e5a9-d16, 5 agents). Goal: apply FEAT_FlagM (RMIF/SETF/CFINV) in the a64 JIT to speed
guest PPC carry arithmetic, targeting CPU-bound **Lost Odyssey** (irrelevant to GPU-bound Blue
Dragon, where the CPU is ~84% idle). Device disconnected ~38h → correctness PROVEN, not tested.

## Shipped (gated default-off, build-verified, hardware-UNVALIDATED)
`OPCODE_ADD_CARRY` (guest `adde/addc/subfe` carry chains), I32 + I64, non-constant-carry path
only, behind `arm64_flagm_fastpath` (default false) AND runtime `kA64EmitFlagM` detection:
```
and  x2, <carryX>, #1     ; defensive bit0 mask
rmif x2, #63, #2          ; PSTATE.C = x2[0]; N/Z/V untouched
adc  dest, src1, src2     ; dest = src1 + src2 + C
```
replacing the prior `mov/add/add(full carry)/mov`. Constant-carry, all-constant-fold, and
ADD_CARRY_I8/I16 paths are unchanged.

## Why this is bit-exact-equivalent (PROVEN, high confidence)
- **`rmif Xn,#63,#2` sets C = Xn[0] and nothing else.** Verified from the ARM machine-readable
  spec: shift is rotate-right; `flags[3:0] = ROR(Xn,sh)[3:0]`; mask bit3→N,2→Z,1→C,0→V; mask=2
  writes only C; with sh=63, C = ROR(Xn,63)[1] = Xn[0].
- **The carry-in is provably ∈ {0,1}.** `OPCODE_ADD_CARRY` src3 = `LoadCA()` = `PPCContext.xer_ca`;
  every writer is a `cset`/`AND`/`OR` boolean or `StoreXER &1` (ppc_emit_alu.cc:27-46,86,153,173,
  486-530,1176-1272; ppc_hir_builder.cc:494,497-508). `cset` emits exactly 0/1. So adding the full
  src3 (old path) == adding the C flag (new path).
- **Independent corroboration:** the x64 backend already lowers ADD_CARRY as `mov ah,src3; sahf;
  adc` (x64_sequences.cc:1242-1269) — sahf sets CF from AH bit0 ONLY. xenia has *always* defined
  the carry-in as bit0-only; if it could be >1, x64 and a64 would already disagree. They don't.
- The defensive `and x2,carry,#1` mask removes any reliance on that frontend invariant (costs ~1
  instr; safe against a future non-PPC producer).
- **RMIF safely clobbers PSTATE.C:** the HIR never carries host NZCV across sequence boundaries
  (conditions materialize to value regs via `cset`; branches use cbz/cbnz/tbz). Host NZCV is dead
  at every boundary.

## Detection wrinkle (important)
The vendored xbyak_aarch64 `hwCap_t` enum stops at JSCVT and never reads `HWCAP_FLAGM`, so the
`cpu_.has()` / `TEST_EMIT_FEATURE` pattern used for LSE **cannot** detect FlagM. Detection is a
self-contained `getauxval(AT_HWCAP) & HWCAP_FLAGM` probe (HWCAP_FLAGM = 1<<23) in
platform_arm64.cc, Linux/Android only, gated by `a64_extension_mask`. `kA64EmitFlagM = 1<<2` added
to `A64FeatureFlags`.

## Do NOT apply (equivalence not airtight — verifier-rejected)
- **SETF8/SETF16:** no matching site. `UpdateCR` emits explicit CompareSLT/SGT/EQ→cset into 3 INT8
  context bytes; never materializes host N/Z/V from a narrow result. SETF leaves C unchanged + sets
  V as narrow signed-overflow — matches nothing here.
- **CFINV:** no host-flag subtract-borrow site. Borrow is closed-form boolean IR (Or(CompareUGT,
  IsFalse) / Or(CompareULT,CompareULT)), never a host subs+carry chain.
- **RMIF for general CR/XER bit extract/insert:** CR is 4 INT8 context bytes manipulated by integer
  shift/mask on memory-backed context, not host PSTATE.
- **Constant src1/src2/carry; ADD_CARRY_I8/I16:** keep existing paths.
- **Default-ON / relying on compiled default:** the device's persisted xenia.config.toml overrides
  compiled defaults; enable only via `--ez arm64_flagm_fastpath true` for the A/B.

## Honest caveats
- **UNPROFILED + UNVALIDATED.** No Lost Odyssey CPU profile shows `adde/addc/subfe` is hot, and
  the device is gone ~38h so functional behavior is untested (the build only proves it compiles).
  The win is a few host instructions per carry op — it may be entirely in the noise. The device A/B
  decides.
- A wrong HWCAP constant that falsely enables on a non-FlagM core makes `rmif` SIGILL — SM8550 /
  Cortex-X3 does implement FEAT_FlagM, so detection should succeed on the real device.

## Device A/B on reconnect (Lost Odyssey — CPU is the lever, NOT the GPU)
After the safety gate (temp<60C / busy-low; tools/thor/thor_evidence.ps1):
1. **Correctness gate FIRST** (the only thing that matters — this is untested): launch Lost Odyssey
   at matched guest uptime, run A = default (cvar off, bit-identical current path), run B =
   `--ez arm64_flagm_fastpath true`. Because the lowering is PROVEN bit-exact, ANY A/B divergence
   (or a crash → rmif SIGILL = false-positive detection) is a hard STOP + revert. Confirm FlagM
   actually detected (else B==A, harmless no-op).
2. **Only if correct, measure win** on content-matched frames with a CPU/JIT-side proxy (NOT GPU
   frame_us — irrelevant here). Do not claim a win not measured.

Source: wf_25f8e5a9-d16 (full output in the task temp file). All file:line verified in-repo.
