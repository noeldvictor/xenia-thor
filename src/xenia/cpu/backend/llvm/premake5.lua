project_root = "../../../../.."
include(project_root.."/tools/build")

local llvm_dir = project_root.."/src/xenia/cpu/backend/llvm"

-- The cross-built android-arm64 libLLVM (P0 output) is staged here:
--   third_party/llvm-android/{include,lib/libLLVM.so}
-- copied from the WSL build (~/llvm-android-build). When present, the backend
-- compiles for real (XE_LLVM_BACKEND_ENABLED=1) and links libLLVM; otherwise it
-- compiles as a stub (the skeleton) so the tree always builds.
local llvm_prebuilt = project_root.."/third_party/llvm-android"
local has_llvm = os.isfile(llvm_prebuilt.."/lib/libLLVM.so")

group("src")
project("xenia-cpu-backend-llvm")
  uuid("c8a3f1e2-7b4d-4e9a-9c5f-2d1a8b6e0f73")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-cpu",
    "xenia-cpu-backend-a64",  -- LLVMBackend derives from A64Backend (thunks,
                              -- code cache, dispatch, A64Function, fallback).
  })
  files({
    llvm_dir.."/*.cc",
    llvm_dir.."/*.h",
  })

  if has_llvm then
    defines({ "XE_LLVM_BACKEND_ENABLED=1" })
    includedirs({ llvm_prebuilt.."/include" })
    -- NOTE: keep RTTI ON (xenia's cvar.h -> cpptoml uses dynamic_cast). LLVM is
    -- built -fno-rtti but uses its own isa<>/cast<> (no C++ RTTI), and we never
    -- subclass LLVM polymorphic types here, so cpptoml's RTTI stays self-
    -- contained and linking against the -fno-rtti libLLVM is fine.
    libdirs({ llvm_prebuilt.."/lib" })
    links({ "LLVM" })
  else
    -- Stub build until P0 stages the prebuilt LLVM. The .cc files guard all
    -- LLVM use behind XE_LLVM_BACKEND_ENABLED, so they compile clean here.
    print("[xenia-cpu-backend-llvm] no prebuilt libLLVM at "..llvm_prebuilt..
          " - building stub (XE_LLVM_BACKEND_ENABLED=0). Run P0 then stage it.")
  end
