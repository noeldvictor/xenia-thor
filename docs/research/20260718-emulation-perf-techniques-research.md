# Xbox 360 emulation performance techniques — research findings (2026-07-18)

Deep-research sweep (arXiv + ISCA/MICRO/ASPLOS/PLDI/CGO/EuroSys + emulator-scene
primary sources), 24 claims that survived 3-vote adversarial verification (23
confirmed, 1 refuted). Grounded in the two committed tracks: CPU (PPC→ARM64
recompiler) and GPU (D3D9→Vulkan HLE on Adreno/Turnip TBDR).

---

## Executive summary

The research strongly **corroborates the committed direction on both tracks** and
surfaces **three genuinely new, actionable items**. On CPU: keeping guest
registers resident across call boundaries is the proven #1 lever (a shipping
Xbox 360 static recompiler measured ~several ms/frame from exactly this), **but
there is an ARM64-specific trap** — the calling-convention trick that pins guest
regs into host regs does not exist in upstream LLVM, so on an AArch64 host LLVM
spills the guest register state to memory anyway, undercutting the win. A 2026
EuroSys technique (**LCCRA**) directly attacks both that residency gap and the
+140 % translation-time cost the residency approach otherwise incurs. On GPU:
the pass-fusion / GMEM-residency direction is confirmed as the correct answer to
the ~95-pass wall, and the research pins down the exact Vulkan mechanism
(`VK_KHR_dynamic_rendering_local_read`), the concrete rules that break driver
merging, and the load/store-op discipline that controls tile traffic.

## Top 3 new, actionable findings

1. **The ARM64 residency trap (Instrew/VEE 2021).** The register-mapping
   calling-convention optimization that makes whole-function LLVM DBT fast "is
   not supported by upstream LLVM, which causes the entire register state to be
   written back to memory," and the paper measured emulation overhead
   *larger on AArch64 hosts than on x86-64* partly for this reason. **This is a
   direct warning for this project's LLVM-IR→ARM64 recompiler:** the residency
   win you get for free on a desktop x86-64 build may not materialize on the
   Thor unless you force guest hot-state into fixed ARM64 registers yourself.
   Source: Engelke et al., "Efficient LLVM-Based Dynamic Binary Translation,"
   VEE 2021 — https://home.cit.tum.de/~engelke/pubs/2104-vee.pdf

2. **LCCRA — low-compilation-cost register allocation (EuroSys 2026).** A
   register-allocation strategy for LLVM-based binary translators that
   propagates guest-register values in virtual registers within each block,
   substitutes virtual regs for loads only where values cross block boundaries,
   and eliminates redundant stores via control-flow analysis. Measured (on
   CrossDBT, x86-64→AArch64, PARSEC): **5.76–7.79 % lower end-to-end latency and
   69.55–74.98 % lower register-allocation time when host registers are
   limited.** This is the antidote to the residency approach's two problems: it
   keeps guest regs resident *and* it fixes the +140 % translation-time blowup
   that Instrew reported when many registers are fixed at call boundaries.
   Source: "Low-Compilation-Cost Register Allocation in LLVM-Based Binary
   Translation," EuroSys 2026, DOI 10.1145/3767295.3803591 —
   https://dl.acm.org/doi/abs/10.1145/3767295.3803591

3. **XenonRecomp is a working Xbox 360 PPC static recompiler with a measured
   residency win.** It statically recompiles 360 PPC executables to portable
   C++ (AOT, not JIT), passing CPU state + a base pointer as function args. In
   the shipping Unleashed Recompiled title, promoting guest registers to C++
   locals (instead of writing back to the context struct) shrank the executable
   ~20 MB and **reduced frame times by several milliseconds** — an independent,
   real-title confirmation of this project's guest-register-residency-writeback
   lever. It also shows the AOT path avoids JIT/dispatch overhead entirely.
   Source: https://github.com/hedge-dev/XenonRecomp

---

## Track 1 — CPU (PowerPC 64-bit → ARM64)

### 1.1 Register residency across calls — the proven lever and the ARM64 catch
- **What:** Whole-function LLVM lifting (not TCG basic-block granularity) plus
  the HHVM calling convention that maps frequently-used guest registers into
  host registers cut emulation overhead to **53 % over native** (x86-64),
  vs 124 % for HQEMU and 716 % for QEMU. The two mechanisms — register mapping
  ("hhvm") and whole-function-past-calls ("callret") — combined reduce mean
  normalized overhead by 0.43 (geomean 1.96→1.53).
- **The catch (magnitude-limiting):** that combination *also increases
  translation time by 140 %* because LLVM's code generator struggles when many
  registers are fixed at call boundaries; and the calling-convention trick is
  **absent from upstream LLVM**, so an AArch64 host spills the full guest
  register state to memory, making overhead *larger* on ARM64 than x86-64.
- **Applies to:** the project's LLVM-IR→ARM64 recompiler + residency-writeback
  lever. Validates the lever, but flags that the desktop-measured win may not
  transfer to the Thor without explicit register pinning — and that naive
  residency inflates compile time.
- **Difficulty:** conceptual — high confidence; the fix is 1.2.
- Source: VEE 2021, https://home.cit.tum.de/~engelke/pubs/2104-vee.pdf
  (claims 0/1/2, 3-0 votes, primary)

### 1.2 LCCRA — cheap register allocation that keeps guest regs resident
- **What / magnitude:** see Top-3 #2. 5.76–7.79 % latency + 69.55–74.98 %
  regalloc-time reduction (register-limited regime; register-abundant yields
  only "slightly better" latency).
- **Applies to:** directly bolt-on to the LLVM recompiler as a custom regalloc
  pass; targets the exact residency + compile-time problems from 1.1. Most
  relevant single new CPU technique found.
- **Difficulty:** high (a custom LLVM regalloc/IR pass), but well-specified.
- Source: EuroSys 2026, DOI 10.1145/3767295.3803591 (claims 3/4, 3-0, primary)

### 1.3 Static AOT recompilation (XenonRecomp) — the Xbox 360 reference
- **What:** see Top-3 #3. Directly converts PPC → C++ (not decompiled, not JIT);
  CPU state struct + 32-bit base pointer as function args; VMX via x86
  intrinsics with ARM64 through SIMDe.
- **Applies to:** validates a hybrid AOT+JIT direction (AOT the whole image, JIT
  the SMC tail) and removes dispatch overhead — the path to turning a
  guest-code-bound title GPU-bound. XenonRecomp is a concrete reference impl to
  study for 360-specific issues (jump-table extraction, function-boundary
  analysis via XenonAnalyse).
- **Difficulty:** high (AOT pipeline), but a working exemplar exists.
- Source: https://github.com/hedge-dev/XenonRecomp (claims 8/9/10/11/12, 3-0)

### 1.4 RPCS3 ARM64 — reuse-IR-once, retarget-many + VMX-on-NEON
- **What:** RPCS3 got ARM64 not by writing a new backend but by adding an **IR
  transformer** that takes the LLVM JIT IR already generated for x86-64
  (reference arch) and tweaks it to satisfy ARM64 before LLVM compiles it. Its
  PPUTranslator emits LLVM IR that LLVM's backend now compiles to ARM64
  (proven with commercial titles, Dec 2024). VMX/AltiVec was ported by adding
  AArch64 NEON intrinsics to replace the x86 intrinsics, exploiting ARM's 32
  NEON registers.
- **Applies to:** de-risks the LLVM-IR→ARM64 model on a *PPC console emulator*
  (same guest ISA family), and offers a cheaper retargeting strategy than a
  bespoke ARM64 backend if the project ever generates x86-first IR.
- **Difficulty:** medium (transformer layer) to reuse the idea.
- Source: https://blog.rpcs3.net/2024/12/09/introducing-rpcs3-for-arm64/
  (claims 13/14/15, 3-0, primary)

### 1.5 Indirect branches / returns — mostly free under AOT
- **What:** Indirect-branch handling is the single biggest DBT overhead source,
  and function returns are the most common indirect branch; MAMBO-X64's ReTrace
  translates returns to use the host's hardware return predictor (~10 % avg
  overhead vs native).
- **Caveat / applies to:** under the AOT static-recompilation model (1.3), guest
  returns already lower to native host `call`/`ret`, inheriting hardware return
  prediction *for free* — so ReTrace is largely redundant if you go AOT. Keep
  this in mind if any JIT fallback path remains hot. (Note: one narrower ReTrace
  sub-claim about hash-table-guard mispredictions was **refuted** 1-2 in
  verification; the general overhead observation stands.)
- **Difficulty:** n/a (mostly informational / free under AOT).
- Source: MAMBO-X64, PLDI 2017 / TACO, DOI 10.1145/3062341.3062371 (claim 5, 3-0)

### 1.6 VMX/AltiVec on NEON — portability vs optimality
- **What:** Two shipping approaches: XenonRecomp uses x86 intrinsics + SIMDe on
  ARM64 (portability over optimality); RPCS3 hand-adds AArch64 NEON intrinsics
  replacing x86 ones. Register-count facts confirmed: ARM64 has 32 NEON
  registers (vs the AArch32 bank-aliasing quirk that holds up to 48 FP values).
- **Applies to:** Track 1(c). The project's memory already flags NEON-only (no
  SVE) and that guest FP32 geometry must keep guest precision — SIMDe is the
  fast-to-ship route, hand-written NEON the fast-to-run route. Confirms the
  precision/portability tension is real and both paths are viable.
- Source: XenonRecomp README + RPCS3 blog (claims 6/9/11/15, 3-0)

---

## Track 2 — GPU (Xenos + EDRAM on Adreno/Turnip TBDR)

### 2.1 Pass fusion via dynamic_rendering_local_read / subpass merging
- **What:** On TBDR GPUs, merged subpasses keep intermediate attachment data
  (e.g. a deferred G-buffer) entirely in on-chip tile memory and **never write
  it to external VRAM** — the direct antidote to ~95 passes each flushing GMEM.
  The modern, recommended mechanism is **`VK_KHR_dynamic_rendering_local_read`**
  (core in Vulkan 1.4): fragment shaders read the current pixel's
  color/depth/stencil attachments under dynamic rendering, getting subpass-like
  on-chip fusion *without VkRenderPass objects* — the exact mechanism an HLE
  renderer needs to fuse EDRAM-style passes.
- **Applies to:** the committed native GMEM-resident-RT HLE. Your CLAUDE.md
  confirms Turnip already exposes `dynamic_rendering_local_read` + ROAA, so the
  mechanism is available on the target. This is the concrete API to build the
  fused renderer on.
- **Magnitude:** ARM measured ~55 % tile/bandwidth reduction when merged
  (Mali-G76); on Adreno the win is deleting the per-pass tile store/reload.
- **Difficulty:** high (part of the multi-month HLE rewrite) but well-defined.
- Sources: https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html
  and Arm "Vulkan subpasses" blog (claims 18/20/21/22, 3-0)

### 2.2 What breaks merging — the concrete rules
- **What:** Subpass/local-read fusion into a single hardware pass is **not
  guaranteed** — it is implementation-dependent, and it is *broken by "global"
  dependencies*: pipeline barriers or descriptor-set updates between subpasses,
  and omitting `VK_DEPENDENCY_BY_REGION_BIT` deactivates tile-shading. Access
  must be framebuffer-**local** (same-pixel), not arbitrary sampling.
- **Applies to:** a build-time checklist for the HLE renderer — to keep RTs
  GMEM-resident you must avoid inter-draw barriers/descriptor churn inside the
  fused pass and use by-region dependencies. This explains prior fusion attempts
  that didn't collapse pass count: a stray barrier/descriptor update splits it.
- **Difficulty:** medium (discipline + validation), high value.
- Source: Khronos TBDR best practices + Arm blog (claims 19/21, 3-0)

### 2.3 loadOp / storeOp — tile-traffic control
- **What:** `LOAD_OP_CLEAR` initializes tile memory with no external traffic
  (faster than `vkCmdClearAttachments`); `LOAD_OP_DONT_CARE` skips loading prior
  contents; `STORE_OP_DONT_CARE` prevents writing an attachment back to main
  memory when it isn't needed after the pass. ARM measured up to 36 %/62 %
  reductions in external read/write bytes.
- **Applies to:** every native RT in the HLE — audit each pass's load/store ops;
  EDRAM scratch/intermediate targets should be DONT_CARE on store. Cheap, high
  ROI, orthogonal to the big rewrite.
- **Difficulty:** low.
- Source: Khronos TBDR best practices (claim 23, 3-0)

### 2.4 LRZ and shader-written depth
- **What:** Adreno LRZ does draw-order-independent depth rejection via a
  low-res Z-buffer built during binning, rejecting occluded fragments before the
  full-res depth test. **But a fragment shader that writes depth makes the depth
  value unpredictable and completely disables LRZ** (Turnip does not honor the
  `conservative_depth` hint that could otherwise preserve it).
- **Applies to:** matches the project's own finding that BD's alpha-test/discard
  and shader-depth passes defeat LRZ. Actionable: in the HLE, isolate
  shader-depth-writing passes so they don't poison LRZ for the rest of the
  frame, and prefer hardware depth (no `SV_Depth`) wherever the guest allows.
- **Difficulty:** medium (shader/pass classification).
- Source: https://blogs.igalia.com/dpiliaiev/adreno-lrz/ (claims 16/17, 3-0)

---

## Corroborates-existing vs genuinely-new

**Corroborates the committed direction (use as validation, not new work):**
whole-function LLVM + residency (1.1), XenonRecomp AOT + its measured register
win (1.3), RPCS3 LLVM-IR→ARM64 proof (1.4), the GMEM-resident-RT / pass-fusion
GPU thesis (2.1), LRZ-defeated-by-shader-depth (2.4).

**Genuinely new / not yet in the ledger:** the ARM64 upstream-LLVM residency
trap (1.1 catch), **LCCRA** as the regalloc fix (1.2), the reuse-IR-once
retarget-many option (1.4), and the concrete merge-breaking rules + load/store-op
audit as cheap GPU wins independent of the big rewrite (2.2, 2.3).

## Caveats
- The 53 %/716 % Instrew figures are **same-ISA x86-64→x86-64** (SPEC);
  cross-ISA PPC→ARM64 overhead is typically higher — treat as directional.
- LCCRA's large numbers hold in the **register-limited** regime; the
  register-abundant case yields only "slightly better" latency.
- XenonRecomp's ~20 MB / "several ms" is developer-self-reported, not
  independently benchmarked.
- Subpass/local-read fusion is **implementation-dependent** — must be
  Thor/Turnip-verified (matches the project's PC-vs-TBDR split rule); a PC pass
  count won't prove the GMEM-residency win.
- One ReTrace sub-claim was refuted (1-2); the general indirect-branch finding
  stands.

## Open questions
1. Does Turnip's `dynamic_rendering_local_read` implementation on Adreno 740
   actually keep the HLE's resource-keyed RTs GMEM-resident across the fused
   field pass, or does it still split? (Must be device-measured.)
2. Can the project pin guest PPC hot-state into fixed ARM64 registers despite
   upstream LLVM lacking the custom calling convention — i.e., is an LCCRA-style
   pass or an inline-asm/GHC-CC hack the practical route on ARM64?
3. Would a hybrid AOT (XenonRecomp-style static recompile of the shipped XEX) +
   JIT-only-for-SMC model beat the current all-JIT LLVM path on the Thor's
   thermal/dispatch budget for the CPU-bound field?
4. What is the exact set of BD passes that write shader depth and poison LRZ,
   and can they be reordered/isolated in the HLE?

---

*Method: 5-angle fan-out web search → source fetch → 3-vote adversarial
verification (2/3 refutes to kill) → synthesis. 23/24 claims confirmed 3-0.
Full verifier evidence and quotes retained in the workflow journal.*

---

## Codebase cross-check (2026-07-18): is the ARM64 residency trap present here?

**Yes — verified in `src/xenia/cpu/backend/llvm/llvm_assembler.cc`.** The
research's #1 CPU caveat (finding 1.1) is present in this project's LLVM backend
today, and deliberately so.

At every guest call (`EmitGuestCall`, ~line 281) and every kernel/extern call
(`EmitCallExtern`, ~line 298) the backend emits:
```
if (writeback_) WriteBackCtxRegs();  // flush ALL guest regs -> PPCContext
b_.CreateCall(xe_llvm_guest_call, {ctx_ptr_, target, ret_addr});
if (residency_) ReloadCtxRegs();     // reload ALL guest regs from PPCContext
```
So the register-residency win (guest GPRs held in host registers via the eager
alloca mirrors + mem2reg/GVN) is **intra-function only**; at any call the full
guest register file is materialized to the in-memory `PPCContext` struct and
reloaded afterward. `reserve-x20/x21 + @llvm.read_register` pins only the
*context pointer* and *membase*, not the guest GPRs. This is exactly Instrew's
finding [2] ("the entire register state is written back to memory").

**Why it's this way (not an oversight):** the code comment at `EmitGuestCall`
(lines 261-275) records that a direct inline-cache call (which would allow a
host calling convention to keep guest regs live across the call) was tried twice
and **crashes BD at opt=2 on device**, because the guest ABI "returns" by
branching to a guest return address in `x0` through the dispatcher, not a host
`RET`. So calls must route through the C helper `xe_llvm_guest_call`
(host_to_guest_thunk), which forces the write-back/reload.

**The fix is already scoped in-code, and it's not regalloc:** the comment names
it — emit a *per-call-site host return trampoline* (a `blockaddress` of the
post-call point, registered in the guest→host map as the `x0` the callee
branches back to), enabling a direct guest→guest call that preserves live guest
regs across the boundary and eliminates the write-back/reload. This is the
"~13% dispatch lever." LCCRA (finding 1.2) is a *secondary* refinement (prune
redundant write-back stores via CFG analysis); the return-trampoline would
remove the round-trip entirely and is the higher-value item.

**But the ceiling is small for Blue Dragon.** Prior device profiling
(memory `llvm-jit-backend-build`, 2026-06-27) put JIT'd guest code at only
~4.65-5% of BD CPU (BD is host-emulation/sync/GPU-bound), so closing the
residency trap is a **≤5% lever for BD** — consistent with the standing
"don't grind LLVM residency for BD fps" conclusion. It could matter more for a
genuinely guest-compute-bound title (heavy VMX/ALU inner loops that don't stall
on host work) — Burnout/Gears/Lost Odyssey are the untested candidates. Net: the
research correctly identifies a real trap in this code; the project's own
measurements bound its BD payoff. **Recommended: profile a guest-compute-bound
title to decide if the return-trampoline is worth building; do not chase it for
BD.**
