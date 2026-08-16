# What the outside world thinks makes a 360 emulator fast on ARM — and what we can actually do

Researched 2026-08-16. Companion to CLAUDE.md `SWEEP 2026-08-16`.

**Read the last section first if you want the action list.** The research exists to rank the actions.

---

## ⚠ SOURCE QUALITY FIRST, BECAUSE MOST OF IT IS BAD

Most 2026 "Xbox 360 on Android" content is SEO content-farm material that paraphrases itself
(heldgames, pocket-gaming, shattered.io, xenia-emulator.com — note the last is NOT the official
xenia.jp). It carries no measurements. It is weighted low here.

The credible sources are: box86.org's own benchmarks, the Mesa/Turnip docs, Beyond3D on Xenos,
the Xenia GitHub docs, and the ReXGlue interview at readonlymemo.

**🔑 NOBODY PUBLISHES HARD NUMBERS ON ARM64 360 EMULATION.** No instruction mix, no frame
breakdowns, no A/B tables. **Our own corpus and device measurements are better data than anything
online.** Do not treat this document as a source of truth about our bottlenecks. We measure those.

---

## IDEA 1 — "Drop the JIT, recompile statically" (the loudest idea of 2026)

**The claim.** XenonRecomp and ReXGlue lift PowerPC ahead of time into C++, compile it with Clang,
and ship a native binary. "There is no JIT. There is no instruction interpretation at runtime."
One write-up prices a typical dynamic translator at **~15-17 host instructions per guest
instruction** and says static recompilation deletes that tax, then lets the full Clang optimizer
run over whole functions.

**Why it looks aimed at us.** ReXGlue's showcase title **is Blue Dragon**, reportedly above
original-hardware framerates.

**❌ VERDICT: NOT A PIVOT. WE ARE ALREADY ON THIS ROAD.**
| their claim | our reality |
|---|---|
| JIT costs 15-17 host instructions per guest instruction | That prices a NAIVE JIT. **~80% of our guest entries already run through the LLVM AOT backend** — Clang-grade optimization, applied ahead of time |
| static recomp gets whole-function optimization | So does our AOT path. That is what it is |
| ReXGlue runs BD fast | It **borrows Xenia's GPU**, is per-game engineering, and is stated to be early-stage |
| — | **ReXGlue has published NOTHING about ARM64 or mobile.** The whole comparison is desktop |
**⇒ The real gap between us and ReXGlue is not JIT-vs-AOT. It is that they specialize per game and
drop the guest ABI. That is a different product, not a faster one.**

---

## IDEA 2 — The x86-on-ARM playbook (Box64, FEX, Rosetta 2)

**Their hardest problem does not exist for us.** x86 has Total Store Order; ARM is weakly ordered.
FEX's conservative TSO emulation is "computationally intensive". Rosetta 2 is fast partly because
Apple silicon has a **hardware TSO mode**, which Snapdragon does not have.

Measured, on the same machine: **Box64 reaches 57% of native, Rosetta 2 reaches 71%.** That ~14
point spread is roughly what hardware support plus AOT buys.

**🔑 POWERPC IS WEAKLY ORDERED, LIKE ARM. WE PAY NO TSO TAX AT ALL.** The single largest cost
center in x86-on-ARM emulation is absent from our problem. This is a structural advantage that
nobody writes down.
**⚠ The inverse is our bug class, and CLAUDE.md already says so: "TSO hides missing fences."
Weak-to-weak means we must place fences deliberately, because nothing hides a missing one.**

**What DOES transfer from Box64:**
- hardware floats instead of softfloat (we already do this)
- indirect-branch and call/ret specialization
- **native library substitution — do not emulate the library, call the host one.** This is the HLE
  argument. Our archived D3D9-HLE era was chasing exactly this.

---

## IDEA 3 — The GPU lineage argument. ❌❌ IT IS FALSE FOR BD, AND WE ALREADY MEASURED THAT.

**The fact is true. The conclusion people draw from it is wrong on this device.**

**The true part.** Xenos and Adreno are the same family. The ATI R400 line branched into the
**Imageon Z430, which became Qualcomm Adreno 200**. Both descend from one design.

**The seductive conclusion.** "Both are tilers with fast on-chip memory. The 360's 10 MB EDRAM and
Adreno's GMEM are the same idea, so the 360 render model is a native fit on Adreno, and the cost is
render pass breaks, because each break forces a GMEM store/restore."

**❌ WE MEASURED THIS ON THE REAL GAME AND IT IS NOT WHAT HAPPENS.** See `37005f22f`:
| measurement | result |
|---|---|
| per-pass trace, 248,055 passes | **248,033 go DIRECT. 22 bin.** |
| autotune decisions | 245,965 "Autotune selected sysmem", **2,068 "Can't fit attachments into gmem"** |
| `TU_DEBUG=gmem` A/B, one arm per cooldown | autotune 15.83 fps vs forced binning 11.15 fps — **binning is 42.0% SLOWER** |
**⇒ BD NEVER USES GMEM. There is no GMEM store/restore to eliminate, because there is no GMEM
traffic.** A pass break in direct mode costs a flush and re-setup, which is real but far cheaper.
`37005f22f` already retired the whole tile-oriented lever class on this evidence: GMEM residency,
tile load/store elision, `VK_QCOM_tile_memory_heap`, subpass merging and LRZ.

**🔑 THE LINEAGE FACT, APPLIED CORRECTLY, ARGUES THE OPPOSITE.** The shared ancestry is in the
SHADER CORE, not the memory architecture. The 360's EDRAM is a separate daughter die with hardware
resolve; Adreno's GMEM is tile memory driven by a binning pass. They are analogous in spirit and
different in size and shape. **Our oversized EDRAM-span surfaces are exactly what makes attachments
fail to fit GMEM — 2,068 passes said so out loud.** The 360 model pushes Adreno OUT of its tiler
fast path rather than fitting it.

**⚠ THIS IS WHY THE INDEX SAYS TO CHECK THE LEDGER FIRST.** The argument above is plausible, is
repeated widely, and is wrong here. It was written into this document before the measurement was
checked, and then corrected. **A general architectural argument does not outrank a device A/B on
the actual title.**

---

## IDEA 4 — Smaller confirmations

| claim | our status |
|---|---|
| Turnip is mandatory for playable performance, vs the Qualcomm blob | **Already known and written down.** Good to learn it is universal, not a local quirk |
| 16 KB pages (Android 15+) cut TLB walks, which suits large working sets | Real, but it breaks 4K assumptions. It is exactly the `allocation_granularity() > 0x1000` path whose branch we removed in `3390c9b62` |
| ARM has single-instruction `REV`, so byte swaps are cheap per access | True. **The win is not the instruction, it is deleting redundant swaps** — see the action list |
| Flagship Snapdragon + 8 GB RAM minimum | Matches the Thor |

---

## ✅ WHAT WE CAN ACTUALLY DO, RANKED

Ranked by (expected win x confidence) / effort. **Nothing here needs a pivot.**

### 🥇 A. A/B THE LEVERS WE ALREADY BUILT AND LEFT DEFAULT-OFF — needs ONLY a device
This is the best value per hour in the whole list and it needs **no new code**. The tree carries a
pile of ported, allowlisted, default-off levers that have **never been device-measured**:
`gpu_stall_spin_iterations` (XenDroid ships **32**, we default 500), `vulkan_mid_frame_submission_draws`
(XenDroid ships **1300**, we default 0), `vulkan_direct_host_resolve`, `vulkan_cache_sampler_parameters`,
`a64_spin_hint_isb`, `a64_vmx_nan_fixup_branchless`, `vulkan_fast_register_ranges`.
**⚠ Read `A DEFAULT-OFF PATH IS NOT A CONTROL` before starting.** Some off-branches are untested code.
**⇒ Free performance may already be sitting in the tree, switched off. Connect the Thor and find out.**

### 🥈 B. INLINE THE TRANSCENDENTALS IN THE LLVM BACKEND — buildable today, no device
**`llvm_assembler.cc:3211/3220/3227` emits FOUR host libm calls per `vexptefp`/`vlogefp`/`vrsqrtefp`**
(`EmitVecLaneCall`, one per lane). That is on the backend carrying **~80% of guest entries**. Each
call is a scheduling barrier that blocks vectorization and register residency around it.
Edge `fb225d975` replaces the math with a branchless polynomial plus a 2^-11 grid snap, which is
**closer to the hardware estimator than libm is** — the Xenon instructions are 12-bit estimates, not
exact functions.
**⇒ The clearest pure-speed item in the sweep. Validate with the corpus + a qemu differential.**

### 🥉 C. PORT EDGE'S FP NaN / FPSCR FAMILY — buildable today, measurable today
Closes roughly **24,000 of our 35,917 corpus failures**. Mostly correctness — but
`9900f7ceb` and `b2d6a4140` make the a64 NaN fixups **branchless on the hot FMA path**, and
`vmaddfp`/`vnmsubfp` are hot in real game code. Commit list is in CLAUDE.md `SWEEP 2026-08-16`.

### D. MEASURE REDUNDANT BYTE SWAPS — the one genuinely NEW idea from this research
Cheap to measure, unknown payoff, so **measure before building**. Every guest load and store carries
a swap. The question is how many are provably redundant (`SWAP(SWAP(x)) -> x`, a swap feeding an
operation that does not care, a swap on a value that never leaves guest memory).
**Step 1 is a census over a full BD AOT dump, not a pass.** If the count is small, drop it — this is
exactly the shape of the `ppc_cross_block_dead_gpr_elim` result, where 12,942 removals bought nothing.

### ❌ E. GPU PASS-BREAK REDUCTION — DEMOTED. Idea 3 does NOT support it.
This entry originally read "Idea 3 says it is the right target". **That was wrong and is retracted.**
BD renders 100% direct/sysmem (`37005f22f`), so pass breaks do not cost GMEM store/restore here, and
the ledger already says BD is CPU-bound.
The open lever `gpu_bd_inpass_float_transfers` still exists and is still worth an A/B under item A.
It is a small per-title lever, not an architectural direction.

### ❌ F. DO NOT PIVOT TO STATIC RECOMPILATION
The LLVM AOT path already captures most of the theoretical win. ReXGlue is per-game engineering with
no published ARM story and a borrowed GPU. **Revisit only if our AOT coverage stalls well below the
current ~80%.**

---

## Sources

- ReXGlue interview — https://readonlymemo.com/rexglue-xbox-360-recompilation-interview/
- Box86/Box64 vs QEMU vs FEX vs Rosetta2 — https://box86.org/2022/03/box86-box64-vs-qemu-vs-fex-vs-rosetta2/
- Beyond3D, ATI Xenos demystified — https://www.beyond3d.com/content/articles/4/11
- Electronic Design, AMD's unified shader GPU (Xenos -> Imageon -> Adreno) — https://www.electronicdesign.com/technologies/embedded/article/55262594/jon-peddie-research-electronics-history-amds-unified-shader-gpu
- Turnip Vulkan driver and tiled rendering — https://deepwiki.com/sailfishos-mirror/mesa/3.3.1-turnip-vulkan-driver-and-tiled-rendering
- Freedreno, Mesa docs — https://docs.mesa3d.org/drivers/freedreno.html
- xenia/docs/cpu.md — https://github.com/xenia-project/xenia/blob/master/docs/cpu.md
- xenia PR #2259, ARM64 backend — https://github.com/xenia-project/xenia/pull/2259
- box86 issue #519, dynarec and x86 strong memory ordering — https://github.com/ptitSeb/box86/issues/519
- Hackaday on XenonRecomp — https://hackaday.com/2025/03/07/run-xbox-360-games-on-your-pc-with-xenondecomp/
- Time Extension on ReXGlue — https://www.timeextension.com/news/2026/03/i-hold-my-work-to-a-strict-standard-the-driving-force-behind-xbox-360-recomp-tool-rexglue-speaks-out
- Android 16 KB page sizes — https://developer.android.com/guide/practices/page-sizes
- Emulation General Wiki, Xbox 360 emulators — https://emulation.gametechwiki.com/index.php/Xbox_360_emulators
