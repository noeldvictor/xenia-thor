# Thor Fast Dev Loop Options

Date: 2026-05-17 19:50 EDT

Scope: research-only xenia-thor Android ARM64 bring-up for AYN Thor Max. This is
not official Xenia guidance, not a compatibility claim, and not something to
take to upstream maintainers as a support burden.

## Why This Port Is Hard

- Official Xenia is still framed as an experimental Xbox 360 emulator, with
  public downloads centered on Windows x64 and Linux still described as coming
  later. Android is therefore not a normal supported target; we are stacking an
  Android app shell, ARM64 CPU backend, Vulkan renderer, and handheld lifecycle
  concerns on top of an emulator architecture built mostly around desktop PCs.
- Xbox 360 CPU execution is the first wall. Xenia has to translate Xenon
  PowerPC guest code into host code. The mature path is x64; our Thor path is
  building an AArch64 backend while debugging correctness, endian behavior,
  register allocation, helper calls, MMIO, code-cache permissions, and generated
  code crashes.
- The current Blue Dragon crash is very likely a CPU/JIT code-cache permission
  race, not an Adreno rendering failure. Our ARM64 cache is flipped between RW
  and RX for the whole region. When Blue Dragon starts several XThreads, one
  guest thread can execute from the cache while another compile path temporarily
  removes execute permission. The observed Thor faults have `fault == pc` inside
  the ARM64 code cache, which matches an instruction-fetch protection fault.
- The GPU will become the next hard wall. Xenia's own GPU writeups describe Xbox
  360 GPU emulation as tightly tied to unified 512 MB memory, eDRAM behavior,
  render target resolves, texture layouts, shader memory export, and slow
  protection-fault invalidation paths. Vulkan is the right cross-platform route,
  but the Android driver path must still satisfy these desktop-oriented
  assumptions.
- Android adds security and tooling friction. Android has an app sandbox, NX,
  ASLR, and dynamic-code-loading concerns. Native library loading is controlled
  by the Android linker and target API behavior. Vulkan validation/AGI/RenderDoc
  work best when the app is debuggable and layers are configured per app.

## Online Findings

- Xenia download page currently lists Windows x64 builds and says Linux builds
  are coming eventually:
  https://xenia.jp/download/
- Xenia GitHub describes the project as experimental:
  https://github.com/xenia-project/xenia
- The Vulkan backend issue explicitly calls out Linux and Android as Vulkan
  drivers for cross-platform GPU emulation, and notes OpenGL is not a practical
  answer:
  https://github.com/xenia-project/xenia/issues/2028
- Xenia's GPU article explains why the GPU side is not a normal renderer port:
  512 MB shadow memory, eDRAM resolves, synchronization, memory protection
  invalidation, shader complexity, and driver compilation stutter all matter:
  https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html
- Android Vulkan validation layers can be pushed to app-local storage and
  enabled per app with global settings, without root, for debuggable apps:
  https://developer.android.com/ndk/guides/graphics/validation-layer
- AGI requires a debuggable app and can inject its Vulkan layer; its system
  profiler is built on Perfetto and can capture CPU scheduling, GPU counters,
  Vulkan calls, memory, and battery data:
  https://developer.android.com/agi/start
  https://developer.android.com/agi/sys-trace/system-profiler
- Android's NDK simpleperf is the right fast CPU profiler for hot native loops:
  https://developer.android.com/ndk/guides/simpleperf
- Android Perfetto is available from ADB and captures kernel/user-space/native
  memory traces:
  https://developer.android.com/tools/perfetto
- Android's security guidance warns about dynamic loading from external storage;
  for our debug-only loader, pushed libraries should live under the app-private
  directory and be treated as signed/trusted build artifacts:
  https://developer.android.com/privacy-and-security/security-tips
- Android linker behavior matters for any hot native loader. Public native APIs,
  clean `DT_NEEDED` entries, and dependency resolution need to be controlled:
  https://android-developers.googleblog.com/2016/06/android-changes-for-ndk-developers.html
  https://developer.android.com/ndk/reference/group/libdl
- Xenia Edge is an experimental fork focused on faster iteration, compatibility,
  usability, and platform support; it is relevant for ARM64/backend ideas, but
  not something to blindly merge:
  https://github.com/has207/xenia-edge

## Faster Dev Loop Options

### Option A - Immediate Thor Debug JIT Mode

Add a research-only ARM64 code-cache mode that avoids whole-cache RW/RX flips.
The fastest experiment is `PROT_READ | PROT_WRITE | PROT_EXEC` for debug builds
only. If Thor allows it, this should remove the current multi-thread
instruction-fetch fault quickly. The real fix is dual mapping, but RWX tells us
whether the crash is the permission race.

Pros:

- Fastest path to see if Blue Dragon moves beyond the current file-thread crash.
- Smallest code change.
- Useful as a controlled experiment.

Cons:

- Security-hostile and unsuitable outside local research APKs.
- May be blocked or behave differently on some Android builds.
- Does not solve long-term correctness or release posture.

### Option B - Dual-Mapped ARM64 Code Cache

Use one backing object mapped twice: a writable alias for code generation and an
executable/read-only alias for guest execution. Emit/copy through the writable
view, flush instruction cache for the executable address range, then install the
RX address in the function table. This is the right long-term answer for
multi-threaded JIT execution.

Pros:

- Removes the global execute-permission race.
- Keeps generated code non-writable during execution.
- Scales to multiple guest threads.

Cons:

- More engineering than RWX.
- Needs careful Android backing object choice and address bookkeeping.
- Any code/data pointer assumptions in the current mini-JIT must be audited.

### Option C - Hot Native Core Loader

Split the APK shell from the native core. Ship a tiny stable Java/JNI loader in
the APK, then load a pushed debug build from:

`/data/data/jp.xenia.emulator.github.debug/files/devlibs/libxenia-app.so`

The loop becomes native incremental build, `adb push`, `run-as cp`, force-stop,
launch Blue Dragon, capture. No Gradle packaging or APK install unless Java,
resources, manifest, or dependencies change.

Pros:

- Biggest day-to-day speedup.
- Lets APK/UI changes and native-core changes rebuild separately.
- Pairs cleanly with scripts and worklogs.

Cons:

- Must handle Android linker dependencies. A single mostly-monolithic core
  library is simplest.
- Requires a stable shim so `System.load` or `dlopen` can select the dev core.
- Debug-only; do not present as production distribution.

### Option D - In-App Headless Harness

Add a debug activity/service that runs CPU/JIT/kernel tests and selected game
boot steps without the Android GUI/game picker. It should accept ADB extras for
content path, CPU mode, GPU mode, stop marker, timeout, and log path.

Pros:

- Faster than clicking/launching the full UI every time.
- Makes Blue Dragon boot regressions measurable.
- Good place for trace replay and generated-code dump triggers.

Cons:

- Still runs inside Android app lifecycle.
- Needs guardrails to avoid accidentally running long battery-draining loops.

### Option E - Trace/Replay Before Full Game Boot

Record early Blue Dragon guest PPC/HIR/function/MMIO sequences from a known-good
desktop x64 run, then replay targeted chunks against the ARM64 backend on Thor.
The first version can be tiny: compile and run one translated guest function
with captured input context and memory windows.

Pros:

- Converts "game crashed somewhere" into small reproducible JIT tests.
- Lets Ghidra focus on exact guest/native addresses.
- Finds CPU bugs before Vulkan noise enters the picture.

Cons:

- Requires trace format discipline.
- Some bugs need full kernel/thread timing and will not reproduce in replay.

### Option F - Tiered Capture Modes

Define capture lanes:

- `fast`: logcat filtered, screenshot, PID, APK/core hash, last progress marker.
- `cpu`: plus simpleperf sample/call graph.
- `trace`: plus Perfetto/ATrace markers and thread scheduling.
- `gpu`: plus Vulkan validation/AGI/RenderDoc, only after frames are produced.
- `ghidra`: only when there is a concrete native/generated/guest address.

Pros:

- Avoids expensive tools on the wrong class of bug.
- Keeps worklogs comparable between runs.
- Makes automation classify crashes instead of making us manually read logs.

Cons:

- Requires scripts to maintain progress markers and run manifests.

## Recommended Order

1. Add Option A as a debug-only experiment and run Blue Dragon once. If the
   `fault == pc` crash disappears, immediately replace with Option B.
2. Build Option C so native-only changes stop reinstalling APKs.
3. Add Option F around the existing Thor scripts so each run auto-classifies
   `CPU/JIT`, `kernel/file`, `Vulkan`, `content`, or `lifecycle`.
4. Add Option D for headless launch and repeatable boot markers.
5. Add Option E after we have the next concrete JIT correctness crash.

## Near-Term Decision

For the next Blue Dragon title-screen push, the best move is:

- Implement `xenia_arm64_jit_code_cache_mode = rwx_debug|wx_dualmap|wx_flip`.
- Default the Thor debug APK to `rwx_debug` only while proving the race.
- Keep `wx_flip` as a comparison mode.
- Start the hot native core loader in parallel or immediately after the next
  device result.
