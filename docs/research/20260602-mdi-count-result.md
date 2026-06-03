# Lever 1 result — corrected MDI Count is BREAK-EVEN (draw-call batching is dead on this TBDR)

Device A/B of `vulkan_merge_draws_indirect` after the zero-padding `vkCmdDrawIndexedIndirectCount`
fix (commit 020ba739b). Same build, content-matched on rendered draw count + avg_vertices.
- OFF: `docs/evidence/20260602-202404-mdicount-off.txt` (fps 2.39)
- ON:  `docs/evidence/20260602-202629-mdicount-on.txt` (fps 2.39); screenshot **bit-identical**.

## Result: NEUTRAL (~break-even)
| rendered/avg | OFF gpu_frame_us | ON gpu_frame_us | Δ |
|---|---|---|---|
| 1330/133 | 398,530 | 397,868 | −0.2% |
| 1368/129 | 401,821 | 400,290 | −0.4% |
| 1590/118 | 420,714 | 423,732 | +0.7% |
| 1799/115 | 448,469 | 452,727 | +0.9% |
| 1936/110 | 531,987 | 529,981 | −0.4% |
| 1985/109 | 549,786 | 549,900 | +0.0% |

Spread −0.4%..+0.9%, avg ~+0.2% = statistically neutral. **The zero-padding removed the padded
MDI's +8-14% loss (`mdi-device-ab-result.md`) — but there is no net win.**

## The decisive lesson (this closes the batching question)
Draw-call batching — concatenation AND MDI, padded or zero-padding — does NOT reduce frame time on
this Adreno. Batching N draws into one `vkCmdDrawIndexedIndirectCount` is bit-correct and overhead-
free now, but the GPU still pays the per-draw-FIXED cost (~140µs/draw, the context-roll/bin-restart the
binning front-end incurs per primitive group) for EACH sub-draw — it is NOT coalesced by issuing them
as one indirect command. Confirmed by: skip-tiny (which ELIMINATES draws) cut ~25%, while MDI (which
keeps the same draws as sub-draws) cuts ~0%. So the per-draw-fixed cost is removable only by FEWER
draws (guest behaviour — can't change) and the per-TRIANGLE cost only by a real triangle cull.

## Consequence for the 24h push
- Keep `vulkan_merge_draws_indirect` committed + DEFAULT OFF (a validated-neutral, bit-correct
  experiment; not a perf default).
- **The CPU/NEON triangle CULL (Lever 2) is now the SOLE remaining software lever.** Per the linear
  fit (per-draw-fixed ~140µs + per-triangle ~148µs/draw, ~50/50), culling 56% of triangles saves
  ~83µs/draw ≈ ~28% of the per-draw cost — meaningful IF the affine-MVP fast transform qualifies on BD
  and holds bit-exactness. Gate it on the per-shader affine-MVP classifier counter first
  (docs/research/20260602-neon-cull-impl-plan.md §2).
