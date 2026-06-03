# Lever 2 Step 2a-ii diagnostic — OR fix works (dynamic topology active), but ALL qualifying draws bail in BuildCulledIndexList

After making gpu_cull_compaction imply dynamic-topology pipelines (commit 75e7fbe37) and adding a per-frame
cull funnel, a device run with `-SetCvar gpu_cull_compaction=true` on Blue Dragon's heavy cinematic gives:

```
cull[branch=1831 skip_dyntop=0 skip_qual=225 skip_build=1606 draws=0 dropped_tris=0]
```
(Evidence: `docs/evidence/20260603-002333-cull-on-diag.txt`; screenshot correct - all draws verbatim.)

## What the funnel proves
- **skip_dyntop=0**: the OR fix WORKED. can_emit_list is true for every triangle-list/strip draw - dynamic
  topology is active under gpu_cull_compaction alone. (My earlier "runtime broadcast can't enable a startup
  cvar" guess in 71241e7c5 was WRONG: for a FRESH launch the harness passes -SetCvar cvars as --ez launch
  extras, and vulkan_dynamic_state_topology IS allowlisted. The real blocker was never the topology cvar.)
- **branch=1831, skip_qual=225**: 1606 draws are position-affine-MVP-qualifying (and list/strip + kGuestDMA).
- **skip_build=1606, draws=0**: ALL 1606 qualifying draws return FALSE from BuildCulledIndexList. The cull
  reaches the method but the method rejects every BD draw, so every draw fell back to verbatim - hence
  gpu_frame_us did not drop.

## gpu_frame_us is INCONCLUSIVE here (content-confounded)
This run reached rendered=2187 / gpu_frame_us=810678; the dyntop-off baseline only reached rendered=2132 /
682629 (the runs desync, and the cinematic gets heavier with guest uptime). 810k vs 682k is a deeper scene
point, NOT a measured dynamic-topology cost. A content-matched comparison (same rendered+avg_vertices) is
still needed before claiming any dynamic-topology overhead.

## Next: pinpoint the BuildCulledIndexList bail (it has several early-returns)
BuildCulledIndexList returns false on any of: source_select != kDMA, tessellation, !CanInterpretShader
(whole-shader texture fetch), vtx_xy_fmt (pre-divided positions), clip_disable, or primitive-restart
(multi_prim_ib_ena). ALL 1606 bailing points to one systematic cause. Candidates, by suspicion:
- CanInterpretShader rejects whole-shader texfetch - BUT the 0a->0b classifier jump was attributed to
  color-path TRANSCENDENTALS (which CanInterpretShader allows), so maybe not.
- primitive-restart on the strips (multi_prim_ib_ena) - plausible for strip meshes.
- source_select kAutoIndex vs kDMA mismatch with the kGuestDMA index-buffer type.
ACTION (next fire): add a bail-REASON breakdown to BuildCulledIndexList (return a small enum; count each
reason in the command processor funnel). One device run then names the exact bail, and the fix follows
(e.g. a position-slice replay that bypasses CanInterpretShader's whole-shader texfetch rule, or handling
restart, or accepting kAutoIndex).

## Status
The cull path is correctly gated and reaches BuildCulledIndexList; off-path + verbatim remain correct on
device (screenshot clean). The remaining work is identifying + removing the single systematic bail.
