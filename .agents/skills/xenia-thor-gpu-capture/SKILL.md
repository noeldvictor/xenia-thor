---
name: xenia-thor-gpu-capture
description: Capture and inspect a single Vulkan frame from xenia-thor on the AYN Thor (Adreno 740) to diagnose black-screen / wrong-render bugs - especially on the Turnip (Mesa) driver loaded via libadrenotools. Answers "did geometry rasterize, is the color/depth render target written, what do the attachments contain" for one frame, headless over adb, on a non-rooted device.
---

# Xenia Thor GPU Capture

Use this when BD (or any title) renders black / wrong on the device and you need
to know **where in the GPU frame it breaks** - did draws issue, did geometry
rasterize, is the color attachment written, is the resolve/present the problem.

## First Rule: prefer xenia's own dump cvars (Lane A), not a capture layer

On the Thor, Turnip is loaded **in-process via `adrenotools_open_libvulkan`**
(`src/xenia/ui/vulkan/vulkan_instance.cc:~104-143,164`), which dlopens the
custom `.so` and hands the app its `vkGetInstanceProcAddr`. Whether a standard
gfxreconstruct/RenderDoc **layer** (inserted via the Android Vulkan loader's
debug-layer mechanism) actually interposes *that* path is **unverified** - the
layer-discovery + isolated-namespace + custom-driver combo is fragile. So a
`.gfxr`/`.rdc` of the **Turnip** frame is NOT a reliable default.

Instead, **Lane A** reads the color/depth render-target images back with xenia's
own `vkCmdCopyImageToBuffer` (`vulkan_render_target_cache.cc:~6239 / ~6389`,
gated by cvars) and logs a checksum line. This is xenia's own Vulkan call, so it
runs **identically on Turnip or the proprietary driver**, headless over adb, no
APK/layer install, no loader gamble. It directly answers the black-screen fork.

RenderDoc's in-app API (`src/xenia/ui/vulkan/renderdoc_api.cc:38`) is
`dlopen(RTLD_NOLOAD)` = attach-only, and `StartFrameCapture/EndFrameCapture` are
only wired into the desktop console tool (`vulkan_trace_dump_main.cc:~36,47`),
NOT the Android present path - so programmatic RenderDoc capture on-device does
not work today (see Future Fix).

Always confirm the run reached a real frame before trusting any number: read a
screenshot, and check `Loaded Turnip Vulkan driver` is in logcat for a Turnip
claim. See `xenia-thor-evidence-discipline` and `xenia-thor-gpu-profile`.

## Never-thrash gate (every launch)

Per CLAUDE.md + `xenia-thor-experiment-gate`: BEFORE any launch read
`/sys/class/kgsl/kgsl-3d0/temp` (milli-C; require < 55000) and confirm
`pidof jp.xenia.emulator.github.debug` is empty. ONE launch per step; a watchdog
that force-stops at >= 64C; `am force-stop` + `svc power stayon false` after.
Wake the screen first (`input keyevent KEYCODE_WAKEUP; svc power stayon true`) -
a sleeping screen pauses the guest (black is not a regression). Prefer
`tools/thor/thor_evidence.ps1`, which enforces the cool/force-stop/watchdog.

## hooks_path gotcha (re-derive after EVERY install)

`adb install -r` changes the app's `/data/app/~~<hash>~~/...-<hash2>` dir, so the
hardcoded `gpu_vulkan_driver_hooks_path` goes STALE -> Turnip enumerates 0
devices ("No Vulkan physical devices available") -> black, busy 0. After every
install re-derive it:

```powershell
$adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe"
$base = (& $adb -s c3ca0370 shell pm path jp.xenia.emulator.github.debug) -replace '^package:','' -replace '/base\.apk\s*$',''
$hooks = "$base/lib/arm64"   # pass as --es gpu_vulkan_driver_hooks_path $hooks
```

## Lane A - in-tree attachment readback (DEFAULT, works on Turnip)

The decisive cvars (all default-off, allowlisted in `EmulatorActivity.java`):

- `--ez vulkan_trace_dump_rt_image true`   - reads back the widest base-0 1xMSAA
  COLOR RT image (the present target), logs `dump RT IMAGE checksum ... nonzero=N varying=N`.
- `--ez vulkan_trace_dump_depth_image true` - same for the base-0 1xMSAA DEPTH RT,
  logs `dump DEPTH IMAGE checksum ... nonzero=N varying=N`.
- `--ez vulkan_trace_draw_outcomes_per_frame true` - logs `GPU draw outcomes/frame ... rendered=N ... guest_ms=N`.

Procedure (run via `tools/thor/thor_gpu_capture.ps1 -Mode LaneA -Driver turnip`,
or by hand): gate -> wake screen -> `logcat -G 64M; logcat -c` -> launch BD with
the Turnip cvars + the three dump cvars + the intro-skip `hid_nop_button_sequence`
-> watchdog to the heavy 3D scene (~120-150s; confirm `rendered ~= 1000+` and
`guest_ms ~= 150000` in draw-outcomes so it actually reached the scene, not a
flaky early-boot hang) -> read the three logcat lines + a screenshot -> force-stop.
Repeat with `-Driver system` for the reference, then diff at matched `guest_ms`.

**Decision rule** (per the in-code comment at `vulkan_render_target_cache.cc:~6290`):

| signal | meaning |
|---|---|
| `rendered == 0` | draws not issued (CPU/command-processor problem, not GPU) |
| DEPTH `varying` HIGH (hundreds+) | geometry rasterized -> if COLOR `nonzero=0`, it's a color-write / resolve bug |
| DEPTH `varying` ~ 0 / only the clear value | no geometry rasterized -> vertex/position/clip/cull/viewport bug |
| COLOR `nonzero > 0` | the color target IS written -> bug is downstream (dump/resolve/present) |

Parse + diff two runs:

```powershell
python tools\thor\thor_inspect_capture.py turnip_logcat.txt system_logcat.txt
```

## Lane B - gfxreconstruct reference frame (SYSTEM driver only)

Only for a known-good reference through the normal loader (NOT a Turnip capture).
Debuggable-app layer enable needs no root:

```powershell
$adb shell settings put global enable_gpu_debug_layers 1
$adb shell settings put global gpu_debug_app jp.xenia.emulator.github.debug
$adb shell settings put global gpu_debug_layers VK_LAYER_LUNARG_gfxreconstruct
$adb shell settings put global gpu_debug_layer_app <gfxr-replay-apk-pkg>   # provides the layer .so
# launch with --es gpu vulkan (NO turnip cvars = system driver), GFXRECON_CAPTURE_FILE via the layer
# pull the .gfxr, then host-side: gfxrecon-info / gfxrecon-convert --output ref.jsonl / gfxrecon-extract --dir shaders
# IMMEDIATELY delete the four gpu_debug_* settings after.
```
Replay an Adreno capture **on the device** (`gfxrecon-replay --dump-resources`),
not on the Windows GPU (cross-arch memory-type translation can mask the bug).
For the RenderDoc layer enable/disable, reuse `tools/thor/thor_renderdoc.ps1`.

## Future Fix (enables a true Turnip RenderDoc capture)

Wire `renderdoc_api_->StartFrameCapture(...)` / `EndFrameCapture(...)` around one
frame in the Android present path (`vulkan_command_processor.cc` IssueSwap),
copying the `vulkan_trace_dump_main.cc:~29-49` pattern, gated by a cvar. With the
RenderDoc Android layer attached this captures the actual Turnip-fed `VkQueue`
regardless of the loader chain. Until then, Lane A is the Turnip answer.

## Files To Inspect

- `src/xenia/ui/vulkan/vulkan_instance.cc` (~104-143,164 - adrenotools load + cvars)
- `src/xenia/gpu/vulkan/vulkan_render_target_cache.cc` (~6239 RT readback, ~6290 decision comment, ~6389 depth readback)
- `src/xenia/ui/vulkan/renderdoc_api.cc` (:38 RTLD_NOLOAD attach-only)
- `src/xenia/gpu/vulkan/vulkan_trace_dump_main.cc` (~36 the only StartFrameCapture)
- `android/.../EmulatorActivity.java` (the cvar allowlist)
- `tools/thor/thor_evidence.ps1`, `tools/thor/thor_renderdoc.ps1`

## Output

End with: which driver(s) captured, the three logcat values (rendered / depth
varying / color nonzero) per driver, the failure mode from the decision rule,
the capture/artifact paths, and the next experiment + cvars.
