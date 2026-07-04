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
| `gpu_flatten_predicated_tiling` (+`_widen`) | merge predicated tiles into one pass | BREAKS pixels — BD PARTITIONS draws across passes (pass A pitch360/msaa2 303 draws vs pass B pitch720/msaa1 1194 foliage), different configs can't merge | **DEAD** (naive flatten; bin-once must be at the D3D source = EndTiling HLE, OPEN) |
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
