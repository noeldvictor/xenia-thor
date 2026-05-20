# Blue Dragon 8272A8B4 Copy-Loop Fastpath Dead End

Date: 2026-05-19 23:45 EDT
Branch: `master`
Base commit: `bbab199aa`
Device: AYN Thor Max / `c3ca0370`
Target: `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

Can the hot `8272A3A4:8272A8B4` Blue Dragon decompressor byte-copy loop be
replaced with a hand-emitted A64 forward byte loop to reduce the current
loading-route CPU wall?

## Static Shape

The PPC loop:

```text
8272A8B4 lbz   r8, 0(r10)
8272A8B8 addi  r11, r11, -1
8272A8BC addi  r10, r10, 1
8272A8C0 cmpli cr6, 0, r11, 0
8272A8C4 stb   r8, 0(r19)
8272A8C8 addi  r19, r19, 1
8272A8CC bne   cr6, 8272A8B4
8272A8D0 lwz   r7, 0x40(r29)
```

Important HIR detail: `loc_8272A8B4` is not just the tight loop. The optimized
HIR block also includes the fallthrough code starting at `8272A8D0`, so a
prefix fastpath must resume at source offset `8272A8D0`, not at the end of the
HIR block.

## Attempts

First attempt:

- Replaced the whole HIR block when `current_guest_function_ == 0x8272A3A4`
  and block guest PC was `8272A8B4`.
- Result: early guest crash at `PC=0x8272A8B4`, function reported as
  `0x8272A834-0x8272AC00`.
- Bad runtime state in crash dump included `r11=FFFFFFFFD0018363`, `r10=70000000`,
  and `r19=6FFFC504`.
- Capture: `scratch/thor-debug/20260519-232831-*`.

Second attempt:

- Added runtime count guards and fallback to original HIR for zero, high-word,
  or greater-than-`0x4000` counts.
- Changed the fastpath branch to resume at `8272A8D0` inside the HIR block.
- Result: no immediate crash, but Blue Dragon went black and idle with guest
  counters dropping to zero.
- Short capture: `scratch/thor-debug/20260519-234005-*`, APK SHA-256
  `CBD814693DBF63790544BA10FA1A17F7200E486366302E50919E4292DE211B2F`.
- Full capture: `scratch/thor-debug/20260519-234143-*`, same APK SHA-256.

## Evidence

The route-clean baseline before this experiment:

- Capture: `scratch/thor-debug/20260519-232621-*`
- APK SHA-256: `967893404DACBB6675A5FAE46F840FB0735D0C49C948236F1F14AEC95324AC13`
- `arm64_blue_dragon_decompress_copy_fastpath=false`
- Reached the Blue Dragon loading spinner with no searched fatal markers.
- Final interval still moved: `entry_delta=18165200`.

The guarded fastpath-on full run:

- Capture: `scratch/thor-debug/20260519-234143-*`
- APK SHA-256: `CBD814693DBF63790544BA10FA1A17F7200E486366302E50919E4292DE211B2F`
- No searched fatal signal, AndroidRuntime crash, `VK_ERROR_DEVICE_LOST`, or
  GPU-hung marker.
- Guest execution stopped after the early burst:
  - `23:42:01`: `entry_delta=3888813`
  - `23:42:16`: `entry_delta=68060`
  - `23:42:31` onward: `entry_delta=0`
- Final perf sample showed Xenia threads sleeping and the screenshot was a
  black frame, not the route-clean loading spinner.

## Decision

Do not keep the `8272A8B4` copy-loop fastpath as code. It is too easy to make
locally plausible and globally wrong because the hot loop lives inside a larger
HIR block and the decompressor state is sensitive to exact register and
fallthrough behavior.

If this path is revisited, require all of the following before committing code:

- A PPC/HIR side-by-side proof for the full `8272A8A8` through `8272AA50`
  region, not only the `lbz/stb` loop.
- A route-clean fastpath-on capture that reaches the same visible milestone as
  the fastpath-off baseline.
- Final counters still moving or a clearly identified intentional wait state.
- A screenshot proving route status.
- A before/after body-time comparison against the same route and cvars.

## Next Better Target

The block profiler still usefully identified `8272A3A4` as expensive, but the
next optimization should target a full PPC region or a generic A64 lowering
improvement rather than a single partial HIR-block splice. Candidate next work:

- Add an OODA probe for the full `8272A3A4` state machine around
  `8272A3F4`, `8272A474`, `8272A548`, `8272A8B4`, and `8272AA50`.
- Compare ARM64 lowering for `load_offset/store_offset`, context stores, and
  self-loop branches against x64 backend patterns.
- Use Ghidra/PPC dumps to identify whether `8272A3A4` is an LZ/decompressor
  routine with a wider safe fastpath boundary.
