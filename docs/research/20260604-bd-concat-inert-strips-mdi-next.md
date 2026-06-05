# Blue Dragon heavy scene: draw CONCATENATION is inert (strips); multiDrawIndirect (Lever 2b) is the right untested lever

Device baseline, BD desert-village gameplay (OSD 5.9 FPS, hole-free, NOT a movie:
rendered=1182, total_vertices=263181, avg=222). Safe fire: `vulkan_merge_draws` OFF
(bit-identical) + `--ei gpu_merge_vf_index_stride_fix 1` so the eligibility classifier
is correct. Evidence: scratch/thor-debug/turnip_mergebaseline_logcat.txt +
_frame.png + _top.txt (2026-06-04).

## The decisive numbers
- **Topology is ~99% triangle STRIPS:** `prim[pt=144 ll=0 ls=0 tl=0 tf=0 ts=1014 rect=10 quad=16 poly=0]`.
  ZERO triangle lists. Concatenation (`vulkan_merge_draws`) is HARD-GATED list-only
  (strips would stitch across draw boundaries — design doc correction #1).
- **`elig_runlen[1=0 2=0 ... 65+=0]` = ALL ZERO.** The true-eligible-run histogram
  (list-only + contiguous + same-pipeline, computed at baseline regardless of the cvar)
  is completely empty. **Draw concatenation has ZERO eligibility on Blue Dragon.**
  The Steps 1-4 build-complete `vulkan_merge_draws` lever is INERT on BD.
- Same-pipeline runs ARE long (`runlen[1=95 2=31 3-4=29 5-8=18 9-16=9 17-32=11 33-64=9]`)
  — 9 runs of 33-64 same-pipeline draws — but the list-only gate kills every one (strips).
- Contiguity is also only ~40% even with the stride-fix: `vf[same=0 contig=386 scattered=568]`.
- **Confirmed GPU-bound** (so the per-draw cost is real GPU binning, worth attacking):
  top shows NO pegged thread (656/800 = 82% idle; busiest = XMA Decoder 32%, GPU Commands 21%),
  gpu_busy ~80%, and cpu_setup 130ms ≈ gpu_frame 128ms (CPU IssueDraw thread is awaiting the GPU).
  ~7.8fps GPU-time (OSD 5.9 end-to-end). 1182 draws/frame binned on the Adreno front-end.

## So concatenation can't be the BD lever. The right lever is Lever 2b (multiDrawIndirect).
`vulkan_merge_draws_indirect` (gpu_flags.cc:512) is build-complete and FULLY WIRED
(vulkan_command_processor.cc: mdi_supported_ gate @424 requiring vkCmdDrawIndexedIndirectCount;
per-frame VkDrawIndexedIndirectCommand[] ring @417-431; accumulate+emit path @4570-4668;
mutually exclusive with vulkan_merge_draws @5803). Per its own description it batches
consecutive same-state kGuestDMA draws into ONE vkCmdDrawIndexedIndirectCount(drawCount=N)
where **each sub-command is a SEPARATE draw — so it ALSO batches triangle STRIPS (no
cross-draw stitching) and NON-contiguous index ranges (per-command firstIndex) — exactly
the Blue Dragon case (all strips, scattered).** It is UNTESTED on device.

## NEXT-CYCLE EXPERIMENT (content-matched A/B on the BD heavy scene)
Open question Lever 2b answers: does an Adreno/Turnip vkCmdDrawIndexedIndirect(drawCount=N)
amortize the per-draw FIXED binning cost across its sub-draws (GPU win), or only the CPU
submission cost (no win, since BD is GPU-bound not CPU-submit-bound)? HW/driver-dependent
— only device tells.
1. BASELINE (this file): merge OFF, gpu_frame_us ~128ms @ rendered=1182.
2. MDI-ON: relaunch same scene with `--ez vulkan_merge_draws_indirect true` (+ trace).
   Read gpu_frame_us, the new MDI run counters, screenshot MUST be hole-free/identical.
   - If gpu_frame_us DROPS materially: Lever 2b is the BD GPU win concatenation couldn't be
     -> ship it as a stacking XeniaOptimizations toggle in the bluedragon profile.
   - If gpu_frame_us is flat: Adreno bins each MDI sub-draw at full cost -> per-draw GPU cost
     is irreducible by batching -> the ONLY BD GPU lever is triangle CULL
     (gpu_cull_compaction, already ~13% GPU-proven 91a85f382; blocker = CPU cost: half-float
     k_16_16_16_16_FLOAT position decode + fewer M-recovery samples + NEON, per
     20260603-fast-cull-gpu-frame-drop.md).
Use a frozen frame (gpu_freeze_at_guest_ms) for a clean content-matched compare.
