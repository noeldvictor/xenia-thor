# Blue Dragon PPC Global Reference Scan

Date: 2026-05-18 09:20 EDT

Repo: `xenia-thor`

Branch: `master`

Status: research-only experiment, not official Xenia support or compatibility.

## Question

Which translated PPC functions directly reference Blue Dragon guest global
`0x82785548` before the Thor crash?

## Change

Replaced the previous crash-time text search with an instruction-aware scanner.
It parses PPC instruction words from Xenia's translated source-disassembly
lines, tracks simple global address formation, and reports exact load/store
references to `0x82785548`.

The tracker currently understands:

- `addis`
- `addi`
- `ori`
- `or` register copies
- D-form load/store instructions such as `lwz` and `stw`
- DS-form 64-bit load/store instructions such as `ld` and `std`

## Device Run

Capture:

- `scratch/thor-debug/20260518-091952-logcat.txt`
- `scratch/thor-debug/20260518-091952-logcat-filtered.txt`
- `scratch/thor-debug/20260518-091952-meta.txt`
- `scratch/thor-debug/20260518-091952-screenshot.png`
- `scratch/thor-debug/20260518-092516-logcat.txt`
- `scratch/thor-debug/20260518-092516-logcat-filtered.txt`
- `scratch/thor-debug/20260518-092516-meta.txt`
- `scratch/thor-debug/20260518-092516-screenshot.png`

Launch:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchBlueDragon -MountCache true -DisassembleFunctions true -Arm64GuestStoreWatch 82785548
```

## Result

The scanner found exactly one translated direct reference:

```text
PPC global ref 0x82785548: load lwz fn 0x826A23C8-0x826A23F4 at 0x826A23DC:
826A23DC 816B5548   lwz        r11, 0x5548(r11)
```

Summary:

```text
PPC global ref search 0x82785548: 1 translated direct refs, 1 loads, 0 stores
```

The crash remains:

- Guest PC: `0x826A23E8`
- Function: `0x826A23C8-0x826A23F4`
- Global value: `0x82785548 == 0x00000000`
- LR: `0x821904A4`
- Direct call site: `0x821904A0 bl 0x826A23C8`

## Interpretation

The failure is no longer a broad "many operations missing" problem for this
specific stop. In the translated guest code available at crash time, the only
direct reference to `0x82785548` is the crashing load, and no direct writer was
translated or reached.

The initializer is likely one of these:

1. not reached before the caller invokes the thunk;
2. not translated yet in this run;
3. using an indirect/non-direct addressing path the simple scanner does not
   catch;
4. part of a native/module setup path outside translated PPC functions.

The repeated failed opens under `D:\!necessity\font\...` still occur shortly
before the crash and remain a high-value trace target.

The second capture added path-focused `NtCreateFile` success telemetry. It
shows that the disc archive itself is present and opens successfully:

```text
D:\pack\!necessity.ipk
```

Earlier pack files also open:

```text
D:\pack\gimmick.iph
D:\pack\gimmick.ipk
D:\pack\map\gimmick.iph
D:\pack\map\gimmick.ipk
```

That makes the later `D:\!necessity\font\...` failures look less like a missing
disc file and more like missing archive virtual-path setup or a skipped
game-side archive initializer.

## Next Fast Loop

Add path-focused file open telemetry and targeted function/return tracing:

- caller function `0x821903B8-0x821904F8`;
- helper `0x822710D0`;
- crashing thunk `0x826A23C8`;
- functions that touch `\pack` or `\!necessity`.
- reads and return values around `D:\pack\!necessity.ipk`.
