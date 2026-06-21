# BD GPU efficiency budget — does the Thor need frame-gen? (2026-06-21, workflow wucin4nlr)

User's question: the Adreno 740 has ~12-15x the Xenos GPU FLOPS (+ more fillrate/bandwidth) - it should NOT
need frame-gen; figure out where the inefficiency goes. ANSWER (code+device-grounded, 13-agent analysis):

## Headline
The user's thesis is CORRECT: the Thor is ~5-8x GPU-under-efficient vs a native render of BD's content
(real emulation/architecture overhead, NOT a FLOPS limit; theoretical native-efficient Adreno cost for this
exact overdraw = ~15-25ms vs the measured 126ms). BUT the recoverable fraction is MEDIUM, not large, because
the two biggest slices are architecturally irreducible on the a740. **Recovering everything realistically
buildable lands BD ~80-90ms (~11-12fps), NOT 33ms/30fps. The residual ~50-70ms is a740-irreducible -> frame-gen
is the HONEST, justified answer for that slice (not a band-aid over recoverable overhead). Both tracks are
right; they target non-overlapping slices.**

## The ms-budget of BD's 126ms heavy frame
| Slice | ms | % | Recoverable? |
|---|---|---|---|
| (a) Inherent overdraw the 360 ALSO shaded (43% foliage + 34% blended layers) | ~50-60 | ~45% | NO as fragment-count (frame-gen / content-cut only) |
| (b1) LRZ-defeat extra fragments (OpKill/demote -> Turnip has_kill -> Adreno LRZ off -> layers the 360 hi-Z rejected now fully shade) | ~40-55 | ~35-40% | DEVICE-PROVEN near-irreducible on a740 (VRS recovers part) |
| (b2) RT byte-width inflation: depth D32S8=8B (no D24S8 on Turnip), FP10 color->R16G16B16A16=64bpp not 32bpp, UBWC off on MUTABLE_FORMAT RTs | ~10-18 | ~8-14% | **YES - the clean recoverable slice** |
| (c) EDRAM pass/transfer traffic (loadOp/storeOp per pass-break + ~25 ownership transfers GMEM<->LPDDR5X) | ~20-25 | ~16-20% | mostly NO (coalesce shipped fps-flat; necessary resolve bandwidth) |
| (d) Binning (per-vertex position VS, ~263k verts) | ~6 | ~5% | NO (deinterleave A/B flat) |

The "per-fragment unit cost ~5x too high" the user hunted = (b1) LRZ-defeat (largest, irreducible) + (b2) the
byte inflation (smaller, recoverable) + the ~67GB/s UMA vs 256GB/s on-die EDRAM bandwidth gap (a real HW gap).
It is NOT shader ALU (constant-color FS = no change) and NOT source-texture sampling (BC stays compressed on a740).

## Recoverable roadmap (by recoverable-ms)
1. **VRS** ~25ms/-20% — SHIPPED, validated, defaulted-on (BD/BTTF). The only quality-neutral big win.
2. **RT byte-width fixes (b2) ~10-18ms — the net-new buildable headroom:**
   - **UBWC-keep on color RTs** — BUILT 2026-06-21 (cvar gpu_vulkan_rt_keep_ubwc, commit 3b7afbe75): add a
     2-entry VkImageFormatListCreateInfo {base, transfer-alias} to the MUTABLE_FORMAT color RT so UBWC stays on.
     default-off; A/B (on vs off / vs TU_DEBUG=noubwc on a frozen field) to size the win, then default-on.
   - **FP10 color as 32bpp** (NEXT) — gpu_fp10_color_as_unorm10: map k_2_10_10_10_FLOAT -> A2B10G10R10_UNORM_
     PACK32 for SDR (64bpp->32bpp halves the color RT bandwidth on BD's dominant fmt 12). Extends the crude
     8-bit vulkan_force_float_color_unorm at vulkan_render_target_cache.cc:1848. GATE for HDR (loses 7e3 range).
     Probe: vulkan_force_float_color_unorm=true on a frozen field = the format-tax upper bound.
   - Depth toward 4B — weakest (D24S8 absent on Turnip, D16 loses precision). gated/uncertain.
3. **FDM** — speculative, attacks (a)+(b1) harder than VRS, stacks (gate-green, unbuilt). A/B vs VRS-on; UNMEASURED.
4. **frame-gen** — the honest answer for the irreducible ~50-70ms (a). [[fdm-overdraw-lever-next-major-build]]

## DEAD - do not re-attempt (device-proven on a740)
LRZ restoration (gpu_foliage_lrz_feedback +0.6%/-8.9%-densest; opaque-prepass+force-depth NET-NEGATIVE
127->137.5ms; OpDemote already emitted + still disables LRZ); EDRAM coalesce (fps-flat); RT-coalesce (~0 safe budget).

## Cheapest probe to run FIRST (one fire, no code)
gpu_collapse_alphatest_coverage OFF vs ON on a gpu_freeze_at_guest_ms-locked BD field, read gpu_frame_us:
splits the 95% per-fragment cost by draw class = the alpha-test-foliage envelope (a-foliage + b1 + foliage-b2),
backing out irreducible (frame-gen territory) vs recoverable without burning fires on the dead levers. Then the
noubwc + force-float-unorm fires size the b2 ~10-18ms. Full output: tasks/wucin4nlr.output.
