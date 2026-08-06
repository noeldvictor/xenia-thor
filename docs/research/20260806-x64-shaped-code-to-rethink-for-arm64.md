# x64-shaped code in the a64 backend that needs rethinking for ARM64 (2026-08-06)

The a64 backend was derived from the x64 backend, so it inherits x86's
**two-operand destructive** instruction model and its **fixed-register**
conventions. AArch64 is three-operand and non-destructive almost everywhere.
Every place we transliterated instead of re-designing costs a real instruction,
and those instructions land on the **arithmetic ports** — the scarce resource on
this SoC (A715/A710: three 128-bit load ports vs two arithmetic).

This is the "where can we actually take advantage of the hardware" audit. Each
item is a verified code location, not a suggestion.

---

## 1. Variable shifts emit 3 instructions where ARM64 needs 1 ⭐⭐ biggest, 12 sites

`a64_sequences.cc`, the `SHL/SHR/SHA` × `I8/I16/I32/I64` family — grep the
comment `"Read shift amount first"` (**12 occurrences**). The non-constant path is:

```cpp
// Read shift amount first — dest may alias src2.
e.mov(e.w0, i.src2);                       // 1: count -> scratch
if (dest != src1) e.mov(i.dest, i.src1);   // 2: src1 -> dest
e.lsl(i.dest, i.dest, e.w0);               // 3: destructive-style shift
```

**Why it looks like this:** x86 `shl` is destructive *and* requires the count in
`cl`. Both `mov`s are forced by x86, not by the algorithm.

**ARM64 reality:** `LSLV`/`LSRV`/`ASRV` are fully three-operand
(`lslv Wd, Wn, Wm`), and `xbyak_aarch64` exposes them as
`lsl/lsr/asr(WReg,WReg,WReg)` (mnemonic_def.h:1060-1080). Both sources are read
before the destination is written, so **the aliasing worry the comment describes
cannot occur** — that hazard is an x86 artifact too.

```cpp
e.lsl(i.dest, i.src1, i.src2);   // one instruction, no scratch, no alias hazard
```

**3 → 1 instructions, and it frees `w0`.** Shifts are pervasive in PPC guest code
(`rlwinm`, `srawi`, `slw`, and every bitfield extract lowers through them), so
this is the broadest single win identified so far. Constant-shift paths already
use the immediate form and are fine — only the variable path is affected.

## 2. Every VMX float op copies both operands to scratch — for nothing ⭐⭐ ~10 sites

`a64_seq_util.h:473` `PrepareVmxFpSources`, called from ~10 VMX FP sequences
(add/sub/mul/max/min/…):

```cpp
// Copy to scratch v0/v1 so we don't modify live allocated registers.
if (s1 != 0) e.mov(VReg(0).b16, VReg(s1).b16);
if (s2 != 1) e.mov(VReg(1).b16, VReg(s2).b16);
if (!e.IsFeatureEnabled(kA64FZFlushesInputs)) {   // <-- the only destructive part
  FlushDenormals_V128(e, 0);
  FlushDenormals_V128(e, 1);
}
```

The copies exist **only** to protect live registers from the destructive
`FlushDenormals_V128`. **On the Thor `kA64FZFlushesInputs` is detected and set**
(the hardware flushes denormal inputs, confirmed by the startup probe in
`platform_arm64.cc`), so the flush is skipped — and the copies then guard against
code that never runs. NEON's `fadd v2, vA, vB` does not touch `vA`/`vB`, so the
op can read the allocated registers directly.

**2 vector MOVs per VMX float op, pure waste on this device.**

⚠️ **Not a blind edit.** Some callers hardcode `v0`/`v1` afterwards —
`FixupVmxMaxMinNan` (`a64_seq_util.h:459`) documents *"Expects: v0=flushed src1,
v1=flushed src2"*, and `a64_sequences.cc:5433` even comments `// s1=v0, s2=v1`.
The correct shape is to return the **original** registers when no flush is
needed, and fix the few callers that assume the scratch pair (or give them a
separate helper). Sequence-by-sequence, with the qemu-a64 differential.

## 2b. `rlwinm` is 3 instructions where ARM64 has a single `UBFM` ⭐⭐⭐ deepest one

**The HIR models PPC rotate-and-mask as rotate + AND because x86 has no
rotate-and-mask instruction.** ARM64 does — `UBFM` *is* rotate-and-mask, and
`rlwinm` is arguably the most common non-trivial PPC instruction there is.

`ppc_emit_alu.cc:InstrEmit_rlwinmx` already has real work behind it: three
fastpath cvars (`ppc_rlwinm_shift_fastpath`, `_mask_fastpath`,
`_general_fastpath`) with genuine correctness analysis, including an
adversarially-verified counterexample for wrapping masks
(`rlwinm rA,rS,4,28,3`). The general non-wrapping case lowers to:

    ROR (32-bit) + AND (mask) + UXTW      = 3 instructions

`rlwinm rA,rS,SH,MB,ME` computes `ROTL32(rS,SH) & MASK(MB,ME)`. For a
**non-wrapping** mask (`MB<=ME`) the selected bits are a contiguous run of the
rotated source, which is exactly what `UBFM Wd,Wn,immr,imms` produces — the
`UBFX`/`UBFIZ`/`LSL`/`LSR` aliases are all just `UBFM`. So the entire instruction
is **one** ARM64 op, and `SH`/`MB`/`ME` are compile-time constants, so the
encoding can be computed at translation time with no runtime cost.

**3 → 1 on one of the hottest PPC instructions**, and unlike most items here it
is not a micro-cleanup: it removes two arithmetic-port ops per `rlwinm`.

**How to build it.** Do NOT try this as an a64 sequence peephole — same trap as
`EOR3`: by the time the `AND` sequence runs, the `ROTATE_LEFT` is already emitted
and register-allocated. The clean shape is a **new HIR opcode** carrying
`(value, rotate, mask)` with constant rotate/mask, emitted directly by
`InstrEmit_rlwinmx` (which already has `sh`/`mb`/`me` in hand):
- **a64** lowers it to one `UBFM` when the mask is contiguous and non-wrapping;
- **x64/LLVM** lower it exactly as today (rotate + and), so nothing regresses;
- wrapping masks (`MB>ME`) keep falling through to the generic path, preserving
  the documented counterexample behaviour.

**Measure applicability first** (the `EOR3` lesson): count how often the general
non-wrapping path is taken versus the existing slwi/srwi/clrlwi fastpaths. If
those already catch nearly everything, the remaining slice is small — that is a
cheap counter, not a guess.

## 3. `mov`+`cmp` for constants — ✅ DONE 2026-08-06

Was 8 sites of `mov scratch, #K; cmp rn, scratch`. Now `A64Emitter::EmitCmpImm32`
picks `CMP #imm`, `CMP #x,LSL#12`, or `CMN` (compare-against-negative:
`0xFFFFFFFF` → `CMN #1`). Removes an arithmetic-port op *and* the serial
dependency; frees a scratch register in the four `RtlEnterCriticalSection`
fastpaths. Flag-exact, so safe under `LO`/`GE` and not just `EQ`/`NE`.

## 4. Constant materialisation: `MOVZ`+`MOVK` chains vs a literal load

`mov(XReg, uint64_t)` is a pseudo-op expanding to `MOVZ` + up to three `MOVK`,
**all on the scarce arithmetic ports and each dependent on the last**. x86 encodes
a 64-bit immediate directly in the instruction, so the x64 backend never had to
think about this. A PC-relative literal load is one instruction on an abundant
port. Rule to implement: literal-load when the chain would be ≥3 instructions.
Needs a literal pool near the code. See the hardware-exploitation map, item 2.

## 5. Byte-swapping: no `MOVBE`, so it is a real arithmetic-port tax

x86 has `MOVBE` (load/store with byteswap, free). **AArch64 has no equivalent** —
every big-endian guest access costs an explicit `REV`, on the arithmetic ports.
65 `rev` sites across the emitter and memory sequences. Cannot be removed
instruction-for-instruction; the tractable angle is **eliding provably-redundant
swaps** (equality and bitwise comparisons are endian-agnostic, so any
`rev` → compare → `rev` round trip is deletable). Biggest single line item, still
unmeasured, deserves its own audit.

## 6. Fixed-scratch-register discipline is an x86 habit

`SrcVReg(e, op, 0)` / `SrcVReg(e, op, 1)` and the pervasive use of `w0`/`x0`,
`v0`/`v1` as mandatory staging registers mirror x86's fixed-register constraints
(`cl` for shifts, `rax:rdx` for mul/div, `xmm0` for blends). AArch64 has 31 GPRs
and 32 vector registers with no such constraints. Items 1 and 2 are both
instances of this; the general fix is to let sequences consume allocated
registers directly and reserve scratch for genuine spills.

---

## Ordering

1. **#1 variable shifts** — 12 sites, mechanical, 3→1, biggest breadth. Do first.
2. **#2 VMX FP operand copies** — ~10 sites, needs per-caller care.
3. **#5 byteswap elision audit** — likely the largest total, needs analysis.
4. **#4 constant literal-loads** — broad, genuinely uncertain, measure.

Everything measures under the CLAUDE.md protocol: one session, cvar-gated arms,
equal thermal starts, uncapped, `entry_delta` as the metric — run-to-run drift
here is ~2.8% and will otherwise swamp the result. Correctness for #1/#2 goes
through the qemu-a64 differential, which is device-free.
