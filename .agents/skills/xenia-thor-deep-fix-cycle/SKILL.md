---
name: xenia-thor-deep-fix-cycle
description: "Use to find + fix an a64 (ARM64) JIT codegen bug that surfaces as a game-specific failure on the AYN Thor (e.g. Banjo Nuts & Bolts' bundle-verify false-failing on byte-correct data -> dirty-disc). The end-to-end deep-fix loop: localize the failing guest path by RUNTIME OPERAND-CAPTURE (not static-RE guessing), disasm the failing function, capture computed-vs-expected at the decision, prove a per-instruction divergence (self-compute the expected value, or differential cpu-test host-x64 vs a64-qemu), fix in the a64 backend, verify, ship. Encodes the operand-log instrument (arm64_pc_operand_log_pcs), the validate-path-first rule, and the build/capture/Ghidra tooling gotchas that otherwise get rediscovered every fire."
---

# Xenia Thor — Deep JIT-Fix Cycle

Use this when a title fails because the **a64 JIT miscompiles some PPC instruction**
(the same data/work succeeds on PC xenia/x64, fails on the Thor). This is the
**emulator-bug** lane, deeper than a game-patch bypass. A bypass can hide a
symptom but cannot synthesize a correctly-computed value — only fixing the
miscompiled op does. Pairs with `xenia-ghidra-ooda-loop` (RE), `xenia-thor-gpu-capture`
(the fire tool), `a64-qemu-harness` + `host-cpu-test-harness` memories (differential
testing), and `xenia-windows-powershell-command-hygiene`.

## RULE 0 — VALIDATE THE PATH IS EXECUTED BEFORE DEEP-DIVING IT

The #1 wasted-fire failure: static RE / a decompiler names a "decision branch",
you patch or disasm-deep-dive it, and it turns out **not to be on the live fail
path** (a vtable/async dispatch went elsewhere). This cost ~5 Banjo fires
(82719134, 82715a54, 82719448/RANK1 were all dead). **Before trusting any PC:
operand-capture it on-device and confirm it actually executes on the failing run,
near the failure timestamp.** Branch decode + an assumed path is a hypothesis, not
a fact, until the capture shows the PC firing with the expected register state.

## The cycle

1. **Localize the fail to a thread + a small PC set.** Use the call-trace ENTRY
   hook (`arm64_compiled_call_trace_*`, works) with a tight `..._guest_tids` +
   `..._functions` filter, or start from prior RE, to find the verify/compute
   region and which guest thread runs it. Correlate hits with the failure
   timestamp (the dirty-disc / crash / wrong-output moment) — capture the path
   that runs JUST BEFORE the failure, not post-fail retries.

2. **Operand-capture at the decision PCs (the workhorse).** The instrument:
   `arm64_pc_operand_log_pcs` (comma/space/range list of guest PCs),
   `arm64_pc_operand_log_tids` (thread filter), `arm64_pc_operand_log_budget`
   (line cap). Emitted at `A64Emitter::MarkSourceOffset` (a64_emitter.cc) ONLY into
   functions containing a target PC, via the SAFE entry-hook `CallNativeSafe`
   path (NOT the buggy epilog return-hook). Logs `A64 PC operand pc {PC} thid r3..r11,r27,r29-r31,lr`.
   - Pick PCs at **ctx-boundaries** (just after a `bl`/`bctrl` return, or a
     `std/stw`-to-ctx) so `ctx->r[N]` is authoritative; if a value might be in an
     unflushed host reg, the values can be stale — re-fire (the cvar
     `arm64_context_value_cache` is not allowlisted, so this is the main caveat).
   - To read a memory operand (computed/expected buffer), extend `LogPcOperands`
     to deref `ctx->processor->memory()->TranslateVirtual(ctx->r[N]+off)` +
     `xe::load_and_swap<uint32_t>` (proven pattern, a64_emitter.cc).

3. **Disasm the failing function** (Ghidra headless — see gotchas) to get the
   exact instruction sequence around the decision. Decode what each captured
   register means.

4. **Prove the divergence.** Two ways, prefer (a):
   - (a) **Self-compute the expected value** from the captured inputs (when the
     op is pure arithmetic — e.g. a hash/modulo). If a64's captured result !=
     the value you compute by hand from the same inputs, the op miscompiles.
     No reference boot needed.
   - (b) **Differential cpu-test**: reproduce the instruction sequence + captured
     input as a Catch2 test; run host-x64 (`xenia-cpu-tests.exe`) vs a64-under-qemu
     (`a64-qemu-harness`). Divergence isolates the op; parity means the bug is
     elsewhere (byteswap, guest-global/init, or the path is wrong — back to RULE 0).

5. **Fix in the a64 backend** (`a64_seq_*.cc` / `a64_emitter.cc`), cvar-gated if
   risky. **Verify with a qemu cpu-test** (add a case that would have caught it).
   Then **device-validate** the title boots/renders past the failure.

6. **Ship**: targeted `git add` the fix + the new test only; commit
   (`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`). If it's a
   correctness fix, default-on; expose perf-relevant ones as a stacking
   `XeniaOptimizations` toggle.

## Tooling gotchas (these get rediscovered every fire — don't)

- **Gradle APK build**: `subst X: "<repo>"` then `& "X:\android\android_studio_project\gradlew.bat" -p "X:\android\android_studio_project" :app:assembleGithubDebug`. Run **foreground** (background does NOT see the subst; real paths with spaces break resource merge with `!directory.isDirectory`). **NEVER `2>&1`** on gradlew in PowerShell 5.1 — it raises `NativeCommandError` and corrupts the run (stderr is already captured). Native change ~40s-2.5min. APK: `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`.
- **Logcat buffer**: `adb -s c3ca0370 logcat -G 16M` BEFORE a capture fire, or the per-frame GPU-draw-outcomes spam rotates your operand lines out of the default ring buffer. `vulkan_trace_draw_outcomes_per_frame` is NOT allowlisted (can't `--ez` it off) — the big buffer is the fix.
- **Capture tool logcat lands at a cwd-relative path**: `thor_gpu_capture.ps1`'s `-OutDir` resolves against the PowerShell cwd, so the saved logcat can end up nested (e.g. `scratch/thor-debug/scratch/thor-debug/`). Dump it yourself after the fire: `adb -s c3ca0370 logcat -d > scratch/<tag>.txt` (with the 16M buffer it holds the whole run). Ghidra/PowerShell `>` writes **UTF-16** — `iconv -f UTF-16LE -t UTF-8` before grep.
- **Ghidra headless**: run it in the MAIN loop with a LONG timeout (600000), NOT inside a Workflow subagent (they hit the Bash default timeout and leave zombie `analyzeHeadless.bat`). `$env:JAVA_HOME=...jdk-21...; & ...\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat <projDir> <projName> -process -scriptPath <scripts> -postScript <Script>.java -noanalysis > log`. Reuse the `DumpVerify5.java`/`DumpLookup.java` window-dumper pattern (disassemble + print bytes + insn).
- **adb `--es` values**: the device shell splits on `;` (drops the rest of the `am` command incl. `--es target` = wasted no-game fire). Use **COMMAS** (the trace filter parser accepts `,`/`;`/space).
- **Thermal/battery + never-thrash**: gate FIRST (`/sys/class/kgsl/kgsl-3d0/temp` < 45°C, battery >= ~30%); ONE fire per cooldown (~10-15min); force-stop `jp.xenia.emulator.github.debug` + screen asleep (`input keyevent 223`) after EVERY fire. Read the png (RULE 0: never measure a movie). Never fabricate — numbers from this turn's output only.

## Anti-patterns (failure signatures to refuse)
- Patching/disassembling a PC you have NOT confirmed executes on the fail path (RULE 0).
- Re-firing the same refuted hypothesis (a game-patch that hides a symptom but hangs = the value isn't computed; only the JIT fix helps).
- Running the deep grind in blind 15-min ticks without a self-compute/differential gate — the cycle converges only when each fire either confirms a divergence or eliminates a path.
