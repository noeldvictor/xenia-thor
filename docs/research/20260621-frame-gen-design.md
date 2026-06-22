# BD 60fps via frame generation — grounded design (2026-06-21)

Context: BD game logic is fixed-timestep 30Hz (see 20260621-bd-60fps-surgical-re.md). The robust way to get
60fps *visuals* without touching game logic is to synthesize in-between frames. This doc grounds the build in
the ACTUAL xenia presenter architecture (read src/xenia/ui/presenter.h this session) and picks the approach.

## The presenter architecture (what we have to work with)
- **3-image mailbox** of guest output (`kGuestOutputMailboxSize == 3`): the GPU-emulation thread REFRESHES
  (writes) the `writable` image via RefreshGuestOutput; painting CONSUMES the `ready`/`acquired` image via
  ConsumeGuestOutput. Lock-free SPSC + consumer lock (presenter.h:925-958).
- **Already supports presenting more often than the guest** (presenter.h:110-112,137): paint can run from the
  guest-output thread immediately (low latency, no UI) OR from the UI paint event; the design explicitly
  contemplates "presenting a 30 or 60 FPS guest to a 144 Hz host surface." So an EXTRA synthetic paint between
  guest refreshes fits the existing model — the hook is "a paint tick fires with no new guest image."
- **PaintAndPresentImpl** is the present primitive; the guest output is an intermediate image at frontbuffer
  size, then paint-flow effects (bilinear/CAS/FSR) + letterbox are applied (presenter.h:130-142).

## THE CRITICAL CONSTRAINT (reshapes the plan)
**The presenter sees COLOR ONLY.** `RawImage` is R8 G8 B8 X8 (presenter.h:86-92); `GuestOutputProperties`
carries only width/height/aspect/is_8bpc (presenter.h:364-389). There is **no depth buffer and no camera /
view-projection matrices** at the presenter layer — those live upstream in the GPU command processor / render
target cache (where SetupFastAffineReplay etc. operate). So the memory's "depth-reprojection warp" plan is a
CROSS-LAYER change, not a presenter-local one.

## Three approaches, ranked for THIS codebase

### A. Optical-flow INTERPOLATION (self-contained, recommended first build)
Hold the last two guest color frames (N-1, N) in the presenter; a GPU compute pass estimates motion (block-
match or pyramidal flow) and synthesizes the midpoint frame, presented between N and N+1.
- PRO: fully self-contained in ui/vulkan/vulkan_presenter (operates on the existing color mailbox); NO GPU-
  layer plumbing; works for ALL titles, not just BD; no guest RE.
- CON: adds ~1 guest-frame of latency (must have N+1 to interpolate the middle of N..N+1 — i.e. present N,
  then synth-mid, then N+1); flow quality is the hard part (artifacts on disocclusion, transparencies, HUD).
- COST: 2 history images + a flow + a blend compute pass at present res. On a740 at 1280x720 this is cheap
  (~ms); the engineering risk is flow QUALITY, which needs device iteration to tune.

### B. Optical-flow / motion EXTRAPOLATION (self-contained, no added latency)
Hold N-1, N; estimate flow; EXTRAPOLATE forward to synthesize N+0.5 presented immediately after N (no waiting
for N+1).
- PRO: self-contained; NO added latency (better for a handheld/input feel).
- CON: extrapolation guesses the future → more artifacting than interpolation on direction changes; same flow-
  quality challenge.

### C. Depth-reprojection warp (highest quality, cross-layer, biggest build)
Plumb the guest depth buffer + per-frame view-projection delta from the GPU command processor to the
presenter; a compute pass reprojects N by the camera delta + fills disocclusions.
- PRO: highest geometric accuracy for camera motion (the dominant motion in 3D scenes).
- CON: large CROSS-LAYER change (GPU RT cache -> presenter data flow for depth + matrices); depth is in EDRAM/
  host RT formats (D32S8) needing conversion; per-object motion (characters) still needs separate handling;
  HUD must be excluded. Most engineering + most device tuning.

## Recommendation
**Build A (interpolation) first as the cvar-gated, self-contained foundation** — it's the cleanest fit for the
color-only presenter mailbox, needs no guest RE or cross-layer plumbing, and benefits every GPU-bound title.
Accept the +1-frame latency initially; if input feel suffers on device, switch the same machinery to B
(extrapolation) — they share the history buffers + flow compute, differing only in the synthesis time target.
Defer C (depth-reproject) unless A/B's flow artifacts prove unacceptable AND the cross-layer cost is justified.

## First code increment (safe, reusable for A or B)
1. cvar `present_frame_extrapolation` (default-off) + an int `present_frame_gen_factor` (2 = one synth frame
   per guest frame).
2. In vulkan_presenter: allocate 2 history color images (prev, prevprev) at frontbuffer size; after each guest
   paint, copy the consumed guest output into the history ring. Inert until the synth pass is enabled.
3. The synth hook: when a paint tick fires with NO new guest image (the existing "present more often" path),
   if frame-gen is on and 2 valid history frames exist, run the (initially passthrough) synth compute pass and
   present its result instead of re-presenting N.
This increment is build-verifiable and behavior-neutral with the cvar off. The flow + blend compute shader is
the next increment and is where DEVICE iteration begins (quality tuning).

## Open decision for the user (latency vs feel)
Interpolation (A, +1 frame latency, smoother) vs extrapolation (B, no added latency, more artifacts). For an
RPG (BD) where input latency is less critical than smoothness, A is the better default; for action titles
(Burnout) B may feel better. The machinery is shared, so we can ship both as a toggle. Defaulting to A.

## HUD/UI caveat (applies to all approaches)
Frame-gen warps the whole frame including the 2D HUD, which has no real motion → HUD ghosting/wobble. Mitigation
(later): the guest 2D HUD is typically composited last; without per-element info the presenter can't cleanly
exclude it. Acceptable for a first pass; a depth/stencil-aware mask (approach C territory) fixes it properly.
This is a known frame-gen limitation, documented so it's not a surprise on device.

## DEVICE RESULT — Increment 2 (cross-fade blend) VALIDATED (2026-06-22, commit fb0b2fefd)
Built the 2-input cross-fade blend pass (InitializeFrameGenBlend + RecordFrameGenBlend in vulkan_presenter):
the synth path now blends history N-1/N-2 (mix 0.5) into a synth target via the committed sampler-less
guest_output_frame_blend_ps FS, reusing the rect VS + intermediate render pass; no extra cross-submission sync
(paints mutex-serialized + 3-deep fence throttle on a single queue + the history copy's transfer->shader-read
post-barrier); one blend descriptor set per submission slot. Build clean (gradle, native arm64).
DEVICE (BD heavy field, 192.168.1.32:5555, present_frame_extrapolation=true gen_factor=2): field renders
CORRECTLY (crisp, no corruption/black/flicker), NO Vulkan validation errors / device-lost / crash / SIGSEGV,
blend pipeline created OK (no failure log), reached the full field (rendered~1201, total_vertices~251k,
alphatest~534, blended~386), gpu_frame_us ~124-136ms unchanged (synth blend is a separate cheap present, does
not touch real-frame GPU cost). HONEST: cross-fade = modest smoothness w/ ghosting (no MVs); perceived fps only
(no input-latency reduction). NEXT increment = motion-warp (depth-aware MV splatting, Mob-FGSR-style), reusing
this 2-input synth-pass infra. Default-off cvar.
