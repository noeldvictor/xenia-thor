# Breaking the Thor's Floors: A Re-Architecture / Recomp Design Proposal (v2)

> Produced 2026-06-06 by an 8-agent deep-research workflow (survey of recomp/DBT/xenia/Adreno/arxiv/Ghidra → adversarial critique → strengthened synthesis). The critique (Part 1) deliberately **killed the draft's own headline** because it rested on a false architectural premise; the surviving plan is in Part 2. Read Part 1 to understand *why* Part 2 says what it says.

**Target:** AYN Thor (Snapdragon 8 Gen 2 / QCS8550, Adreno 740v2 TBDR, 16 GB UMA), xenia fork, PPC VMX128 → ARM64 JIT + Vulkan/Turnip.
**Scope:** novel re-architecture to break (a) the per-vertex Adreno binning wall and (b) the spin-wait CPU waste. Every shippable win lands as a stacking `XeniaOptimizations` toggle.

---

## Part 1 — Adversarial critique of the draft (what was wrong)

### CRITICAL — the "passthrough binning VS" is not a thing xenia can emit
xenia issues **one** `vkCmdDrawIndexed` per draw with **one** bound VS (`deferred_command_buffer.cc:200`). There is no API surface to give the binning pass a different shader than the render pass. The position-only binning VS is **Turnip's own ir3 clone** of the single VS xenia hands it — xenia does not author it, cannot replace it, cannot make it a "passthrough." So the only real levers over binning are: **(i) make the single submitted VS cheaper** (Turnip's clone inherits the saving) and **(ii) make the per-vertex data the GPU fetches smaller.**

### CRITICAL — CPU pre-transform is a per-vertex-count-NEUTRAL move that mostly relocates work
Even if pre-transforming positions on the CPU made the VS a near-passthrough, the binning pass still runs that passthrough over all ~263k verts and still fetches a position per vertex. Pre-transform attacks only the **math** half of the floor (BD's position math is skinning + one matrix mul — a handful of NEON ops Turnip already runs at GPU throughput), at the cost of 263k FP32-exact transforms/frame on CPU + an extra UMA write. It is the **same class of move as R4**, which this fork already measured as a **net loss on strip-heavy BD**. Demoted to conditional, last-resort.

### De-interleave is the only part of the GPU plan that is novel, low-risk, and on-axis — promote it
De-interleaving guest vertex data into a tight position-only stream attacks the **bandwidth** half of the per-vertex floor (the triple-cited Adreno-specific concern). No extension, no precision risk (byte repack, not math), works on current Turnip, genuinely untried on this fork (R2/R3/R4/R5 never touched vertex-stream layout). Caveat the draft buried: **bandwidth-vs-math is UNMEASURED** — so de-interleave doubles as the cheap kill/go diagnostic.

### Mislabeled claims corrected
- **C1 spin-wait** is a **thermal/battery/headroom** win, NOT a floor-breaker — on GPU-paced titles reclaiming spin-wait CPU does not raise fps.
- **fp16/snorm16 "binning-copy position"** is incoherent (one buffer, one VS — no separate binning copy you control) and is the exact precision foot-gun that black-screened BD. **Cut.**
- **G4 "side-steps the a0-indexed-UBO miscompile"** — already solved via SSBO; only the FP16/RelaxedPrecision-on-non-position-interpolants part is real (fragment hygiene, not binning).
- **C4 2nd optimizing JIT tier** — near-empty value set (CPU 75% idle, GPU-paced); the part worth shipping is **PPTC persistence** (stutter), not a rejit tier (throughput).

### Kept (correct in the draft)
Killing whole-game AOT recomp, mesh shaders, GPU compute-cull, LRZ-for-binning, FSI (#148), all-LLVM, i8mm/bf16 on guest position. The R4/R5 "must beat the recorded loss" gate (now also applied to CPU pre-transform). C2/C3 as host/qemu-testable codegen hygiene that stacks but is **not** an fps lever on current titles.

---

## Part 2 — FINAL strengthened design doc

### 1. Problem restatement (facts, corrected framing)

**GPU floor.** Heavy 3D (Blue Dragon) is GPU-bound on the Adreno *binning* front-end. Turnip internally derives a position-only binning VS clone of the single guest VS xenia submits; that clone runs **per-vertex, per-draw, before cull**, over ~263k verts/frame → ~128ms (~5-6fps). Because the cost is **per-vertex, not per-draw**, draw-coalescing caps at ~1.3× and LRZ/Z-prepass cannot help. The binning cost has two components — **per-vertex position math** and **per-vertex position-fetch bandwidth** — and **which dominates is UNMEASURED.** This is the single most important unknown; the entire GPU plan is gated on Phase 0.

**Architectural constraint (verified in code).** xenia emits one `vkCmdDrawIndexed` with one bound VS per draw (`deferred_command_buffer.cc:200`). It cannot author/replace Turnip's binning shader, nor bind a different shader/precision/buffer to binning than to render. The only binning levers are (i) cheaper submitted VS, (ii) smaller per-vertex fetch.

**CPU floor.** Apparently-CPU-bound titles (LO, Gears, Magna) are *spin-wait/sync-bound* on an XDK spin-then-block primitive (~12-15% CPU) and mostly **GPU-paced** (`cpu_beginsubmit ≈ gpu_frame`). CPU ~75% idle. Therefore CPU throughput optimizations (2nd JIT tier, codegen fast-paths) do **NOT** raise fps on current titles. The only CPU plays with value are (a) spin-wait reclamation (thermal/battery/headroom), (b) PPTC persistence (stutter).

**Device-truth corrections.** Fast+correct path is Mesa Turnip; `VK_EXT_fragment_shader_interlock` ABSENT (FSI/#148 not a building block). R4 (CPU pre-cull) and R5 (draw-concat) already tried and lost on this fork — any CPU-side per-vertex/per-triangle preprocessing must beat that recorded loss. FP16/int8 on guest-visible position/geometry FORBIDDEN (black-screened BD); since binning and render read the same buffer via the same VS, there is no safe "throwaway binning copy" to lower to fp16.

### 2. Ranked candidates

#### GPU track (attacks the per-vertex binning wall — primary floor)

**G1 — De-interleave guest vertices into a tight position-first stream (HEADLINE + FIRST BUILD).** On geometry upload, repack the interleaved guest vertex buffer into a position-only (or position-first) stream + separate attributes stream, so the binning pass fetches only position bytes per vertex. Pure CPU byte-repack into UMA; no precision change, no extension. Attacks per-vertex **fetch bandwidth** — the only lever that reduces per-vertex binning cost without reducing visible geometry, without precision risk, without an architecture xenia lacks, and that is **its own kill/go diagnostic** (if it moves `gpu_frame_us`, binning is bandwidth-sensitive; if not, the bandwidth thesis is falsified in ~2 days). LOW-MEDIUM effort, LOW risk. Touches `vulkan_primitive_processor.cc`, `primitive_processor.cc`, `vulkan_shared_memory.cc`.

**G1b — Make the submitted guest VS cheaper (CONDITIONAL on Phase 0 = math-bound).** Only if binning proves math-bound: (1) SPIR-V dead-output trim in `spirv_shader_translator.cc` (low risk, may be subsumed by ir3); (2) CPU pre-transform of position on idle cores (R4-class, must beat the recorded R4 loss, bit-exact FP32 or black-screen) — high effort/risk, build only after Phase 0 proves math-bound AND de-interleave is shown insufficient.

**G2 — CPU cluster-cull-before-binning.** GATED, likely dead on strips (R4 already lost this axis). Only revisit on a non-strip / high-overdraw scene with the NEON position-slice micro-interpreter to make cull cheap. No GPU compute-cull (net-negative on binning-bound TBDR).

**G5 — Port edge EDRAM-transfer-coalescing + direct-resolve (#176/#175).** Hygiene only — pass-reduction is fps-neutral on heavy binning-bound BD. LOW effort/risk, ref impl exists; ship for light/menu/CPU-paced. Skip #148 (FSI absent).

#### CPU track (attacks spin-wait waste — secondary floor)

**C1 — Spin-wait/sync-primitive detection → real host block (HIGHEST CPU VALUE, NOT an fps lever).** Detect the XDK spin-then-block pattern, replace host busy-spin with a futex block. Targets: `NtYieldExecution` (`xboxkrnl_threading.cc:744`), spin-lock→`xeKeWaitForSingleObject` (`:1211`, RW-spinlock `:1677-1741`). Reclaims ~12-15% CPU → cooler/longer battery/more headroom. LOW-MEDIUM effort, low risk. Ship for thermal/headroom, not fps.

**C2/C3 — PPC flag-in-registers (FLAGM/NZCV) + dead-flag elim + block chaining + peephole.** Codegen QUALITY, host/qemu-a64 testable, stacks. Explicitly not an fps lever on current titles. `src/xenia/cpu/compiler/passes/` + a64 emitter.

**C4 — PPTC persistent translation cache (SHIP); 2nd optimizing JIT tier (DEFER).** PPTC kills warm-up stutter, ~free on idle cores (respect W^X / 16K-page / x30-link discipline). Optimizing rejit tier deferred until a title is *measured* hot-block-bound (none today). LLVM stays OUT.

### 3. Recommended approach

**TOP: G1 (de-interleave position stream) as the first build AND the Phase-0 diagnostic, run in parallel with C1 (spin-wait reclamation) for thermal/headroom.**

**Phase 0 — Instrument & gate by *building* G1.** G1's `gpu_frame_us` delta on a content-matched, movie-free BD heavy frame **is** the bandwidth-vs-math answer — faster than standing up the full ADB stage-split, and ships a candidate either way. In parallel, stand up the host-x64 + qemu-a64 differential verifier so *if* G1b pre-transform is ever justified, its NEON kernel is provably bit-exact before any device fire. Only if G1 shows no movement: invest in the driver-path per-stage split before considering G1b.

**Phase 1 — G1 de-interleave (FIRST CONCRETE BUILD).** Split guest vertex data into a position-first stream + attributes stream; bind position-only to the path the binning pass fetches. Toggle `gpu_deinterleave_position_stream` (default-off, GPU·Vulkan, explained). **Go** if `gpu_frame_us` drops at equal `rendered`/`guest_ms`, GPU busy% falls, PNG pixel-correct. **Kill the bandwidth thesis** if no movement → escalate to stage-split, do NOT proceed to G1b blind.

**Phase 2 (CONDITIONAL) — G1b dead-output SPIR-V trim.** Low-risk VS cleanup in `spirv_shader_translator.cc` (guard with `is_vertex_shader()` at 245/584/691/829). Expect small, may be subsumed by ir3.

**Phase 3 (CONDITIONAL on Phase 0 = math-bound AND Phase 1 insufficient) — G1b CPU pre-transform.** Gated, bit-exact verifier proven, R4-loss bar as explicit go/no-go on strip-heavy BD. Toggle `gpu_cpu_pretransform_position`. Kill on any black/holes or net regression vs de-interleave-only.

**Phase 4 (parallel, independent) — C1 spin-wait reclamation.** Intercept the spin-then-block primitive → real host block. Toggle `cpu_spinwait_yield`. Thermal/headroom; no fps claim on GPU-paced titles.

**Ongoing (no fps claim) — C2/C3 codegen quality + C4 PPTC.** Host/qemu-testable; grind for quality and stutter.

**Shipping as toggles.** Per the established pattern (`XeniaOptimizations.java` → `SettingsActivity` → `XeniaAndroidSettings.createLaunchArguments` → allowlist in `EmulatorActivity`): one `Optimization` entry per cvar, default-OFF, explained `detail`. Seed per-game defaults via `GameProfiles.java` (BD on once validated).

### 4. Explicitly rejected

Whole-game AOT/static recomp (BD is GPU-bound, CPU 75% idle; recomp renderers resubmit the same draw/vertex stream 1:1 → same binning cost; per-game RE tax). "Separately-authored passthrough binning VS" (architecturally impossible). fp16/snorm16 "binning-copy" position (no separate copy; black-screens BD). CPU pre-transform as a headline (demoted to conditional G1b). Mesh/task shaders (not on Turnip A7xx). GPU compute-cull prepass (net-negative on binning-bound TBDR). LRZ/Z-prepass to beat binning (wrong pass). Draw-coalescing as fps lever (~1.3× cap; R5 dropped). FSI/#148 (absent). All-LLVM JIT (~40% slower; stutter). 2nd optimizing JIT tier now (no proven hot-block bottleneck). i8mm/bf16/SDOT/FP16 on guest position (black-screens BD; heuristics only). LLM transpilation (watch-only; keep only its verification-harness pattern).

### Bottom line
The surviving novel move is **G1 de-interleave: feed Turnip's binning pass a thin position-only vertex stream** — attacks the per-vertex *bandwidth* with zero precision risk and is its own cheap kill/go diagnostic. **First concrete build: `gpu_deinterleave_position_stream` in `vulkan_primitive_processor.cc` / `primitive_processor.cc` / `vulkan_shared_memory.cc`, measured by `gpu_frame_us` delta on a content-matched movie-free BD heavy frame.** Run **C1 spin-wait reclamation** in parallel for thermal/headroom (not fps). Everything else is conditional, hygiene, or dead on this exact device.

### Key files
- `src/xenia/gpu/vulkan/vulkan_primitive_processor.cc`, `src/xenia/gpu/primitive_processor.cc`, `src/xenia/gpu/vulkan/vulkan_shared_memory.cc` — G1 de-interleave (first build)
- `src/xenia/gpu/vulkan/deferred_command_buffer.cc:200` — one VS / one vkCmdDrawIndexed (basis for cutting the "passthrough binning VS")
- `src/xenia/gpu/spirv_shader_translator.cc` — `is_vertex_shader()` guards at 245/584/691/829 (G1b SPIR-V trim, conditional)
- `src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc` — NtYieldExecution:744, KeWaitForSingleObject:1211, RW-spinlock:1677-1741 (C1)
- `src/xenia/cpu/compiler/passes/` — C2/C3 codegen quality (host/qemu testable)
- `android/.../XeniaOptimizations.java` + `GameProfiles.java` — toggle registry + per-game seeds

---

## Addendum — G1 implementation feasibility (verified against code, 2026-06-06)

Before building G1, the doc's "pure CPU byte-repack of the vertex buffer" framing was checked against the actual xenia vertex path. **It is inaccurate for this architecture — corrected here.**

**There is no host vertex buffer to repack.** At draw time, `vulkan_command_processor.cc:4383-4426` does **not** bind per-attribute vertex buffers — for each `Shader::VertexBinding` it only calls `shared_memory_->RequestRange(vfetch_constant.address<<2, vfetch_constant.size<<2)`, ensuring the **guest's own vertex memory** (at the guest stride, `binding.stride_words`) is resident in the unified shared-memory GPU mirror of guest RAM. The translated VS computes each vfetch address **in-shader** from the guest fetch constant (base + index·stride + attr offset) via `var_main_vfetch_address_` (`spirv_shader_translator.cc:624`) and loads from shared memory. Turnip's binning clone re-runs the *same* vfetch. So "de-interleave" cannot be a host-side buffer relayout; the data the GPU fetches IS the guest's interleaved layout.

**What real G1 actually requires (3 parts, not 1):**
1. **Position-vfetch identification** — per vertex shader, determine which `vfetch`/attribute flows to the position export (dataflow analysis at translation time; the translator already parses `ParsedVertexFetchInstruction`, see the trace hook near `vulkan_command_processor.cc:2063`).
2. **CPU gather pass** — allocate a tight position buffer and, per vertex, read position from `shared_memory[guest_base + i·stride + pos_offset]` (honoring guest endian/format) and write it contiguously. This is an **R4-class per-vertex CPU pass**, and **R4 (CPU per-vertex preprocessing) already measured a net LOSS on strip-heavy BD** — the gather must beat that recorded loss or G1 is dead.
3. **Specialized VS variant** — redirect the position vfetch to read the tight buffer (stride = pos size) instead of guest memory (full stride). Requires a shader-cache key variant + translator support; position must stay **bit-exact** (fp16/lossy = black-screen per the held cvar rule).

**Revised effort/risk:** MEDIUM-HIGH effort, MEDIUM risk (not LOW/LOW). The "G1 is its own cheap kill/go diagnostic" claim is **weakened** — the diagnostic now costs the full build. **Implication:** before committing to G1, prefer the cheaper bandwidth-vs-math gate via the per-stage GPU split (`xenia-thor-adb-gpu-stage-split`: binning µs vs render µs on a matched movie-free BD heavy frame), which the issues-roadmap already names as the gating measurement. Only build the full G1 gather+variant if that split shows binning is fetch-bandwidth-dominated. C1 spin-wait reclamation (thermal/headroom, HLE-only, no per-vertex cost) is unaffected by this correction and remains the cheapest parallel ship.
