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
field. The dump shows the original 1280 at both words; the dump fired at +5 s, close to the
patcher, so whether the patcher applies it is not settled. The `Patcher:` log lines of one
launch settle it. Not done: the device was in use.

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
Results: not measured yet.

## Not examined

LostOdysseyRecomp's game hooks. Lost Odyssey is not a current target here.
