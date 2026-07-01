# BD-30 EDRAM lever synthesis (2026-07-01)

Consolidates this session's device + research findings into an actionable next-session plan
and an honest ceiling analysis. Goal unchanged: Blue Dragon 30fps via the EDRAM/fusion solve;
other games 30-60. Do NOT re-walk the dead-ends below.

## The problem, precisely
BD's field renders ~42 render passes, most breaking because post-process **composites read a
producer RT from an EARLIER pass at a TRANSFORMED texcoord** (not same-pixel). On the Adreno 740
TBDR each such break evicts GMEM to DRAM + reloads (the ~74ms tile-I/O, device-isolated via
`gpu_edram_passes_dont_care` 128->49ms). Desktop immediate-mode GPUs ("fable") pay none of this —
they read the producer from VRAM directly. That architectural gap is the whole difficulty.

## Lever verdicts (evidence-backed — do not re-chase the DEAD ones)
- **Same-pixel fusion (feedback_merge / VK local_read / ROAA): DEAD for BD.** Composites sample at
  transformed texcoords; subpassLoad/input-attachment force gl_FragCoord => garble (device: prior
  `merges=1 of 26` + block corruption). 3-agent research confirms the law: offset/transformed
  sampling cannot use any same-pixel on-chip read. Turnip also lacks `dynamic_rendering_local_read`.
- **cap=1 (MSAA 2x->1x, -42% GPU): DEAD (intrinsic garble).** EDRAM tile addressing is tied to the
  sample count; halving samples changes tiles-per-row so shared guest bases (2x main @0, 4x effects
  @810) alias. Not fixable per-site or by base-remap (addressing is sample-coupled).
- **`inpass_edram_transfers=1`: SAFE but MODEST.** Device: `inpass[x=22]` folds 22 transfer-dest
  passes, renders correctly, but `brk_img_sr` stays 42 — it folds the transfer PASS, not the
  RT-source->SHADER_READ barrier. Keep as a stacking micro-lever; not a BD-30 lever alone.
- **skip_bloom / consumer-draw skips: INERT.** Xenia has no lazy-resolve; the composite PASS runs
  even with 0 draws (opaque dst=ZERO). Only producer-PASS elimination helps.
- **Full-atomic buffer path for the WHOLE scene: likely LOSES.** Software ROP on BD's heavy overdraw
  (844 draws) is costlier than the tile-I/O it saves (Xenia ships host-RT because "the FSI path is
  much slower now"). So do NOT route the main scene through the buffer.

## The correct architecture (the hybrid — partially built, default-off)
Main overdraw-heavy scene stays **host-RT (hardware ROP, GMEM)**; the **zero-overdraw composites**
(full-screen, 1x coverage, races harmless) route through the **EDRAM buffer path** with
arbitrary-address reads (handles the transformed producer reads). Built: `gpu_vulkan_hybrid_postprocess`
renders BD correctly with brk_img_sr 42->21. This is the right shape and the user's "fusion pipelines."

### Actionable next-session steps (device COOL, fresh context — the SPIR-V is delicate)
1. **Complete composite routing:** route all 26 `rtfc` composites (not just 21) through the buffer
   path. Detection = the existing fscomp classifier; the remaining ~5 are interleaved with depth/shadow
   passes (harder ordering). Target brk_img_sr 42 -> ~16.
2. **Atomic-ROP core for composites only** (races are harmless for 1x-coverage composites, so this is
   LOW-risk vs the main scene): drop `OpBegin/EndInvocationInterlockEXT` (done, gated on
   `edram_fsi_no_hardware_interlock_` at spirv_shader_translator_rb.cc:734/1316); the depth/color RMW
   for composites needs no atomics (barrier-separated). So the composite buffer path may need NO
   atomicMin at all — validate the plain-store composite path renders correctly first (cheapest test).
3. **Do NOT naively stack `inpass_edram_transfers=1` with hybrid** — device-tested 2026-07-01:
   they FIGHT over pass boundaries (hybrid ends the host pass per composite; inpass folds transfers
   INTO the guest pass) → brk_img_oth exploded 21->254, pass_break_barrier 15->137, + black-streak
   CORRUPTION. To combine, inpass must be suppressed on the passes/frames the hybrid rewrites, or the
   composite routing must not fragment transfer passes. inpass ALONE (clean host-RT) is safe.
4. Measure with a SINGLE-RUN alternating A/B (never cross-run gpu_frame_us).

## Honest ceiling (why even complete fusion may not reach 30)
Device decomposition: complete composite fusion removes the composite tile-I/O (~13-26ms), but a
**~66ms residual of NECESSARY rendering remains** (geometry/binning ~19ms + per-sample hardware ROP
on the overdraw + the main-scene depth/shadow-pass tile-I/O + clears). 33ms (30fps) requires cutting
that residual too. Candidate residual levers (separate, unproven): safe color STORE_OP_NONE elision
(extend `gpu_vulkan_skip_unused_depth_store` to color), foliage overdraw reduction (VRS ships -22%),
and — the one research-grade direction that could break the ceiling — **compute post-process**: run
the composites as compute dispatches reading/writing the EDRAM buffer, eliminating their passes
entirely (needs a compute variant of the translated composite shaders = the "frame EDRAM-I/O
recompiler"; large but the only path that removes composite passes without any ROP tradeoff).

## Other games (requirement #2) — status
Regression fix `52c16faff` restored the default host-RT init path (was SIGABRT-ing every non-hybrid
launch). Device-validated in-target + correct: **Burnout Revenge** (~30-42fps, Car Select) and
**Back to the Future** (31.6fps, DeLorean/mall). Untested/available: Gears 1/2/3/Judgment, Lost
Odyssey, Infinite Undiscovery, Magna Carta 2, Project Sylpheed (spot-check when device is cool).
