project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-apu-android")
  uuid("81081b2f-b3fb-4d50-b8f5-7a9cd0f51d8a")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-apu",
    "xenia-base",
  })
  defines({
  })
  local_platform_files()
