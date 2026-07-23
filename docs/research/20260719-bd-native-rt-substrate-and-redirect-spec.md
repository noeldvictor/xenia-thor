# BD full-native HLE: the resource-keyed RT substrate + redirect spec (2026-07-19)

Device-free structural work built this session (gated default-off, desktop
build-verified), plus the concrete spec for the device-validated consumer
redirect that consumes it. This is the "resource-keyed native RTs, EDRAM
deleted" foundation the memory kept circling, now built from the LIVE capture.

---

## What is built (the substrate) — all under `gpu_bd_native_reserve_captured_surfaces`

At frame end, inside the existing `gpu_bd_full_native>=1` capture
(`vulkan_command_processor.cc`), the per-frame draw capture is analyzed into the
**distinct render-target surface set** and each surface is reserved as a
persistent, resource-keyed native `VkImage` via `BdNativeRenderer`:

1. **Distinct-surface enumeration + draw priority.** Each draw's color/depth
   targets are keyed by `base_tiles+pitch+format+msaa`; draw counts per surface
   give coverage priority (main scene = most draws, bloom pyramid = small
   pitches, composite = single large draw). Height comes from the max
   `PA_SC_SCREEN_SCISSOR_BR.br_y`; width = pitch.
2. **Color↔depth pairing.** Each color surface is associated with the depth it
   renders against (first-seen wins; conflicts counted), so a reserved surface
   gets its REAL depth format (via `GetDepthVulkanFormat`), not the D24S8
   default.
3. **Depth-only surfaces.** Depth passes with no color (prepass / shadows) that
   aren't already some color surface's paired depth get their own reservation
   through `BdNativeRenderer::AcquireDepthOnlySurface` — a sampled depth image +
   depth-only render pass/framebuffer (no color attachment).
4. **MSAA (2×/4×).** `AcquireSurface`/`CreateSurfaceResources` are samples-aware:
   the MSAA color image drops `SAMPLED` and gets a paired single-sample
   `resolve_image` (+view); color render-pass layout is `COLOR_ATTACHMENT` for
   MSAA vs `SHADER_READ` for single-sample; no in-pass resolve (Adreno-compat).
5. **Key namespacing.** Color keys fold into the low 32-bit half (MSB clear),
   depth-only into the high half (MSB set), so they never collide in the shared
   `surfaces_` registry.

Result per frame (logged):
```
BD NATIVE RESERVE: color reserved=N (msaa=M paired_depth=P) failed=… of …;
  depth_only reserved=D (msaa=…) skip_paired=… failed=… of …; pair_conflicts=…
```

**Files:** `bd_native_renderer.{h,cc}` (AcquireDepthOnlySurface, samples-aware
CreateSurfaceResources, resolve fields), `vulkan_command_processor.cc` (capture
analysis + reservation), `command_processor.cc` (cvar). All uncommitted with the
in-flight GPU work.

## What remains: the consumer redirect (DEVICE-VALIDATED — the next step)

The substrate ALLOCATES the native RTs but does not render into them. The
redirect is where the EDRAM tile-store wall is actually deleted, and per the
memory it MUST be Thor-measured (TBDR GMEM residency is invisible on desktop;
depth changes behave differently on Turnip). Spec:

1. **At each field draw, resolve the target surface.** Compute the same
   `base_tiles+pitch+format+msaa` key from the live registers, `FindSurface`
   (or `AcquireSurface`) the reserved native surface, and begin its
   `render_pass_clear` (first draw this frame) / `render_pass_load` (re-begin)
   on its `framebuffer` instead of the EDRAM render target. `BeginSurfaceFrame`
   already resets per-frame CLEAR flags.
2. **Record the draw natively** into the surface's render pass, reusing xenia's
   field pipelines (require render-pass format compatibility — `EnsureColorFormat`
   pattern). This is Brick 2b territory in `bd_native_renderer.h`.
3. **MSAA: populate `resolve_image`.** For MSAA surfaces, after the surface's
   pass ends, `vkCmdResolveImage` the MSAA color → `resolve_image` (mirror the
   primary `ResolveMsaa`), so the surface is sampleable. This method is the one
   clearly-correct piece not yet written; it's a standard resolve but is only
   meaningful once draws render into the MSAA color, so it belongs with the
   redirect.
4. **Redirect consumers.** Later passes that SAMPLE a surface bind its
   `color_view()` (or `resolve_view` for MSAA) via `LookupSampledSurface` /
   the texture-fetch redirect, instead of the EDRAM-resolved upload.
5. **Drop the now-covered EDRAM transfers.** Once a surface's producers AND
   consumers are native, its EDRAM ownership transfer is redundant — drop it
   (the tile-store saving). Partial coverage leaves the bridging transfers, so
   drop only fully-covered surfaces.

## Validation plan (when Thor runs resume)

- **Correctness:** BD field renders identically with the redirect on (screenshot).
- **Coverage:** the `BD NATIVE RESERVE` log confirms all field surfaces reserved;
  RenderDoc/pass-count confirms the covered EDRAM transfers dropped.
- **Perf (the point):** per-pass GPU timestamps on Turnip — the deleted
  transfers' tile-store should drop the field `gpu_frame_us` toward the ~17-25ms
  / 30fps target. Cross-run fps is confounded (memory rule); use matched-scene
  A/B or per-pass timestamps.
- **MSAA:** confirm the separate resolve produces correct (non-banding) sampled
  content on Adreno.

## Why the substrate/redirect split is the right shape

The substrate is 100% device-free structural (allocation + valid Vulkan objects,
verifiable on desktop) and now spans BD's entire distinct-surface set. The
redirect is 100% device-dependent (it deletes the TBDR tile-store wall, the whole
point). Splitting them means the hard, expensive, device-gated work starts from a
complete, correct, keyed RT set derived from the real frame — not from
hand-RE'd RenderDoc guesses.
