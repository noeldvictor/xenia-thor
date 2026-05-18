# Blue Dragon ARM64 Vec128 Mini-JIT Fallback-Zero Capture

Date: 2026-05-18 11:52 EDT

Status: research-only experiment, not compatibility evidence.

## Goal

Push the ARM64 mini-JIT past the Blue Dragon Disc 1 vector fallback wall on the
AYN Thor Max, while preserving the split native/APK deploy loop and dated
captures.

## Implementation Summary

- Added a real mini-JIT stack slot layout instead of fixed 64-bit slots.
- Tracked HIR local types from the builder into `Arm64Function::Program` so
  vec128 locals can be allocated and copied safely.
- Added 16-byte aligned vec128 value/local slots.
- Added helper-backed vec128 local/context/memory load-store support.
- Added helper-backed vector and mixed scalar/vector HIR coverage:
  `LOAD_VECTOR_SHL`, `LOAD_VECTOR_SHR`, `UNPACK`, `BYTE_SWAP`, `EXTRACT`,
  `SPLAT`, `PERMUTE`, `SWIZZLE`, `DOT_PRODUCT_3`, `DOT_PRODUCT_4`,
  `VECTOR_CONVERT_I2F`, `VECTOR_CONVERT_F2I`, vector compare, vector add/sub,
  vec128 select, vec128 not/neg/abs/sqrt/rsqrt/recip/pow2/log2, and vec128
  mul-add/mul-sub.

This remains a bring-up mini-JIT, not a final backend. Many helpers are C++
calls for correctness and iteration speed rather than optimized AArch64/NEON.

## Captures

All captures used:

- Device: AYN Thor Max, serial `c3ca0370`
- Package: `jp.xenia.emulator.github.debug`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Store watch: `82785548`

Capture progression:

- `scratch/thor-debug/20260518-112417-*`
  - Cleared vec128 slot storage issues enough to expose vec128 byte-swap,
    constant-address, and binary operation gaps.
- `scratch/thor-debug/20260518-112859-*`
  - Cleared byte-swap/constant/binary gaps and exposed `PERMUTE`, `SWIZZLE`,
    `EXTRACT`, and `DOT_PRODUCT_3`.
- `scratch/thor-debug/20260518-113514-*`
  - Cleared permute/swizzle/extract/dot3 and exposed `SPLAT`,
    `DOT_PRODUCT_4`, vec128 mul-add/sub, and `VECTOR_CONVERT_I2F`.
- `scratch/thor-debug/20260518-114058-*`
  - Cleared that wall and exposed `ROUND`, `VECTOR_COMPARE_SGE`,
    `VECTOR_ADD`, `RSQRT`, `RECIP`, and vec128 not/neg.
  - APK SHA-256:
    `54D570DAFC9E649FCB52369685915C72043FB9B67A12C2F1E5FDBAE6A7F46935`.
- `scratch/thor-debug/20260518-114733-*`
  - Cleared numeric opcode fallbacks and exposed only vec128 `SELECT`.
  - APK SHA-256:
    `8798F32F5FDDD58A0054B843951D2B4FACD5D04B65B0966D3D7B78C575D13468`.
- `scratch/thor-debug/20260518-115022-*`
  - No `ARM64 JIT fallback`, unsupported opcode/type, native crash, tombstone,
    or fatal-signal lines found by `rg` in the full and filtered logcats.
  - APK SHA-256:
    `D391A883C0F4EDDA23DECC2134DB3DF2FF10D8C608AD673FB7056DF05A0F7054`.

## Findings

- The old sound-bank null-thunk crash still does not recur.
- The store watch still hits:
  `826A2550@826A2598->82785548`.
- Blue Dragon continues to reach later pack loading:
  `effect.iph`, `item_rec.iph`, `minigame.iph`, `minimap.iph`, `sca.iph`,
  `sequence.iph`, `summon.iph`, `battle.ipk`, `d2anime.ipk`,
  `database.ipk`, `script.ipk`, `packmem_us.ipk`, and `battle_start.ipk`.
- The final 90-second capture has zero mini-JIT fallback lines.
- The screenshot is still only the Android emulator shell/OSD with the static
  `AArch64 JIT pending` badge; Blue Dragon has not reached visible title video.

## Decision

Keep the helper-backed mini-JIT path as the fast correctness lane for now. The
fallback-zero run means the next bottleneck is probably not the first-order HIR
coverage wall for this launch window. The next loop should profile progress
after pack loading and decide whether the stall is guest CPU progress, render
submission/presentation, shader/texture work, or static Android OSD masking.

## Next Actions

- Replace the static Java `AArch64 JIT pending` badge with runtime backend/JIT
  state so screenshots do not mislead the debugging loop.
- Add progress sampling after `battle_start.ipk`: thread PCs, guest function
  counters, and present/draw-submit counters.
- Add focused graphics telemetry for command processor progress, swapchain
  presents, and first non-clear draw evidence.
- Consider a RenderDoc/AGI capture only after a normal non-RenderDoc run proves
  the guest is reaching render submission.
