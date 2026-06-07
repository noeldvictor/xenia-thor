# Thor Major CPU+GPU Rewrite — Research Synthesis (2026-06-07)

Source: 5 parallel research agents (arxiv + academic venues + emulator source/commits + vendor docs),
commissioned for the user directive "major xenia gpu/cpu arch rewrite to take advantage of ayn thor
cpu gpu like neon etc … get latest research … games need to be full speed." All claims cited in the
agent transcripts; this file distills the actionable, cross-verified conclusions. Memory:
[[thor-major-arch-rewrite-goal]], [[thor-rewrite-research-findings]].

## The one strategic truth (converged across 4 of 5 reports)
- **CPU/JIT work does NOT speed up GPU-bound titles** (Blue Dragon heavy field, Burnout race, BTTF
  gameplay). It raises the ceiling for CPU-bound titles (Lost Odyssey load, Gears menus, Banjo deep-JIT)
  and cuts the IssueDraw `cpu_other` cost. Two tracks, not one.
- **The recomp movement offers NO GPU shortcut.** The AOT projects on our EXACT games — ReXGlue, re:Blue
  (Blue Dragon), reNut (Banjo N&B) — **still run Xenia's emulated renderer**, so they have not beaten our
  bottleneck either. Confirmed: do NOT port a recomp. The prize is the *renderer* techniques the best one
  (UnleashedRecomp's `plume`) was forced to invent. (Corroborates [[bd-recomp-verdict]].)

---

## GPU TRACK — the fps lever for the GPU-bound priority titles

### NEW high-value findings (not in prior memory)
1. **An FSI alternative EXISTS on Turnip.** `VK_EXT_rasterization_order_attachment_access` (in Mesa since
   22.1.0) + `VK_KHR_dynamic_rendering_local_read` (Vulkan Roadmap 2024) provide the rasterization-ordered
   framebuffer read-modify-write that `fragment_shader_interlock` would — the capability we logged as
   ABSENT. This unlocks a **third render-target path** (`Path::kRasterizationOrderAttachment`) that keeps
   EDRAM resident in GMEM and does programmable-blend/format-pack in-shader, eliminating BOTH the FSI
   requirement AND the FBO ownership-transfer copies — and it's *native* to a tiler's tile buffer.
   ⚠️ This is a correctness/bandwidth win (the ~3% EDRAM-transfer + ~12% render-pass-break terms), NOT
   the binning floor. High confidence it's feasible; medium confidence on fps magnitude. Files:
   `vulkan_render_target_cache.{cc,h}` (the Path enum + FSI→FBO fallback at .cc:351-362).
2. **`VK_EXT_descriptor_buffer` may actually be available on Turnip** — our ABSENT capture was likely on
   the proprietary Qualcomm driver. FOSDEM 2024 confirms Turnip exposes it on A7xx. ACTION: re-run
   `vulkaninfo` under Turnip; if it (and dynamic_rendering/sync2/local_read) appear, our GPU-driven
   headroom is larger than the PRESENT/ABSENT table implies.
3. **LRZ hygiene (cheap, new).** Adreno A7XX has **bidirectional LRZ** (two LRZ buffers → no direction-flip
   penalty), but LRZ is still fully disabled by: FS writes depth, FS side-effects (SSBO/image writes),
   depth-write direction change on pre-A7xx; temporarily disabled by blending/logicop/colorWriteMask/
   discard/EQUAL-NEVER compares. ACTION: audit our pipeline translation so opaque/depth passes stay
   LRZ-eligible — free binning-pass rejection we may be forfeiting.

### The binning floor (confirmed mechanism, our measured fps wall)
- freedreno wiki + Mesa confirm: the Adreno binning/visibility pass **runs a position-only VS per vertex,
  per draw, before any cull**. Backface/LRZ reject happens AFTER binning. So the levers are ONLY:
  - **Fewer DRAWS / context-rolls** → draw coalescing (primitive-restart strip stitching → one
    `vkCmdDrawIndexed`; + `multiDrawIndirect`/`draw_indirect_count` as the carrier). RANK 1 — but GATE
    per-DRAW vs per-TRIANGLE first (constant-vertex A/B); if the floor is per-triangle the merge win
    collapses. (Matches [[thor-bd-draw-coalescer-plan]].)
  - **Fewer submitted VERTICES** → (a) de-interleaved position-only binning stream (G1 — attacks the
    bandwidth half; Android AGI explicitly flags "vertex streams not split" + "≤32 bytes/vertex" + "%
    Vertex Fetch Stall" as the Adreno binning counters); (b) compute-cull BEFORE binning → compacted
    indirect buffer (we ALREADY have the cull math: BD ~51% triangles dropped, device-validated no-holes,
    [[lever2-cull-feasibility-bd]]; wire it to indirect). (Matches [[thor-novel-rearch-design-v2]].)
  - **DEAD:** instancing (re-bins identical tris N times on TBDR), post-bin GPU cull (cost already paid),
    mesh/task shaders (Turnip "distant future"), `VK_EXT_multi_draw` (absent + CPU-only = wrong bottleneck).
- **GATE EVERYTHING on the per-stage GPU split** (binning µs vs render µs). Route A in-engine timestamps
  shipped (4b671a5e1) — USE them to decide per-draw vs per-triangle before building the coalescer.

### Renderer modernization to STEAL (from UnleashedRecomp/plume + XenosRecomp)
- **Bindless** textures/samplers (cut descriptor churn — already a planned registry toggle).
- **Specialization constants** to collapse pipeline permutations (fewer pipeline compiles/binds).
- **Eliminate redundant texture copies** modern APIs don't need.
- **Async shader/pipeline compilation** off the render thread + **persistent on-disk pipeline cache** +
  **smol-v** SPIR-V compression (kills first-run compile stutter on a storage-limited handheld).
- **TBDR render-pass discipline:** `VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT` for transient/EDRAM
  intermediates (exist only in tile memory = zero external bandwidth), merge subpasses, kill spurious
  barriers that force GMEM tile flush/reload (the ~12% render-pass-break term).

---

## CPU TRACK — for the CPU-bound titles + finally using the 7 idle cores

Design the ARM64 backend AROUND the ISA, don't port the x64 backend (x64 has 16 GPRs → forced spills;
ARM64 has 31 GPRs ≈ near 1:1 with PPC's 32 → pin registers, rarely spill; MAMBO-X64 shows an ARM-host DBT
can approach/beat native). Ranked:
1. **NZCV-resident, deferred flag evaluation** (FEX-Emu's #1 win): keep PPC CR/XER[CA] in the host NZCV
   flag register, compute lazily, use ARMv8.4 **FLAGM/FLAGM2** (`rmif`/`cfinv`/`setf8/16`) which the 8 Gen 2
   has. FEX: +17.6% Geekbench, "some games >2× FPS" from this one change. PURSUE — best-evidenced,
   hardware-matched CPU win.
2. **Parallel/background JIT compilation** — the ONLY thing that attacks "1 of 8 cores." Get the ARM
   weak-memory publication protocol right: build block fully → i-cache maintenance (`dc cvau`/`ic ivau`+
   `dsb`/`isb`) → release-store the code pointer (acquire/`ldapr` read) → atomic single-word link-patch.
   Persistent on-disk code/profile cache (FEX/RPCS3 precedent) makes cores = faster startup too.
3. **Static global register pinning + CR-as-bool + resident XER** (Cemu's PPC recompiler model is the
   cleanest published template) — the backend design foundation.
4. **Block linking/chaining + branch following** (Dolphin) — table stakes; atomic + i-cache-coherent on ARM.
5. **Macro-op fusion / peephole** — `rlwinm`→`ubfx/bfi`, `cmp+bc`→flag-resident branch, `lis+ori`→`movz/movk`;
   our rlwinm fast-path is on the right track. ~1.28× geomean in one DBT study. Incremental.
6. **Guest-memory fast-path** — preserve the pinned membase + UXTW indexed addressing (R3, already
   device-validated); don't regress it in the rewrite.

### SKIP (CPU)
Full OSR (high cost, priority titles GPU-bound), the "delete the IR" rewrite (arXiv:2501.03427's 35× is a
synthetic PoC; real projection 10-17.5×, keep our HIR), learned-translation-rules (CGO'24, overlaps
hand fusion), SIMDe for VMX (only as a fallback for a missing a64 op).

---

## NEON / SIMD (the user specifically said "like neon etc.")

Our VMX128→NEON mapping is already largely CORRECT and fidelity-critical — most of the win is NOT touching
it. **The triple-confirmed meta-lesson: more accurate is NOT automatically safe** — guest code is tuned to
specific estimate/rounding/denormal behavior; cvar-gate + on-device-validate every change.
- **DO NOT TOUCH (correct + game-breaking if changed):** fused `fmla` for `vmaddfp`/`vnmsubfp`; the
  software/HW denormal flush (VMX NJ-bit semantics); `EmitFmaWithPpcNan` (PPC NaN propagation, RPCS3-validated);
  `fmax`/`fmin` for `vmaxfp` (signed-zero/NaN order, [[a64-x64-vmaxfp-signed-zero]]); the non-IEEE
  `EmitFloatToXenosHalf4` (Xenos half saturates to 0x7FFF, NEON `fcvtn` would emit IEEE inf/NaN and corrupt
  vertices); saturating pack (`sqxtn`/`uqxtn` + BE `rev32`).
- **i8mm / bf16 (BFMMLA/BFDOT/SMMLA): PERMANENT SKIP for guest FP32 geometry** — bf16's 8-bit mantissa
  guarantees position divergence → black screen (empirically proven: `arm64_vmx_dot_f32_fastpath`
  black-screened BD, [[approx-math-guest-visible-vs-heuristics]]).
- **asimddp (SDOT/UDOT): bit-EXACT for INTEGER** → SAFE for INTERNAL heuristics only (cull counters,
  extent/bbox accumulation, bin histograms — fold 4×int8 MAC into one op). PURSUE for heuristics; never
  guest-visible.
- **PURSUE (low-risk throughput, CPU-bound titles):** extend the permute → `zip1/zip2/uzp/trn/ext`
  fast-path recognizer (cheaper than LoadV128Const+TBL); `frsqrte`+1×`frsqrts` (and `frecpe`+`frecps`) for
  `vrsqrtefp128`/`vrefp128` behind a validated cvar (PPC estimate is only ~12-bit; one NR step ≈16-bit
  exceeds the guarantee — but validate per title); **RPCS3-style per-class "VMX float accuracy" toggle**
  in XeniaOptimizations (Accurate=current / Approximate=relaxed), default Accurate.

---

## HANDHELD-SPECIFIC (fanless thermal envelope — gates everything)
1. **ADPF — Android Dynamic Performance Framework** (Performance Hint API + Thermal API, Android 12+).
   Google states **manual `sched_setaffinity` is SUPERSEDED by ADPF on Android 12+**. Numbers: up to **+57%
   frame rate, −12% power, −25% frame-time variance**. Register the frame-critical thread as a hint
   session, report per-frame durations, and self-throttle off `getThermalHeadroom()` BEFORE the OS throttles
   hard. Mostly Java/JNI + a cvar toggle → device-free buildable. Highest impact:risk on a fanless
   thermal-gated device. **PURSUE FIRST.** (Our thermal gate STAYS — ADPF complements it.)
2. **Cluster-aware thread placement** (RPCS3 precedent): co-locate the tightly-synced guest-CPU ↔
   GPU-command threads on the big cluster (X3+A715s share L3), push INDEPENDENT host work (shader compile,
   audio, file IO, texture upload) to the A510 efficiency cluster; let ADPF own per-thread boost rather
   than hard-pinning one X3 core.
3. **Async GPU command building + async shader compile** onto idle cores — the real "use 7 idle cores" win.

---

## RECOMMENDED BUILD SEQUENCE (each a stacking XeniaOptimizations toggle, cvar-gated, device-validated)
1. **ADPF Performance-Hint + Thermal self-throttle** — handheld-wide fps/power/variance + thermal safety;
   helps ALL titles; cheapest high-impact; device-free buildable. (FIRST.)
2. **Per-stage GPU split readout** (use the shipped Route A timestamps) — the GATE that decides the binning
   attack (per-draw coalescer vs per-triangle position-stream). Plus `vulkaninfo`-under-Turnip to confirm
   descriptor_buffer / local_read availability.
3. **GPU binning attack** (gated by #2): draw-coalescer (if per-draw) and/or compute-cull→indirect +
   position-only binning stream (if per-triangle/bandwidth). This is the BD/Burnout/BTTF fps lever.
4. **CPU track for CPU-bound titles:** NZCV/FLAGM deferred flags → parallel JIT → register pinning/CR-as-bool.
5. **Renderer modernization + TBDR hygiene:** LRZ-eligibility audit, lazily-allocated transients, async
   shader compile + persistent pipeline cache, bindless/spec-constants.
6. **EDRAM ROAA/local_read third RT path** — correctness/bandwidth + future headroom (not the primary fps).
7. **NEON throughput:** permute fast-path recognizer, SDOT heuristics, VMX-accuracy toggle.

## Honest gates / unknowns (do not build blind)
- per-DRAW vs per-TRIANGLE binning split — UNPROVEN; measure (#2) before the coalescer.
- ROAA/local_read/descriptor_buffer/tile_* exact A740-Turnip support — verify via `vulkaninfo` before designing on them.
- UMA CPU→GPU geometry feed carries guest-precision risk + `external_memory_host` absent + UMA-present hangs
  ([[burnout-uma-present-hang-regression]]) — usable only as a gated geometry feed, not the whole mirror.
- ADPF/parallel-JIT fps magnitude on OUR workload — validate on device per title.
