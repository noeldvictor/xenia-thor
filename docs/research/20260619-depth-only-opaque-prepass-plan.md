# Opaque depth-prepass: black-frame fix + depth-only plan + the load-bearing falsifier (2026-06-19)

## ⛔ FALSIFIER RESULT (2026-06-20): LEVER DEAD — do NOT build Part B
Part A (black-frame fix) VALIDATED on the recovered device: force_depth+prepass now renders the BD town
CORRECTLY (1.9MB png, Shu in the town, vs the earlier 15K black) - the host-state-tracker desync fix works.
**But the falsifier KILLED the perf thesis.** Three free-running town captures (composition noted):
- baseline (no prepass/fdepth): ~127ms @ alphatest~470
- prepass-only (no fdepth):     ~131ms @ alphatest 521-527 (MORE foliage)
- prepass + force_depth:        ~137.5ms @ alphatest 469 (FEWER foliage)
Adding force_depth made it SLOWER (137.5) than prepass-only (131) even though the force_depth run had FEWER
foliage draws -> **foliage discard does NOT early-Z-reject against primed depth on Adreno 740.** This CONFIRMS
CLAUDE.md ("alpha-test defeats Adreno LRZ") and REFUTES the Lever-A latest-research hope ("LRZ-TEST survives
discard") for BD's foliage on this HW. The full-color prepass overhead itself is small (~+4ms), so opaque is
cheap - but there is no foliage-reject benefit to capture, so a cheaper depth-only Part B would still net ~0
(no reject) -> **DO NOT BUILD PART B.** Caveat: free-running (scene-confounded, alphatest 469 vs 521); a
gpu_freeze matched A/B would be airtight, but the direction is consistent + decisive across 3 runs and the
adversarial `actually-cheaper` lens (sound=False) predicted exactly this. BD overdraw answer stays the SHIPPED
foliage/blended thinning toggles (2.2x stacked) + VRS (coarse-shade, unvalidated). Part A black-fix is kept
(a real bug fix: the prepass scaffold no longer black-screens) but the prepass has NO perf value on BD.
force_depth stays default-off (no-op-to-negative).

---


An adversarial design workflow (8 agents, 770s) diagnosed why `gpu_opaque_depth_prepass` black-screened +
ran ~8% slower on BD, and designed the fix. Two of three verify lenses returned **sound=False** — the
findings below fold in their corrections. **The win rests on an UNRESOLVED hardware question; gate it.**

## Black-frame ROOT CAUSE (fixed — Part A, shipped)
NOT a dangling handle / broken render pass. It's a **host-state-tracking desync at the splice.** The
deferred command stream records **emit-on-change** against tracked `current_*` state. `EmitOpaquePrepassDraw`
records a **self-contained** block (rebinds pipeline + all descriptor sets + all dynamic state + index
buffer) into `prepass_command_buffer_`. `EndRenderPass` splices that block to the **front** of the pass. At
execute time the spliced block leaves a full set of binds in effect, but the **first original color command**
was recorded assuming the normal fresh-pass leading state (tracker reset by BeginSubmission), so it does NOT
re-emit those binds → it executes against the prepass block's inherited (wrong) pipeline/descriptors/dynamic
state → **black/garbage.**
- **FIX (Part A, done):** in `BeginRenderPass`'s `if (cvars::gpu_opaque_depth_prepass)` block, invalidate the
  host-state tracker (mirror the BeginSubmission reset, EXCLUDING `current_render_pass_`/`current_framebuffer_`
  which BeginRenderPass owns): null the pipeline/layout handles, zero `current_graphics_descriptor_sets_bound_
  up_to_date_`, set the non-EDS `dynamic_*_update_needed_` flags true. The first color draw then re-emits all
  binds, overwriting the spliced block's leftovers. cvar-gated default-off.
- Part A makes the prepass produce a **correct frame** but it is still the **full-color reorder** (double
  opaque color render) → still slow. Part A alone is the falsifier vehicle + the prerequisite for Part B.

## ⚠️ THE LOAD-BEARING FALSIFIER (gate Part B on this)
The `actually-cheaper` lens (sound=False): **the entire payoff rests on alpha-test foliage early-Z-rejecting
against primed opaque depth — which CLAUDE.md + the prepass doc say Adreno LRZ DEFEATS for discard, but the
Lever-A latest research (ARM/Mesa/Qualcomm 2024-25) says LRZ-TEST survives discard.** This contradiction is
unresolved. ALSO: the prepass DOUBLES opaque binning+raster (~73k opaque verts, ~25% of submitted verts)
binned TWICE on a TBDR = real added cost. **Run the falsifier BEFORE building Part B:**
- With Part A (correct frame) + the current full-color prepass + `gpu_foliage_lrz_force_depth=true`, gpu_freeze
  A/B vs baseline at a matched frozen frame. Opaque is ~18% of the BD frame, so the full-color prepass adds
  ~+18% (one extra opaque render). Read the delta: if `gpu_frame_us(prepass-on) ≈ baseline + ~18%` → foliage
  rejects ~0 (LRZ defeated for discard, **lever DEAD, do not build Part B**). If notably **less** than +18%
  (or below baseline) → foliage IS early-Z-rejecting → build Part B to remove the double-color cost for the
  real win. First A/B must also confirm the frame is now CORRECT (no black) — the load-bearing Part A gate.

## Part B — depth-only sibling (build ONLY if the falsifier shows foliage rejects)
Remove the double-color cost so the prepass is a cheap depth prime. WITH the verify corrections:
1. `vulkan_pipeline_cache.h`: add `uint32_t is_depth_only : 1;` to PipelineDescription (2nd word after
   stencil_back_compare_op:3; auto-keys via the struct's memcmp/XXH3 over sizeof, Reset memsets 0). Add
   `bool force_depth_only = false` trailing param to `ConfigurePipeline`.
2. `vulkan_pipeline_cache.cc` GetCurrentStateDescription(force_depth_only): set `is_depth_only=1`, force
   `depth_write_enable=1`, **KEEP `depth_compare_op` = the guest's normalized zfunc (do NOT force LESS/LEQUAL —
   reverse-Z mis-primes; correctness fix)**, and **FORCE STENCIL OFF in the sibling** (`stencil_test_enable=0`,
   all ops `kKeep`, compare `kNever` — mirror the EDS stencil-zeroing block; else the sibling double-writes
   stencil = corruption). Make the force_depth_only key mutation **mutually exclusive with the
   vulkan_dynamic_state_depth EDS-zeroing** (only in the `else`, or assert EDS-depth off — the prepass is
   already EDS-off gated).
3. `vulkan_pipeline_cache.cc` EnsurePipelineCreated colorWriteMask (:2305): `(... || description.is_depth_only)
   ? 0 : ...`. Do NOT touch the FS select — keep the real guest PS (preserves alpha-test/discard; free).
4. `vulkan_command_processor.cc` EmitOpaquePrepassDraw (:7316): get the depth-only sibling via a second
   `ConfigurePipeline(..., force_depth_only=true)` and bind THAT handle (same layout → bound descriptor sets/
   offsets stay valid). Order the call so it doesn't thrash the `last_pipeline_` single-entry fast path
   (call depth-only BEFORE the color ConfigurePipeline, or cache the sibling, so steady-state color draws keep
   hitting the fast path). Skip the sibling when the pass has no color/depth attachment.
5. Keep the original in-place opaque color draw (:5872) — it tests the primed depth, shades color once.

## Honest ceiling (the adversarial skeptic's case)
Even built perfectly, Part B may NOT net-win: (a) Adreno may not early-Z-reject discard foliage (the
falsifier decides); (b) it doubles opaque binning on a TBDR (real cost the depth-only fix doesn't remove);
(c) it can't help the 34% blended overdraw or unoccluded foliage. The known-cheap shipped wins
(gpu_foliage_thin_factor + gpu_blended_thin_factor, 2.2x stacked) attack the same overdraw without this risk.
So: Part A (black fix) is banked; Part B is GATED on the falsifier; if the falsifier is null, this whole
opaque-depth-prepass line is DEAD and the foliage/blended thinning toggles remain the BD overdraw answer.
