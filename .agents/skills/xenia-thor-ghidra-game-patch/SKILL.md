---
name: xenia-thor-ghidra-game-patch
description: "Use to author Xbox 360 game patches (.patch.toml) for the xenia-thor patcher with Ghidra: performance fixes (60 FPS, disable blur/SSAO), cheats (infinite lives/money/ammo), and compatibility bypasses (Banjo Nuts & Bolts dirty-disc false-verification). Covers extracting the XEX from an ISO, loading PowerPC/Xenon big-endian into Ghidra, getting the exact title build hash the engine matches, finding the instruction/value to change, computing the guest patch address, and emitting + testing the .patch.toml. Pairs with the in-app Game Patches manager and the xenia-ghidra-ooda-loop skill."
---

# Xenia Thor — Ghidra Game-Patch Authoring

Keep this skill repo-local. Use it to turn a goal ("infinite money in Blue
Dragon", "boot Banjo past the dirty-disc error", "uncap to 60 FPS") into a
`.patch.toml` the fork's native patcher applies at title launch.

This is the **guest-content** lane (patch the game's PowerPC binary). For
emulator-bug RE (JIT/HLE/native crashes) use **xenia-ghidra-ooda-loop** instead;
share its Ghidra home, scratch layout, and safety rules.

## How a patch maps to the binary (read first)

The patcher is `src/xenia/patcher/` (engine) + `GamePatchManager` (in-app UI).
At title launch `KernelState::LoadUserModule` calls
`Patcher::ApplyPatchesForTitle(memory, title_id, hash)`, which writes each
patch's bytes straight into guest virtual memory.

- **Patch files** live in `files/patches/*.patch.toml` on the device
  (`storage_root/patches`; Android `storage_root == getFilesDir()`). The in-app
  **Game patches** screen downloads/toggles them; you can also `adb push` one.
- **Matching key** = `title_id` (8 hex) **+ build hash**. The hash is XXH3-64 of
  the module's code section (`UserModule::CalculateHash`). A wrong/missing hash =
  the patch silently does not apply.
- **Address** fields are **guest effective addresses** (`0x82xxxxxx`). Ghidra's
  listing address == the TOML `address` directly (load base `0x82000000`).
- **Value** types: `be8/be16/be32/be64` (integers, stored big-endian — the
  patcher byte-swaps), `f32/f64` (floats), `string`, `u16string`, `array`
  (hex bytes). A PPC instruction patch is `be32` with the instruction **word**
  as the value (e.g. `0x60000000` = `nop`).

`.patch.toml` shape (community xenia-canary/game-patches format):

```toml
title_name = "Blue Dragon"
title_id = "4D5307DF"
hash = "ABCDEF0123456789"   # the Module Hash from logcat (see below)

[[patch]]
name = "Infinite Gold"
desc = "Gold never decreases on purchase"
author = "you"
is_enabled = true

  [[patch.be32]]
  address = 0x8265F1A0   # the instruction to overwrite
  value = 0x60000000     # nop
```

## Step 1 — get the exact build hash (required)

The hash is logged by `UserModule::Dump()`. Launch the game once, then read it:

```powershell
adb -s c3ca0370 logcat -d | Select-String "Module Hash"
# -> "Module Hash: 00000000ABCDEF01"  (use the 16-hex value as `hash`)
```

`title_id` is logged alongside (and shown in the in-app Game Patches header).
The launch must use the **same ISO build** you will RE — different builds /
title updates have different hashes and different addresses.

## Step 2 — extract the XEX from the ISO

The executable is `default.xex` inside the GDFX ISO. Work only in the ignored
`scratch\ghidra\guest\` (never commit XEX/ISO/keys).

- Extract with `scripts/gdfx_extract.py` (mirrors the fork's own GDFX parser;
  random-access, never loads the multi-GB ISO):
  `python gdfx_extract.py "<image.iso>" default.xex scratch/ghidra/guest/<title>/default.xex`
- No separate decrypt/decompress step is needed — **XEXLoaderWV (Step 3) decrypts
  + decompresses the XEX inside Ghidra.** (To inspect: the FILE_FORMAT_INFO
  optional-header key `0x000003FF` holds `encryption_type` then `compression_type`
  — basic block compression = `1`, LZX = `2`; IMAGE_BASE key `0x00010201` is the
  load base, almost always `0x82000000`.)

## Step 3 — load into Ghidra with XEXLoaderWV (decrypts + decompresses)

Ghidra home is in **xenia-ghidra-ooda-loop**
(`...\toolchains\ghidra_12.0.4_PUBLIC`). Two one-time setup items (already done in
this environment):

- **XEXLoaderWV extension** (zeroKilo) — handles AES decrypt, basic/LZX
  decompress, sections, and **names xam/xboxkrnl imports by ordinal** (so you can
  search for `XamShowDirtyDiscErrorUI` directly). Grab the build matching your
  Ghidra version from `github.com/zeroKilo/XEXLoaderWV/releases` (a `12.0.4`
  build exists) and unzip into `<GHIDRA>/Ghidra/Extensions/`.
- **JDK 21** — Ghidra 12 needs it; set `JAVA_HOME_OVERRIDE=<jdk21>` in
  `<GHIDRA>/support/launch.properties` if the system JDK is older.

Import + analyze headless (the loader auto-detects the XEX2 magic — no `-loader`
needed); keep the project so follow-up scripts run fast with `-process`:

```
analyzeHeadless.bat <projDir> <name> -import default.xex \
    -scriptPath scratch\ghidra\scripts -postScript <YourScript>.java
# re-run scripts against the analyzed project without re-analysis:
analyzeHeadless.bat <projDir> <name> -process default.xex -noanalysis \
    -scriptPath scratch\ghidra\scripts -postScript <YourScript>.java
```

A GhidraScript that walks `getReferencesTo()` from the named import to its callers
(decompile + assembly with `addr : bytes : insn`) pinpoints the branch/value to
patch. If a handler is reached indirectly (no code xref), search memory for its
big-endian pointer bytes to find the dispatch table.

## Step 4 — find the target

**Cheats (infinite lives / money / ammo):**
1. Find the variable's address with a live memory search (value freeze) OR by
   reasoning from the HUD/inventory code.
2. In Ghidra, xref that address to the `stw/sth/stb` that decrements it, or the
   `subi/subf` that computes the new (smaller) value.
3. Patch to neutralize the decrement: replace the decrement instruction with
   `nop` (`be32 value = 0x60000000`), or change `subi rX,rX,1` to add-zero, or
   force the store source to the pre-decrement value. One `[[patch.be32]]` per
   instruction word; consecutive instructions are `address`, `address+4`, …

**Performance (uncap / 60 FPS / disable a pass):**
- Find the frame-cap constant or the update-delta and patch it (`f32` for a
  float cap, `be32` for an integer/instruction). Disabling an effect is usually
  NOPing the call that sets it up. A/B against the community 60-FPS patch's
  addresses for the same title if one exists.

**Banjo Nuts & Bolts dirty-disc bypass (the known target):**
- Root cause (device-confirmed, see memory `banjo-dirty-disc-stuck-pending`): a
  **guest-side false verification** — data is byte-correct and the ISO is
  SHA-identical to a working PC copy, yet the bootstrapper calls
  `XamShowDirtyDiscErrorUI` then `XamLoaderLaunchTitle(NULL)` (exit to dash).
- In Ghidra, find the **import stub for `XamShowDirtyDiscErrorUI`** (xam.xex
  export) and follow its **xrefs** to the caller — that is the verify routine.
  The runtime trace shows it fires right after reading the head of
  `\bundle\50\685374`, so the verify is in/after that bundle check.
- Patch the **conditional branch** that routes to the error call so it always
  takes the success path: flip `beq`↔`bne`, or `nop` the branch, or patch the
  verify function's epilogue to return success. Verify you did not also skip
  required setup (read the function, not just the branch).

## Step 5 — emit the .patch.toml

Use the template above. Helpers in this skill:

- `references/ppc_patch_cookbook.md` — common PPC instruction words (nop, `li`,
  `blr`, branch flips), endianness notes, and value-type cheatsheet.
- `scripts/emit_patch_toml.py` — given title_id/title_name/hash + a list of
  `address,value[,type]`, writes a well-formed `.patch.toml`.

Keep `is_enabled = true` while testing; the in-app manager defaults community
patches to off.

## Step 6 — test on device (thermal-gated, FORWARD ONLY)

1. Put the file in `files/patches/` (in-app **Download/Game patches**, or
   `adb -s c3ca0370 push <file> /data/.../files/patches/`).
2. Launch via `tools/thor/thor_gpu_capture.ps1` (the ONLY launch path — it
   thermal-gates, 64 °C watchdog, one fire, force-stops when hot).
3. Confirm it applied: `adb logcat -d | Select-String "Patcher: Applying patch"`.
4. Read the screenshot + behavior. If wrong, **fix forward** with a corrected
   address/value — never revert. Re-arm with cooldown between device fires.

## Safety

- Analyze/patch only legally-owned local content.
- Never commit ISO contents, extracted/decompressed XEX, title keys, or Ghidra
  projects — keep them in ignored `scratch\ghidra\guest\`.
- A `.patch.toml` contains only addresses + replacement values (no copyrighted
  code) and **is** committable, with author credit. Commit derived notes too.
- A bad patch can crash the title but cannot brick the app — worst case, disable
  it in the Game patches screen or delete the file.
