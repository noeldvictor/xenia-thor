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
