---
name: xenia-ghidra-ooda-loop
description: "Use for repo-local Ghidra-assisted xenia-thor OODA work: turning Thor logcat, A64 speed profiles, guest PPC PCs, native ARM64 crash stacks, generated-code ranges, Blue Dragon wait loops, XEX imports, and suspected translation/HLE failures into a small static-analysis queue and a dated Markdown triage report. Use when Codex needs Ghidra, analyzeHeadless, PPC/XEX function inference, native libxenia-app.so analysis, or faster evidence-to-patch loops for AYN Thor."
---

# Xenia Ghidra OODA Loop

Keep this skill repo-local. Do not create or update global skills for this
project.

## Core Loop

1. Start from runtime evidence, not a whole-disc guess:
   - latest `scratch/thor-debug/*speed-logcat*.txt`;
   - A64 speed-profile top PCs;
   - A64 body-time top PCs when `arm64_speed_profile_body_time_filter` was
     enabled;
   - native crash/tombstone addresses;
   - generated-code ranges;
   - Xenia focused PPC dumps.
2. Classify each address:
   - `guest-ppc`: Xbox 360/Xenon PPC, XEX imports, waits, globals.
   - `native-arm64`: `libxenia-app.so`, JNI, Vulkan, Android, A64 backend.
   - `generated-jit`: emitted A64 bytes tied to a guest function.
3. Build a tiny analysis queue:
   - address or symbol;
   - evidence path;
   - question to answer;
   - likely owner: A64 JIT, GPU/Vulkan, kernel/XAM/HLE, audio/XMA, input, or
     automation.
4. Import only the smallest useful binary into Ghidra.
5. Write results to `docs/research/YYYYMMDD-HHMMSS-topic.md` and update
   `docs/worklogs/YYYYMMDD.md`.

## Local Paths

- Discovered Ghidra home:
  `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC`
- Default headless script:
  `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat`
- Ignored scratch root:
  `scratch\ghidra\`
- Native imports:
  `scratch\ghidra\native\`
- Guest PPC/XEX scratch:
  `scratch\ghidra\guest\`
- Generated JIT scratch:
  `scratch\ghidra\generated-jit\`
- Triage reports:
  `scratch\ghidra\reports\`

## Safety

- Analyze only legally owned local content.
- Do not commit ISO contents, extracted XEX files, title keys, account data,
  private screenshots, Ghidra projects, or generated game-code artifacts.
- Keep guest artifacts under ignored `scratch\ghidra\guest`.
- Commit only derived notes, scripts, and repo code.

## Native ARM64 Import

Use this for Android/native failures:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\ghidra_headless_import.ps1 -ProjectDir scratch\ghidra\native -ProjectName xenia-thor-native
```

Default import target:

```text
android/android_studio_project/app/build/intermediates/ndkBuild/githubDebug/obj/local/arm64-v8a/libxenia-app.so
```

## Guest PPC Lane

Use this for hot PCs such as `8246B408`:

- First prefer Xenia's focused PPC dumps:
  `-DisassembleFunctions true -DisassembleFunctionFilter "8246B408,8248B040"`.
- Use Ghidra guest PPC only after there is a concrete guest address and a
  question like "what token/timer/global does this loop wait on?"
- If body-time points at a large generated guest function such as `8272A3A4`,
  first classify the function shape, inner loop, imports/globals, and state
  fields before attempting a whole-body fastpath.
- For current Blue Dragon opening work, use
  `tools/thor/thor_hir_call_path_report.ps1` before Ghidra when block body-time
  points at a call-heavy HIR block. The 2026-05-22 audit found
  `822825E0 -> 0x82282490` and `822825C8 -> 0x8227FEE8` are inclusive
  call-path charges; a targeted `8227FEE8` dump capture idled before route
  progress. Next static question is whether `8227FEE8` is generated-code body
  work, guest-call overhead, or a helper/HLE boundary, but only after a stable
  route capture or direct-call edge profiler isolates it.
- Cross-check Ghidra output against Xenia's PPC disassembly, HIR dump, import
  logs, and runtime capture.

## Output Shape

End each Ghidra/OODA pass with:

- capture path and APK hash;
- address queue;
- inferred function purpose;
- imports/globals/tokens/timers touched;
- likely owner subsystem;
- next single experiment;
- files or cvars to change;
- rollback path.
