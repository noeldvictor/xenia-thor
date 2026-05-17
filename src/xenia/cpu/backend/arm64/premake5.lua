project_root = "../../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-cpu-backend-arm64")
  uuid("fa89fd9c-e5f4-48f7-806f-90cf7a7805fd")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-cpu",
  })
  local_platform_files()
