# HYBRID HLE GPU PATH in xenia — the Cemu model for the 360 (user-decided 2026-07-04)

## The decision
Blue Dragon is slow because xenia **LLE-emulates** the 360 GPU (PM4 command stream + register file + EDRAM
memory + predicated tiling + SSBO-vertex-fetch). Proof it's the emulation and not the chip: **the user runs
Resident Evil 2 Remake (far heavier) on the same AYN Thor via GameNative, which uses DXVK — an HLE API
*translator*, not a GPU emulator.** So: build the DXVK equivalent **inside xenia**, opt-in per game.

## The model = Cemu (exactly)
Cemu = general emulator + **HLE graphics** (translate the GX2 *API* → host GL/Vulkan, no register-level GPU
emulation) + **per-game graphic packs** (toggleable patches). We bring the same to xenia:
- **Keep** xenia's LLE PM4 GPU path = the **compatible default** (runs any title correctly).
- **Add** an opt-in **HLE GPU path** = translate BD's D3D9 draw/state/resource calls → native Vulkan,
  bypassing PM4/EDRAM/tiling entirely.
- **Toggle per game** via the existing `GameProfiles` / `XeniaOptimizations` system (= graphic packs).
  Default OFF (LLE). BD's profile flips it ON. Untested titles are unaffected.

## The boundary problem (why xenia went LLE) + the solution
Cemu HLEs graphics because Wii U GX2 is a clean runtime API. The 360's D3D9 is **static-linked/inlined** in the
XEX — no runtime hook point. Solution = **load-time XEX signature-HLE** (committed; intercept mechanism PROVEN
by the bin-once running a host body in place of a guest D3D fn): at game-load, scan the XEX, byte-signature-
match the statically-linked XDK D3D9 functions, install HLE trampolines. This recreates the API boundary. The
XDK D3D9 is the SAME across titles (signatures stable per XDK version) ⇒ a signature DB generalizes it, NOT a
per-game port (the user explicitly rejected per-game ports).

## Architecture — the HLE path reuses xenia's Vulkan BACKEND, replaces its FRONT-END
xenia's Vulkan backend (shader translator Xenos→SPIR-V, texture cache, pipeline cache, command buffer) is
GOOD and already exists. The slow part is the FRONT-END: PM4 decode → register file → SSBO-vfetch → EDRAM. The
HLE path is a NEW front-end that feeds the SAME backend from the D3D9 device state DIRECTLY:
- **draw** (DrawIndexedPrimitive/DrawPrimitive) → native VkCmdDrawIndexed with NATIVE vertex input (the guest
  vertex buffer bound as a real VkBuffer + a real input layout from the D3D9 vertex declaration — not SSBO).
- **state** (SetRenderState/blend/depth/raster) → native pipeline state (not register-file reconstruction).
- **resources** (SetTexture/vertex+pixel shaders) → xenia's texture cache + shader translator (reused).
- **render targets** (SetRenderTarget/Resolve) → native VkImage RTs, no EDRAM tile emulation, no round-trips.

## Staged build order (each stage measurable; keep LLE as fallback)
1. **Load-time D3D9 signature scan + trampoline infra** — extend the proven page-watch/harvest RE to pin BD's
   D3D9 fns (Draw*/SetState/SetTexture/SetRenderTarget/Resolve) at load; install trampolines. (Partially built.)
2. **HLE the DRAW path** — DrawIndexedPrimitive → native Vulkan draw with native vertex input, reading the D3D9
   device state, feeding xenia's pipeline/shader/texture caches. Prove ONE scene renders via the HLE front-end.
3. **HLE state + resources** — blend/depth/raster + textures + shaders, native.
4. **HLE render targets** — native VkImage RTs, drop the EDRAM round-trips (the biggest structural win).
5. **Signature DB** — generalize the fn identification across titles/XDK versions; add a `GameProfile` toggle.

## Why this succeeds where the 4 bricks failed
The bricks (native-input/bindless/native-RT/fp16) bolted onto the SIDE of the LLE emulator while PM4/EDRAM kept
running underneath — you can't patch an emulator into a translator. The HLE path REPLACES the front-end: the
GPU stops getting emulated-360-shaped work and gets native-shaped work, exactly like DXVK gives RE2. Confirm
the mechanism first with a GPU perfcounter profile (ALU-busy vs stall) to aim stage 2 at the worst starvation.

## Status: PLAN committed 2026-07-04. Next = stage 1 (load-time D3D9 signature scan, building on the proven
## page-watch RE + bin-once trampoline mechanism). Multi-session. `check` the experiment DB before each step.
