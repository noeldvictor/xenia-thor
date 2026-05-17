# Android Vulkan Layer, RenderDoc, And Ghidra Debug Notes

Created: 2026-05-17 16:31:29 -04:00

## Scope

This note defines the debug split for the local `noeldvictor/xenia-thor`
research fork. It is not official Xenia guidance and should not be used to file
upstream Xenia issues.

## Sources Checked

- Android Developers, "Vulkan validation layers on Android", accessed
  2026-05-17:
  https://developer.android.com/ndk/guides/graphics/validation-layer
  - The page was last updated 2026-05-02 UTC.
  - It documents Android Vulkan layer discovery, per-app GPU debug settings,
    `gpu_debug_app`, `gpu_debug_layers`, `gpu_debug_layer_app`, and the global
    `debug.vulkan.layers` property.

## Decisions

- Keep APK/UI rebuilds separate from native core rebuilds:
  - `ApkShell`: Java, XML, resources, manifest, and packaging with the last
    existing native library.
  - `NativeCore`: C++/Vulkan/CPU backend library rebuild only.
  - `FullApk`: native core plus APK packaging.
- Keep Ghidra in two lanes:
  - Native lane: unstripped `libxenia-app.so` for Android ARM64 crashes, JNI,
    Vulkan, and ARM64 backend code.
  - Guest lane: legally owned and locally extracted/decrypted Xbox 360 PPC code
    only, guided by Xenia guest addresses from logs.
- Use RenderDoc as a Vulkan frame-capture lane, not as a CPU/JIT debugger.
  RenderDoc should be enabled only for focused graphics captures because it can
  perturb timing and Vulkan behavior.
- Add the Android manifest metadata
  `com.android.graphics.injectLayers.enable=true` so Android 11+ layer injection
  remains available for debug/testing builds.

## Android GPU Layer Commands

Per-app layer setup:

```powershell
adb shell settings put global enable_gpu_debug_layers 1
adb shell settings put global gpu_debug_app jp.xenia.emulator.github.debug
adb shell settings put global gpu_debug_layers VK_LAYER_RENDERDOC_Capture
adb shell settings put global gpu_debug_layer_app <installed-renderdoc-layer-package>
```

Global layer setup until reboot:

```powershell
adb shell setprop debug.vulkan.layers VK_LAYER_RENDERDOC_Capture
```

Cleanup:

```powershell
adb shell settings delete global enable_gpu_debug_layers
adb shell settings delete global gpu_debug_app
adb shell settings delete global gpu_debug_layers
adb shell settings delete global gpu_debug_layer_app
adb shell setprop debug.vulkan.layers ""
```

The repo wrapper for this is:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode EnablePerApp
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Disable
```

## RenderDoc Caveats

- The exact RenderDoc Android layer package can vary by install. Check with:

```powershell
adb shell pm list packages | grep -i renderdoc
```

- Captures should include:
  - Xenia branch and commit.
  - APK SHA256.
  - Thor Vulkan driver lines.
  - The exact target path/content URI.
  - A screenshot and full logcat.
- A RenderDoc capture that shows a Vulkan issue should be paired with the same
  run without RenderDoc to avoid chasing capture-layer side effects.

## Ghidra Caveats

- Do not import or commit ISO contents, extracted XEX files, title keys, account
  data, or copyrighted screenshots.
- Native imports should use:

```text
android/android_studio_project/app/build/intermediates/ndkBuild/githubDebug/obj/local/arm64-v8a/libxenia-app.so
```

- Guest PPC imports should be documented by title, local legal source, guest
  address range, and Xenia log artifact only.
