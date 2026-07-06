---
name: xenia-renderdoc-replay
description: Headless RenderDoc capture + Python replay analysis of xenia (desktop --gpu=vulkan) GPU frames, WITHOUT the RenderDoc GUI. Use to inspect BD's native-render frame per-draw - post-VS NDC positions (frustum clipping), render targets, viewports, scissors, pipeline state - when screenshots + register logs are ambiguous ("geometry lands where the registers don't predict"). This is how the BD buildings-black tiling bug was diagnosed. Local env at tools/renderdoc/.
---

# Headless RenderDoc capture + replay analysis for xenia

RenderDoc (portable install at `C:\Program Files\RenderDoc\`) is the frame debugger for the
desktop Vulkan native renderer. Capture headless, then analyze the `.rdc` with the Python
**replay** API (no GUI). This resolves per-draw questions the CPU/API register state can't.

## 1. Capture a frame (trigger from inside xenia)
The CP has an in-app trigger (`src/xenia/gpu/vulkan/vulkan_command_processor.cc`, via
`ui::RenderDocAPI`): cvar **`gpu_bd_renderdoc_capture_frame=N`** triggers a capture at swap N
when xenia runs under `renderdoccmd`. Launch (PowerShell/Bash, absolute paths):
```
"C:\Program Files\RenderDoc\renderdoccmd.exe" capture -c "<out\bd_cap>" \
  "<...\xenia.exe>" --gpu=vulkan --gpu_bd_native_renderer=true \
  --gpu_bd_renderdoc_capture_frame=3600 --hid=nop \
  "--hid_nop_button_sequence=start@18000:600;...;a@128000:600" "<...\bd_disc1.iso>"
```
Reach the field (~swap 3600 at ~120s). Saves `bd_cap_frame3601.rdc` (~73MB). Kill xenia after.

## 2. Analyze headlessly via `qrenderdoc --python`
The portable install has NO standalone `renderdoc.pyd` module, but **`qrenderdoc.exe --python
<script.py>`** runs a script with the `renderdoc` module + full replay API available
(confirmed working, v1.44). qrenderdoc is a GUI app with no usable stdout, so scripts WRITE
RESULTS TO A FILE; poll it for a `=== DONE ===` sentinel, then kill qrenderdoc.

Env is at **`tools/renderdoc/`**: `rd_analyze.py` (per-draw RT + post-VS NDC x-range),
`rd_min.py` (module smoke test), `run.ps1` (runner that polls + cleans up).
```
"C:\Program Files\RenderDoc\qrenderdoc.exe" --python "<abs path>\tools\renderdoc\rd_analyze.py"
```

## Gotchas (these cost real time - obey)
- **`__file__` is NOT defined** in the `--python` context. HARDCODE absolute output/rdc paths
  in the script (do not use `os.path.abspath(__file__)` - it throws before any output).
- **Write output to a file, not stdout** - qrenderdoc GUI stdout is not captured. End the
  script by writing `=== DONE ===` and `os._exit(0)` (so the UI doesn't block).
- **v1.44 API differs from old docs:** `GetPipelineState().GetOutputTargets()` returns
  `Descriptor` objects whose RT id is `.resource` (NOT `.resourceId`). There is NO
  `GetTexture(id)` - build `{tex.resourceId:(w,h)}` from `controller.GetTextures()`.
  Post-VS: `controller.GetPostVSData(inst, view, rd.MeshDataStage.VSOut)` then
  `GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0)`; positions are `<ffff>` (x,y,z,w),
  NDC x = x/w. `SetFrameEvent(eventId, True)` before per-draw queries - it REPLAYS (slow), so
  SAMPLE draws (every Nth), don't iterate all ~1300.
- **Launch is flaky:** qrenderdoc sometimes doesn't run the script. Kill ALL qrenderdoc procs
  first (`Get-Process qrenderdoc | Stop-Process -Force`), wait ~5s, launch, wait ~40s for init
  before polling. Retry if `rd_out.txt` never appears. Convert-to-XML (`renderdoccmd convert -c
  xml`) is a reliable fallback for API-stream (viewport/scissor/renderpass) but NOT post-VS.

## What it proves (the BD tiling diagnosis)
Screenshots + registers said the BD field buildings should render (scissor/render-area/image
all full 1280) but they were black. The replay showed field draws with **post-VS NDC x beyond
[-1,1]** (e.g. 0.78..1.03, or -5.16..-3.82 fully off-screen) = frustum-clipped. So BD renders
the field in windows whose per-draw ndc_offset positions them; a uniform offset treatment
clips one window off-screen. That per-draw truth is invisible to CPU-side register logs and
ONLY visible via replay. See `docs/research/20260705-native-vulkan-renderer-plan.md` +
`exp_ledger.py check "RenderDoc"`.
