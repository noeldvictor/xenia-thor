# Turnip Driver on xenia-thor — Scope & Plan (2026-06-02)

Lead-architect synthesis of the scope designs + adversarial verdicts for the directive
**"add turnip driver install and test to xenia ui, make turnip default."**

Target device: AYN Thor, Adreno 740v2, Snapdragon 8 Gen 2 (QCS8550), Android 13 (SDK 33),
kernel 5.15, **NON-ROOTED** (uid 2000 shell, no `su`). Stock GPU driver is the **proprietary
Qualcomm Adreno Vulkan 1.3** (not Turnip). Turnip = Mesa's open-source Vulkan driver for Adreno
(`libvulkan_freedreno.so`), loaded rootless via libadrenotools.

All file:line anchors below were re-verified against the working tree this session.

---

## 1. GO / NO-GO — is Turnip-on-this-device feasible?

**GO for LOADABILITY (with a mandatory safe fallback). NO-GO for "make it the default."**

### verify:will-it-load — YES, the load mechanism is sound and the fallback is real

The single Android Vulkan loader hook point is verified at
`src/xenia/ui/vulkan/vulkan_instance.cc:54-63`:

```cpp
#if XE_PLATFORM_ANDROID
  const char* const loader_library_name = "libvulkan.so";
#else
  const char* const loader_library_name = "libvulkan.so.1";
#endif
  vulkan_instance->loader_ = dlopen(loader_library_name, RTLD_NOW | RTLD_LOCAL);
  if (!vulkan_instance->loader_) {
    XELOGE("Failed to load {}", loader_library_name);
    return nullptr;
  }
```

This is exactly the slot libadrenotools' `adrenotools_open_libvulkan` is designed to replace:
it returns a handle that the existing `dlsym(loader_, "vkGetInstanceProcAddr")` at
`vulkan_instance.cc:64-82` resolves identically. The destructor already pairs the handle with
`dlclose(loader_)` at `vulkan_instance.cc:543-546`, and adrenotools handles are
`dlclose`-compatible — so cleanup needs no change. `loader_` is a `void*` member under
`#if XE_PLATFORM_LINUX` (Android is Linux here), set inside the static `Create()`.

Why it works rootless on THIS device (from the research, [HIGH] confidence): libadrenotools
replaces **no** system file. It builds an isolated linker namespace, preloads its hook payload
(`libmain_hook.so` / `libhook_impl.so`) and substitutes a custom ICD from app-private storage —
all under the app's own UID, no `su`. Turnip's KGSL kernel backend (`freedreno-kmds=kgsl`) matches
this device's stock KGSL interface, and the community packages declare `minApi: 27` ≤ our API 33.
Precedent: PPSSPP, Skyline, yuzu/Citron, Vita3K, Winlator all do this on non-rooted Adreno.

**The fallback is the load-bearing safety property:** if `adrenotools_open_libvulkan` returns null
for ANY reason (toggle off, bad path, missing hook libs, ICD reject, a740 instability), the code
falls straight back to the existing `dlopen("libvulkan.so")` → proprietary driver. A second net
exists downstream: if Turnip loads but exposes no compatible device, `VulkanProvider::Create` /
`VulkanDevice::CreateIfSupported` already return `nullptr` and GPU init fails cleanly rather than
crashing. So the **worst realistic outcome of the load increment is "we silently ran on the
proprietary driver,"** never a brick — provided the compiled cvar default stays `system`.

### What is NOT a GO

- **Performance:** Turnip is **not** a perf lever for Blue Dragon (§4). The device-proven bottleneck
  is the hardware geometry/binning front-end (`gpu_busy 77-79%`, ~300µs/draw), identical across
  Vulkan drivers. Turnip moves none of it.
- **FSI / single-pass EDRAM "headline":** does not materialize. The FSI path is **opt-in via an
  explicit cvar** (`render_target_path_vulkan=="fsi"`, `vulkan_render_target_cache.cc:281-286`), it is
  NOT auto-selected on interlock capability — so a driver swap alone changes nothing about EDRAM. And
  even when requested, xenia documents the path as *"much slower now"* (`vulkan_render_target_cache.cc:55-57`).
  Mainline a7xx Turnip is also not known to expose `VK_EXT_fragment_shader_interlock` anyway.
- **a740/a7xx maturity:** Turnip's newest, least-conformance-hardened tier ([MED]). Reliability of a
  heavy Vulkan app like xenia on THIS retail SD8G2 is inferred from other emulators, not demonstrated.

**Verdict:** GO to ship Turnip as a **default-OFF, opt-in compatibility/experiment feature** with a
proven automatic fallback. NO-GO to make it the shipped default on this device (§5 default decision).

---

## 2. Minimal first INCREMENT — load a user-imported Turnip, log driverID, fall back

Goal: build-verifiable; one supervised device launch reading the driver-ID log line; **no Settings UI
yet**. The cvar default is `system`, so this increment cannot regress any title.

### 2a. New cvars (`src/xenia/ui/vulkan/vulkan_instance.cc`, near the existing `DEFINE_bool` at :29)

```cpp
DEFINE_string(gpu_vulkan_driver, "system",
    "Vulkan ICD to load on Android: 'system' (proprietary Qualcomm, default) "
    "or 'turnip' (user-imported Mesa Turnip via libadrenotools).", "Vulkan");
DEFINE_string(gpu_vulkan_driver_path, "",
    "Absolute dir containing the imported Turnip .so (+ meta.json) "
    "when gpu_vulkan_driver=turnip.", "Vulkan");
DEFINE_string(gpu_vulkan_driver_lib, "libvulkan_freedreno.so",
    "Filename of the Turnip ICD .so inside gpu_vulkan_driver_path.", "Vulkan");
DEFINE_string(gpu_vulkan_driver_hooks_path, "",
    "Absolute dir holding libadrenotools' hook .so (the app nativeLibraryDir).",
    "Vulkan");
```

- `DEFINE_string` (not `DEFINE_path`) so `cvar_android.cc:167-183` handles them; the absolute Android
  paths cross from Java as plain strings, which is preferable for **normalization predictability**
  (both string and path handlers exist; the string handler avoids `std::filesystem::path` surprises).
- **Compiled default `system`** — satisfies default-OFF and respects the Config-gotcha
  (`files/xenia.config.toml` overrides compiled defaults; only `--ez/--ei/--es` beat it). The user's
  "make turnip default" intent is honored later at the UI layer ONLY after the A/B gate (§5).
- The 4th cvar (`..._hooks_path`) is REQUIRED because libadrenotools' hook payload lives in
  `nativeLibraryDir`, NOT in the imported-driver dir (see §3 build-architecture fix).

### 2b. The loader hook (`src/xenia/ui/vulkan/vulkan_instance.cc:52-63`, the core change)

Wrap the Android dlopen. Guard with **both** Android AND arm64 (`XE_ARCH_ARM64`, confirmed the
correct macro at `platform.h:61`) so the x86_64 Android ABI keeps the plain dlopen path:

```cpp
#if XE_PLATFORM_ANDROID && XE_ARCH_ARM64
#include <adrenotools/driver.h>
#endif
...
#if XE_PLATFORM_LINUX
#if XE_PLATFORM_ANDROID
  const char* const loader_library_name = "libvulkan.so";
  void* loader = nullptr;
#if XE_ARCH_ARM64
  if (cvars::gpu_vulkan_driver == "turnip" &&
      !cvars::gpu_vulkan_driver_path.empty() &&
      !cvars::gpu_vulkan_driver_hooks_path.empty()) {
    loader = adrenotools_open_libvulkan(
        RTLD_NOW | RTLD_LOCAL,
        ADRENOTOOLS_DRIVER_CUSTOM,
        /*tmpLibDir=*/nullptr,                                   // unused on API>=29
        /*hookLibDir=*/cvars::gpu_vulkan_driver_hooks_path.c_str(),  // nativeLibraryDir
        /*customDriverDir=*/cvars::gpu_vulkan_driver_path.c_str(),
        /*customDriverName=*/cvars::gpu_vulkan_driver_lib.c_str(),
        /*fileRedirectDir=*/nullptr,
        /*userMappingHandle=*/nullptr);
    if (!loader) {
      XELOGW("Turnip load failed (dir={}, lib={}, hooks={}); falling back to "
             "system Vulkan", cvars::gpu_vulkan_driver_path,
             cvars::gpu_vulkan_driver_lib, cvars::gpu_vulkan_driver_hooks_path);
    } else {
      XELOGI("Loaded Turnip via libadrenotools from {}",
             cvars::gpu_vulkan_driver_path);
    }
  }
#endif  // XE_ARCH_ARM64
  if (!loader) {
    loader = dlopen(loader_library_name, RTLD_NOW | RTLD_LOCAL);  // existing path
  }
  vulkan_instance->loader_ = loader;
  if (!vulkan_instance->loader_) {
    XELOGE("Failed to load {}", loader_library_name);
    return nullptr;
  }
#else  // non-Android Linux
  const char* const loader_library_name = "libvulkan.so.1";
  vulkan_instance->loader_ = dlopen(loader_library_name, RTLD_NOW | RTLD_LOCAL);
  if (!vulkan_instance->loader_) { XELOGE(...); return nullptr; }
#endif
```

> **Signature caveat (adversarial flaw, accepted):** `third_party/adrenotools` is **not vendored
> yet** (Glob confirmed zero files), so the `adrenotools_open_libvulkan` argument order/flags above
> are from the upstream `include/adrenotools/driver.h` per the research, **NOT** read from this tree.
> Treat the call as PSEUDOCODE. **Before writing the wrapper, vendor the lib and pin the prototype +
> `ADRENOTOOLS_DRIVER_CUSTOM` value to the real header with a file:line cite.** A wrong COMPILE-time
> signature breaks the build (the runtime null-fallback only de-risks a runtime failure, not a
> compile error).

**No device-side logging change needed:** `vulkan_device.cc:571-583` already prints `driverID`,
`driverName`, `driverInfo`, `conformanceVersion` (gated on `ext_1_2_KHR_driver_properties`).
Success shows `driverID: VK_DRIVER_ID_MESA_TURNIP`; fallback shows the Qualcomm proprietary ID. That
single early-init logcat line is the entire verification for this increment — no game-load required.

### 2c. EmulatorActivity allowlist (`EmulatorActivity.java`, next to the existing `--es gpu` copies at :129-164)

A cvar absent from this allowlist is silently dropped (the filter is `onCreate` at :125-...). Add:

```java
copyStringExtra(intent, launchArguments, "gpu_vulkan_driver");
copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_path");
copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_lib");
copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_hooks_path");
```

(`copyStringExtra` already exists; pattern verified at `EmulatorActivity.java:129-164`.)

### 2d. Native build — vendor libadrenotools (premake → ndk-build)

`.Android.mk` files are **generated by premake**; never hand-edit `build/*.Android.mk`. Edits:

1. **Vendor** `third_party/adrenotools/` (from bylaws/libadrenotools — pin a known-good commit) +
   `third_party/adrenotools.lua` modeled on `third_party/snappy.lua` (confirmed present, the
   vendoring template). **arm64-ONLY** (adversarial flaw #2 fix — `not Android-*` does NOT exclude the
   x86_64 Android ABI):

   ```lua
   group("third_party")
   project("adrenotools")
     uuid("<new-guid>")
     kind("StaticLib")
     language("C++")
     defines({ "_LIB" })
     includedirs({ "adrenotools/include", "adrenotools/lib/linkernsbypass" })
     files({ "adrenotools/src/*.cpp", "adrenotools/src/*.c" })
     filter("platforms:not Android-ARM64")   -- exclude desktop AND Android-x86_64
       flags({ "ExcludeFromBuild" })
   ```

   Register at the root include list in `premake5.lua` (the `include("third_party/...lua")` block)
   ideally inside an Android guard.

2. **CRITICAL build-architecture fix (adversarial flaw #1, fatal-if-missed):** the StaticLib above
   only emits the adrenotools *entry point* into `libxenia-app.so`. The rootless hook **requires
   standalone `.so` files** (`libmain_hook.so`, `libhook_impl.so`, + the `linkernsbypass` helper) to
   exist in `nativeLibraryDir` (the `hookLibDir` argument), because they are **runtime-dlopen'd, not
   link-time deps**. A StaticLib produces no such `.so`, and gradle's `targets 'xenia-app'`
   (`app/build.gradle:27`) builds only xenia-app + its **link-time** deps — so the hook libs would be
   neither built nor packaged → `adrenotools_open_libvulkan` returns null on **every** device and
   Turnip never loads. **Fix:** add `libmain_hook` / `libhook_impl` / `linkernsbypass` as separate
   premake **`SharedLib`** projects (arm64-only) so ndk-build emits real `.so`. Make them link-time
   deps of xenia-app (add to `links()` at `src/xenia/app/premake5.lua:96-99`) **or** add them to the
   gradle `targets` list at `app/build.gradle:27`, so they get built and packaged into the APK lib
   dir. Then Java passes `hooksDir = context.getApplicationInfo().getNativeLibraryDir()` as the
   `gpu_vulkan_driver_hooks_path` cvar.

3. **Link from the app** under `filter("platforms:Android-*")` at `src/xenia/app/premake5.lua:95-102`
   (next to `xenia-apu-android`/`xenia-hid-android`):

   ```lua
   filter("platforms:Android-*")
     links({ "xenia-apu-android", "xenia-hid-android",
             "adrenotools", "main_hook", "hook_impl", "linkernsbypass" })
     links({ "aaudio" })
   ```

   (Only `adrenotools` is a true code dependency; the three hook libs are listed here purely to force
   ndk-build/gradle to build+package them — confirm they don't introduce duplicate-symbol link
   errors; if they do, package them via the gradle `targets` list instead of `links()`.)

4. **Add the include dir** to `xenia-ui-vulkan` (`src/xenia/ui/vulkan/premake5.lua` includedirs) so
   `vulkan_instance.cc` finds `<adrenotools/driver.h>`:

   ```lua
   includedirs({
     project_root.."/third_party/Vulkan-Headers/include",
     project_root.."/third_party/adrenotools/include",
   })
   ```

5. **Regenerate** `.Android.mk` (the `xb` / `premake5 androidndk` wrapper), then build via the
   documented gradle command. Confirm `build/adrenotools.prj.Android.mk` and the new hook-lib mk files
   appear and that the x86_64 Android variant still compiles (the arch guard must hold).

### 2e. Minimal launch test (no UI yet)

Push a KGSL-built Turnip `.adpkg` (Weab-chan / K11MCH1 A7xx build) `.so` + `meta.json` to
`files/turnip/`, then launch with the extras (now allow-listed):

```
--es gpu_vulkan_driver turnip
--es gpu_vulkan_driver_path /data/data/jp.xenia.emulator.github.debug/files/turnip
--es gpu_vulkan_driver_lib libvulkan_freedreno.so
--es gpu_vulkan_driver_hooks_path <getApplicationInfo().nativeLibraryDir>
```

Verify in logcat that `driverID`/`driverName` (`vulkan_device.cc:571-583`) flips to
`VK_DRIVER_ID_MESA_TURNIP`, and that a deliberately-bad path cleanly falls back (the `XELOGW` + plain
`dlopen` lines). **This launch MUST route through `tools/thor/thor_evidence.ps1`** (pre-launch
busy/temp gate + watchdog + force-stop), per NEVER THRASH (§5). The driver-ID line is in early init,
so a single short supervised launch suffices; no game-load needed to confirm the hook.

**Only after this load+log+fallback increment is proven on-device** do we add the Settings UI below.

### 2f. Second increment — the Settings UI (import + select), default-OFF

Mirror existing patterns in `SettingsActivity.java` / `XeniaAndroidSettings.java`:
- Preference keys `KEY_VULKAN_DRIVER="vulkan_driver"` (+ dir), constants `VULKAN_DRIVER_SYSTEM`/
  `VULKAN_DRIVER_TURNIP`, modeled on the existing `KEY_GPU_DRIVER`.
- Radio group "Vulkan Driver {System (Qualcomm), Turnip (Mesa)}" via the existing `addRadioGroup(...)`;
  load/save in `loadFromPreferences`/`saveToPreferences` like `KEY_GPU_DRIVER`.
- An **"Import Turnip driver…"** button (`ACTION_OPEN_DOCUMENT` for `.zip`/`.adpkg`, same pattern as
  `LauncherActivity`), unzip into `getFilesDir()/turnip/`, parse `meta.json` for `libraryName`. Grey
  out the Turnip radio until a valid package is present.
- Emit in `XeniaAndroidSettings.createLaunchArguments` after the `gpu` put: the 4 cvars, with
  `gpu_vulkan_driver_path = getFilesDir()/turnip` and
  `gpu_vulkan_driver_hooks_path = getApplicationInfo().getNativeLibraryDir()`.
- **Re-init on toggle:** the driver is chosen once in `VulkanInstance::Create` (per process start), so
  switching requires relaunching EmulatorActivity — matches all existing `--es gpu` choices, which
  also only take effect next launch. Show the standard "applies on next launch" toast.
- **UI default stays System** until the A/B gate passes (§5).

### 2g. Driver DOWNLOAD manager + per-device RECOMMENDATION (user directive: "download + select + recommend best for AYN Thor")

Extend §2f's import-only flow into a full **GPU Driver Manager** (the pattern Yuzu/Citron/Eden ship),
so the user does not have to find/sideload a `.adpkg.zip` by hand:

- **Source (in-app download):** **`K11MCH1/AdrenoToolsDrivers`** GitHub releases — the community-standard
  Turnip distribution for AdrenoTools loaders. Fetch the release/asset list from the public GitHub API
  (`https://api.github.com/repos/K11MCH1/AdrenoToolsDrivers/releases`, no auth, paginated) on a worker
  thread; list assets matching `Turnip-*.adpkg.zip`. Tapping one downloads it to
  `getFilesDir()/turnip_pkgs/<name>/`, validates `meta.json` (`schemaVersion`, `libraryName`,
  `minApi<=33`), and registers it in the Installed list. (Mirror source: `StevenMXZ/Adreno-Tools-Drivers`.)
- **Format:** `.adpkg.zip` = zip with `meta.json` + `libvulkan_freedreno.so`, loaded **without unzipping
  the .so** by libadrenotools; we extract to a dir and point `gpu_vulkan_driver_path` at it +
  `gpu_vulkan_driver_lib` = the `libraryName` from `meta.json` (usually `libvulkan_freedreno.so`).
- **Manifest:** add `<uses-permission android:name="android.permission.INTERNET"/>` (download is
  user-initiated, opt-in; the emulator core itself stays offline). Turnip is MIT/Mesa-licensed →
  redistribution-safe, but downloading on explicit user action from the upstream repo is the cleanest.
- **RECOMMENDATION (researched, device-targeted):** surface a curated GPU→driver map so the user is told
  the best build for their hardware. For the AYN Thor's **Adreno 740 (a7xx)**: **⭐ Turnip v26.0.0 R7**
  (Mesa v25/v26 series — current a7xx community favorite for stability, early 2026), with **v25.3.0** as
  the offered fallback for any title that misbehaves. Implement as a small bundled JSON
  (`assets/turnip_recommended.json`: `{ "Adreno 740": {"recommended":"Turnip-v26.0.0_R7", "fallback":
  "Turnip-v25.3.0"} }`) keyed off the detected GPU model (KGSL `gpu_model` = "Adreno740v2" or the
  Vulkan `deviceName`), so it is data-updatable without a code change. Flag the recommended asset ⭐ and
  sort it to the top of the Download list; show "Recommended for your device (Adreno 740)".
- **UX:** Driver Manager screen with two lists — **Available (download)** (⭐ recommended first) and
  **Installed** (radio-select active; System/Qualcomm is always present and is the default-selected
  entry). Selecting active writes the preference; takes effect next launch (toast). Keep an
  **Import from file…** path too (ACTION_OPEN_DOCUMENT) for offline/custom builds.
- **Discipline:** the recommendation is a *suggestion*, not an auto-install; nothing is downloaded or
  selected without a user tap, and System stays the default until the §5 A/B gate. Network failures
  degrade gracefully to the Import-from-file path.

Sources for the recommendation: [Adreno 740 Turnip guide (Pocketgaming, 2026-02)](https://pocket-gaming.org/2026/02/13/level-up-your-android-emulation-the-adreno-740-turnip-driver-guide/),
[K11MCH1/AdrenoToolsDrivers releases](https://github.com/K11MCH1/AdrenoToolsDrivers/releases/),
[StevenMXZ/Adreno-Tools-Drivers releases](https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases),
[Yuzu/Adreno/Turnip driver list](https://yuzuemulator.org/gpu-drivers/).

---

## 3. TEST plan — A/B Turnip vs proprietary on Blue Dragon

All on-device steps run via **`tools/thor/thor_evidence.ps1`** (enforces the cooldown/busy gate +
watchdog; no hand-rolled `am start` loops). Honor the MEASUREMENT RULE: Blue Dragon content is a
function of guest uptime, so compare configs at **matched `guest_ms` AND equal `rendered` count**;
relaunch A/Bs are scene-confounded.

### T0 — Load & identity (gate for everything else)
- Launch Turnip (§2e). Confirm `driverID: VK_DRIVER_ID_MESA_TURNIP` in logcat.
- Confirm the deliberate-bad-path fallback emits `XELOGW` and runs proprietary. PASS = both paths
  behave; force-stop after. (If T0 fails to load Turnip, stop — fix the build/hook packaging.)

### T1 — Boot & render parity on all four priority titles
For Blue Dragon, Lost Odyssey, Banjo, Burnout: one supervised launch each on Turnip, skip intros to a
known scene, **read the screenshot** (never assert visual state unread). PASS = boots to title and
renders the reference scene with **no black-screen / no corruption** vs the proprietary baseline. Any
black-screen on any title = Turnip stays non-default (and likely a kill signal, §5).

### T2 — Feature exposure on the loaded Turnip device
From the early-init logcat (`vulkan_device.cc` feature dump) record, on Turnip vs proprietary:
- `VK_EXT_fragment_shader_interlock` (`fragmentShaderSampleInterlock`/`fragmentShaderPixelInterlock`).
- `VK_EXT_rasterization_order_attachment_access`.
- `multiDrawIndirect` (already queried at `vulkan_device.cc`), `VK_KHR_draw_indirect_count`.
- Sparse: `sparseResidencyBuffer` (shared-memory path, §4 risk).
This SETTLES the [MED]/[LOW] research items with a primary on-device read. Expectation: FSI absent on
both → no EDRAM rearchitecture justified by Turnip.

### T3 — Does xenia's FSI EDRAM path activate?
Only if T2 shows interlock exposed: relaunch Turnip with `--es render_target_path_vulkan fsi` (already
allow-listed at `EmulatorActivity.java:140`) and read
`VulkanRenderTargetCache: render_target_path_vulkan='fsi' selected=...`
(`vulkan_render_target_cache.cc:333-336`). `selected=fsi` confirms activation; `selected=fbo` means the
feature gate (`:309-316`) rejected it. **Note: a driver swap alone never activates FSI** — it is opt-in
via this explicit cvar (`:281-286`). Even if it activates, xenia documents it as slower (`:55-57`); do
NOT make it default.

### T4 — Content-matched perf delta (the only number that could justify default)
Enable `--ez vulkan_trace_draw_outcomes_per_frame true`. On the Blue Dragon heavy 3D field scene,
capture `gpu_frame_us` on proprietary vs Turnip at **matched `guest_ms` AND equal `rendered`**. Report
the delta with the screenshot. **Expectation per §4: ~zero — the bottleneck is the hardware binning
front-end, driver-independent.** A real, repeatable Turnip win here is the prerequisite for even
discussing default (§5).

---

## 4. Why Turnip is not a perf lever (grounds the default decision)

- **Binning floor is hardware, not driver.** The device-proven bottleneck is the geometry/binning
  front-end (`gpu_busy 77-79% @ 615MHz`, super-linear in draw+vertex count, pixel-independent — CLAUDE.md
  root-cause verdict; `docs/research/20260601-deep-novel-perf-ideation.md`). Both Turnip and the
  Qualcomm driver feed the same binning silicon; the MDI A/B
  (`docs/research/20260602-mdi-device-ab-result.md`) proved each sub-draw bins independently. Turnip
  changes none of it.
- **FSI "headline" deflates twice.** (a) It is opt-in via `render_target_path_vulkan=="fsi"`
  (`vulkan_render_target_cache.cc:281-286`), never auto-selected — a driver swap is decoupled from EDRAM
  path entirely. (b) Even when requested, xenia documents it as *"much slower now"* and
  overdraw-bound (`:54-57`); it attacks the ~12-15% pass-break/tile-traffic minority, not the binning
  majority, and serializes overdraw the FBO path gets free from fixed-function ROP. Realistic EV:
  negative-to-neutral.
- **multiDrawIndirect / draw_indirect_count:** a cheaper Turnip CP path would shave CP overhead, which
  is NOT the bottleneck (`mdi-device-ab-result.md`). EV ~0.

**Net for Blue Dragon: ~zero to negative.** The actual lever stays driver-independent (NEON/GPU-compute
triangle cull; constants→UNIFORM_BUFFER_DYNAMIC descriptor-churn fix).

---

## 5. DEFAULT decision — opt-in first; default gated on the A/B

### verify:default-safety — what must be proven BEFORE Turnip can be default

Per the Config-gotcha, a UI-default of Turnip means the persisted `files/xenia.config.toml` / launch
bundle carries `gpu_vulkan_driver=turnip`, **silently swapping the driver under every future
measurement** — and every device result this session (`gpu_busy 77-79%`, the ~300µs/draw binning floor,
the UMA present-hang verdict) is on the proprietary driver. A driver swap is the **whole-Vulkan-
feature-chain change-class** that black-screened all games before (UMA present-hang regression,
`burnout-uma-present-hang-regression`). Therefore, before default:

1. **T0** loads Turnip and fallback works.
2. **T1** all four priority titles boot to title and render their reference scene cleanly (screenshots
   read), no black-screen / corruption.
3. **T4** Turnip `gpu_frame_us` is **>= proprietary** on the content-matched Blue Dragon heavy scene
   (matched `guest_ms` + equal `rendered`), repeatable.
4. No sparse / feature-chain regression observed (T2 + clean boots).

Only if **all** hold does a default flip get reconsidered. Given §4 (binning floor is hardware-fixed),
the perf win that would justify default is **not expected to materialize**.

### Recommendation — OPT-IN FIRST

Ship Turnip **default-OFF at BOTH layers** (compiled cvar AND the UI/preference default = `system`).
Honor the user's "make turnip default" intent as **"installable + one-tap selectable, fully
supported"** — NOT as the shipped default on this retail device. The directive's *aim* (Turnip default)
is gated on the A/B passing; until then, defaulting it would silently corrupt the perf baseline and
risk the project's highest-regression change-class for no measured gain. If a future patched-Turnip
build demonstrably wins T4 and passes T1, revisit the default then.

---

## 6. Honest risks + kill criteria

| Risk | Severity | Mitigation / status |
|---|---|---|
| Hook libs not packaged (StaticLib only) → null on every device | **Fatal-if-missed** | §2d.2: ship `libmain_hook`/`libhook_impl`/`linkernsbypass` as SharedLib, link-time dep or gradle `targets`. |
| x86_64 Android ABI fails to compile adrenotools | Major | §2b/§2d: guard `XE_ARCH_ARM64` in code; `not Android-ARM64` ExcludeFromBuild in .lua. |
| `adrenotools_open_libvulkan` signature unverified (nothing vendored) | Major | §2b: vendor first, pin prototype/flag to the real header before writing the wrapper. |
| Feature-chain / UMA-style black-screen on a driver swap | Major | Default-OFF both layers; T1 screenshot gate on all 4 titles; automatic fallback. |
| Turnip sparse-buffer **bug** (vs absence) on the 512MB shared-memory buffer | Medium | Existing graceful fallback `vulkan_shared_memory.cc` (gated `vulkan_sparse_shared_memory` + `sparseResidencyBuffer`); set `--ez vulkan_sparse_shared_memory false` if it misbehaves. |
| a740/a7xx Turnip immaturity / instability | Medium | Opt-in only; automatic fallback; one supervised launch at a time. |
| No perf gain (binning floor hardware-fixed) | Expected | §4 — Turnip is a compatibility/experiment feature, not a perf default. |

### Kill criteria (abandon / keep strictly opt-in)
- Turnip **black-screens or corrupts** any priority title (T1) that the proprietary driver renders →
  Turnip stays non-default; if no Turnip build renders Blue Dragon's heavy scene, shelve the feature.
- Turnip is a **net `gpu_frame_us` regression** on the content-matched Blue Dragon scene (T4) → never
  default.
- The hook payload cannot be reliably packaged/loaded on-device after T0 attempts → drop the loader
  increment (the proprietary path is untouched throughout, so this costs nothing live).
- Any device thrash (GPU pegged / temp ≥ ~70°C) during testing → immediate `am force-stop`, cool, and
  reassess; never push through it.

### Discipline
- **Never thrash the Thor:** build is the primary verification; the only device touches are
  T0–T4 via `tools/thor/thor_evidence.ps1` (pre-launch busy/temp gate, watchdog, force-stop after).
  One device action per step; read the result before the next.
- **Bit-exactness:** SPIR-V is unchanged, so guest-visible math is unlikely affected, but a
  driver-level shader-compiler difference must be screenshot-validated per title (the
  `arm64_vmx_dot`/fp16 black-screen guardrail).
- **Never fabricate:** every driverID/feature/`gpu_frame_us`/visual claim comes from device output read
  that turn; unmeasured = "not measured".
- **Targeted `git add` only**; commit each durable, proof-backed step.

---

## Key file:line anchors (re-verified this session)
- Loader hook (the one change): `src/xenia/ui/vulkan/vulkan_instance.cc:52-63`; dlsym `:64-82`;
  `dlclose` destructor `:543-546`; `loader_` member under `XE_PLATFORM_LINUX`.
- Driver-ID logging (reuse, no change): `src/xenia/ui/vulkan/vulkan_device.cc:571-583`.
- FSI is opt-in, NOT auto-selected: `src/xenia/gpu/vulkan/vulkan_render_target_cache.cc:281-286`;
  feature gate `:309-331`; selection log `:333-336`; "FSI much slower now" doc `:39-58`.
- Allowlist + copy helpers: `EmulatorActivity.java:125-164` (`copyStringExtra`, incl.
  `render_target_path_vulkan` at :140).
- Arch macro: `src/xenia/base/platform.h:61` (`XE_ARCH_ARM64`).
- App Android links: `src/xenia/app/premake5.lua:95-102`.
- Build ABIs + targets: `app/build.gradle:27` (`targets 'xenia-app'`), `:31`
  (`abiFilters 'arm64-v8a', 'x86_64'`), `:17-18` ndkBuild Application.mk.
- Vendoring template: `third_party/snappy.lua` (present); `third_party/adrenotools*` **absent** (must
  be vendored).
- cvar→native: `cvar_android.cc:167-200` (string/path handlers).

## Sources / upstream (for the unverified-in-tree items)
libadrenotools (bylaws/libadrenotools — API, hook mechanism, rootless design); PPSSPP integration
(call pattern + meta.json parsing); Weab-chan/freedreno_turnip-CI + K11MCH1/AdrenoToolsDrivers
(KGSL build, package format, A7xx releases); Mesa freedreno docs; Phoronix a730/a740 initial support.
Pin the adrenotools commit + read `include/adrenotools/driver.h` in-tree before writing the wrapper.
