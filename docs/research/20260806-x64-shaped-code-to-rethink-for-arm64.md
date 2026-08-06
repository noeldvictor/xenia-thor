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

## 2c. `UpdateCR` re-compares the same operands three times ⭐⭐⭐ very broad

`ppc_hir_builder.cc:500 UpdateCR` models a PPC condition-register update as
three INDEPENDENT HIR comparisons of the SAME operand pair:

```cpp
Value* lt = CompareSLT(lhs, rhs);   StoreContext(cr0 + 4*n + 0, lt);
Value* gt = CompareSGT(lhs, rhs);   StoreContext(cr0 + 4*n + 1, gt);
Value* eq = CompareEQ (lhs, rhs);   StoreContext(cr0 + 4*n + 2, eq);
```

Each lowers independently in the a64 backend to `cmp` + `cset`
(`a64_sequences.cc`, the compare macros), so one CR update emits:

    cmp, cset, str,  cmp, cset, str,  cmp, cset, str     = 3 CMPs

**ARM64 needs one.** A single `CMP` sets N/Z/C/V, and `CSET` reads the flags
without disturbing them, so the whole thing is `cmp` + 3×`cset` + 3 stores. That
is **two redundant CMPs on every CR update** — and CR updates fire on every
`cmpw`/`cmpwi` and on every instruction with `Rc=1`, which is a large fraction of
compiled PPC code.

CSE cannot help: the three ops have DIFFERENT opcodes (`COMPARE_SLT` /
`COMPARE_SGT` / `COMPARE_EQ`), so they are not common subexpressions even though
they share operands and the flags they need are identical.

**⭐ Unlike `EOR3` and `rlwinm`, this does NOT need a HIR pass.** The redundant
instruction is emitted by the *later* sequence, not the earlier one, so the
backend can simply not emit it. Implementation is emitter-local state:

- remember `(src1, src2, width)` of the last emitted `cmp`;
- a compare sequence whose operands match, with no flag-clobbering instruction
  since, emits **only** the `cset`;
- invalidate the record on ANY instruction that writes NZCV, at a label/branch
  target, and at block boundaries.

The invalidation set is the whole risk and must be conservative — get it wrong
and a stale flag silently produces a wrong comparison, which is a correctness
bug, not a perf regression. Gate it behind a cvar and validate with the qemu-a64
differential. `cset` and `str` do not touch flags, which is precisely why the
`UpdateCR` sequence is safe and profitable.

Note the existing CR cvars (`arm64_cr_compare_branch_across_context_barrier`,
`arm64_cr_store_elide_for_fused_branch`) address compare/branch FUSION, not
redundant-compare elision — this is a different and complementary win.

## 3. `mov`+`cmp` for constants — ✅ DONE 2026-08-06

Was 8 sites of `mov scratch, #K; cmp rn, scratch`. Now `A64Emitter::EmitCmpImm32`
picks `CMP #imm`, `CMP #x,LSL#12`, or `CMN` (compare-against-negative:
`0xFFFFFFFF` → `CMN #1`). Removes an arithmetic-port op *and* the serial
dependency; frees a scratch register in the four `RtlEnterCriticalSection`
fastpaths. Flag-exact, so safe under `LO`/`GE` and not just `EQ`/`NE`.

## 3b. Guest OFFSET loads/stores: x86 folds base+index+disp, ARM64 cannot ⭐⭐ very hot

PPC `lwz r3, disp(r4)` and its store/byte/half siblings are among the most
common instructions in compiled PPC. x86 lowers the whole guest access in ONE
addressing mode - `mov eax, [membase + reg + disp]` - so the x64 backend never
had to think about the displacement at all. **AArch64 has no base+index+immediate
form**: you get `[base, Xm]`, `[base, Wm, UXTW]` or `[base, #imm]`, so the
displacement MUST be folded into a register add first. Which add you emit, and
whether you copy the guest register on the way, is then a real decision the x64
backend never faced.

Today `ComputeOffsetMemoryAddress` (`a64_seq_memory.cc:104`) takes the naive
two-step by default: `ComputeMemoryAddress` copies the guest register into the
`w0` scratch, then `AddGuestMemoryOffset` adds the displacement to it.

    mov w0, w_r4          <-- pure copy, only needed because the next step
    add w0, w0, #disp         assumes it is working in the scratch
    ldr dest, [membase, x0]

`ComputeMemoryAddressOffset` (`a64_seq_util.h:385`) folds it properly, adding
straight out of the source register:

    add w0, w_r4, #disp
    ldr dest, [membase, x0]

It also picks the right encoding for the displacement (`#imm12`, `#imm12,LSL#12`,
or a materialised constant) instead of always materialising.

**⚠️ It is behind `arm64_offset_memory_address_fastpath`, which is compiled
DEFAULT-OFF.** So the naive form is what actually ships. One instruction per
offset access does not sound like much until you count how many guest
loads/stores carry a displacement — this is a per-access cost on one of the
hottest instruction classes there is. **Needs a gameplay A/B (not attract), then
default-on if it holds.** Note it only folds 4KB-granularity mappings and falls
back to the two-step where large-page compensation is required, which is correct.

## 3c. The inline MMIO range check is two full-width compares ⭐ latent

`LOAD_*`/`STORE_*` under `emit_inline_mmio_checks` test the MMIO window with two
32-bit compares, each needing its constant materialised first:

    mov w0, 0x7FC00000 ; cmp w17, w0 ; b.lo normal
    mov w0, 0x7FFFFFFF ; cmp w17, w0 ; b.hi normal

That is the x86 shape, where a 32-bit immediate is free inside `cmp`. **The
window `[0x7FC00000, 0x7FFFFFFF]` is exactly `addr >> 22 == 0x1FF`**, so ARM64
does the whole test in three instructions with an encodable immediate:

    lsr w0, w17, #22
    cmp w0, #0x1FF        // 511, fits imm12
    b.ne normal_access

**Latent, not live:** `emit_inline_mmio_checks` is default-off, so this costs
nothing today. Worth fixing because the cheap form may be what makes the feature
viable at all — the alternative for possible-MMIO instructions is a native call
per access.

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
