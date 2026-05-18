# Faster ARM64 Port OODA Loop

Date: 2026-05-17 21:02 EDT

Scope: research-only xenia-thor Android ARM64 bring-up for AYN Thor Max and
Blue Dragon Disc 1. This is an unofficial experiment fork, not official Xenia
guidance, not a compatibility promise, and not something to take to upstream
maintainers for support.

## User Constraints Captured

- Cleaner is ideal, but we still need fast Blue Dragon testing.
- Dependencies are allowed if they make the loop faster.
- PC x64 Xenia can be used as an oracle.
- Proper AArch64 JIT or an LLVM lane is acceptable.
- Slower first boot and generated artifacts are acceptable.
- Thor can stay tethered, but ADB occasionally drops and reconnects even when
  USB stays plugged in.
- Target remains Blue Dragon Disc 1 to title screen on AYN Thor Max.

## Core Reframe

This is not really an x86-to-ARM port.

Xenia is translating Xbox 360 Xenon PowerPC guest code into Xenia HIR, then into
a host backend. The mature backend is x64, so x64 is the reference oracle, not
the thing to mechanically convert. The faster path is:

1. make x64 and interpreter behavior easy to capture and compare;
2. make ARM64 backend failures reproducible away from the full Android app;
3. import or port real AArch64 JIT structure systematically;
4. use Thor as the final smoke test, not the only microscope.

The current one-crash-per-APK loop is too slow because every missing HIR opcode,
ABI bug, helper-call bug, byte-swap bug, code-cache bug, and guest crash is
being discovered at full Blue Dragon boot speed on one USB-connected device.

## Recommendation

Use a three-lane loop.

### Lane 1 - PC Oracle and Trace Replay

Build a deterministic CPU correctness lane on PC first:

- run Blue Dragon or extracted boot functions on desktop x64 Xenia;
- enable or extend existing function tracing:
  - `trace_functions`
  - `trace_function_coverage`
  - `trace_function_data`
  - `trace_function_data_path`
- add a compact ARM64-compatible trace format for:
  - guest function address and end address;
  - optimized HIR opcode stream;
  - PPCContext before and after function call;
  - touched guest memory windows;
  - helper/MMIO calls;
  - return status and branch target;
- replay each captured function against:
  - known-good x64 result;
  - HIR interpreter result;
  - ARM64 interpreter;
  - ARM64 mini-JIT or proper A64 backend when available.

This makes "Blue Dragon crashed somewhere" become "guest function 0x826A23C8
diverged at HIR instruction N after this context and memory window."

Local hooks already exist:

- `src/xenia/cpu/cpu_flags.cc` defines tracing cvars.
- `src/xenia/cpu/processor.cc` opens `trace_function_data_path` and allocates
  per-function trace data.
- `src/xenia/cpu/function_trace_data.h` stores call count, thread mask, caller
  history, and optional instruction coverage counters.
- `src/xenia/cpu/backend/x64/x64_emitter.cc` already emits x64 trace counters.
- `src/xenia/cpu/backend/x64/x64_tracers.*` already has context and memory
  trace helpers, although current compile-time `ITRACE`/`DTRACE` are off.

Work needed:

- make trace enablement runtime cvar-driven instead of compile-time only;
- add a shared trace sink not tied to x64-only `__m128` types;
- add an ARM64/interpreter trace writer;
- add a small `tools/arm64/replay_hir_trace.*` runner.

### Lane 2 - Proper AArch64 JIT Path

Stop growing the current interpreter into a second emulator. Keep it as a
correctness fallback, but move speed work to a real AArch64 backend.

Best immediate path:

- use `has207/xenia-edge` as the primary A64 backend source to study and port,
  as already recorded in `docs/research/20260517-183520-xenia-arm64-fork-audit.md`;
- keep the repo's current Xbyak_aarch64 lane for the first real emitter
  because `src/xenia/cpu/backend/arm64/arm64_jit.cc` already uses it;
- port opcode clusters from x64/HIR semantics into AArch64 in batches;
- gate the mini-JIT until it can prove parity with interpreter/x64 traces;
- add a function blacklist and "force interpreter for guest range" cvar so one
  bad compiled function does not kill the whole boot.

VIXL remains a strong option if we need a safer assembler/simulator layer.
AOSP's VIXL mirror describes programmatic A64/A32/T32 assemblers,
disassemblers, and an A64 simulator, which is useful for x64-host validation of
generated A64 code. VIXL also recommends MacroAssembler for safer codegen
around operand encodability.

LLVM ORC is a longer spike, not the first Blue Dragon title-screen path. ORC is
good for modular JIT infrastructure, lazy compilation, and cross-compiling JIT
use cases, but it would require lowering Xenia HIR into LLVM IR, handling code
cache/object linking on Android, and debugging optimizer-induced semantic
changes. That may be cleaner later, but it is not the fastest way to get this
fork past the current ARM64 wall.

Dynarmic and QEMU TCG are design references, not drop-in answers. Dynarmic is
an ARM guest recompiler with x86-64 and AArch64 hosts, useful for JIT API,
instrumentation, and test harness ideas. QEMU TCG is the proof that portable
IR-to-host dynamic translation scales, but its architecture and license make it
reference material rather than code to vendor into this fork.

### Lane 3 - Thor Smoke and Robust Capture

Thor stays in the loop, but every run should classify the failure automatically:

- APK hash and native core hash;
- branch and commit;
- target ISO path;
- ADB serial and device state before each major command;
- package PID;
- focused activity;
- guest PC and guest function range if a crash happens;
- code-cache range and compiled/interpreter mode;
- logcat filtered/full;
- screenshot;
- optional simpleperf/Perfetto/RenderDoc only when the failure class needs it.

ADB should be wrapped in retry logic because the user reports intermittent
disconnects even while USB remains connected. Android's adb documentation
defines connection states such as `offline`, `device`, and `no device`, and the
local Platform Tools help for version `37.0.0-14910828` supports:

- `adb wait-for-device`
- `adb reconnect`
- `adb reconnect device`
- `adb reconnect offline`
- `adb kill-server`
- `adb start-server`

Recommended wrapper behavior for `tools/thor/thor_xenia_debug.ps1`:

1. before any install, launch, capture, or file push, run `adb devices -l`;
2. require serial `c3ca0370` in `device` state;
3. if serial is `offline`, run `adb reconnect offline`, then
   `adb -s c3ca0370 wait-for-device`;
4. if the command fails with transport/device offline text, retry once through
   `adb reconnect device`;
5. if the server is wedged, run `adb kill-server`, `adb start-server`, and
   `adb -s c3ca0370 wait-for-device`;
6. write all reconnect actions into the capture `meta.txt`.

This makes flaky USB a recoverable state instead of losing a run.

## Concrete Build Order

### Phase 0 - Stabilize Failure Attribution

Fastest value before more porting:

- add ARM64 crash attribution:
  - guest function start/end;
  - current HIR source offset;
  - whether compiled mini-JIT or interpreter;
  - host code-cache start/end;
  - generated-code offset when available;
- add cvars:
  - `arm64_enable_mini_jit`;
  - `arm64_mini_jit_blacklist`;
  - `arm64_force_interpreter_guest_ranges`;
- default Blue Dragon Thor debug runs to interpreter for suspect functions while
  mini-JIT parity is not proven.

### Phase 1 - Coverage and Replay Reports

Add tools:

- `tools/arm64/hir_coverage_report.ps1`
  - summarize missing or fallback HIR ops from logs;
  - sort by Blue Dragon hit count;
  - map opcode numbers to names;
  - link x64 reference sequence files and ARM64 implementation points.
- `tools/arm64/trace_manifest.ps1`
  - gather latest logcat, screenshot, hash, device state, target path, and
    progress markers into one markdown summary.
- `tools/arm64/replay_hir_trace.*`
  - run captured function snapshots against interpreter and ARM64 backend.

### Phase 2 - PC Oracle Corpus

Use PC x64 Xenia as oracle:

- capture the first Blue Dragon boot functions on x64;
- convert hot functions and crash-adjacent functions into replay fixtures;
- replay fixtures locally before any Thor deploy;
- only send Thor a build after the fixture batch passes.

Generated artifacts are acceptable, so store them under ignored scratch paths
first, then promote tiny derived fixtures into repo tests only when legal and
copyright-safe. Never commit ISO data, extracted game binaries, title keys, or
copyrighted screenshots.

### Phase 3 - A64 Backend Import/Port

Use the fork audit decision:

- study/import small attributed slices from `has207/xenia-edge`;
- keep AX360E as Android glue reference only;
- use Wunkolo/wmarti branches for provenance and portability comparison;
- do not wholesale copy unclear app code;
- document every imported slice in worklogs.

Start with:

- code cache and generated-code mapping;
- thunk/prolog/epilog and ABI save/restore;
- context load/store;
- integer arithmetic;
- branches and calls;
- memory load/store with byte-swap and MMIO;
- vector hot clusters from Blue Dragon coverage.

### Phase 4 - Hot Native Core Loop

Separate APK shell changes from native core rebuilds:

- keep Gradle/APK only for Java, resources, manifest, assets, and dependencies;
- build native core separately for CPU/backend work;
- push a debug native core into app-private storage;
- launch with a debug-only flag selecting packaged core vs pushed core;
- record native core hash in every capture.

This is a major speed win, but it depends on a stable loader shim and Android
linker discipline. It should come after crash attribution or in parallel with
PC replay, not before the CPU debugging is measurable.

## What To Avoid

- Do not keep adding interpreter opcodes one crash at a time as the main plan.
  It helps progress, but it will not produce playable speed.
- Do not attempt generic x86 machine-code translation. Xenia's x64 backend is a
  semantic reference, not a transpilation input.
- Do not make LLVM the first path unless Xbyak/VIXL and imported A64 backend
  code are blocked. LLVM is attractive, but integration cost is high.
- Do not make RenderDoc/Ghidra the default for every crash. Use Ghidra when we
  have a native/generated/guest address. Use RenderDoc after the game produces
  frames or when Vulkan state is the suspected wall.
- Do not ask official Xenia maintainers to support this fork's Android/AI
  experiment crashes.

## Answer To "How Do We Stay Cleaner But Test ASAP?"

Use a temporary dual track:

- **Clean track:** PC oracle trace/replay and proper A64 backend import/port.
- **ASAP track:** Thor smoke runs after each fixture-passing batch, with ADB
  retry and crash attribution.

That gives us fast proof without letting panic-driven Thor crashes define the
architecture.

## Immediate Next Implementation Tasks

1. Add ADB reconnect/retry wrapper in `tools/thor/thor_xenia_debug.ps1`.
2. Add ARM64 mini-JIT gate and blacklist cvars.
3. Add crash attribution around `Arm64Function::CallImpl` and generated-code
   setup.
4. Add `tools/arm64/hir_coverage_report.ps1` for latest Thor logs.
5. Start PC x64 oracle capture for Blue Dragon's early boot functions.
6. Begin an A64 backend import spike from `has207/xenia-edge`, starting with
   code cache and function metadata, not the whole backend at once.

## Sources

- Android adb documentation: <https://developer.android.com/tools/adb>
- Local adb help: Android SDK Platform Tools `37.0.0-14910828`.
- AOSP VIXL README: <https://android.googlesource.com/platform/external/vixl/+/refs/heads/master/README.md>
- Xbyak_aarch64 README: <https://github.com/fujitsu/xbyak_aarch64>
- LLVM ORC JIT docs: <https://llvm.org/docs/ORCv2.html>
- QEMU TCG translator internals: <https://www.qemu.org/docs/master/devel/tcg.html>
- QEMU TCG IR docs: <https://www.qemu.org/docs/master/devel/tcg-ops.html>
- Dynarmic README: <https://github.com/azahar-emu/dynarmic>
- Existing fork audit: `docs/research/20260517-183520-xenia-arm64-fork-audit.md`
- Existing fast-loop options: `docs/research/20260517-195019-thor-fast-dev-loop-options.md`
- Existing tooling note: `docs/research/20260517-195621-arm64-port-tooling-skills.md`
