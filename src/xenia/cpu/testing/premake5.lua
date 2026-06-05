project_root = "../../../.."
include(project_root.."/tools/build")

test_suite("xenia-cpu-tests", project_root, ".", {
  links = {
    "capstone",
    "fmt",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
  },
  filtered_links = {
    {
      -- x86_64 host build keeps the full link (incl. the GUI deps it has
      -- always pulled) and the x64 backend.
      filter = 'architecture:x86_64',
      links = {
        "xenia-cpu-backend-x64",
        -- TODO(benvanik): cut these dependencies?
        "xenia-kernel",
        "xenia-ui", -- needed by xenia-base
      },
    },
    {
      -- Lean ARM64 cross build (qemu device-free a64 testing): a64 backend
      -- only, no GUI/kernel deps to cross-compile.
      filter = 'architecture:ARM64',
      links = {
        "xenia-cpu-backend-arm64",
        "xbyak_aarch64",
        "mspack",
      },
    },
  },
})
