# Blue Dragon Null Thunk On Thor

Date: 2026-05-17 23:10 EDT

Repo: `xenia-thor`

Branch: `master`

Status: research-only experiment, not official Xenia support or compatibility.

## Question

Why is the current AYN Thor Blue Dragon run still "not working" after the
Android cache mount fix and noisy import stubs?

## Evidence

Latest useful capture:

- `scratch/thor-debug/20260517-230738-logcat.txt`
- `scratch/thor-debug/20260517-230738-logcat-filtered.txt`
- `scratch/thor-debug/20260517-230738-meta.txt`
- `scratch/thor-debug/20260517-230738-screenshot.png`

The latest capture still stops in guest PPC code:

- Guest PC: `0x826A23E8`
- Guest function: `0x826A23C8-0x826A23F4`
- Host generated-code offset: function code `+0x1D8`
- Blue Dragon thunk global probe: `0x82785548 == 0x00000000`
- LR: `0x821904A4`
- CTR: `0x00000000`
- CR: `0x44000448`

The source disassembly around the fault shows a thunk-like sequence:

```text
826A23DC lwz r11, 0x5548(r11)
826A23E8 lwz r10, 0x0(r11)
```

The `0x82785548` global is zero, so the second load dereferences zero. That is
the immediate reason the title does not reach the title screen.

The HIR coverage report for this log shows no unimplemented interpreter opcode.
It does still show unsupported mini-JIT opcode `111`
(`OPCODE_ATOMIC_COMPARE_EXCHANGE`) twice, plus many intentional mini-JIT
fallbacks for non-integer HIR types.

## What Was Eliminated

`mount_cache` was a real Android bug. Before the fix,
`scratch/thor-debug/20260517-230032-logcat.txt` aborted while trying to create
`cache0` in a read-only current directory. After moving cache devices under the
Android cache root, that failure disappeared.

`XamSwapDisc` was real import noise. After adding a research stub for
`XamSwapDisc` and `XamLoaderGetDvdTrayState`, the previous "ordinal 0xA28 not
found" warning is gone. The Blue Dragon crash did not move.

The current stop is not the Android activity shell, not the Vulkan surface, not
RenderDoc, and not a missing interpreter opcode.

## I/O Clue

Immediately before the crash, the title attempts to open several paths:

- `D:\!necessity\font\font_01_02_us.gly`
- `D:\!necessity\font\font_02_01.kng`
- `D:\!necessity\font\font_02_02.gly`
- `D:\!necessity\font\font_03_02.gly`

They fail with `C000000F`. Earlier GDFX inspection showed
`pack\!necessity.ipk` exists on disc, but not a root `\!necessity\font`
directory. This may be normal virtual archive behavior, or it may be a clue
that a filesystem/cache/STFS path is not being initialized before the thunk is
called.

## External Lead

The public Xenia game-compatibility issue for Blue Dragon tags the title as
requiring `clear_memory_page_state=true`.

Source, accessed 2026-05-17:

- https://github.com/xenia-project/game-compatibility/issues/988

This is only a lead. The current fork has a much earlier Android ARM64 null
guest global, and local source search did not yet find a directly reusable
`clear_memory_page_state` implementation in the current codebase.

## Working Hypotheses

1. A guest initialization function that should write `0x82785548` is skipped,
   miscompiled by the ARM64 mini-JIT, or returns early due to an emulation gap.
2. A memory/cache-page semantic difference known to affect Blue Dragon on
   upstream Xenia may be leaving a guest state page uncleared or uncleared at
   the wrong time.
3. The failed `D:\!necessity\font\...` opens may be harmless fallback probes,
   but they are close enough to the crash to keep in the trace until proven
   irrelevant.
4. The new XAM/XboxKrnl stubs reduce noise but may also be hiding needed
   semantics. They should stay clearly marked as research stubs.

## Fastest Next Loop

1. Use Ghidra or a smaller PPC disassembly helper on
   `scratch/blue-dragon/default.xex` to resolve:
   - the global at `0x82785548`;
   - the crashing thunk at `0x826A23C8`;
   - caller `LR=0x821904A4`;
   - stack return `0x82069C44`.
2. Add a guest memory watch for writes to `0x82785548` and maybe the surrounding
   global block.
3. Run three Thor variants from one script without APK shell rebuilds:
   - mini-JIT on;
   - mini-JIT off;
   - mini-JIT on with `0x826A23C8-0x826A23F4` and any initializer candidates
     forced through the interpreter.
4. If no write ever reaches `0x82785548`, trace the control-flow path from
   `0x821904A4` backward to the expected initializer instead of adding more
   random kernel stubs.

## Decision

Do not claim Blue Dragon works. The current milestone remains "get Blue Dragon
to title on AYN Thor." The next useful work is targeted guest-state debugging,
not more broad unimplemented-operation whack-a-mole.
