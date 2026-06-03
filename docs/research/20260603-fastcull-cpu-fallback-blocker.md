# Lever 2 fast cull — correct at 40% tri reduction, but ~44% of draws still fall back (CPU blocker)

Two CPU-reduction changes this round: half-float position decode (commit 2c587dd2b) + recover M from the
self-decoded basis instead of the overwritable interpreter leaf register (commit c2548f43a).

## Good: culling INCREASED + still correct
- Device (harbor): cull[branch=1856 draws=511 dropped_tris=86550] = ~40% of ~218k triangles dropped (up from
  ~30% before the decode-basis fix - more draws now take the fast path). Screenshot HOLE-FREE: ships, rigging,
  terrain, sky all intact at 40% triangle reduction. The fast affine replay is correct.
- The decode-basis fix mattered: fitting M from the interpreter leaf register read AFTER Execute was wrong
  (the color path overwrites that register on many shaders), so M failed the residual self-check and those
  draws fell back. Decoding the basis input the same way as the apply fixed the consistency.

## Blocker: cpu_issuedraw still ~811ms -> net fps DOWN (1.33 vs 1.75)
~56% of qualifying draws use the fast path; ~44% still FALL BACK to the full per-vertex ShaderInterpreter
(est. ~700 draws x ~1ms). The fallback is because SetupFastAffineReplay only handles k_32_32_32_FLOAT /
k_32_32_32_32_FLOAT / k_16_16_16_16_FLOAT / k_16_16_FLOAT position formats; BD evidently uses OTHER formats
for a large fraction of draws (candidates: k_2_10_10_10, k_11_11_10, k_16_16_16_16 (snorm/unorm), k_10_11_11).
Plus the 6-interpreter-sample M recovery (~97ms over ~1700 draws) is a fixed cost.

## Next: DIAGNOSE the fallback formats, then eliminate them
1. Add a counter: how many cull draws took the fast path vs fell back, AND a histogram of the leaf vfetch
   format for fallbacks (so we KNOW which formats to add rather than guess). Device-measure.
2. Add decode for the dominant fallback formats (likely normalized 2_10_10_10 / 16_16_16_16 - an affine
   int->float scale+bias, so clip stays affine in the raw int; M absorbs the scale). That moves the bulk
   onto the fast path.
3. If recovery cost then dominates: cache M per (shader+constant-state) where possible, or NEON the per-vert.
Target: CPU cull << GPU (~528ms) so it hides -> net fps >= OFF.

## Status
The cull is proven correct + a ~13% GPU lever (content-matched earlier). Net fps is still gated by the
interpreter FALLBACK for unhandled position formats. Default-off; off path untouched.
