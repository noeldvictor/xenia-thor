---
name: xenia-bd-pc-reverse-engineer
description: Reverse-engineer Blue Dragon's (and any 360 title's) rendering on DESKTOP xenia (Windows, D3D12 backend) where iteration is seconds - not the AYN Thor where every probe is a 150s nav + thermal + build/install cycle. Use for finding the D3D9 draw path, the foliage submission, the field draw seam, IB structure, or any "how does the game render X" question. Then implement the optimization in the Vulkan/Turnip path and PATCH + validate on the Thor (Adreno 740). RE on PC, ship on Thor.
---

# RE Blue Dragon on PC, patch on the Thor

**🚨 UPDATED 2026-07-05 (user, TOP PRIORITY): BUILD THE FULL D3D9→VULKAN HLE NATIVE RENDERER — dev on desktop with
`--gpu=vulkan` (NOT D3D12), but PERF-OPTIMIZE on the Thor because TURNIP/ADRENO VULKAN ≠ DESKTOP VULKAN.**
- **Desktop `xenia.exe --gpu=vulkan`** = the SAME backend code (VkImage/VkRenderPass/VkPipeline/SPIR-V) that ships
  to Turnip → develop the native renderer's STRUCTURE + CORRECTNESS there (RenderDoc to verify pass count/pipelines,
  iterate in seconds). D3D12 is throwaway for this — don't use it for the renderer build.
- **BUT the 30fps comes from THOR-SPECIFIC super-optimization** (desktop is immediate-mode, hides all of it): the
  Adreno 740 is TBDR — every render pass = a GMEM tile store/flush (BD's 95-pass wall). Perf = minimize passes,
  keep the RT GMEM-RESIDENT across draws (one held pass / subpasses via dynamic_rendering_local_read/ROAA), tune
  load/store_op + LRZ-friendly depth, NEON. Validate STRUCTURE on desktop Vulkan → PERF-TUNE + measure on the Thor.
- **The target = a SEPARATE native Vulkan renderer** (seam 0x82489F40 → own full-surface RT, few passes, native
  vertex-input/shaders/ROP-blend, depth-prepass+early-Z) that BYPASSES xenia's PM4/EDRAM/95-pass LLE back-end. The
  decoupled-native-HLE was PERF-FLAT because it KEPT that back-end. Blueprint: repo
  `docs/research/20260705-native-vulkan-renderer-plan.md`. Skill: `xenia-vulkan-adreno-renderdoc` for the capture.

**WHY (user-directed 2026-07-04):** RE on the Thor is brutal — 150s navs, thermal watchdog, device flakiness,
no debugger, ~10min build+install per probe. **Desktop xenia renders BD at playable speed** and iterates in
SECONDS with a real debugger. It runs the SAME guest CPU emulation + CommandProcessor/register-file GPU path as
the Thor. So the *guest* RE (how BD submits draws, the D3D9 dispatch, the IB recorder, the foliage draw structure)
is IDENTICAL and far faster on PC. **Then perf-tune the Adreno/Turnip TBDR specifics on the Thor + validate there.**

**Key fact this unlocks:** desktop @~60fps PROVES BD's foliage is NOT intrinsically slow — those exact 262k verts
render fast on a desktop GPU. So the Thor's ~15ms foliage cost is **TBDR-binning / tiling / emulation-specific**,
which means "optimize how the foliage uses the GPU, no gfx loss" is genuinely achievable (user-permitted 2026-07-04).

## Build desktop xenia (our code — RE findings map straight to what we patch on the Thor)
1. **Generate the solution** (xb.bat is Python-broken → use premake5 directly):
   `tools\build\bin\premake5.exe --file=premake5.lua vs2022` → `build/xenia.sln` (incl. xenia-app WindowedApp +
   the D3D12 GPU backend).
2. **Build the app:** MSBuild is at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\
   Current\Bin\MSBuild.exe` (VS 2022 **Build Tools**, note Program Files **(x86)**). Run
   `MSBuild build\xenia-app.vcxproj /p:Configuration="Release Windows" /p:Platform=x64 /m`. Output
   `build/bin/Windows/Release/xenia.exe`. (Use "Debug Windows" — the cpu-tests config — if Release breaks;
   builds identically, runs slower.)
3. **Run BD:** `xenia.exe "<Blue Dragon ISO>"` (the ISO the Thor uses is on the device; keep a local copy for PC).
   Reach the field the same way (menu → New Game → intro-skip). No nav script needed — you drive it live.

## RE approach on PC (fast)
- **Reuse the exact diagnostic hooks** already built (they're in the shared cpu/gpu code, so they work on desktop):
  `cpu_d3d_hle_signatures` (load-time D3D9 signature scan → finds the 7 XDK fns + logs), `cpu_d3d_hle_diag_endtiling`,
  `cpu_d3d_hle_diag_draw_addr=<hex>` (intercept any guest fn, log its call pattern + args), `cpu_watch_guest_write_page`
  (page-watch a guest address, emulate-on-fault, log the writer's caller — THE tool that cracks deferred recorders).
- **The guest debugger:** xenia has a built-in guest debugger; breakpoint guest functions (e.g. the D3D9 dispatch
  slots) and inspect the call stack live — far better than the Thor's stack-chain probes.
- **Trace the draw path directly:** add temporary `XELOGI` to `VulkanCommandProcessor`/`CommandProcessor::IssueDraw`
  or the guest-call path; on desktop you rebuild+rerun in ~1min and read the log immediately.

## What we KNOW (from memory/bd-d3d-hle-re-state.md — don't re-derive)
- **ALL D3D9 identified:** the D3D9 dispatch table @0x8207E2C0 = 224 method slots (each → a D3D9 method in the
  0x822Fxxxx/0x823xxxx band, DISTINCT from the 0x8248xxxx XDK tiling band).
- **The field draw seam is ELUSIVE:** EndTiling(0x8248A680), tiling-replay(0x82487CC0), and dispatch draw method
  0x822FF490 are all identified but **DON'T FIRE at the field** — BD's 1194 foliage draws are recorded into
  INDIRECT BUFFERS by a deferred mechanism. **The open RE = find the IB-recorder** (page-watch the IB memory on PC).
- The field = a 4-pass foliage COMPOSITE (pass B = 1194 foliage draws), NOT the tiling path.

## The end-to-end loop
1. **RE on PC:** reach the field → find the IB-recorder / how the foliage is submitted → identify WHY the Adreno
   path is slow where desktop is fast (TBDR binning cost? per-tile? submission?) → the no-gfx-loss fix.
2. **Build on Thor:** implement the fix in `src/xenia/gpu/vulkan/*` (the Turnip/Adreno path).
3. **Validate on Thor:** the device is the only place the Adreno/Turnip perf is real. `check` the experiment DB
   first; single-run alternating A/B; screenshot correctness.

## Gotchas
- The repo is Android-focused; the Windows app path may have bit-rot — fix build breaks forward (they're our code).
- Desktop D3D12 ≠ Thor Vulkan/Turnip: perf numbers DON'T transfer (RE the STRUCTURE on PC, MEASURE perf on Thor).
- **Get the BD ISO onto the PC** (needed to boot — the local `scratch/blue-dragon/default.xex` is just the
  extracted exe, won't boot without disc data): `adb pull` it via **PowerShell, NOT Git Bash** — MSYS mangles
  the device's `/storage/...` into `C:/Program Files/Git/storage/...` (No such file). PowerShell:
  `& $adb -s c3ca0370 pull "/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr)
  (Disc 1).iso" "<repo>\scratch\blue-dragon\bd_disc1.iso"`. 7.8GB, one-time; scratch/ is gitignored so keep it.
- xenia.exe: `build/bin/Windows/Release/xenia.exe`. Run `xenia.exe <iso>`; log at `<xenia-dir>/xenia.log`.
  Reuse the diag cvars via a config toml or command line; the guest debugger is in the GUI (Debug menu).
