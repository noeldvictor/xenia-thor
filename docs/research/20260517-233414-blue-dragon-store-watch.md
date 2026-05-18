# Blue Dragon Store Watch Results

Date: 2026-05-17 23:34 EDT

Repo: `xenia-thor`

Branch: `master`

Status: research-only experiment, not official Xenia support or compatibility.

## Question

Does the ARM64 Android backend ever write Blue Dragon guest global
`0x82785548` before the crash at `0x826A23E8`?

## Method

Added `arm64_guest_store_watch` and launched Blue Dragon with:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchBlueDragon -MountCache true -DisassembleFunctions true -Arm64GuestStoreWatch 82785548
```

The watch covers:

- ARM64 interpreter `STORE` and `STORE_OFFSET`;
- ARM64 interpreter atomics;
- ARM64 interpreter `MEMSET` ranges;
- ARM64 mini-JIT direct stores;
- ARM64 mini-JIT `memset` helper.

## Captures

- `scratch/thor-debug/20260517-232146-logcat.txt`
- `scratch/thor-debug/20260517-232704-logcat.txt`
- `scratch/thor-debug/20260517-233052-logcat.txt`
- `scratch/thor-debug/20260517-233344-logcat.txt`

All watched runs still crash at:

- Guest PC: `0x826A23E8`
- Function: `0x826A23C8-0x826A23F4`
- Global: `0x82785548 == 0x00000000`

## Result

No store-watch hit appeared for `0x82785548`.

The HIR report for `20260517-233344-logcat.txt` shows:

- no unimplemented interpreter opcodes;
- unsupported mini-JIT opcode `111` / `OPCODE_ATOMIC_COMPARE_EXCHANGE` twice;
- guest crash PC `0x826A23E8`;
- no guest store watch hits.

## Caller

Crash LR maps to function `0x821903B8-0x821904F8`.

The direct call site:

```text
82190490 7FA7EB78   or         r7, r29, r29
82190494 80810070   lwz        r4, 0x70(r1)
82190498 38C00000   addi       r6, r0, 0x0
8219049C 38A00000   addi       r5, r0, 0x0
821904A0 48511F29   bl         0x826A23C8
```

This calls the thunk with the singleton/global still null:

```text
826A23DC 816B5548   lwz        r11, 0x5548(r11)
826A23E8 814B0000   lwz        r10, 0x0(r11)
```

## Static Probe Result

Plain file-offset probing of `scratch/blue-dragon/default.xex` did not map
guest addresses directly to PPC code bytes. That file is not enough for simple
flat-address static analysis.

Ghidra `analyzeHeadless` was not found in `PATH`, `GHIDRA_HOME`, common Program
Files paths, or obvious user folders checked during this session.

## Interpretation

The current evidence points away from "the ARM64 backend writes the wrong value
to `0x82785548`." More likely:

1. the initializer path is not reached before the caller invokes the thunk;
2. the initializer was not translated in this run;
3. some earlier I/O or kernel emulation gap skips setup;
4. a native loader/module initialization path should seed the global but does
   not on this Android ARM64 bring-up path.

The recurring failed opens under `D:\!necessity\font\...` remain suspicious
because they happen shortly before the null thunk call, but they are not proven
causal.

## Next Fastest Step

Build an instruction-aware guest reference scanner from Xenia's translated PPC
debug info. It should decode global address patterns rather than string-match
opcode bytes, then list candidate writers to `0x82785548` and caller functions
near `0x821903B8`.
