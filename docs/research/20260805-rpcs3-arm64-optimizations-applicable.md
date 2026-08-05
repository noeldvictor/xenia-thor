# RPCS3's ARM64 work — what transfers to xenia-thor (2026-08-05)

Source: "PS3 emulation is fast on ARM now", Whatcookie (RPCS3 dev), plus the
merged PRs behind it in RPCS3/rpcs3. Transcript mined locally (skill:
`video-transcript-mining`); every item below was cross-checked against the
actual PR diff, not the video alone.

## Why this transfers directly, not by analogy

**The device in that video is an AYN Odin 2 — a Snapdragon 8 Gen 2. That is the
same SoC as our Thor**, same four-core-type layout (Cortex-X3 / A715 / A710 /
A510). Their measurements are on our silicon.

And the workload is closer than it looks: the PS3's PPU and the 360's Xenon are
both PowerPC with AltiVec/VMX. Several of these are literally the same guest
instruction (VPERM, VMAXFP, VMINFP).

**Claimed, by them, unverified by us: ~60% faster at ~75% the power, over ~6
months.** Do not restate that as ours. Everything below needs a Thor A/B before
any number is claimed here.

Device feature check (`/proc/cpuinfo`, 2026-08-05): `asimddp i8mm bf16 fphp
asimdhp atomics lrcpc ilrcpc sha3`. **No SVE/SVE2** — Qualcomm shipped 8 Gen 2
as ARMv9 without it, so RPCS3's SVE2 PRs (XAR, SVE multiplies, SVE2 FMS) do
NOT apply to us. `sha3` does, and that matters (see #5).

---

## 1. `yield` is a no-op — use `ISB` for spin-wait backoff  ⭐ HIGH

RPCS3 found **half of all CPU time** sitting in a four-line `busy_wait`. On
essentially every modern ARM core `yield` does nothing at all; the effective
x86-`pause` equivalent is `ISB` (instruction synchronization barrier). It costs
slightly more power than an ideal stall (it restarts instruction fetch) but it
is a real backoff, and it still measured as a net power win in their loop.
(RPCS3 PR 18151; also 18055, scaling the spin count by the ARM timer frequency
rather than an x86-derived constant, and 18830 for a hardware spin helper.)

**Ours:** we have exactly this shape. `A64Backend` logs
`clock-spin-yield disabled (stride=64, sleep_us=0, window_us=50)` at startup,
and CLAUDE.md already records that *"Burnout's Main thread busy-waits on the GPU
ring read-pointer"* — which is why the Burnout profile raises the CP thread
priority (`gpu_cp_worker_nice=-15`) to shorten that bubble. A spin that does
nothing on ARM is the worst case for that bubble.

**Action:** audit every spin/backoff for `yield`/`__yield()`/`YieldProcessor`
and swap in `ISB`; re-derive spin counts from `CNTFRQ_EL0` (~19.2 MHz on
Qualcomm) instead of x86-tuned constants.

## 2. `vmaxfp` / `vminfp` → ARM `fmax` / `fmin`  ⭐ HIGH, tiny

ARM's `fmax`/`fmin` NaN semantics **match AltiVec exactly**. x86 needs a
compare+select dance to emulate it; on ARM that is one instruction. RPCS3's fix
(PR 18060) is 8 lines behind an `#ifdef ARCH_ARM64`.

**Ours:** `OPCODE_VECTOR_MAX` / `OPCODE_VECTOR_MIN` in
`src/xenia/cpu/backend/llvm/llvm_assembler.cc:1642`. Xenon has the same VMX
instructions. Check whether we carry x86-shaped NaN workaround code there and
put the ARM path behind the same kind of guard.

## 3. Feature detection that silently excludes Qualcomm  ⭐ HIGH, cheap

RPCS3 was gating FMA on the CPU *name* containing "cortex" — so every Qualcomm
and Apple core fell off the fast path despite FMA being **baseline** on ARM64.
PR 18133 replaced the guesswork by detecting features properly and passing them
to LLVM as target attributes, so LLVM stops generating conservative code.

**Ours — two places to check:**
- Our LLVM backend: are we passing real target features (`+dotprod`, `+i8mm`,
  `+fp16`, `+sha3`) for the Thor? If not, LLVM is emitting generic AArch64.
- `arm64_fma_v128_fastpath` is **default-off** in our tree. If it is off because
  of a detection problem rather than a correctness problem, this is free
  performance. Worth re-testing on device.

## 4. Mid-core port asymmetry: prefer loads over arithmetic  ⭐ NOVEL

The A715/A710 mid-cores have **three 128-bit load ports but only two 128-bit
arithmetic ports**. So on the mid cores, a materialized constant load can be
cheaper than computing the value — the opposite of the usual advice. Whatcookie
believes this is a novel optimization and notes it should apply broadly to
Android SoCs (the same imbalance appears across the A7xx line).

**Ours:** directly relevant to the a64 backend's constant materialization and to
any VMX sequence that synthesizes masks with arithmetic. Our guest threads are
spread across exactly these cores. This is the item most likely to be a real
edge over XenDroid/Edge, because it is not in anyone else's tree.

## 5. Three-input bitwise instructions (`EOR3`, `BCAX`) — we have them

The `sha3` feature on our device brings `EOR3`, `BCAX`, `RAX1`, `XAR`. These fuse
three-input bitwise operations that would otherwise be two instructions, and
they are usable well outside cryptography. RPCS3 walks through which ones do and
do not fit (`BSL`/`BIF` lack an XOR, `EOR3` is three chained XORs, `BCAX` was
missing what they needed for one case).

**Ours:** a64 backend VMX bitwise sequences. Cheap to try, gated per-opcode.

## 6. `VPERM` → native `TBL` — we already do this, and our workaround is vindicated

RPCS3 PR 18056 replaced emulated x86 `pshufb` with AArch64 `TBL1`/`TBL2` for
VPERM/SHUFB (9 → 5 instructions for SHUFB).

**Ours: already done.** The a64 backend emits `tbl` directly
(`a64_seq_vector.cc`), and the LLVM backend emits **two `TBL1`s OR'd together**
rather than one `TBL2` — with a device-confirmed comment explaining that the
`aarch64.neon.tbl2` intrinsic needs a *consecutive register pair*, which the
backend cannot satisfy under our register pressure (x20/x21 reserved), crashing
in the AsmPrinter.

Worth recording: **RPCS3 hit the same wall independently** ("Cannot scavenge
register without an emergency spill slot") and solved it differently — compile
with `TBL2` enabled, and on that specific LLVM error, recompile the block with
`TBL2` disabled. Our OR-of-two-TBL1s avoids the retry entirely. Ours is likely
the better trade; theirs gets the 4-instruction form when registers allow.

## 7. `UDOT`/`SDOT` for byte-sum patterns

`asimddp` is present. RPCS3 used `UDOT` with a multiplicand of 1 as a
sum-of-bytes, replacing x86 `VPDPBUSD`, and for gather-bit style operations.

**Ours:** applies wherever we reduce bytes across a vector. Lower priority than
1–4 but free correctness-wise (it is an exact integer reduction).
⚠️ Note our standing rule: dot-product/i8mm are for **heuristics only, never
guest FP32 geometry** — that black-screened before. This use is integer, so it
is inside the rule, but keep it there.

## 8. Inline the timer read into recompiled code

RPCS3 already inlined guest timer reads on x86; enabling it for ARM was a
one-line change (PR 18516 area). Reading `CNTVCT_EL0` inline beats calling out
to a host function per read.

**Ours:** `Clock::QueryGuestTickCount` and friends are called from guest code
paths; titles that poll the clock hard (Burnout's main thread does) pay this per
call.

---

## Suggested order

1. **ISB spin-wait** (#1) — biggest single measured win for them, and we have a
   known busy-wait bubble in the exact shape they describe.
2. **fmax/fmin** (#2) — hours of work, exact-semantics, zero risk.
3. **LLVM target features / FMA gate** (#3) — cheap, possibly free perf.
4. **Mid-core load-vs-arithmetic** (#4) — the differentiator; needs real
   measurement on X3 vs A715 vs A510.
5. EOR3/BCAX (#5), UDOT (#7), inline timer (#8).

Every one of these needs a Thor A/B under the measurement discipline in
CLAUDE.md (single-run in-place A/B; cross-run fps is confounded), and a
`tools/exp_ledger.py` entry either way.
