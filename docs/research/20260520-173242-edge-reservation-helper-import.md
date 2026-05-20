# Edge Reservation Helper Import

Date: 2026-05-20 17:32 EDT
Branch: `master`
Base commit: `638651f6e`
Device: AYN Thor Max / `c3ca0370`
Target: `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

Can we pull a concrete newer Xenia Edge ARM64 backend behavior that addresses
the "Xbox 360 had three PPC cores" problem instead of only shrinking scalar
hot loops?

## Donor Check

- `aenu1/ax360e` `main` is still at
  `2b4c889e8f849d07ede83334fa0b323f5c647828`.
- `has207/xenia-edge` `edge` moved from
  `302034249048dc9d664b12b6a3560a6d95933803` to
  `eee166febd27dfdffc556c4474e1aa49f2b4ccc5`.
- The Edge A64 backend opcode surface is not meaningfully ahead of this fork:
  both cover the same HIR opcode names for the current A64 files. This means
  the missing "speed magic" is not a large unimported opcode table.
- The latest Edge A64 changes are mostly trace plumbing plus a reservation
  helper path for `RESERVED_LOAD/STORE`.

## What Was Imported

Added Edge-style global reservation helper code to the Thor A64 backend:

- `TryAcquireReservationHelper`
- `ReservedStore32Helper`
- `ReservedStore64Helper`
- helper pointer wiring in `A64Backend::Initialize`
- an opt-in `arm64_global_reservation_helpers` cvar
- a Thor launcher flag: `-Arm64GlobalReservationHelpers true`

The helper path stores PPC reservations in a global per-cache-line bitmap and
routes `RESERVED_LOAD/STORE` through host helpers. This is closer to the donor
model than the old inline generated-code CAS path, and it directly targets PPC
multicore reservation semantics rather than raw scalar speed.

## Thor Evidence

Default-on experiment before adding the cvar gate:

- Capture: `scratch/thor-debug/20260520-172518-*`
- APK SHA-256:
  `5F62F4975DF6A33669DC08D1940615F83B0905BB58B3E06B32778756BB2EED94`
- NativeCore and FullDeploy succeeded.
- No searched fatal signal, AndroidRuntime crash, `VK_ERROR_DEVICE_LOST`, or
  GPU-hung marker.
- Route regressed to black idle:
  - first active interval: `entry_delta=3849847`
  - second interval: `entry_delta=171433`
  - later intervals: `entry_delta=0`
- Final screenshot was black.

Default-off gated build:

- Capture: `scratch/thor-debug/20260520-173018-*`
- APK SHA-256:
  `1E9D57B65AE7CE7E86D54254010C3E981D1C20C760CCE9FB7F0CF34A1D72E639`
- `arm64_global_reservation_helpers=` in metadata, so the legacy inline path
  was used.
- NativeCore and FullDeploy succeeded.
- The run still hit the known timing-sensitive black-idle route, but the first
  interval preserved the pre-import body-time code-size shape:
  `8272A3A4 code_size=12544`, `8272A8E8 code_size=5756`,
  `827294CC code_size=412`, `826BF770 code_size=296`.

## Decision

Keep the imported reservation helpers, but keep them default-off.

This is useful backend maturity infrastructure and a clean donor import, but it
is not a Blue Dragon speed fix yet. Turning it on by default makes the current
Blue Dragon route idle after the early burst. Future sync investigations can
enable it explicitly with:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -Arm64GlobalReservationHelpers true
```

## Bigger Lesson

aX360e/Edge does not appear to have a hidden giant opcode table that we simply
forgot to copy. The faster path is to mature the backend around the actual
walls we observe on Thor:

- reduce PPC context CR/GPR loads and stores in hot functions;
- fuse common PPC compare/branch/CR-store shapes with exact-use guards;
- improve direct and indirect block linking;
- keep function/body/block profiling cheap enough for fast OODA;
- use donor changes only when they map to a measured route problem.

For Blue Dragon, the immediate CPU wall remains `8272A3A4` and surrounding
decompress/state-machine code. The next high-leverage work should not be more
blind donor copy. It should be either a full-region `8272A3A4` proof or a
generic A64 register/context lowering audit that tells us which PPC state
round-trips are still burning generated-code cycles.
