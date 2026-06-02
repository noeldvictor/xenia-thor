-- libadrenotools (bylaws/libadrenotools) — rootless custom Vulkan driver (Turnip)
-- loading for Android. ARM64-ONLY (the only ABI Turnip/adrenotools support); every
-- project here is ExcludeFromBuild on any non-Android-ARM64 platform, so the desktop
-- and Android-x86_64 builds are unaffected. Used only behind the default-OFF
-- gpu_vulkan_driver=turnip cvar (see src/xenia/ui/vulkan/vulkan_instance.cc).
--
-- Four projects:
--   linkernsbypass (StaticLib) — Android linker-namespace bypass helper.
--   adrenotools    (StaticLib) — driver.cpp (adrenotools_open_libvulkan); links into xenia-app.
--   hook_impl      (SharedLib) -> libhook_impl.so  (runtime-dlopen'd by adrenotools)
--   main_hook      (SharedLib) -> libmain_hook.so  (runtime-dlopen'd by adrenotools)
-- The two SharedLibs are NOT linked by xenia-app (they are dlopen'd at runtime from
-- nativeLibraryDir); they are built+packaged by adding them to the Gradle ndk-build
-- `targets` list, and the APK must use useLegacyPackaging=true so they land in
-- nativeLibraryDir (libadrenotools requirement).

local ARM64_ONLY = { "platforms:not Android-ARM64" }

group("third_party")

project("linkernsbypass")
  uuid("a3d5e1b2-7c64-4f0a-9b11-2e6c0f3a4d51")
  kind("StaticLib")
  language("C++")
  files({
    "adrenotools/lib/linkernsbypass/android_linker_ns.cpp",
    "adrenotools/lib/linkernsbypass/android_linker_ns.h",
    "adrenotools/lib/linkernsbypass/elf_soname_patcher.cpp",
    "adrenotools/lib/linkernsbypass/elf_soname_patcher.h",
  })
  includedirs({
    "adrenotools/lib/linkernsbypass",
  })
  links({ "android", "dl" })
  filter(ARM64_ONLY)
    flags({ "ExcludeFromBuild" })

project("adrenotools")
  uuid("c8f2a906-5d3e-43b7-8a2c-1f9b7e0d6a42")
  kind("StaticLib")
  language("C++")
  -- driver.cpp ONLY — provides adrenotools_open_libvulkan. bcenabler.cpp + its .s ASM
  -- (BC texture-format enabling) are an unused, build-complicating feature; skipped.
  files({
    "adrenotools/src/driver.cpp",
    "adrenotools/include/adrenotools/driver.h",
    "adrenotools/include/adrenotools/priv.h",
  })
  includedirs({
    "adrenotools/include",
    "adrenotools/src",                    -- driver.cpp does #include "hook/..."
    "adrenotools/lib/linkernsbypass",     -- #include <android_linker_ns.h>
  })
  links({ "linkernsbypass", "android" })
  filter(ARM64_ONLY)
    flags({ "ExcludeFromBuild" })

-- main_hook/hook_impl are listed in the Gradle ndk-build `targets`, so they must be
-- valid targets for EVERY built ABI. The real ARM64 hook can't build on x86_64
-- (libadrenotools/Turnip are ARM64-only), so on Android-x86_64 they build an empty
-- stub .so (never loaded — the loader is XE_ARCH_ARM64-guarded).
project("hook_impl")
  uuid("e1b47d38-2a90-4c55-b6f3-0c5a8d2e7b13")
  kind("SharedLib")        -- -> libhook_impl.so
  language("C++")
  filter("platforms:Android-ARM64")
    files({
      "adrenotools/src/hook/hook_impl.cpp",
      "adrenotools/src/hook/hook_impl.h",
      "adrenotools/src/hook/hook_impl_params.h",
    })
    includedirs({
      "adrenotools/include",
      "adrenotools/src",
      "adrenotools/lib/linkernsbypass",
    })
    links({ "linkernsbypass", "log" })
  filter("platforms:not Android-ARM64")
    files({ "adrenotools_arch_stub.cc" })

project("main_hook")
  uuid("f6c30a71-9e84-4d22-a1b8-3d7e6f08c294")
  kind("SharedLib")        -- -> libmain_hook.so
  language("C++")          -- main_hook.c compiles as C by extension; stub is .cc
  filter("platforms:Android-ARM64")
    files({
      "adrenotools/src/hook/main_hook.c",
    })
    includedirs({
      "adrenotools/include",
      "adrenotools/src",
    })
    links({ "hook_impl" })
    -- libadrenotools requires main_hook to export its symbols globally so the loaded
    -- driver can resolve the intercepted entry points.
    linkoptions({ "-z", "global" })
  filter("platforms:not Android-ARM64")
    files({ "adrenotools_arch_stub.cc" })
