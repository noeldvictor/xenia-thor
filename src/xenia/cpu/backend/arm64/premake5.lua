project_root = "../../../../.."
include(project_root.."/tools/build")

local a64_dir = project_root.."/src/xenia/cpu/backend/a64"

group("src")
project("xenia-cpu-backend-arm64")
  uuid("fa89fd9c-e5f4-48f7-806f-90cf7a7805fd")
  kind("StaticLib")
  language("C++")
  links({
    "xbyak_aarch64",
    "xenia-base",
    "xenia-cpu",
  })
  includedirs({
    project_root.."/third_party/capstone/include",
    project_root.."/third_party/xbyak_aarch64",
    project_root.."/third_party/xbyak_aarch64/src",
    project_root.."/third_party/xbyak_aarch64/xbyak_aarch64",
  })
  files({
    a64_dir.."/*.cc",
    a64_dir.."/*.h",
    "arm64_backend.h",
  })
  removefiles({
    a64_dir.."/a64_code_cache_posix.cc",
    a64_dir.."/a64_code_cache_win.cc",
  })

  filter("platforms:Windows")
    files({a64_dir.."/a64_code_cache_win.cc"})

  filter("platforms:not Windows")
    files({a64_dir.."/a64_code_cache_posix.cc"})
