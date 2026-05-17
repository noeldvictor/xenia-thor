# Thor Vulkan Device Baseline

Accessed: 2026-05-17 14:22:24 -04:00

## Device

- ADB serial: `c3ca0370`
- Manufacturer: `AYN`
- Model: `AYN Thor`
- Device/platform: `kalama`
- Android release: `13`
- Android SDK: `33`
- Hardware: `qcom`
- Board platform: `kalama`
- Display: `1080x1920`
- Density: `369`

User-provided model answer:

- Target is Thor Max. Exact storage SKU is still not confirmed beyond "Max".

## Vulkan / GPU Facts

Device features:

- `android.hardware.vulkan.compute`
- `android.hardware.vulkan.level=1`
- `android.hardware.vulkan.version=4206592`
- `android.software.vulkan.deqp.level=132514561`

System properties:

- `ro.boot.hardware.revision`: `Qualcomm 8 Gen 2`
- `ro.hardware.egl`: `adreno`
- `ro.hardware.vulkan`: `adreno`
- `ro.gfx.driver.1`: `com.qualcomm.qti.gpudrivers.kalama.api33`
- GPU frequencies include `680000000` down to `124800000`.

SurfaceFlinger:

- GLES renderer: `Qualcomm, Adreno (TM) 740`
- OpenGL ES driver: `OpenGL ES 3.2 V@0676.53`
- Driver date in GLES string: `12/27/23`

Xenia Vulkan window demo log:

- Vulkan instance API version: `1.3.0`
- Physical device: `Adreno (TM) 740`
- Device API: `1.3.128`, Xenia uses `1.3`
- Vendor ID: `0x5143`
- Device ID: `0x43050A01`
- Driver: `Qualcomm Technologies Inc. Adreno Vulkan Driver`
- Driver ID: `QualcommProprietary`
- Driver build: `69e13475cb, I1df7ad3aa9, 1703680572`
- Compiler version: `E031.41.03.47`
- Conformance version: `1.3.1.0`
- Swapchain: `1920x970`, format `37`, color space `0`, presentation mode `1`

## On-Device Smoke Test

APK:

- `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`
- Package: `jp.xenia.emulator.github.debug`

Commands:

```powershell
adb -s c3ca0370 install -r -d android\android_studio_project\app\build\outputs\apk\github\debug\app-github-debug.apk
adb -s c3ca0370 logcat -c
adb -s c3ca0370 shell am start -n jp.xenia.emulator.github.debug/jp.xenia.emulator.WindowDemoActivity
cmd /c "adb -s c3ca0370 exec-out screencap -p > thor-window-demo.png"
```

Result:

- Install succeeded.
- `WindowDemoActivity` launched.
- Process stayed alive as pid `17973`.
- Xenia created a Vulkan swapchain on Adreno 740.
- Screenshot shows the ImGui demo and metrics/debugger windows rendering.
- ImGui metrics showed roughly `16.818 ms/frame`, about `59.5 FPS`.

Screenshot artifact:

- `thor-window-demo.png`

## Interpretation

This proves Android native APK install, Java/native activity startup, Vulkan instance/device creation, Adreno 740 selection, swapchain creation, and UI rendering on the Thor Max target. It does not prove Xbox 360 game execution.

Game execution remains blocked by:

- Android emulator launcher flow not yet wired for `.xex` / `.iso`.
- `src/xenia/app/premake5.lua` still excludes `xenia_main.cc` on Android.
- ARM64 backend is currently a scaffold; there is no AArch64 JIT yet.

Next target from user:

- "Vulkan game running", with Blue Dragon as likely first legally owned test content.
