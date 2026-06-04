# Blue Dragon / Turnip Speed — Execution Plan & the CLOSED GPU-reduction search (2026-06-03)

From the `bd-speed-execution-plan` workflow (w3nt5mcpc), which read the existing device A/B docs.

## Decisive device evidence — the GPU software-reduction search for BD is CLOSED
Content-matched, device-measured A/Bs (in docs/research/) close every GPU-reduction lever:
- **Draw batching (concat/MDI/R5): NET LOSS +8-14%** (20260602-mdi-device-ab-result.md).
- **Per-triangle cull (R4 amortized): +6% net loss / neutral** (20260603-cull-net-neutral-verdict.md).
- **Whole-draw frustum cull (R4 core): NEUTRAL** -0.1%..+2.0% while skipping 24% of verts (20260603-cull-family-closed-tbdr.md).
- **Present/pacing: premise FALSE** - GPU is **99% busy @ 680MHz (MAX clock)**, not 80% with an idle gap; no idle, no DVFS headroom to recover.

## Mechanism (why every GPU-side lever is dead)
Adreno 740 FlexRender TBDR: the binning pass runs the position-VS per vertex, then writes COVERED triangles to per-tile primitive lists. An off-screen triangle is frustum-rejected and written to NO tile list -> it already costs only the cheap position-VS, NOT the expensive per-tile writes. So CPU-side culling (R4) removes work the GPU was already doing nearly for free, while ADDING CPU. Merging draws (R5/MDI) doesn't help because the **~333us/draw floor is paid per SUB-draw regardless of submission**. (Render-pass-break reduction was also tested, B35: -34% render_pass_begins, ZERO fps move - so that's not it either.)

## The wall: ~333us/draw
Frame ~666ms at 99% GPU busy across ~2000 draws = **~333us/draw** for a ~130-vertex draw. That is pathologically high for raw binning compute -> it is almost certainly a per-draw **context-roll / state-relatch / stall**, NOT binning math. **WHAT it is cannot be determined headless** (perfetto blocked, no root). It requires a **user-run Snapdragon Profiler per-stage GPU capture** to split the 333us (binning-VS % vs per-tile-write % vs % stalled). Until that split exists, no GPU lever is targetable.

## Honest fps ceiling for BD's heavy vista
- R2 / R5 / R4 / present: **~0% fps on BD** (GPU-bound at 99% max clock; R4/R5 device-proven dead).
- The heavy vista is **structurally capped at ~1.5fps** by the ~333us/draw x ~2000-draw floor.
- 30/60fps is NOT achievable on the heavy vista via any software lever found. We never become CPU/fill/present-bound - the GPU stays the wall at 99%/680MHz.
- Reopens ONLY if: (a) a Snapdragon per-stage capture shows the 333us is a fixable per-draw STALL/state-relatch (not raw binning), or (b) the per-draw cost is cut at the guest-draw-STRUCTURE level (merge guest meshes upstream - outside the emulator's current reach).

## What actually ships (forward, not dead levers)
1. **R2 constant-elision pre-check** = CPU HYGIENE, NOT a BD fps lever. cvar gpu_skip_unchanged_constant_upload; wrap vulkan_command_processor.cc:6613 to early-return when current_constant_buffers_up_to_date_==kAllConstantBuffersMask (consts_same~47%). Expected BD fps: 0% (GPU-bound). Payoff = CPU headroom for the CPU-BOUND titles (Lost Odyssey), composes with R1/multi-cpu. Validate: pixel-identical + cpu_issuedraw_us/cpu_bind_us drop at matched rendered/avg_vertices. NOT an fps claim on BD.
2. **R5 DROPPED for BD** (no mechanism: per-draw floor per sub-draw; merge_vf_contig_=0 = zero eligible runs). Keep MDI cvar default-off as documented negative.
3. **R4 DROPPED for BD** (the cache OUTPUT - fewer binned verts - was already A/B'd directly via whole-draw cull = ZERO gpu_frame reduction; caching a no-win is a no-win). XL effort, device-proven-zero return.
4. **THE KEY UNBLOCK: request a user-run Snapdragon Profiler per-stage capture** of the heavy field vista. Zero engineering, one user GUI session. Success = a per-stage % breakdown of the 333us/draw. Every doc converges here.

## Forward priorities (where speed IS reachable)
- CPU-BOUND titles (Lost Odyssey): R1 affinity (shipped), multi-cpu thor_guest_thread_affinity_mask (shipped), R3 flat-membase fastmem, R6/R7 - these levers WORK on CPU-bound work.
- Verify BD's NON-vista gameplay fps (the ~1.5fps is the WORST heavy vista; most gameplay may be faster).
- R2 CPU hygiene (ship).
- BD heavy vista: blocked on the Snapdragon capture. Note honestly; do not grind dead GPU levers.
