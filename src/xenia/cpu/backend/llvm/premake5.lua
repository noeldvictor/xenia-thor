project_root = "../../../../.."
include(project_root.."/tools/build")

local llvm_dir = project_root.."/src/xenia/cpu/backend/llvm"

-- The cross-built libLLVM (P0 = android-arm64 / P0b = linux-arm64 for qemu) is
-- staged per target; the LLVM headers are platform-independent and shared:
--   third_party/llvm-android/include            (shared headers)
--   third_party/llvm-android/lib/libLLVM.so     (android/bionic, for the APK)
--   third_party/llvm-linux-arm64/lib/libLLVM.so (glibc, for the qemu cpu-tests)
-- When the matching .so is present the backend compiles for real
-- (XE_LLVM_BACKEND_ENABLED=1) and links libLLVM; otherwise it compiles as a stub
-- so the tree always builds. The build scripts are in scratch/thor-debug/.
local llvm_inc = project_root.."/third_party/llvm-android/include"
local android_lib = project_root.."/third_party/llvm-android/lib"
local linux_lib = project_root.."/third_party/llvm-linux-arm64/lib"
local has_android = os.isfile(android_lib.."/libLLVM.so")
local has_linux = os.isfile(linux_lib.."/libLLVM.so")

group("src")
project("xenia-cpu-backend-llvm")
  uuid("c8a3f1e2-7b4d-4e9a-9c5f-2d1a8b6e0f73")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-cpu",
    "xenia-cpu-backend-arm64",  -- the a64 backend project (a64::A64Backend);
                                -- LLVMBackend derives from it (thunks, code
                                -- cache, dispatch, A64Function, fallback).
  })
  files({
    llvm_dir.."/*.cc",
    llvm_dir.."/*.h",
  })
  -- NOTE: llvm_object_cache.cc subclasses llvm::ObjectCache + instantiates
  -- llvm::orc::SimpleCompiler, which (built -frtti like the rest of this lib for
  -- cvar.h's dynamic_cast) reference those classes' typeinfo - absent in the
  -- -fno-rtti libLLVM. A per-file rtti("Off") is NOT portable (the androidndk
  -- generator only sets RTTI per-module), so that file instead provides weak
  -- placeholder typeinfo symbols. See llvm_object_cache.cc.

  -- Enable the real backend per platform where libLLVM is staged. The .cc guard
  -- all LLVM use behind XE_LLVM_BACKEND_ENABLED, so platforms without a staged
  -- lib compile as a stub. Keep RTTI ON (cvar.h -> cpptoml uses dynamic_cast;
  -- libLLVM uses its own isa<>/cast<> so linking the -fno-rtti lib is fine).
  if has_android then
    filter("platforms:Android-ARM64")
      defines({ "XE_LLVM_BACKEND_ENABLED=1" })
      includedirs({ llvm_inc })
      libdirs({ android_lib })
      links({ "LLVM" })
    filter({})
  end
  if has_linux then
    filter("platforms:Linux")
      defines({ "XE_LLVM_BACKEND_ENABLED=1" })
      includedirs({ llvm_inc })
      libdirs({ linux_lib })
      links({ "LLVM" })
    filter({})
  end
  if not has_android and not has_linux then
    print("[xenia-cpu-backend-llvm] no prebuilt libLLVM staged - stub build "..
          "(XE_LLVM_BACKEND_ENABLED=0). Run scratch/thor-debug/p0_build.sh.")
  end
