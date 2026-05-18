# Blue Dragon ARM64 Mini-JIT Vector Wall

Date: 2026-05-18 11:13 EDT

## Summary

The Blue Dragon Thor run moved past the previous float, atomic, stack-frame,
and code-cache fallback walls. The latest validated capture still does not
show game video or title screen, but the remaining ARM64 mini-JIT rejection set
is now almost entirely vector/vec128 related.

Latest capture:

- Log: `scratch/thor-debug/20260518-111309-logcat.txt`
- Screenshot: `scratch/thor-debug/20260518-111309-screenshot.png`
- APK SHA-256:
  `E0D689EFAEEF54516BC1D3ECE4020CC0D49ECEE93DC88D78B9E1ED1A931E9FCA`
- Device: AYN Thor Max, serial `c3ca0370`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Implemented This Slice

- Mini-JIT rejection reasons are now retained per function and surfaced in
  slow-function logs.
- Slow interpreter logs include guest source offset, HIR opcode name, nearby
  HIR instruction window, and the stored JIT rejection reason.
- The mini-JIT now accepts raw `FLOAT32_TYPE` and `FLOAT64_TYPE` slots for
  load/store/local/context/memory paths.
- Added helper-backed scalar coverage for:
  - float cast/convert
  - float add/sub/mul/div
  - float mul-add/mul-sub
  - float neg/abs/sqrt
  - float compare
  - `IS_NAN`
  - `ATOMIC_COMPARE_EXCHANGE`
  - MMIO load/store
- Raised the research mini-JIT stack cap with cvar
  `arm64_mini_jit_max_stack_bytes`.
- Replaced giant generated zero-fill stack sequences with a helper call so
  large temporary stack frames do not inflate generated code.
- Raised the ARM64 generated code cache to a cvar-backed 128 MiB default for
  long Thor captures.
- Raised the per-function Xbyak code buffer to 2 MiB.

## Capture Progression

Key run snapshots:

- `20260518-103516`: hot functions rejected on generic
  `load_context has non-integer type`.
- `20260518-103816`: rejection detail narrowed that to `float64`.
- `20260518-104245`: float raw slots moved the blocker to
  `assign has non-integer type float64`.
- `20260518-104607`: same-type float assignment moved the blocker to
  `unsupported opcode 23` (`OPCODE_CONVERT`).
- `20260518-105127`: conversion moved the blocker to float cast and float
  binary arithmetic.
- `20260518-105521`: old hot slow-loop logs disappeared for the first 85 s,
  Blue Dragon loaded many `D:\pack\*.iph` and `*.ipk` files, and the soundbank
  store-watch still hit.
- `20260518-110845`: stack/code-cache fixes collapsed fallback counts to a
  small vector-heavy set plus `SQRT`.
- `20260518-111309`: scalar `SQRT` and MMIO fallbacks disappeared; remaining
  mini-JIT misses are vector/vec128.

## Latest Fallback Table

From `scratch/thor-debug/20260518-111309-logcat.txt`:

```text
Count Name
    8 load_memory has unsupported slot type vec128
    7 unsupported opcode 27
    7 store_context has unsupported slot type vec128
    5 unsupported opcode 109
    3 load_context has unsupported slot type vec128
    1 unsupported opcode 28
```

Opcode map:

- `27`: `OPCODE_LOAD_VECTOR_SHL`
- `28`: `OPCODE_LOAD_VECTOR_SHR`
- `109`: `OPCODE_UNPACK`

## What This Means

This is not at title yet. The visible screenshot is still the native Android
OSD with the static `AArch64 JIT pending` badge.

However, the CPU bring-up wall changed shape. Earlier we were blocked by common
scalar float and synchronization operations inside hot functions. The latest
run gets through those and leaves vector state as the main mini-JIT gap.

The current mini-JIT stores each HIR value/local in a 64-bit stack slot. That is
fine for integer and scalar float raw bits, but `VEC128_TYPE` needs a real
16-byte slot model or a separate vector spill area. Treating vec128 as another
raw 64-bit slot would corrupt guest vector state.

## Next Engineering Decision

The next serious patch should add a mini-JIT slot-layout table:

- Track byte offset and byte size for every HIR value.
- Use 16-byte aligned slots for `VEC128_TYPE`.
- Track local value types, not only `local_count`, so vec128 locals can be
  loaded/stored safely.
- Add helpers or NEON paths for:
  - vec128 context load/store
  - vec128 memory load/store
  - `LOAD_VECTOR_SHL`
  - `LOAD_VECTOR_SHR`
  - `UNPACK`

Short-term alternative: keep vector functions in the interpreter and focus on
non-vector hot loops. The latest fallback table suggests vec128 is now common
enough that the cleaner slot-layout upgrade is probably the right next step.

## Confidence

High confidence for the scalar fallback reduction because it is backed by
device captures, APK hashes, and repeated Blue Dragon launches on the Thor.

Medium confidence that vec128 slot layout is the next best engineering step.
The evidence points there, but a longer run with thread PC sampling would help
separate "vector interpreter is slow" from "GPU is not yet presenting guest
frames."
