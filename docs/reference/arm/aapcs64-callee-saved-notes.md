# AAPCS64 — the manual that governs cross-call register residency

**Source:** Arm IHI 0055, *Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)*.
Canonical text: <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>

**Why this file exists:** `docs/reference/arm/` had the Arm ARM and the four Cortex SWOGs, but **not the
procedure call standard** — and AAPCS64, not the SWOGs, is what decides which registers survive a guest call.
That is exactly the question `cpu_backend_llvm_residency_abi` (XenonRecomp `non_volatile_as_local`) turns on, so
the governing manual was missing for the single highest-value CPU lever in the tree. Fetched 2026-08-08.

## The two clauses that matter

**General-purpose registers — verbatim:**

> "Registers r19-r29 and SP are Callee-saved."

**SIMD/FP registers — verbatim, and this is the one that bites:**

> "Additionally, only the bottom 64 bits of each value stored in v8-v15 need to be `Callee-saved`; it is the
> responsibility of the caller to preserve larger values."

## What that means for this emulator

**Host registers actually available to hold guest state ACROSS a guest call:**

| | count | note |
|---|---|---|
| callee-saved GPRs (x19–x28) | 10 | |
| minus our reservations | **7** | x19 backend ctx, x20 guest ctx, x21 membase (`a64_emitter.h:77`; LLVM gets `+reserve-x20,+reserve-x21`) |
| callee-saved vector regs (v8–v15) | 8 | **but only the LOW 64 BITS survive a call** |
| callee-saved vector regs usable for a **128-bit** value | **0** | the upper half is caller-save by the clause above |

**Guest non-volatile state the ABI lever wants to keep resident:** GPR r14–r31 (**18**), FPR f14–f31 (**18**),
VMX v14–v31 **and** v64–v127 (**82**, the 360's VMX128 non-volatile set).

⇒ **Ceiling, from the manual, not from measurement:**
- **GPRs: at most 7 of 18** can stay resident across a call, and only if LLVM elects to spend every reservable
  callee-saved register on guest mirrors (it also needs them for its own long-lived values).
- **VMX: ZERO of 82.** A 128-bit guest vector **cannot** be held across a call in a host callee-saved register,
  because AArch64 does not have one that preserves 128 bits. LLVM must spill it around the call regardless.
- **FPRs: up to 8**, since a PPC FPR is 64-bit and fits the preserved half exactly — the one part that maps
  cleanly.

## The consequence to carry forward

The lever's help describes keeping "~18+ guest registers RESIDENT in host callee-saved registers ACROSS the
call". **For the GPR and FPR halves that is achievable but capped well below 18+; for the VMX half it is
architecturally impossible on AArch64.** Any expectation of a large speedup from the *vector* side of
`cpu_backend_llvm_residency_abi` is unfounded, and should not be used to justify the lever.

**This does not kill the lever** — 7 GPRs + 8 FPR halves held across calls is still real work removed from a
guest thread this tree has measured as memory-bound. It resizes the claim.

**⚠️ Not verified on our toolchain:** whether clang/LLVM actually assigns guest mirrors to x19–x28 under
`+reserve-x20,+reserve-x21`, or spills them anyway. That is an emitted-code question, answerable with
`clang -S` or by dumping the JIT'd IR — not by this manual.
