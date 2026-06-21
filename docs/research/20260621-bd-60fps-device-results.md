# BD 60fps multi-track — device results + grounded plan (2026-06-21)

Device session + workflow w1rpixw2e (4-agent) outcomes for the "do all three tracks" push.
Full workflow plan: tasks/w1rpixw2e.output. Frame-gen impl detail: 20260621-frame-gen-design.md.

## Track 3 (GPU efficiency) — DEVICE A/B results
Device: AYN Thor wifi, turnip, thor_gpu_capture.ps1 -NoDump, draw-outcomes trace, dense field nav.

**FP10 (gpu_fp10_color_as_unorm10) — VALIDATED ~10.5ms / ~8.5% win, SDR-correct.** Matched-scene A/B
(identical opaque=137/opaque_verts=41691, ~250.8k verts):
| config | rendered | alphatest | blended | gpu_frame_us |
|---|---|---|---|---|
| baseline (off) | ~1030 | ~395 | 356 | ~123.0 ms |
| FP10 on | ~1107 | ~427 | 399 | ~112.5 ms |
FP10-on is ~10.5ms FASTER while rendering MORE overdraw draws -> true like-for-like win >=10.5ms. Screenshot
(credits field) visually correct, no banding. BUT: it CLAMPS 7e3 >1.0 to [0,1] -> will clip bloom/HDR on
bright scenes (battles). Verdict: **keep default-off toggle** (SDR tradeoff); it's an upper-bound on what a
CORRECT 32-bpp 7e3 path (the deferred Rank-1 below) could save. Do NOT default-on.

**UBWC (gpu_vulkan_rt_keep_ubwc) — renders CORRECTLY (no corruption) but timing SCENE-CONFOUNDED this
session.** The UBWC run drifted to a much heavier scene (rendered ~2200, ~293k verts, alphatest ~1230) vs the
~1030-1107 baseline -> ~130-170ms not comparable to ~123ms. BD boot non-reproducibility. **Needs a
frozen-scene A/B (gpu_freeze_at_guest_ms, same value both runs) from a COLD start to size.** It is the
lossless ship-candidate (UBWC is bit-exact) -> promote if the frozen A/B shows margin + bit-identical pixels.

## Track 2 (guest-route 60fps) — CLOSED: no clean guest lever
Workflow + device fully closed the pacing model. The 30Hz is a DISTRIBUTED producer->consumer + vsync
handshake, NOT a single pokeable wait:
- Host feeds 60Hz vblank (graphics_system.cc:163-195). Guest is fixed-timestep 30 logic (1.0f@0x820551AC).
- Present worker = dedicated thread (entry 0x82488148, ExCreateThread @0x82488628), waits INFINITELY on a
  per-thread KEVENT (ctx+i*0x38+0x2bdc) for the producer's KeSetEvent (0x82485360 in GPU-kick 0x82485100).
  The 30ms KeWaitForSingleObject (@0x82488160) is a WATCHDOG, not the cadence.
- bdRenderStep 0x82132F10 = NtSetEvent producer SIGNAL (device-proven: NOPing it hangs BD).
Every isolated poke is verified-inert (present interval 0x8246AB68; 30ms watchdog) or a proven hang (removing
the producer signal). The only faint candidate = the logic/render decouple trampoline (gate bdMainGameStep
0x82126AF8 to even frames via code-cave @0x820C5744 + frame counter @0x820C5750) but render shares the task
table (0x820A2040) so it likely halves render too -> a <=30-min falsification test, NOT a build. **Verdict:
frame-gen is the only robust 60fps path.**

**DEVICE-CONFIRMED 2026-06-21 (interval-1 isolation test):** patched ONLY 0x8246AB68=li r10,1 (no NtSetEvent
NOP this time), fired BD. The light EARLY phase (intro/menu, GPU-trivial) caps at **~30 VdSwap/s**, then
declines into the GPU-bound field (~6/s). It NEVER exceeds 30. Since the render loop calls VdSwap, a loop
pinned at 30 with interval-1 SET proves the 30 is enforced UPSTREAM of the present = logic-side (the
one-present-per-logic-frame producer gate), NOT the swap interval. **Present-interval-1 is INERT even on a
light scene (not just the GPU-bound field) - the user's "interval-1 unlocks light 60fps" hypothesis is
REFUTED on device.** Combined with the gate-NOP hang, every isolated clean guest poke is now device-proven
inert-or-hang. The clean single-edit guest 60fps lever does NOT exist; only the fragile fixed-timestep-retune
+ battle-counter grind (~25%) or the likely-blocked decouple trampoline remain. Frame-gen stands as the path.

## Track 1 (frame-gen) — grounded first-increment plan (the path)
Format note: the presenter intermediate is ALREADY A2B10G10R10_UNORM_PACK32 (kGuestOutputFormat). Effects are
GRAPHICS passes (not compute); shaders are offline-built (xb buildshaders -> committed SPIR-V .h).
CORRECTION to the design doc: there is NO existing "paint tick with no new guest frame" hook -> a SCHEDULER
thread must be added (increment 2). Increment 1 is safe to build blind:
- cvars present_frame_extrapolation(bool,off) + present_frame_gen_factor(int,2): presenter.cc:37-84,
  declare presenter.h:48-50; allowlist + XeniaOptimizations entry.
- GuestOutputImage::Initialize (vulkan_presenter.cc:1711): add optional usage_flags param (default unchanged).
- new members after vulkan_presenter.h:491: frame_gen_history_images_[2], frame_gen_synth_image_,
  valid_count, last_submission, extent; lazy-alloc at frontbuffer size; destroy after AwaitAllSubmissions
  (vulkan_presenter.cc:546-547).
- history copy in PaintAndPresentImpl: vkCmdCopyImage consumed guest image -> ring, in the open
  draw_command_buffer after :2126-2132 (barrier pattern from CaptureGuestOutput :716-758); GUARD on the cvar
  -> zero work when off (byte-identical default path).
Increment 2 (device-tuned): scheduler thread (model DXGIUITickThread presenter.cc:623), synth branch at
vulkan_presenter.cc:1863-1881 (skip ConsumeGuestOutput, set guest_output_image=synth, reuse effect flow),
50% cross-fade blend FS first (guest_output_frame_blend.ps.xesl), then optical-flow. HUD ghosting is a known
color-only limitation (accept first pass).

## Additional stackable efficiency wins (ranked, vulkan_render_target_cache.cc)
1. CORRECT 32-bpp 7e3 (lines ~1872-1886): map 7e3 -> R32_UINT integer-aliased transfer, PS emits 7e3 via
   UnclampedFloat32To7e3. Halves bandwidth AND bit-exact -> SUPERSEDES FP10. Higher effort (PS output pack).
   Defer until frame-gen validates.
2. 10-bit non-float fix (~1869-1871): k_2_10_10_10 currently -> 8-bit A8B8G8R8 (a 10->8 DOWNGRADE); remap to
   A2B10G10R10_UNORM_PACK32. Pure quality, identical bandwidth, lowest risk. CODE REGARDLESS.

## Recommended order (next sessions)
P1 UBWC frozen-scene A/B (cold) -> promote if win. P2 code the 10-bit non-float fix (Rank 2, blind+1 A/B).
P3 frame-gen increment 1 (blind, host-build). P4 frame-gen increment 2 (cross-fade, device-tune). P5 correct
32-bpp 7e3 (Rank 1, after frame-gen). Track 2 = closed.
