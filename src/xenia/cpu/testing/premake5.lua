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
        -- Edge kernel-port (2026-08): the merged kernel's deps.
        "xenia-apu",
        "xenia-hid",
        "xenia-vfs",
        "pugixml",
        "aes_128",
        "mspack",
        "xenia-gpu",
        "imgui",
      },
    },
    {
      -- Lean ARM64 cross build (qemu device-free a64 testing): a64 backend
      -- only, no GUI/kernel deps to cross-compile.
      filter = 'architecture:ARM64',
      links = {
        "xenia-cpu-backend-arm64",
        "xenia-cpu-backend-llvm",  -- LLVM-JIT backend (differential tests)
        "xbyak_aarch64",
        "mspack",
      },
    },
  },
})

-- The LLVM-JIT backend pulls in libLLVM (glibc, linux-arm64) for the qemu
-- differential tests. Only when the prebuilt is staged
-- (scratch/thor-debug/p0b_build_glibc.sh); otherwise the backend is a stub.
if os.isfile(project_root.."/third_party/llvm-linux-arm64/lib/libLLVM.so") then
  filter("platforms:Linux")
    libdirs({ project_root.."/third_party/llvm-linux-arm64/lib" })
    links({ "LLVM" })
  filter({})
end
