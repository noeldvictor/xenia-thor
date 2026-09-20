# re:Blue render levers for Blue Dragon, ported as guest patches

Date: 2026-09-20. User request: "check blue dragon recomp and lost odyssey recomp for tricks we
can pull".

## The projects

| project | what it is | local clone |
|---|---|---|
| zolaware/reblue (re:Blue) v1.2.1 | Blue Dragon static recompilation on the ReXGlue SDK. Own renderer in `src/gpu/` (D3D12, Vulkan, Metal). 84,774 lines. | `../reference/reblue` |
| freefrank/LostOdysseyRecomp v0.6.6 | Lost Odyssey static recompilation on ReXGlue. Not examined yet. | `../reference/LostOdysseyRecomp` |
| rexglue/rexglue-sdk v0.9.0 | The runtime both use. Already a reference here. | `../reference/RexGlue` |

The re:Blue renderer hooks the game's D3D calls (`src/gpu/hooks/draw.cpp`, `resource.cpp`,
`state.cpp`) and runs them on a host API with a pipeline-state cache. That is the per-game
D3D9-HLE approach this fork archived on 2026-08-07 (directive 6). It is not ported.

## What ports: the game knowledge

re:Blue keeps its game-side tweaks as mid-function hooks at fixed guest addresses
(`config/hooks/render_tweaks.toml`, bodies in `src/gpu/hooks/tweaks.cpp`). Each hook changes a
register at a site in the game's own code. In xenia the same sites become instruction patches.
The game then computes its own tile scissors and target sizes. That is the difference from the
emulator-side resolution downscale, which the archive found incompatible with BD's field on
2026-08-16: the emulator scaled the scissor and collapsed the predicated tiles.

re:Blue also ships `config/functions.toml`: 1,649 named guest functions of this build. That is
a symbol map for profiling and for the corpus work.

## The sites, read from a device dump of this build

Dump: `tools/thor/bd_guest_code_dump.py`, 8 MB from 0x82000000, build hash `3C19B6F951F93D49`.
Disassembly: `powerpc-none-elf-objdump -D -b binary -m powerpc:common -EB --adjust-vma=0x82000000`.

| lever | site | instruction in the dump | re:Blue hook | patch |
|---|---|---|---|---|
| FSAA flag | 0x821335A4 | `lwz r11,312(r30)` then `stw r11,0(r25)`, r25 = VisualRender+0x1BC8 | `bdSceneFSAASeedHook`: r11 = MSAA on/off | `li r11,0` = 0x39600000 |
| tiling decision | 0x82186C08 | `lwz r11,7112(r11)` = VisualRender+0x1BC8; zero skips tiling | `bdSceneTilingSuppressHook` at 0x82186C40 | none needed: the FSAA patch clears the same field |
| tiling width test | 0x82186C38 | `cmpwi cr6,r3,640`, r3 = scene surface width (desc+0x18 from 0x82182180) | same | width 640 or less also skips tiling |
| render rate | 0x8213307C | `stfs f30,7108(r31)`, VisualRender+0x1BC4, f30 = 1.0f shared with other fields | `bdSceneRenderScaleHook` after this store | 3 words: 0x82133074 `lis r0,0x3F40`; 0x8213307C `stw r0,0x1BC4(r31)`; 0x82133098 `stw r30,0x1B5C(r31)` |
| shadow map size | 0x821871D8, 0x821871E0, 0x82175174, 0x82175178 | `li r4,1024`, `li r3,1024` at two creation sites | `bdShadowResolutionScaleHook` | `li rX,512` |
| bloom target | 0x8221B360, 0x8221B38C | `addze r4,r10` after `srawi r10,r9,2` (size/4) | `bdBloomTargetSizeHook` scales r4,r5 | not patched yet |
| DOF intermediate | 0x82217600 | `lwz r11,4(r27)` | `bdDOFIntermediateScaleHook` (r28,r26) | not patched yet |

r0 is dead from 0x8213307C to the next call at 0x82133138, so the render rate patch can use it.
The third word folds the displaced `mr r11,r30` into the store that consumed r11.

The FSAA flag and the tiling flag are the same field. So one instruction gives 1x MSAA and a
single-pass field. At 720p 1x the color and depth targets take 7.4 MB of the 10 MB EDRAM. With
FSAA on and tiling off they would take 14.7 MB and overlap, so tiling must not be patched alone.

re:Blue uses the render rate only above 1.0 (supersampling). Below 1.0 is untested by them.

Static confirmation that the game consumes both fields (full disassembly in
`scratch/blue-dragon/bd_code.dis`, not committed):

- Render rate: the view constructor at 0x8213BA68 loads `VisualRender+0x1BC4`, stores it in the
  view (+0x348), keeps it only when above f31 (the zero guard), then multiplies the view height
  and width by it (`fmuls` at 0x8213BA8C and 0x8213BA98) before the call at 0x8213BAA4. A plain
  multiplier, so 0.75 gives a 960x540 view by the game's own code.
- FSAA flag: `VisualRender+0x1BC8` is read through the global VisualRender pointer
  (`*0x82DC9848`) at 0x82186C0C (the tiling decision), 0x82186D40, and 0x82187064 (surface
  setup branches). The constructor initializes it to 0 at 0x821330FC; the seed site overwrites
  it with the config value. Zero is the game's own no-FSAA path.

## An older patch on the device, and what it really is

`files/patches/4D5307DF.patch.toml` on the Thor (2026-07-02, not in the repo, not in the notes)
holds "Field dynamic-res cap 640 v11", enabled: it lowers the clamp at 0x82178660 and 0x82178668
from 1280 to 640 and calls it the 3D field's dynamic resolution. re:Blue names that site: its
`bdReflectionResolutionScaleHook` at 0x82178670 reads `PlaneReflectInfo.width` at +0xA8, the
same field the clamp writes. So that patch limits the planar reflection texture width, not the
field. The dump shows the original 1280 at both words because the dump fired before the
patcher ran. Settled later the same day with the `Patcher:` log lines: the patcher runs
("2 matching patch file(s)", "Applying patch for: ... Field dynamic-res cap 640 v11"). So the
July patch is active in every Blue Dragon run on this device, and the bundled file loads beside
it. I first wrote that the patcher never runs; the lines had been lost in the log burst at the
end of the precompile pass. I was wrong.

## Delivery

`android/.../assets/patches/4D5307DF-thor.patch.toml` (a suffix, so it loads beside the older `4D5307DF.patch.toml` on the device), copied to `files/patches/` by
`GamePatchManager.installBundled` on each launch when absent. The user toggles each patch in
the Game Patches screen (directive 17). All start disabled.

## Measurement plan

Field route, matched pairs, `verts > 50000`, frame generation off, VRS off:

1. control: no patch;
2. FSAA off;
3. render rate 0.75;
4. FSAA off + shadow 512.

Read the screenshots before any number. A frame rate on a corrupted frame is not a frame rate.

## Results (2026-09-20, APK `50caa9bfa4ce2edb` plus the order fix below, `tools/thor/bd_patch_ab.py`)

First attempt: every arm returned 9.9 fps with an identical frame. Cause: `XexModule::LoadContinue`
ran the AOT precompile before `FinishLoadingUserModule` applied the patches, so every function
was compiled from the unpatched bytes and the patch in guest memory was never executed. The PC
has no precompile and applied the same patches. Fix: the precompile call inside `LoadContinue`
is removed; the explicit call after the patcher in `FinishLoadingUserModule` (the former 20 ms
second pass) is the only one. The July "dynamic-res cap 640" patch was inert for the same reason.

Second attempt, with the fix. Route: title, then the documented START/A sequence through the
debug gamepad broadcast, into the village field (Shu at the dock). 30 s of presented fps from
the FPS badge (`xenia-fps` log), 60 samples per arm, same scene in every screenshot. Battery
38 % charging. GPU 39 to 45 C at launch, 58 to 61 C at the end.

| arm | median fps | min | max | frame |
|---|---|---|---|---|
| control (no patch) | 9.9 | 7.9 | 9.9 | complete |
| No anti-aliasing, single pass | **15.8** | 13.8 | 15.9 | complete, same scene, no visible change at this scale |
| Internal resolution 75% | 11.9 | 9.9 | 13.9 | complete, visibly softer (the game's 960x540 scene upscaled) |
| Shadow map 512 | 9.9 | 7.9 | 9.9 | complete |

FSAA off is +60 % at full 720p. The shadow pass costs nothing measurable. The render rate is
+20 % for a softer image, so it loses to FSAA off. Not measured yet: FSAA off combined with the
render rate, and FSAA off combined with the VRS choice (+33 % on its own, directive 14).

## Not examined

LostOdysseyRecomp's game hooks. Lost Odyssey is not a current target here.
