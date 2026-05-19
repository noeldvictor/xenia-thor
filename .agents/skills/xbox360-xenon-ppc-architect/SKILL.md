---
name: xbox360-xenon-ppc-architect
description: Analyze Xbox 360 Xenon PowerPC guest behavior in xenia-thor, including PPC/VMX128 code, XEX metadata, Blue Dragon guest PCs, D3D watchdogs, kernel/video/audio shims, Ghidra guest disassembly, import thunks, wait loops, and guest memory/register evidence.
---

# Xbox 360 Xenon PPC Architect

Use this skill when the blocker looks guest-side rather than host-codegen-side.
The job is to explain what the Xbox 360 program thinks is happening.
For this fork, guest analysis starts from AYN Thor ARM64 runtime evidence unless
Windows is needed as a narrow semantics control.

## Scope

- Xenon PowerPC in 32-bit guest mode with 64-bit-capable registers.
- VMX128 / AltiVec guest behavior as represented by Xenia HIR.
- XEX metadata, import libraries, ordinals, module entry points, and static
  libraries.
- Xbox kernel/video/audio exports under `src/xenia/kernel`.
- Blue Dragon guest functions and DbgPrint / watchdog paths.
- Ghidra guest PPC analysis of legally owned local content only.

## Fast Triage

Search the latest log for:

```powershell
rg -n "DbgPrint|GPU is hung|Breaking into|ERR\[D3D\]|guest crash|XThread::Execute|VdSwap|VdSet|NtCreateFile|DiscImageDevice" scratch\thor-debug\*-logcat.txt
```

Classify the signal:

- `DbgPrint` hang text: identify the caller PC and what guest condition failed.
- import/export issue: inspect the matching shim in `src/xenia/kernel`.
- wait loop: inspect guest wait addresses, event handles, and token memory.
- guest function hot spot: use targeted disassembly or Ghidra, not broad dumps.
- A64 speed-profile top PC: explain the guest loop or helper before changing
  the A64 backend.

## Files To Inspect

- `docs/cpu.md`
- `docs/kernel.md`
- `.agents/skills/xenia-a64-speed-hotpath/SKILL.md`
- `src/xenia/cpu/ppc/`
- `src/xenia/cpu/hir/`
- `src/xenia/kernel/xboxkrnl/`
- `src/xenia/kernel/xam/`
- `src/xenia/kernel/xboxkrnl/xboxkrnl_video.cc`
- `src/xenia/kernel/xboxkrnl/xboxkrnl_debug.cc`
- `src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc`

## Ghidra Rules

- Use `$xenia-ghidra-android-debug` for setup.
- Analyze only local, legally owned, extracted/decrypted guest code.
- Do not commit extracted XEX files, ISO contents, keys, or private images.
- Name functions and globals in notes by guest address plus inferred purpose.
- Tie every Ghidra claim back to a logcat address or export call.

## Blue Dragon Watchdog Checklist

When Blue Dragon reports a D3D/GPU hang:

1. Capture the exact DbgPrint text and thread handle.
2. Identify the guest PC around the break or print path.
3. Check whether ring read/write pointers visible to the guest are stale.
4. Check whether wait-token memory advances through `EVENT_WRITE_SHD`,
   `COND_WRITE`, interrupts, or host-side shortcuts.
5. Do not bypass `DbgBreakPoint` as a compatibility fix; use suppression only
   to keep a research run alive after logging the real condition.

## Output

End with:

- suspected guest invariant,
- evidence paths and lines,
- host subsystem likely responsible,
- next instrumentation or code fix,
- whether Ghidra is needed before changing code.
