# BD / xenia-thor EXPERIMENT LEDGER — grep BEFORE you run anything

**PURPOSE (anti-repetition RAG):** every performance/correctness experiment ever run, its result, and a
DONE/DEAD verdict. **BEFORE running ANY device experiment or building any lever, `grep` this file for the
lever name + keywords.** If it's here with a DEAD/FLAT verdict, DO NOT re-run it — read the note and build on
it. Add every new experiment here the moment you get the result (cvar, hypothesis, device result, verdict, date).

Legend: **DEAD** = tested, does not work / no win, do not re-run. **FLAT** = no measurable effect. **WIN** =
shipped/validated. **GFX-LOSS** = works but corrupts pixels (not "no gfx loss"). **CONFOUNDED** = result
untrustworthy (cross-run scene variance). **OPEN** = not yet resolved.

## ⚠️ MEASUREMENT RULE (why so many results are CONFOUNDED)
BD's GPU scene complexity swings ~4×/second, so **cross-run fps / gpu_frame_us comparisons are WORTHLESS**
(two navs reach different scenes). Only trust: single-run in-place alternating A/B on a confirmed-GPU-busy
frame; screenshot correctness; ir3 shader-stats; qemu byte-identical; code facts. `gpu_frame_us` absolute
value is unreliable (includes idle). OSD fps box = the render-rate truth for ONE screenshot's scene only.

## GPU levers — BD field

| Lever / cvar | Hypothesis | Device result | Verdict |
|---|---|---|---|
| `gpu_hw_vertex_fetch` (+`_force`, +pre-swap) | native input replaces SSBO/tex vfetch → cut foliage vertex cost | partial=12% coverage flat; `_force` cat5 stayed 12 (DMA-indexed foliage INELIGIBLE); pre-swap gave 1172/1172 coverage but STILL FLAT | **DEAD** — vertex FETCH is not the cost |
| `gpu_edram_passes_dont_care` / `_safe` | elide EDRAM tile load/store I/O | BLACK screen / 0fps (BD blend passes NEED the loads); the old "2.6×" was a black-frame artifact | **DEAD** |
| `gpu_depth_only_alpha_shader` | strip lighting from the depth-only foliage SHADOW shader (DCE) | shadows correct (no gfx loss) but gpu_frame_us UNCHANGED — the ~4000-instr guest VS is kept for the alpha test; fragment is NOT BD's cost | **DEAD for BD** (valid mechanism for fragment-bound titles; default-off) |
| `vulkan_merge_draws` | merge 1194 tiny foliage draws → fewer GPU launches | REGRESSES to 9.3fps / 107ms (same scene) | **DEAD** |
| `gpu_collapse_alphatest_depthonly`/`_coverage` | collapse alpha-test foliage draws to 3 idx | +fps (61→45ms) but CORRUPTS foliage (degenerate geometry) | **GFX-LOSS** (proved shadow-pass = ~16ms; but not usable) |
| `gpu_flatten_predicated_tiling` (+`_widen`) | merge predicated tiles into one pass | BREAKS pixels (black EDRAM desync) — but NOT because draws are partitioned; it operated DOWNSTREAM/incoherently | **DEAD (wrong method)** — see BIN-ONCE below |
| **BIN-ONCE HLE** (`cpu_hle_bin_once_begintiling_addr=8248A188`) — intercept BeginTiling, force count=1 + full-surface rect, run body via behavior-toggle | collapse the assumed 2-tile field to 1 pass → halve foliage vertex | **MECHANISM WORKS but DOES NOT APPLY to the reachable field (2026-07-04).** BUILT + DEVICE: renders PIXEL-PERFECT (the host-intercept + arg-rewrite + behavior-toggle-run-body mechanism is PROVEN, no crash) BUT fire-log EMPTY = BeginTiling 0x8248A188 NEVER FIRES at the Shu-village field (planted:1 install only). ⇒ **the reachable field is IMMEDIATE-MODE, NOT deferred-tiled** (agent's "N=2" was an UNVERIFIED assumption from an old measurement; my select=FFFFFFFF resolve-trace was right; the token interpreter 0x82487CC0 also never fired). NO fps change (7.7fps clean). So the tiling-doubling premise is VOID for this field - the foliage cost is INTRINSIC immediate-mode, not tiling. Tiling may be used by OTHER/denser scenes (unreached in nav). | **DEAD for the reachable field** (mechanism reusable for the port pieces that DO apply: native vertex input, native RT) |
| FDM (`fdm-overdraw-lever`, 9f2e19e8d) | fragment density map bin-scaling | garbled output + flat perf; doesn't compose with EDRAM multi-pass | **DEAD** |
| `gpu_resolution_downscale_pct` | cut fill cost | FLAT frame time (not fill-bound) AND regresses via RT-scaling overhead; perf-mode stack (downscale45+decimate55+VRS4) = 8.9fps WORSE than baseline | **DEAD for fps** (quality option only) |
| UBWC | bandwidth | timing-neutral | **FLAT** |
| LRZ / early-Z (foliage_lrz_*) | occlusion cull foliage | upper-bound ZERO — co-planar alpha-test foliage is unoccludable | **DEAD** |
| fp16-relaxed color | cheaper color | +11ms REGRESSION | **DEAD** |
| cap=1 MSAA on stack | halve ROP | no win + GHOST (EDRAM tile aliasing) | **DEAD** (cap=2 clean is the ceiling) |
| `gpu_skip_bloom` | skip bloom pyramid | zero-delta A/B (BD composites opaque dst=ZERO, not additive) | **DEAD/inert** |
| `gpu_uma_direct_shared_memory` | zero-copy upload | SAFE + renders but ZERO fps on heavy field (not upload-bound) | **FLAT for BD-heavy** |
| BeginTiling HLE (0x82487CC0, `cpu_hle_tiling_replay_addr`) | HLE the tiling replay → bin-once | replay runs to completion (reentrancy beaten) BUT NOT on the field's path (TRSTEP:0 at Shu field); ring emit desyncs when exercised | **DEAD for field** (wrong function) |
| foliage decimation / `gpu_foliage_thin_factor` | fewer foliage verts | fps win but GFX-LOSS (fewer plants) | **GFX-LOSS** (quality option) |

## GPU levers — SHIPPED (BD ~19.8fps stack)
| `gpu_fp10_color_as_unorm10` (−8ms), `gpu_vrs_foliage_rate=4` (perf) / =2x2 (clean 17fps), `gpu_force_max_msaa_samples=2` | **WIN — shipped** (but fp10/VRS add the sparkle-corruption seen in screenshots = the field's water/effects, removable). |

## 🔥 THE UN-EXHAUSTED LEVER (2026-07-04) — pipeline binds / TBDR context-rolls
**BD's field binds ~208 distinct pipelines + ~1050 descriptor binds PER FRAME** (device-measured, the
`avg_vertices/pipeline_binds/descriptor_binds` draw-outcomes log). On the Adreno TBDR each pipeline bind is a
CONTEXT-ROLL (state re-emit); **208 × ~250µs ≈ 52ms ≈ the ~53ms AREA-INDEPENDENT frame cost** (gpu_diag_raster_ab:
full-raster 94ms vs quarter-area 53ms → 53ms doesn't scale with pixels). This is the immediate-mode-vs-TBDR gap:
those binds are ~free on desktop, expensive on a tile GPU. **NOT in the exhausted list** (draw-MERGE regressed
but that's draw COUNT, not pipeline VARIANT count; the audit gated bindless "confirm context-roll is a real GPU
cost first" — this is that confirmation, circumstantially). push_descriptors + cache_texture_descriptors already
default-on so descriptor binds are cheap; PIPELINE binds are the target. Pipeline key (vulkan_pipeline_cache.h:137)
= blend + VS/PS hash+mod + render_pass_key + topology; blend is the COLLAPSIBLE driver (code already notes "(future)
EDS3 dynamic blend emit" @:262). **FIX (BUILDING, gpu_dynamic_blend_state, general, no-gfx-loss): EDS3 dynamic
blend** - move blend enable/equation/writemask to VK dynamic state, zero blend from the pipeline key → blend variance
stops minting pipelines → fewer binds → fewer context-rolls. **BUILT + DEVICE-TESTED 2026-07-04 (gpu_dynamic_blend_state):
DEAD/INERT for BD.** EDS3 extension SUPPORTED on Thor Turnip ("dynamic blend enable/equation/writemask: yes") + engaged
+ render PIXEL-PERFECT (dynamic blend reproduces static exactly, mechanism CORRECT) BUT **pipeline_binds stayed EXACTLY
208 (unchanged), frame 7.7fps unchanged** = NOT ONE of BD's 208 pipelines differs only in blend → they are ALL distinct
by SHADER + render_pass (necessary). So blend is NOT a variant driver for BD; the tractable collapse is void here. The
208 binds are IRREDUCIBLE without collapsing the per-format SHADER variants = bindless vertex-fetch (LARGE) — AND it
remains UNPROVEN the 208 binds are even BD's cost (rigorous prior = foliage geometry; geometry levers also flat). **The
EDS3 feature is CORRECT + GENERAL (default-off, kept — may reduce binds on blend-varying titles, unvalidated) but is a
DEAD LEVER FOR BD.** ⇒ pipeline-bind reduction is not BD's reachable win.

## Custom Turnip driver-internals (the "general GPU fix" direction)
| Direction | Finding | Verdict |
|---|---|---|
| **Custom Turnip-internals patch (ir3 compiler / GMEM-tile / LRZ)** — general fix for all games | **DEAD (source-grounded agent audit, 2026-07-04, read /root/mesa Turnip+ir3).** Turnip is already NEAR-OPTIMAL: ir3 sync-scheduling is lazy+batched, tex latency hidden by warp-switch at 12-reg occupancy (the foliage VS's 10 syncs = inherent data-dependency, NOT a compiler bug); GMEM/sysmem autotune is bandwidth+overdraw-aware, render_area-clipped, with conditional tile-I/O + load/store elision (tile-I/O ~1ms, NOT the 79ms confound); LRZ keeps TEST live for alpha-test (only WRITE disabled), co-planar foliage is irreducibly non-occluding. The TBDR-mismatch is expressed THROUGH the Vulkan API (oversized areas, 42 passes, SSBO vfetch) and Turnip already translates each near-optimally. UNLIKE the KGSL fence fix (a genuine submit-path BUG the app couldn't influence), the render path has NO analogous unconditional inefficiency. | **DEAD — do not fund a driver build.** The only unused HW cap (bidirectional LRZ) is narrow+errata-prone, won't help BD. Real BD work is APP-SIDE (xenia): native geometry submission + internal-resolution scaling. |

## CPU / LLVM levers
| Lever | Result | Verdict |
|---|---|---|
| `cpu_backend_llvm_residency_writeback` | qemu 2624-assertion BYTE-IDENTICAL to a64; device opt=2 renders BD correct (windmill+field), UNHANDLED=0, no crash | **WIN — validated** (CPU/thermal; BD field is GPU-bound so not its peak; direct fps on CPU-bound titles). Perf-win magnitude UNMEASURED (cross-run confound). |
| `cpu_backend_llvm_context_residency` (write-through base) | renders; CLAUDE.md flagged an opt=2 crash (092eacdc3) but 2026-07-04 tests did NOT reproduce it | **OPEN** (works in recent tests) |
| `cpu_backend_llvm_lower_vmaddfp=false` (a805a09a9) | fixes the LLVM BD post-load cyan-bars (vmaddfp codegen interaction) | **WIN — default** |
| XMA idle-lock skip, global-lock lockfree, object-handle cache, kernel-call-skip, arm64_jit_inline_extern_thunk | simpleperf %-validated CPU wins | **WIN — shipped** |

## THE STANDING CONCLUSION (do not re-derive)
BD's field is **GPU-bound on foliage geometry** (262k verts × wind shader, shader-stats: 371 instr / 12 reg /
memory-fetch-heavy). Every SINGLE GPU lever above is DEAD/flat/gfx-loss because the overhead is **DIFFUSE**
(PM4 decode + register-file churn + emulated SSBO vfetch + shader bloat + EDRAM round-trips), not one layer.
⇒ **The mandate = REARCH THE PIPELINE TO HOST** (native D3D9→Vulkan, bypass the whole emulation stack). D3D-HLE
RE done: EndTiling @0x8248A680 is the field intercept; DrawRects2D @0x82488C10, FlushRenderState @0x82488670.
Whether the field is genuinely multi-TILED (→ bin-once helps) is being verified (EndTiling decompile).
