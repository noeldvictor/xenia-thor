# The RPCS3-on-ARM video, mined EXHAUSTIVELY — item-by-item verdicts against our tree

**Source:** Whatcookie, *"PS3 emulation is fast on ARM now"*, 60m30s, re-fetched 2026-08-09 via
`yt-dlp --write-auto-subs --sub-format json3` (1,018 KB of captions → 287 timestamped blocks).

**Why re-mine when CLAUDE.md already quotes the video:** prior passes mined the opening and the core-topology
section and extracted ~5 items. The talk contains **far more than that**, and it explicitly says so —
*"we're going to review how every single one of them was fixed"*. This pass reads all 60 minutes and gives every
claim a verdict. Their headline, **theirs not ours**: 60% faster at 25% less power on an Odin 2 (same SoC as the
Thor).

---

## 🥇 THE FINDING THAT MATTERS MOST: **THEY SOLVED THE EXACT TBL2 WALL WE WROTE OFF AS IMPOSSIBLE**

Our `llvm_assembler.cc:2471` comment, verbatim:

> *"Emit TWO single-table TBL1s OR'd, NOT one TBL2. DEVICE-CONFIRMED: the `aarch64.neon.tbl2` intrinsic needs its
> two tables in a CONSECUTIVE register pair; with x20/x21 reserved + high register pressure the AArch64 backend
> can't satisfy that and CRASHES in the AsmPrinter"*

The video, at 17:49–18:56, describing the same thing for SPU `SHFB`:

> *"using TBX or TBL with two input vectors requires both input vectors to be adjacent in the registers, this
> places a lot of constraints on the register allocator. And with how LLVM is configured in RPCS3, it just seems
> to give up at a certain point and crash… So I was left with two choices. Give up on the optimization or just
> enable it for games where it doesn't break… I settled on the world's stupidest solution. **Just catch the
> crash when LLVM fails to compile a block with two source TBX or TBL. Then retry it with a single source TBX
> and TBL.** It's an absolutely awful solution, but hey, when **10,000 recompiled blocks compile successfully
> with the two source versions and only three blocks need the fallback method**, we can keep our 8% speed up and
> keep all of our compatibility."*

**Same wall. They kept the fast path behind a per-function fallback; we took the slow path GLOBALLY.**

**What it costs us today, already priced in CLAUDE.md from the A710 SWOG:** a64 emits a real two-table `tbl` =
**1 µOP**; our LLVM path emits **2×TBL1 + OR = 3 µOPs** on the FP/ASIMD pipe, which is only **2 wide** on the
mid cores — and **LLVM is the shipping default**, so that is the path most VPERMs take. Their measured win on
the analogous instruction was **8%** (SPU-heavy; ours will differ — VPERM frequency here is uncensused).

**⚠️ BUT THE PORT IS NOT A DROP-IN, AND THE DIFFERENCE IS THE WHOLE RISK — DO NOT SKIP THIS PARAGRAPH.**
Their failure is a **clean diagnostic** ("fails to compile a block … crash with this message") which is
catchable. **Ours is a WILD-POINTER RE-FAULT STORM INSIDE `libLLVM.so`'s AsmPrinter** that freezes Blue Dragon —
a segfault, not a thrown error. **You cannot `try`/`catch` a wild pointer.** So "just catch it and retry" as
stated does not port.

**Viable routes, in increasing order of work:**
1. **Find out WHY ours faults where theirs diagnoses.** Ours reserves x20/x21 (`+reserve-x20,+reserve-x21`);
   theirs does not reserve a guest-context register in the same way. If the fault is the allocator running out
   of assignable consecutive pairs *because of our reservations*, that is a configuration difference, not a law.
   **Cheapest first step and it needs no device: build one function with TBL2 and `cpu_llvm_dump_asm`, and see
   whether the failure is a diagnostic or a fault on THIS LLVM version.**
2. **Pressure-gate it:** emit TBL2 only when the function's live-vector count is below a threshold, 2×TBL1
   otherwise. Keeps the fast path for the many, avoids the allocator cliff for the few.
3. **Compile-and-verify:** lower with TBL2 into a scratch module, and if codegen fails *detectably*, re-lower
   the function with the TBL1 form — their design, but gated on a detectable failure rather than a fault.

**⇒ This is the single most concrete "apply the video" item in the tree, and it is squarely on the user's
"take advantage of the hardware" ask: a 3→1 µOP reduction on the scarcest pipe of the mid cores.**

---

## The full item list, with verdicts

| # | video item | what it is | verdict for us |
|---|---|---|---|
| 1 | **`busy_wait` timer frequency** (02:44) | 3,000 ticks assumed a 2-4 GHz timer; ARM timer is **19 MHz** → **150× over-wait**. Fix = scale by timer frequency. **+25% perf, −10% power** | ✅ **N/A — WE ARE CLEAN.** `clock_posix.cc:28` reads `cntfrq_el0` at runtime; the emitter reads CNTVCT+CNTFRQ. No hardcoded rate. Verified twice |
| 2 | **`yield` is a NOP** (07:19) | `yield` only yields under SMT; ~99% of consumer ARM is SMP, so it does nothing. Use `ISB` | ✅ already known and implemented (`SpinLoopHint()`); `a64_spin_hint_isb` measured CONFOUNDED, no win |
| 3 | **`SHFB` → TBX2** (13:34-18:56) | 10 → 5 → 4 insns; **8% uplift**; register-pair crash solved by catch-and-retry | 🥇 **THE TOP ITEM — see above.** We hit the identical wall and gave up |
| 4 | **`TBX` over `TBL`** (15:56) | TBX leaves out-of-range lanes untouched instead of zeroing → drops the final OR | 🔍 **UNCHECKED.** Our 2×TBL1 form ends in an OR precisely because TBL1 zeroes. **TBX1 may remove that OR even without TBL2** — a 3→2 µOP win with NO register-pair constraint. **Cheapest untested idea in this document** |
| 5 | **`BCAX` fuses AND+XOR** (16:33) | one SHA3 instruction replaces two bitwise ops; LLVM only pattern-matches it when inputs are non-constant | ⚠️ partially known (`+sha3` is on so LLVM fuses opportunistically). The **constant-input blind spot is new** and is an upstream LLVM bug they filed |
| 6 | **`FCGT` → inline asm `BSL`** (25:26) | LLVM "completely dropped the ball"; only inline assembly fixed it. **15 → 7 insns** | 🔍 **UNCHECKED** — do we have a compare/select chain that fails to become one BSL? |
| 7 | **`FSM` scalarized** (26:29) | LLVM emitted `SBFX` **one bit at a time** and scalarized the whole vector op. Fix = write the IR idiomatically like the x86 path → **2 insns via `CMTST`** (fused and+compare) | 🔍 **UNCHECKED, and this is a CLASS not an instruction.** Worth grepping our IR for anything LLVM might scalarize on ARM |
| 8 | **Shift poison values** (28:48) | LLVM IR shifts produce **poison**, and the guard code does not fold away on ARM (it does on x86). Fix = use the `USHL` intrinsic. **2 insns saved per shift** | ❌ **N/A — CHECKED IN EMITTED ASM 2026-08-09, see below.** I flagged this as the highest-breadth item and I was wrong |
| 9 | **`UDOT` for `SUMB`/gather-bits** (21:02) | dot product with multiplicand 1 sums horizontally; with **powers of two** it gathers bits; negative powers exploit compare results being −1 | ❌ recorded as N/A — no hot fixed-length site here |
| 10 | **`UMMLA`/`SMMLA` (i8mm)** (22:55) | 2-instruction GBB/GBH | ❌ same, N/A |
| 11 | **Widening multiplies** (24:10) | `XTN` + `SMLAL`: MPY 5 → 3 insns | 🔍 unchecked; PPC VMX has its own multiply forms |
| 12 | **MUL-accumulate for COMPARISON** (34:46-35:47) | **Novel.** Mid cores do **3 loads but only 2 vector ops/clock**; multiplying two compare results (−1×−1=1) and accumulating reaches 0.75 math/load. **+22% mid, +16% A510** | ❌ N/A — needs a hot fixed-length compare; ours are GPU-side or load-time |
| 13 | **`ABD`/`ABA` accumulate for checksum** (37:43) | 2 loads + 1 abs-diff-accumulate. **+38% mid cores, +21% X3** | ❌ N/A, same reason. (CLAUDE.md calls this "the real differentiator"; the honest verdict is we have no site) |
| 14 | **Re-roll unrolled loops** (38:33) | less code-cache pressure. **~2% on BOTH arches** | 🔍 **UNCHECKED and relevant** — we AOT-compile ~28k functions into a 264 MB object cache; code-cache pressure is plausibly real here |
| 15 | **SVE** (39:51+) | predicate registers, etc. | ❌ **HARD N/A** — 8 Gen 2 ships ARMv9 **without SVE**. Confirmed in `/proc/cpuinfo` |
| 16 | **Load/vector asymmetry** (32:02) | mid cores: **3× 128-bit loads but only 2 vector ops per clock**; X3: 3 loads / 4 vector ops. *"unusual … for any other CPU I've ever seen"* | ✅ already in the playbook, confirmed against Table 2-1 |
| 17 | **A510 vector units** (31:12) | two A510s **share one 128-bit** vector unit; the third has **exclusive access to a 64-bit** unit, so 128-bit ops run at half speed | ⚠️ CLAUDE.md marks our probe "REFUTED" — but the probe could not distinguish *shared* from *half-width*. Moot for us: guest threads are off the A510s entirely |

---

## Two framing points from the talk worth keeping

**1. "Just put the square in the square hole."** (20:37) Verbatim:
> *"I can't count the number of times I read on Reddit or YouTube that RPCS3 needs a complete rewrite, or a brand
> new emulator needs to be born to properly take advantage of ARM hardware. And it's like, no dude. We just got
> to put the square in the square hole."*

Their 60% came from **dozens of small, specific instruction-level fixes** — not an architectural rewrite. That is
the opposite of where our effort has gone (residency, module scope, allocator theory), and it is worth weighing:
**every single item in their list is an individual instruction lowering.**

**2. Modern ARM is not simpler than x86.** (19:08) *"ARM is every bit as complex as x86… The manual alone is
17,000 pages."* Useful antidote to "ARM is RISC so the port should be simple".

---

## ⚠️ The honest caveat on transferring ANY of this

RPCS3's hot path is the **SPU recompiler** — a 128-bit-SIMD DSP with 128 registers, where a single instruction
lowering repeated billions of times dominates. **Our hot path is different**: Burnout is capped, Gears is stalled
on a kernel bug, and BD's field is CPU-bound but spread across many functions. So **the SHAPE transfers (fix the
instruction lowerings, do not rewrite) but the MAGNITUDES do not** — an 8% SHFB win does not imply an 8% VPERM
win here, and rule 4 (count before building) still applies to every row above.

---

## Execution log

### ✅ Item 4 — SHIPPED (`cpu_llvm_vperm_tbx`, default off)
`tbl1 + tbl1 + ORR` → `tbl1 + TBX1`. **3 µOPs → 2** on the 2-wide mid-core vector pipe, on the shipping backend.
Stays on the single-table intrinsic, so it carries none of the tbl2 register-pair risk.
**Proven device-free:** `tools/qemu/vperm_tbx_vs_tbl_or.c`, **8/8 PASS** including an exhaustive 32-index ×
16-lane sweep (512 cases), bit-identical to both the old form and a C reference. Default off pending a pixel
check, allowlisted so it can be A/B'd.

### ❌ Item 8 — N/A, AND MY PRIORITISATION WAS WRONG
I called this "possibly the highest-breadth unchecked item" on the strength of us emitting zero `ushl`
intrinsics. **Diffing the emitted asm killed it in one command** — the repo rule that says do that first exists
for exactly this, and this is the fifth time it has paid.
**Why their bug cannot occur here: THE IR SHAPES DIFFER.** Their SPU shift masks **6 bits**, which *permits*
counts ≥ lane width, so the IR needs an out-of-range guard — and that guard is what fails to fold on ARM. **Ours
masks to `(w-1)`** (`llvm_assembler.cc:2017`), so the count is in range by construction and **no guard is ever
generated.** NDK 25 clang 14, `-O2 -march=armv8.2-a+lse -mtune=cortex-a710`:
```
variable amount:  movi + and + ushl                (left)     <- optimal
                  movi + and + neg + ushl          (right)    <- the neg is unavoidable; the video says so too
constant amount:  shl v0.4s, v0.4s, #5             ONE insn
                  ushr / sshr  #5                  ONE insn
```
**No poison guard, no scalarization, and the constant case — the common PPC one — folds to a single
instruction.** There is nothing to win.

### 🔬 Item 3 (TBL2) — INVESTIGATED 2026-08-09, and the picture changed. **The catch-and-retry design is probably portable after all.**

I set out to answer "why does ours fault where theirs diagnoses". Four checkable facts, none needing the device:

**1. ❌ Our x20/x21 reservation is NOT the cause.** The comment blames *"x20/x21 reserved + high register
pressure"*. Built a deliberately hostile case — 8 live table registers, 4 `vqtbl2q_u8` uses in one loop, values
live across it — with NDK clang, **with and without `-ffixed-x20 -ffixed-x21`. Both compiled clean.** The
reservation alone does not break tbl2.

**2. ⚠️ But that test used the WRONG LLVM, and the gap is 6 major versions.** NDK 25 is **clang 14**; the JIT
links **`third_party/llvm-android/lib/libLLVM.so`, which is LLVM 20.1.8**. So (1) is suggestive, not decisive —
the AArch64 backend changed enormously between them.

**3. ✅ The crash is NOT a stale-library artifact — it is live in what we ship.** `libLLVM.so` is dated
**2026-06-26**; the workaround commit `dc5c6cf77` ("LLVM: FIX the storm — OPCODE_PERMUTE tbl2 -> two tbl1") is
**2026-06-27**. Same binary, never rebuilt since. (Note it is **untracked/gitignored**, so `git log` says nothing
about it — the mtime is the only record.)

**4. 🔑 THE FINDING: our libLLVM CONTAINS the clean register-allocation diagnostic.** Grepped the stripped .so
directly (`strings` returns nothing on it — use `grep -a`):
```
"ran out of registers"  : 1      <- the RA failure path EXISTS
"LLVM ERROR"            : 2
"UNREACHABLE executed"  : 1
```
**So LLVM 20.1.8 as we build it CAN report this failure cleanly rather than corrupting memory** — which is
exactly the failure mode Whatcookie catches and retries.

**⇒ THE TWO STORIES NO LONGER MATCH, AND THAT IS THE USEFUL PART.** Our recorded symptom is a *wild-pointer
re-fault storm inside the AsmPrinter*. But the clean `report_fatal_error("ran out of registers…")` path is
present in the same library. Either:
- **(a)** the 2026-06-27 diagnosis **mis-attributed** the storm — tbl2 was correlated with it, not the cause; or
- **(b)** there are **two distinct failures**, and only one is the allocator.

**⇒ THE EXPERIMENT THAT DECIDES IT, with a clear decision rule.** Re-enable the tbl2 lowering behind a cvar,
install an **`llvm::install_fatal_error_handler`** before lowering, and run ONE Blue Dragon boot:
- **Handler fires with "ran out of registers"** → the failure is clean and catchable. **Port their design**:
  handler sets a flag, re-lower that one function with the TBL1 form. Their ratio was 3 fallbacks per 10,000
  blocks, so we would keep the 1-µOP form nearly everywhere.
- **Still a wild-pointer fault** → it is not the allocator, the comment's attribution is wrong, and the real bug
  is elsewhere in IR→asm. That is also worth knowing, because the same storm is blamed for BD instability.

`report_fatal_error` is not a C++ exception and cannot be `try`/`catch`'d — **`install_fatal_error_handler` is
the supported interception point** and is why this is portable at all. Note the handler must not return; the
retry has to be driven from a longjmp/flag at the lowering call site, not from inside the handler.

**This is now a bounded one-launch experiment with a binary outcome, not an open question.**

### ❌ Item 3 (TBL2) — PROBE RUN 2026-08-09. **The retry design does NOT port. Question closed.**
Built the probe (`cpu_llvm_vperm_tbl2_probe` + an `install_fatal_error_handler` routing LLVM's message to
XELOGE, since `report_fatal_error` writes to stderr and stderr is not in logcat on Android) and ran it on a cold
free device with `cpu_llvm_object_cache=false` to force real lowering.

**Result — the negative branch, unambiguously:**
```
last line   : LLVMbegin guest=0x82168540      <- died MID-LOWERING, ~function 1,731
LLVMfatal   : 0                                <- the handler NEVER fired
crash buffer: empty; no tombstone, no SIGSEGV, no SIGABRT
logcat      : "ActivityManager: Process ... has died: fg TOP"
```
**The process vanishes during codegen without LLVM reporting anything.** It is NOT a clean
`report_fatal_error`, so there is nothing for a handler to catch, and **upstream's catch-the-failure-and-retry
design cannot be ported** — it depends on a diagnostic we do not get.

**⇒ TWO CONCLUSIONS, and the second is the more valuable one:**
1. **tbl2 stays disabled.** `cpu_llvm_vperm_tbx` (tbl1+TBX1, 3 µOPs → 2) is the achievable win here, and it is
   already shipped and stability-validated. The 1-µOP two-table form is out of reach on this toolchain.
2. **The original "wild-pointer re-fault storm" characterisation is CORROBORATED, not refuted** — despite our
   libLLVM containing the "ran out of registers" string, that path is not what executes. So this is memory
   corruption inside codegen, not an allocator giving up cleanly. **That matters beyond VPERM: the same storm is
   blamed for Blue Dragon instability**, and it is now a known-live corruption bug in IR→asm rather than a
   suspected one.

**⚠️ One honest loose end:** a silent death with no tombstone is also consistent with an OOM kill. Against that:
it died only ~1,731 functions into lowering, while other `object_cache=false` runs the same day lowered ~18,000
functions over ~4 minutes without trouble. Not conclusive, but it points away from resource exhaustion.

### Remaining, in priority order
2. **Item 7 (LLVM scalarizing a vector op on ARM)** — a bug CLASS, not one instruction. Worth grepping our
   emitted asm for `SBFX`/lane-at-a-time patterns in vector lowerings.
3. **Item 6 (`FCGT` → one `BSL`)** — do we have a compare/select chain that fails to fuse?
4. **Item 14 (re-roll unrolled codegen, ~2% both arches)** — plausible for us given ~28k AOT functions / 264 MB
   object cache.
