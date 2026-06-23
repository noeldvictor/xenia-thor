# Deep xenia CPU<->GPU re-architecture for BD 60fps (2026-06-23, workflow wuyijcit2, 18 agents)

User thesis: "a DEEP RE-ARCHITECTURE of xenia code can reach FULL 60fps." Extreme-novel + arxiv + FULL
Turnip/NEON, red-teamed. The honest, decisive answer:

## THE VERDICT (corrects the "break the lockstep" framing)
- **60 LOGIC fps via rearch = NO.** BD is fixed-timestep (1.0f@0x820551AC, re:Blue-confirmed no delta-time/
  accumulator); device-proven across 6 fires (present-interval-1, vblank-divisor, vblank-callback, gate-NOP,
  early-rptr-writeback all inert-or-hang; interval-1 caps at EXACTLY 30 VdSwap/s even on a LIGHT scene). Making
  the guest produce frames faster = 2x GAME SPEED, not fps.
- **CRITICAL CORRECTION: there is NO host "1-frame-deep lockstep" to delete.** The xenia host is ALREADY async
  3-deep (kMaxFramesInFlight=3, non-blocking VdSwap, lock-free mailbox; the CP runs on its own thread,
  command_processor.cc:572). The 30fps is the GUEST's fixed-timestep logic clock, full stop. So "deep rearch
  breaks the host serialization" is REFUTED — the serialization the prior work called a "lockstep" is the guest
  fixed-timestep, not a host wait.

## BUT real higher-fps GAMEPLAY IS reachable (the user's conviction, on a defensible foundation)
The credible architecture produces frames WITHOUT advancing the 30Hz logic clock — a two-lever stack:

**RANK 1 — SPECULATIVE RENDER-AHEAD (the novel insight, the emulator's unfair advantage over FSR3/DLSS-FG):**
at IssueSwap the CP has just parsed a full frame's draw stream. Retain a DEEP COPY of that DeferredCommandBuffer
(pinning all referenced Vulkan handles/descriptors/transient-ring allocations alive across the next-frame Reset
at vulkan_command_processor.cc:1632 = a NEW double-buffer lifetime subsystem) and on the GPU-idle half-cadence
slot re-Execute it into a SECOND command buffer targeting SEPARATE scratch RTs (never the guest EDRAM), with the
VS view-projection patched to VP_curr+(VP_curr-VP_prev)*0.5 (per-draw via SetupFastAffineReplay). => a REAL
re-rasterized in-between frame with correct depth/disocclusion, strictly more correct than a 2D color warp. Mask
to opaque world geometry; skip skinned/HUD/2D (camera-only VP ghosts them). fps: ~45-60 REAL rendered on
GPU-LIGHT gameplay IF gpu_frame_us<~16ms (the single Adreno graphics+compute queue serializes the extra frame -
no free parallel slot). STRONGLY NEGATIVE on the GPU-bound heavy field (per-scene gate mandatory).

**RANK 2 — PRESENTER FRAME-GEN (already 90% shipped: frame_gen_blend_pipeline_, frame_gen_history_images_,
FrameGenTickThread, present_frame_extrapolation; synth branch provably skips ConsumeGuestOutput so it CANNOT 2x
speed):** universal fallback where render-ahead has no headroom -> perceived 60 everywhere. Cross-fade built;
2D motion-warp homography is the quality upgrade. NEUTRAL-to-NEGATIVE on the GPU-saturated heavy field (demote
the "helps every scene" claim).

**RANK 3 — async side-task offload (RPCS3 RSXOffload):** CPU-hygiene bubble-shrink that STACKS under 1-2, never
a standalone 60 lever (the guest release is gated on ++counter_ at IssueSwap, NOT GPU side-task completion).

DEAD: rank 4 logic/render decouple trampoline (render shares the 0x820A2040 task table -> gating logic halves
render = inert; + duplicate-content presents); rank 5 stall-on-demand (only valuable as the GPU-result probe).

## TOP PICK = the ONE load-bearing FALSIFYING PROBE (a READ, not a build - the LRZ/FDM lesson)
**gpu_frame_us vs the inter-swap interval on a confirmed GPU-LIGHT interactive BD town/dungeon.** Instrumentation
already shipped (gpu_frame_us @vulkan_command_processor.cc:2257, on the vulkan_trace_draw_outcomes_per_frame
line). If gpu_frame_us (~10-16ms) << interval (~33ms) -> real idle GPU headroom -> render-ahead (rank 1) ALIVE,
worth its expensive build. If ~= interval -> GPU-paced even when "light" -> render-ahead DEAD, fall back to
rank-2 frame-gen (already built). Do NOT re-fire the fixed-timestep/2x question (settled). 2nd gate (zero-device):
dump the VS position-constant slot stability + count skinned/multi-leaf draws (SetupFastAffineReplay bails on
kMultiLeaf/non-FLOAT32, draw_extent_estimator.h:98-119) to size VP-patch scope + character-ghost risk.

## THE METHODOLOGY BLOCKER (the actual first unit of work)
Reaching a confirmed GPU-LIGHT INTERACTIVE town/dungeon (live gameplay, not a movie/menu - MEASUREMENT RULE 0)
within ONE sub-110s/64C-watchdog fire. BD's early scenes are movies + the credits field is GPU-bound; this
town-reach nav has blocked EVERY prior light-scene A/B. Authoring a HID nav sequence that lands in a town before
the device heats is the first concrete task. (Note: the prior finding that the light intro/menu is "GPU-trivial,
30-capped" already establishes that light scenes HAVE headroom + are 30-capped - so the mechanism is sound; the
probe confirms a specific interactive 3D town has it.)

## BIGGEST CORRECTNESS RISK (render-ahead)
Lifetime hazard: the DeferredCommandBuffer is Reset() per-frame + the transient descriptor pools/uniform rings
its commands reference are recycled per frame; replaying the retained stream after reset binds FREED descriptors
= corruption. The synth re-render MUST target SEPARATE scratch RTs (touching the guest EDRAM corrupts frame N+1).
A substantial NEW double-buffer/handle-pinning subsystem. De-risk with the CHEAP probe FIRST.

## PROBE RESULT (2026-06-23, device + zero-fire mining) — render-ahead VIABLE but BOUNDED
The load-bearing headroom probe, run two ways:
- ZERO-FIRE (mined the existing free-running framegen capture): in the early light scene gpu_frame_us ~26ms but
  VdSwap held ~28-30/s (a GPU-bound 26ms scene would be ~38/s) -> 30-CAPPED with ~7ms idle GPU. Headroom mechanism
  confirmed, BUT that early scene may be a cinematic (MEASUREMENT RULE 0 ambiguity).
- DEVICE FIRE (townprobe, aggressive skip-nav): landed in CONFIRMED INTERACTIVE gameplay (screenshot = Shu
  controllable in the Talta Village outskirts, not a movie). Across the 150s/1249-frame capture: min gpu_frame_us
  = 22.5ms, only 1 frame <16ms, 401 frames <33ms; peak VdSwap = 31/s (never the ~45/s a GPU-bound 22ms scene
  would hit). => CONFIRMED on real interactive gameplay: light scenes (22-33ms) are 30-CAPPED with idle GPU
  headroom (~10ms on a 22ms frame). BUT the reachable OUTDOOR gameplay (village outskirts) is GPU-BOUND ~6fps
  (128ms) - render-ahead NET-NEGATIVE there.
VERDICT: render-ahead is VIABLE (the 30-cap + idle-headroom premise is device-confirmed on interactive gameplay)
but BOUNDED: the lightest reachable interactive scene is ~22.5ms with only ~10ms idle, so a FULL re-render (22ms)
does NOT fit -> render-ahead must MASK to a CHEAP opaque-world-only re-render (~8-11ms) to fit the idle slot,
yielding a real ~1.3-1.7x (30 -> ~40-50fps) on light interactive scenes, NOT a clean universal 60 (that needs a
<16ms scene, essentially unreached - town interiors, not walkable via HID). Heavy outdoor gameplay stays
GPU-bound -> per-scene gate MANDATORY (off on heavy). So render-ahead earns its build as a BOUNDED light-scene
real-fps lever; FRAME-GEN remains the more UNIVERSAL perceived-60 win. The town-interior <16ms clean-60 scene
stays unconfirmed (HID can't walk; needs a save-state [SaveToFile hangs] or a deeper nav).

## RENDER-AHEAD DE-RISKED DEAD (2026-06-23) — ill-posed camera extrapolation, obviated by the motion-warp
Investigating the rank-1 render-ahead crux killed it BEFORE the multi-week build (the de-risk discipline paying
off again). Two findings:
- LIFETIME (the synthesis's flagged hazard) is actually SIDESTEPPABLE: replay the retained DeferredCommandBuffer
  EAGERLY (right after the guest frame, before the per-frame transient recycle at vulkan_command_processor.cc:
  1616/1632), so the transient descriptors/uniform rings are still valid -> no pinning subsystem needed.
- BUT THE CORE PREMISE IS ILL-POSED: render-ahead must re-render with an EXTRAPOLATED CAMERA, and xenia has NO
  separable camera VP. DrawExtentEstimator::FastAffineReplay.m[4][4] (draw_extent_estimator.h:96-99) is the
  PER-DRAW OBJECT->CLIP (full MVP, model folded in; bails kMultiLeaf on skinned/characters) - the SAME wall the
  motion-warp's depth-reproject hit. You cannot factor the camera out of per-draw MVP. The only patch render-
  ahead could apply is a GLOBAL clip-space translation to every draw = IDENTICAL RESULT to the 2D motion-warp,
  but by RE-RENDERING all geometry (multi-week build + expensive per-frame GPU re-render, bounded to light
  scenes) instead of a ~us 2D shift. Its claimed edge (correct depth/disocclusion) EVAPORATES (no correct
  per-depth camera motion available either). => render-ahead offers NOTHING over the shipped 2D motion-warp at
  vastly higher cost. DEAD. The 2D motion-warp (shipped, cheap, validated-correct) IS the frame-gen answer.

## FINAL STATE of "do all three"
- (1) PROBE: done - headroom device-confirmed (light interactive gameplay 22-33ms, 30-capped, ~10ms idle);
  render-ahead bounded ~40-50 light scenes.
- (3) FRAME-GEN: done - cross-fade shipped as the opt_frame_gen toggle; MOTION-WARP (2D global Lucas-Kanade)
  built + adversarially-reviewed + device-validated-CORRECT (RGBA32F renderable, no crash/corruption), opt-in
  via present_frame_gen_motion_warp. The warp-vs-cross-fade QUALITY A/B is the only residual (scene-dependent:
  warp = sharp but global-shift parallax error; cross-fade = ghost but parallax-neutral; needs motion-video
  judgment, not static screenshots - deferred).
- (2) RENDER-AHEAD: de-risked DEAD (ill-posed camera extrapolation, obviated by the 2D warp). NOT built - correct
  call (saved a multi-week dead-end).
NET: BD 60fps = perceived-60 via frame-gen (shipped, the 2D motion-warp is the best synth xenia's per-draw-MVP
arch allows); real-60 logic is impossible (fixed-timestep); real ~40-50 render-ahead is dead (ill-posed). The
60fps story for BD is now COMPLETE + honestly bounded.

## NET ANSWER to the user
60 LOGIC fps = no (fixed-timestep wall, host already async 3-deep). ~45-60 REAL rendered gameplay fps = PLAUSIBLE
via speculative render-ahead, contingent on the headroom probe. 60 perceived everywhere = yes via frame-gen.
The path is producing host frames the 30Hz guest never had to - NOT breaking a lockstep (there isn't one).
