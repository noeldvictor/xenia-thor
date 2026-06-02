# Cull-prize sizing — device-measured ~56% of triangles cullable (2026-06-02)

After the MDI A/B proved draw-call batching can't touch the ~300us/draw BINNING floor
(docs/research/20260602-mdi-device-ab-result.md), the only lever that lowers that floor
is reducing the TRIANGLES the Adreno binning pass processes. The Adreno bins EVERY
triangle (runs the position VS per vertex) BEFORE rasterization, so back-facing /
off-screen triangles are binned and only culled later at raster — wasted binning work.
A pre-binning cull would eliminate it.

## Measurement
Extended the read-only `gpu_trace_cullable_tris` counter to triangle STRIPS (commit
1bc8f7883 — it was list-only, and BD is strip-dominated so it read 0). Device capture
`docs/evidence/20260602-122319-cullable-strip-size.txt` (Blue Dragon intro):

- `cullable_tris=53835` over `total_vertices=96451` (strips → ~95.8k triangles)
  => **~56% of triangles are cullable** (conservative frustum same-side-clip OR
  homogeneous-determinant backface). Matches theory: ~50% backface on closed meshes +
  some frustum.

## Caveats (honest)
- The counter does a full interpreted VS position-replay PER DRAW: `cpu_other_us=920935`
  (~927ms/frame). That bogged the CPU so the frame only emitted 519 of ~1800 draws before
  swap (screenshot is a dark/partial frame) and dropped to ~1.2fps. The CULL FRACTION is a
  geometric property and robust to the partial frame; the absolute count and that run's
  fps/gpu_frame_us are NOT representative.
- Backface sense uses a det>0<->CCW assumption (+strip winding parity by index parity, not
  re-based after primitive-restart). For a SIZING magnitude (~56%) this is robust; an exact
  cull must verify winding/guard-band against the GPU.

## Implication
The cull prize is LARGE (~half the binning work). This justifies building a real
pre-binning triangle cull — the held, guest-visible lever — but with two hard constraints
the measurement also exposed:
1. **Must be CHEAP.** The interpreted VS replay (~927ms/frame) is far too slow for a
   production cull — it would make the frame CPU-bound, defeating the purpose. The cull
   needs either a GPU compute pre-pass (cull on device, compact indices, indirect draw) or
   a fast NEON CPU path over already-transformed positions — NOT the ShaderInterpreter.
2. **Must be bit-exact to the GPU's cull.** It decides which triangles rasterize, so it
   must match the Adreno's clip-space winding (PA_SU_SC_MODE_CNTL), guard-band/near-plane,
   and kRectangleList exclusion exactly, or it silently drops visible geometry. HELD for a
   careful, device-validated build.

## Next
Design the cheap cull (GPU compute compaction is the most promising on this GPU-bound TBDR;
CPU cull risks re-bounding on CPU). Before investing, a user-run Snapdragon Profiler
per-stage capture would confirm binning is the dominant GPU stage (vs fragment), making the
~56% binning-work reduction translate to ~frame-time.
