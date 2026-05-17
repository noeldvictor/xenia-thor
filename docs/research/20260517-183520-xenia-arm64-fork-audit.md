# Xenia ARM64 / Android Fork Audit

Timestamp: 2026-05-17 18:35:20 -04:00

Purpose: find existing public Xenia ARM64 / Android work that can accelerate
the Thor Max Android ARM64 port without copying unclear code, bothering
upstream maintainers, or mixing this research fork with official Xenia support.

## Short Answer

Yes, there is useful code to study and port. The highest-value path is:

1. Use `has207/xenia-edge` as the primary AArch64 JIT source because it has the
   newest xbyak_aarch64 backend shape, CMake wiring, POSIX code cache, and 121
   unique HIR opcode table entries in `src/xenia/cpu/backend/a64`.
2. Use `aenu1/ax360e` as the Android app-side reference because it wires a
   nested Xenia Canary tree, A64 backend, Android CMake, document-file access,
   Android HID, Vulkan/Adreno configuration, and `libadrenotools` into a real
   Android package.
3. Use `wmarti/xenia-mac` and `Wunkolo/xenia` as provenance and portability
   references, especially for oaknut-based A64 design, ARM64 base portability,
   exception handling, and memory mapping.
4. Ignore the tiny or unrelated Android search results for compiler work.

Do not wholesale import any repository yet. Bring code in as small, attributed
patches after checking the specific file licenses and keeping our fork clearly
marked as an unofficial research experiment.

## Audit Table

| Repository / branch | Evidence | Value for Thor | Import risk |
| --- | --- | --- | --- |
| `has207/xenia-edge`, branch `edge`, commit `3020342` | Fork of Xenia Canary, BSD-3-Clause, pushed 2026-05-17. Has `src/xenia/cpu/backend/a64`, `third_party/xbyak_aarch64`, CMakeLists, Android project. 23 A64 files, 121 unique `EMITTER_OPCODE_TABLE` opcode entries. | Best primary A64 JIT candidate. Newest active code, xbyak_aarch64 is already CMake-friendly, POSIX code cache has `__builtin___clear_cache`. | Still not Android-proven here. Need ABI/thunk validation, page permission validation, fallback for unsupported HIR, and attribution. |
| `aenu1/ax360e`, branch `main`, commit `2b4c889` | Public Android Xenia app claim, pushed 2026-05-15. Root GitHub metadata has no detected license, but nested `app/src/main/cpp/xenia-canary/LICENSE` exists and `assets/licenses.html` links Xenia Canary and `Wunkolo/xenia/tree/arm64-backend`. Has nested A64 backend with 121 opcode entries and Android CMake linking `xenia-cpu-backend-a64`. | Best Android reference. Useful for app CMake, Android HID, SAF/document-file access, config defaults, Adreno tooling, and direct Android emulator flow. | Do not copy blindly. Root license is unclear, app is Play-distributed, has many vendored deps, and may include GPL/LGPL components. Study patterns; port only compatible source with attribution. |
| `wmarti/xenia-mac`, `a64-backend` / `pr-arm64-backend` and related portability branches | Fork of upstream Xenia, BSD-3-Clause. Branches include `a64-backend`, `pr-arm64-backend`, `arm64-all`, `arm64-macos-consolidated`, `pr-base-arm64-portability-v2`, `pr-cpu-arm64-portability-v2`, `pr-linux-arm64`, `pr-windows-arm64`. Current A64 branches show 23 A64 files and 115 unique opcode entries. | Best upstream-style ARM64 portability reference. Good for base memory/math/debug register portability, ARM64 exception context, macOS/Linux/Windows ARM64 build notes. | Mostly oaknut/premake-oriented and not Android-first. Less direct for our CMake Android build than `xenia-edge`. |
| `Wunkolo/xenia`, `arm64-backend` / `canary-arm64-backend` | Fork of upstream Xenia. Branches include `arm64`, `arm64-backend`, `arm64-windows`, `canary-arm64-backend`. Older oaknut A64 backend: 22 A64 files, 105-107 unique opcode entries depending branch. | Important provenance for the A64 backend family. Useful to understand original intent and attribution trail. | Older than `xenia-edge`/`wmarti`; oaknut dependency and fewer implemented opcodes make it less attractive as the direct import base. |
| `joetheshmoe/xenia-winarm`, branch `xenia-canary-mac-rebase`, commit `c68c4cf` | Fork of `wmarti/xenia-mac`, BSD-3-Clause, pushed 2026-02-20. 23 A64 files, 115 unique opcode entries. README/description targets Windows on ARM and testing through Proton rather than native Android. | Good sanity check for ARM64 base portability and Windows ARM64 runtime fixes. | Not Android-native. Use as reference only. |
| `edilsongg/xenia-arm64` and `fillips67fissile/xenia-arm64-backend` | GitHub search found both; branch hashes match the `Wunkolo/xenia` ARM64 heads checked during this audit. | No unique import value found beyond Wunkolo provenance. | Duplicate audit targets; avoid adding noise. |
| `supermonkeygodkingemperor/SbossTreeSitty`, branch `main`, commit `79bf116` | "xenia android port" description. Has Android shell files and a vendored `xeniamaster`, but no A64 backend files. Some CMake text appears pasted into a Kotlin file. | Not useful for JIT. Maybe a negative example of app shell pitfalls. | Low quality / unclear provenance. Do not import. |
| `Xenia-creator2025/Xenia-emulator-android`, branch `main`, commit `49e6999` | Four files, GPL-3.0, no A64 backend, only `src/xenia/gpu` path in tree. | No value for this port. | GPL-3.0 and no relevant code. Avoid. |
| `angeITE/ProyectoXenia_Android` and `angeITE/ProyectoXenia` | Small Flutter/Android school projects, no Xenia backend, no A64 code. | None. | Irrelevant despite matching "Xenia" name. |
| `xenia-project/xenia` | Official upstream. Public branch scan did not reveal an ARM64/A64/Android JIT branch. | Keep as baseline and license/reference source. | Do not send this fork's bugs upstream. |

## Technical Findings

`xenia-edge` and `ax360e` share the most directly usable shape for this repo:

- `src/xenia/cpu/backend/a64/CMakeLists.txt`
- `a64_backend.cc/.h`
- `a64_code_cache.cc/.h`
- `a64_code_cache_posix.cc`
- `a64_emitter.cc/.h`
- `a64_function.cc/.h`
- `a64_seq_control.cc`
- `a64_seq_memory.cc`
- `a64_seq_vector.cc`
- `a64_sequences.cc/.h`
- `third_party/xbyak_aarch64`

The xbyak_aarch64 path is a better immediate fit than the oaknut path because
our Android build already uses CMake/NDK lanes, and the xbyak_aarch64 forks
already have CMake backend and third-party integration. The oaknut forks remain
useful for comparing codegen decisions and ARM64 portability fixes.

AX360E is especially useful for Android-specific glue:

- top-level Android Gradle project with native CMake entry at
  `app/src/main/cpp/CMakeLists.txt`;
- native bridge files such as `ax360e_emu.cpp`, `emulator.cpp`,
  `emulator_ax360e.cpp`, and `document_file.cpp`;
- Android HID path (`xe_android_hid.cpp`, `xe_android_input_driver.cpp`);
- SAF / DocumentFile access for game content;
- default config with `hid = "android"` and Adreno-related options;
- `libadrenotools` integration for Adreno driver handling.

Those app patterns are useful, but they should be reimplemented or ported in
small reviewable pieces. Do not vendor the AX360E app wholesale.

## Import Plan

1. Create an isolated A64 backend import spike under our existing CPU backend
   selection rather than replacing the current ARM64 interpreter scaffold in one
   patch.
2. Import the xbyak_aarch64 third-party dependency with license attribution.
3. Import the minimal A64 code cache and emitter skeleton from `xenia-edge`,
   adapting namespaces/build files to our tree.
4. Wire backend selection so unsupported HIR still falls back to the current
   interpreter path while emitted functions are validated.
5. Start with thunks, context load/store, direct branches/returns, integer
   arithmetic, and guest memory load/store.
6. Add Android-specific executable memory validation:
   `mmap`/`mprotect`, RW-to-RX transition, `__builtin___clear_cache`, and a tiny
   generated-code smoke test on Thor.
7. Use AX360E only as a reference for Android glue after the backend can
   compile, especially SAF paths, HID, and Adreno options.
8. Keep each import slice documented in `docs/worklogs/YYYYMMDD.md` and commit
   often on `master`.

## Immediate Decision

Primary compiler source: `has207/xenia-edge`.

Primary Android glue reference: `aenu1/ax360e`.

Secondary provenance/portability references: `wmarti/xenia-mac`,
`Wunkolo/xenia`, and `joetheshmoe/xenia-winarm`.

Do not spend implementation time on `SbossTreeSitty`, `Xenia-creator2025`, or
the `ProyectoXenia` repositories.

## Commands Run

- GitHub repository searches for `xenia android`, `xenia fork:true arm64`,
  `xenia fork:true a64`, `xenia cpu backend a64`, and related terms.
- `git ls-remote --heads` on the candidate repositories.
- Shallow, blob-filtered clones into `scratch/research/*`.
- `git ls-tree`, `git grep`, and selected `git show` inspection of A64 backends,
  Android app glue, licenses, and build files.

## Source Links

- <https://github.com/has207/xenia-edge>
- <https://github.com/aenu1/ax360e>
- <https://github.com/wmarti/xenia-mac>
- <https://github.com/Wunkolo/xenia>
- <https://github.com/joetheshmoe/xenia-winarm>
- <https://github.com/edilsongg/xenia-arm64>
- <https://github.com/fillips67fissile/xenia-arm64-backend>
- <https://github.com/supermonkeygodkingemperor/SbossTreeSitty>
- <https://github.com/Xenia-creator2025/Xenia-emulator-android>
- <https://github.com/angeITE/ProyectoXenia_Android>
- <https://github.com/angeITE/ProyectoXenia>
- <https://github.com/xenia-project/xenia>
