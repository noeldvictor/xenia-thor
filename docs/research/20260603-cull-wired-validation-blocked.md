# Lever 2 Step 2a-ii result — cull wired + builds, but device A/B shows it does NOT engage (startup-cvar coupling)

Wired the triangle cull into the gpu_cull_compaction draw path (commit af0f03897) and ran a device A/B on
Blue Dragon's heavy cinematic. Both runs use dynamic topology to isolate the cull.

- OFF: `docs/evidence/20260602-235441-cull-off-dyntop.txt` - `-SetCvar vulkan_dynamic_state_topology=true`. gpu_frame_us=682629 rendered=2132 avg=128 (screenshot correct).
- ON:  `docs/evidence/20260602-235743-cull-on.txt` - `-SetCvar "vulkan_dynamic_state_topology=true;gpu_cull_compaction=true"`. Content-matched frame (rendered=2137 avg=128): gpu_frame_us=682914. Screenshot correct.

## Result: cull did NOT engage (gpu_frame_us unchanged: 682629 vs 682914 = +0.04%, noise)
- cpu_issuedraw_us = 44418 (ON) vs ~42208 baseline = +2.2ms = the Step 1 VERBATIM copy cost. The
  interpreter cull (if running on ~76% of draws) would add ~seconds; it did not run.
- So gpu_cull_compaction DID apply (verbatim copy active), but the cull branch's `can_emit_list` was false,
  so every draw fell back to the verbatim NO-OP.

## Root cause: the cull needs dynamic-topology PIPELINES, which a runtime -SetCvar can't create
`can_emit_list` requires `cvars::vulkan_dynamic_state_topology`. That cvar is STARTUP-only: the pipeline
cache builds dynamic-topology (strip->list-class) pipelines at creation time, and CmdVkSetPrimitiveTopology
is only legal on those. The harness applies -SetCvar via a runtime SET_CVAR broadcast AFTER boot, by which
time BD's pipelines are already built WITHOUT dynamic topology - so the cvar never engaged dynamic topology,
`can_emit_list` stayed false, and the cull never attempted (which is also why there was no corruption: the
verbatim strip path is bit-identical). gpu_cull_compaction works via -SetCvar because its Step 1 effect is
per-draw runtime; the cull's topology effect is per-pipeline startup.

## Fix (next fire) - make the cull self-contained + testable at launch
1. Pipeline cache (vulkan_pipeline_cache.cc ~595-680): build dynamic-topology pipelines for triangle
   list/strip when `gpu_cull_compaction` is set, the same as vulkan_dynamic_state_topology already does
   (treat the two as OR for the dynamic-topology + restart-dynamic decision and the strip->list key
   normalization). Update `can_emit_list` to accept gpu_cull_compaction's implied dynamic topology.
2. This makes gpu_cull_compaction a LAUNCH-sensitive cvar (pipelines built at boot), so it must be set via
   --ez at launch, not runtime -SetCvar. Add a `-LaunchEz "a=1;b=true"` parameter to thor_evidence.ps1 that
   appends `--ez/--ei` to the launch intent (needed to validate any startup cvar).
3. Re-validate: launch with --ez gpu_cull_compaction true. PASS = screenshot hole-free + gpu_frame_us DROPS
   at matched rendered/avg_vertices. If front faces vanish, flip the det sign in BuildCulledIndexList.

## Status
Cull is fully wired + build-verified; off-path and the verbatim NO-OP are untouched and correct on device.
The remaining work is purely enabling dynamic-topology pipelines from the cull cvar + a launch-cvar test
path - no change to the cull math itself.
