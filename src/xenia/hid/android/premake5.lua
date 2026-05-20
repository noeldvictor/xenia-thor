project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-hid-android")
  uuid("4b6047db-2e98-4f04-a1bf-8d8b2de9f814")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-hid",
  })
  defines({
  })
  local_platform_files()
