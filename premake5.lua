include("tools/build")
require("third_party/premake-export-compile-commands/export-compile-commands")
require("third_party/premake-androidndk/androidndk")
require("third_party/premake-cmake/cmake")

-- Cross-target ARM64 for Linux to run the a64 backend under qemu-user for
-- device-free testing (see the a64-qemu-harness). Only affects Linux builds
-- invoked with --linux-arm64; all other platforms are unchanged.
newoption({
  trigger = "linux-arm64",
  description = "Cross-target ARM64 for Linux (device-free a64 testing under qemu)",
})

location(build_root)
targetdir(build_bin)
objdir(build_obj)

-- Define an ARCH variable
-- Only use this to enable architecture-specific functionality.
if os.istarget("linux") then
  ARCH = os.outputof("uname -p")
else
  ARCH = "unknown"
end

includedirs({
  ".",
  "src",
  "third_party",
})

defines({
  "_UNICODE",
  "UNICODE",
})

-- C++20: xenia-edge's kernel layer (ported 2026-08) uses concepts/requires
-- clauses, designated initializers and std::span in its public headers.
cppdialect("C++20")
exceptionhandling("On")
rtti("On")
symbols("On")

-- TODO(DrChat): Find a way to disable this on other architectures.
if ARCH ~= "ppc64" then
  filter("architecture:x86_64")
    vectorextensions("AVX")
  filter({})
end

characterset("Unicode")
-- The cross GCC used for --linux-arm64 emits warnings MSVC/clang don't; don't
-- make those fatal for the device-free a64 test build.
if not _OPTIONS["linux-arm64"] then
  flags({
    "FatalWarnings",        -- Treat warnings as errors.
  })
end

filter("kind:StaticLib")
  defines({
    "_LIB",
  })

filter("configurations:Checked")
  runtime("Debug")
  optimize("Off")
  defines({
    "DEBUG",
  })
filter({"configurations:Checked", "platforms:Windows"})
  buildoptions({
    "/RTCsu",           -- Full Run-Time Checks.
  })
filter({"configurations:Checked", "platforms:Linux"})
  defines({
    "_GLIBCXX_DEBUG",   -- libstdc++ debug mode
  })

filter("configurations:Debug")
  runtime("Release")
  optimize("Off")
  defines({
    "DEBUG",
    "_NO_DEBUG_HEAP=1",
  })
filter({"configurations:Debug", "platforms:Linux"})
  defines({
    "_GLIBCXX_DEBUG",   -- make dbg symbols work on some distros
  })

filter("configurations:Release")
  runtime("Release")
  defines({
    "NDEBUG",
    "_NO_DEBUG_HEAP=1",
  })
  optimize("Speed")
  inlining("Auto")
  flags({
    "LinkTimeOptimization",
  })
  -- Not using floatingpoint("Fast") - NaN checks are used in some places
  -- (though rarely), overall preferable to avoid any functional differences
  -- between debug and release builds, and to have calculations involved in GPU
  -- (especially anything that may affect vertex position invariance) and CPU
  -- (such as constant propagation) emulation as predictable as possible,
  -- including handling of specials since games make assumptions about them.
filter("platforms:Linux")
  system("linux")
  -- The --linux-arm64 cross build uses aarch64-linux-gnu-g++ (gcc toolset), so
  -- premake emits gcc-style flags (no clang-only -stdlib=libstdc++).
  if _OPTIONS["linux-arm64"] then
    toolset("gcc")
    -- The GCC-15 cross compiler is stricter about transitive std includes than
    -- MSVC/clang; force-include common stdint/stddef/string so the device-free
    -- a64 build doesn't need a header-by-header sweep. Use the C-style headers
    -- (stdint.h, NOT cstdint) so the same forceinclude works in BOTH the C++ TUs
    -- (they get global uint32_t/size_t) and the third-party C libraries
    -- (capstone/lzma/mspack/zlib) - -include <cstdint> on a C file is a fatal
    -- error (cstdint is a C++-only header).
    forceincludes({"stdint.h", "stddef.h", "string.h"})
  else
    toolset("clang")
  end
  buildoptions({
    -- "-mlzcnt",  -- (don't) Assume lzcnt is supported.
  })
  if not _OPTIONS["linux-arm64"] then
    pkg_config.all("gtk+-x11-3.0")
  end
  links({
    "stdc++fs",
    "dl",
    "pthread",
    "rt",
  })
  -- lz4 is a system lib not present in the aarch64 cross sysroot; the lean a64
  -- cpu-test build doesn't use it, so only link it for the normal x86_64 build.
  if not _OPTIONS["linux-arm64"] then
    links({"lz4"})
  end

filter({"platforms:Linux", "kind:*App"})
  linkgroups("On")

filter({"platforms:Linux", "language:C++", "toolset:gcc"})
  disablewarnings({
    "unused-result"
  })

filter({"platforms:Linux", "toolset:gcc"})
  if ARCH == "ppc64" then
    buildoptions({
      "-m32",
      "-mpowerpc64"
    })
    linkoptions({
      "-m32",
      "-mpowerpc64"
    })
  end

filter({"platforms:Linux", "language:C++", "toolset:clang"})
  disablewarnings({
    "deprecated-register"
  })
filter({"platforms:Linux", "language:C++", "toolset:clang", "files:*.cc or *.cpp"})
  buildoptions({
    "-stdlib=libstdc++",
  })

filter("platforms:Android-*")
  system("android")
  systemversion("26")
  cppstl("c++")
  staticruntime("On")
  -- Imported from the aX360e Android build shape: keep native frames and
  -- unwind tables available for ARM64 JIT/debug crash triage, and make the
  -- linked shared object compatible with Android 16 KiB page devices.
  buildoptions({
    "-fno-omit-frame-pointer",
    "-funwind-tables",
  })
  linkoptions({
    "-Wl,-z,max-page-size=16384",
  })
  -- Hidden visibility is needed to prevent dynamic relocations in FFmpeg
  -- AArch64 Neon libavcodec assembly with PIC (accesses extern lookup tables
  -- using `adrp` and `add`, without the Global Object Table, expecting that all
  -- FFmpeg symbols that aren't a part of the FFmpeg API are hidden by FFmpeg's
  -- original build system) by resolving those relocations at link time instead.
  visibility("Hidden")
  links({
    "android",
    "dl",
    "log",
  })

-- The AYN Thor (the ONE target) is a Snapdragon 8 Gen 2 = ARMv9.0-A with LSE
-- atomics (the `atomics` HWCAP). The NDK default (-moutline-atomics) routes
-- every C/C++ atomic through the __aarch64_casN_*/__aarch64_swpN_* outline
-- helpers, which do a runtime LSE-detection + indirect call PER ATOMIC.
-- Profiling Burnout's CPU-bound race (B86u) showed ~6% of CPU in
-- __aarch64_cas2_acq + __aarch64_swp2_rel from that dispatch alone. Targeting
-- armv8.2-a+lse makes the compiler INLINE the LSE atomic (a single
-- casal/swpal) with no helper call - safe because the Thor is the only device
-- this APK runs on. ARM64-only (the x86_64 ABI rejects -march=armv8).
filter("platforms:Windows")
  system("windows")
  toolset("msc")
  buildoptions({
    "/utf-8",   -- 'build correctly on systems with non-Latin codepages'.
    -- Mark warnings as severe
    "/w14839",  -- non-standard use of class 'type' as an argument to a variadic function
    "/w14840",  -- non-portable use of class 'type' as an argument to a variadic function
    -- Disable warnings
    "/wd4100",  -- Unreferenced parameters are ok.
    "/wd4201",  -- Nameless struct/unions are ok.
    "/wd4512",  -- 'assignment operator was implicitly defined as deleted'.
    "/wd4127",  -- 'conditional expression is constant'.
    "/wd4324",  -- 'structure was padded due to alignment specifier'.
    "/wd4189",  -- 'local variable is initialized but not referenced'.
  })
  flags({
    "MultiProcessorCompile",  -- Multiprocessor compilation.
    "NoMinimalRebuild",       -- Required for /MP above.
  })

  defines({
    "_CRT_NONSTDC_NO_DEPRECATE",
    "_CRT_SECURE_NO_WARNINGS",
    "WIN32",
    "_WIN64=1",
    "_AMD64=1",
  })
  linkoptions({
    "/ignore:4006",  -- Ignores complaints about empty obj files.
    "/ignore:4221",
  })
  links({
    "ntdll",
    "wsock32",
    "ws2_32",
    "xinput",
    "comctl32",
    "shcore",
    "shlwapi",
    "dxguid",
    "bcrypt",
  })

-- Embed the manifest for things like dependencies and DPI awareness.
filter({"platforms:Windows", "kind:ConsoleApp or WindowedApp"})
  files({
    "src/xenia/base/app_win32.manifest"
  })

-- Create scratch/ path
if not os.isdir("scratch") then
  os.mkdir("scratch")
end

workspace("xenia")
  uuid("931ef4b0-6170-4f7a-aaf2-0fece7632747")
  startproject("xenia-app")
  if os.istarget("android") then
    platforms({"Android-ARM64", "Android-x86_64"})
    filter("platforms:Android-ARM64")
      architecture("ARM64")
      -- The AYN Thor (the ONE target) is a Snapdragon 8 Gen 2 = ARMv9.0-A with
      -- LSE atomics. The NDK default (-moutline-atomics) routes every C/C++
      -- atomic through the __aarch64_casN_*/__aarch64_swpN_* outline helpers
      -- (runtime LSE-detection + indirect call PER ATOMIC). Burnout's
      -- CPU-bound race profiled ~6% in __aarch64_cas2_acq/__aarch64_swp2_rel
      -- from that dispatch (B86u). Targeting armv8.2-a+lse inlines the atomic
      -- to a single casal/swpal - safe because the Thor is the only device
      -- this APK runs on.
      -- -march alone leaves the NDK's default -moutline-atomics active (atomics
      -- still routed through __aarch64_*_acq_rel helpers); -mno-outline-atomics
      -- makes the compiler emit the LSE casal/swpal/ldaddal inline.
      -- -mtune picks the SCHEDULING MODEL and cost heuristics; it cannot change
      -- which instructions are legal, so it is safe on every core in this SoC
      -- (unlike -mcpu, which would also set the ISA baseline). Without it clang
      -- schedules for a GENERIC ARM64 pipeline, across the whole emulator - not
      -- just JIT-emitted guest code.
      -- Why cortex-a710 and not a715: the NDK 25 toolchain is clang 14, which
      -- predates Cortex-A715/X3 (added in LLVM 16) and REJECTS them outright
      -- ("the clang compiler does not support '-mtune=cortex-a715'"). a710 is
      -- not a compromise though - the 8 Gen 2 physically contains A710 cores
      -- (X3 + 2x A715 + 2x A710 + 3x A510), and the A715 is its direct
      -- successor with a closely related pipeline. Verified accepted by this
      -- exact toolchain; cortex-x2 / a78 / neoverse-n2 also work, a715/x3 do
      -- not. Moving to NDK 29 (already installed) would allow a715, but that is
      -- an STL/ABI-level change and is not worth bundling in here.
      -- Prompted by rpcsx-ui-android-thor, which targets this same device with
      -- -march=armv8.2-a -mtune=cortex-a715.
      -- UNMEASURED: this is a whole-emulator codegen change and run-to-run
      -- drift on this device is ~2.8%, so it needs a same-session A/B (equal
      -- thermal starts, uncapped entry_delta) before any win is claimed. It is
      -- in because it is low-risk and strictly more informed than generic
      -- scheduling, NOT because it was measured.
      buildoptions({"-march=armv8.2-a+lse", "-mno-outline-atomics",
                    "-mtune=cortex-a710"})
    filter("platforms:Android-x86_64")
      architecture("x86_64")
    filter({})
  else
    if os.istarget("linux") and _OPTIONS["linux-arm64"] then
      architecture("ARM64")
    else
      architecture("x86_64")
    end
    if os.istarget("linux") then
      platforms({"Linux"})
    elseif os.istarget("macosx") then
      platforms({"Mac"})
      xcodebuildsettings({           
        ["ARCHS"] = "x86_64"
      })
    elseif os.istarget("windows") then
      platforms({"Windows"})
      -- 10.0.15063.0: ID3D12GraphicsCommandList1::SetSamplePositions.
      -- 10.0.19041.0: D3D12_HEAP_FLAG_CREATE_NOT_ZEROED.
      -- 10.0.22000.0: DWMWA_WINDOW_CORNER_PREFERENCE.
      filter("action:vs2017")
        systemversion("10.0.22000.0")
      filter("action:vs2019")
        systemversion("10.0")
      filter({})
    end
  end
  configurations({"Checked", "Debug", "Release"})

  include("third_party/aes_128.lua")
  include("third_party/capstone.lua")
  include("third_party/dxbc.lua")
  include("third_party/discord-rpc.lua")
  include("third_party/cxxopts.lua")
  include("third_party/cpptoml.lua")
  include("third_party/FFmpeg/premake5.lua")
  include("third_party/fmt.lua")
  include("third_party/glslang-spirv.lua")
  include("third_party/imgui.lua")
  include("third_party/mspack.lua")
  include("third_party/snappy.lua")
  include("third_party/xbyak_aarch64.lua")
  include("third_party/xxhash.lua")
  include("third_party/pugixml.lua")

  if os.istarget("android") then
    -- libadrenotools (rootless Turnip custom-driver loading) — Android-ARM64-only;
    -- the projects ExcludeFromBuild on Android-x86_64. Used only behind the
    -- default-OFF gpu_vulkan_driver=turnip cvar.
    include("third_party/adrenotools.lua")
  end

  if not os.istarget("android") then
    -- SDL2 requires sdl2-config, and as of November 2020 isn't high-quality on
    -- Android yet, most importantly in game controllers - the keycode and axis
    -- enums are being ruined during conversion to SDL2 enums resulting in only
    -- one controller (Nvidia Shield) being supported, digital triggers are also
    -- not supported; lifecycle management (especially surface loss) is also
    -- complicated.
    include("third_party/SDL2.lua")
  end

  -- Disable treating warnings as fatal errors for all third party projects, as
  -- well as other things relevant only to Xenia itself.
  for _, prj in ipairs(premake.api.scope.current.solution.projects) do
    project(prj.name)
    removefiles({
      "src/xenia/base/app_win32.manifest"
    })
    removeflags({
      "FatalWarnings",
    })
  end

  include("src/xenia")
  include("src/xenia/app")
  include("src/xenia/app/discord")
  include("src/xenia/apu")
  include("src/xenia/apu/nop")
  if os.istarget("android") then
    include("src/xenia/apu/android")
  end
  include("src/xenia/base")
  include("src/xenia/cpu")
  include("src/xenia/cpu/backend/arm64")
  include("src/xenia/cpu/backend/llvm")
  include("src/xenia/cpu/backend/x64")
  include("src/xenia/debug/ui")
  include("src/xenia/gpu")
  include("src/xenia/gpu/null")
  include("src/xenia/gpu/vulkan")
  include("src/xenia/hid")
  if os.istarget("android") then
    include("src/xenia/hid/android")
  end
  include("src/xenia/hid/nop")
  include("src/xenia/kernel")
  include("src/xenia/ui")
  include("src/xenia/ui/vulkan")
  include("src/xenia/vfs")

  if not os.istarget("android") then
    include("src/xenia/apu/sdl")
    include("src/xenia/helper/sdl")
    include("src/xenia/hid/sdl")
  end

  if os.istarget("windows") then
    include("src/xenia/apu/xaudio2")
    include("src/xenia/gpu/d3d12")
    include("src/xenia/hid/winkey")
    include("src/xenia/hid/xinput")
    include("src/xenia/ui/d3d12")
  end
