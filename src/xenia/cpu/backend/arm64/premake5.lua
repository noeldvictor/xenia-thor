project_root = "../../../../.."
include(project_root.."/tools/build")

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
    project_root.."/third_party/xbyak_aarch64",
    project_root.."/third_party/xbyak_aarch64/src",
    project_root.."/third_party/xbyak_aarch64/xbyak_aarch64",
  })
  local_platform_files()
