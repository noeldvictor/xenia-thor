# xenia-thor

`xenia-thor` is a personal, vibecoded, agentic-AI experiment around bringing a
native Android ARM64 Xenia-derived research build to the AYN Thor Max.

It is **EXTREMELY UNSTABLE**.

This is not an official Xenia build, not a compatibility fork, not a release
channel, and not a supportable emulator for normal users. It is a messy local
research playground for learning, debugging, automation, and riding the vibes
while trying to make an Android ARM64 path move forward.

## No Support

There is no support promise here.

- Do not ask the official Xenia project for help with this fork.
- Do not report `xenia-thor` crashes to upstream Xenia, Xenia Discord, or other
  emulator communities.
- Do not treat any result here as a game compatibility claim.
- Do not expect builds to work from commit to commit.
- Do not use this as evidence that Xenia supports Android or AYN Thor.

If something breaks here, it probably belongs here.

## Thanks

Huge thanks to the original Xenia developers and contributors. Their years of
research, engineering, documentation, and open-source work made this experiment
possible at all.

Original project:

- [xenia-project/xenia](https://github.com/xenia-project/xenia)
- [xenia.jp](https://xenia.jp/)

Please respect their time. This fork is not their responsibility.

## Current Experiment

Target device:

- AYN Thor Max
- Android ARM64
- Vulkan on Adreno

Current focus:

- Android native shell and game picker flow.
- Thor-specific ADB build, deploy, launch, and capture scripts.
- ARM64 CPU backend/JIT research.
- Vulkan bring-up and future RenderDoc/AGI lanes.
- Blue Dragon Disc 1 as a legally owned local test case.

Current state as of 2026-05-18:

- The Android Vulkan window/demo path runs on the Thor.
- The emulator activity can launch Blue Dragon Disc 1 from the SD card.
- The earlier helper-backed ARM64 mini-JIT scaffold has been removed.
- The active ARM64 CPU path is now a hard aX360e/Edge-style A64 backend import.
- Blue Dragon can progress through sound-bank initialization and later pack-file
  loading without the earlier null-thunk crash.
- A KTHREAD timer issue was fixed enough for the Blue Dragon draw wait loop to
  move past a zero-uptime stall.
- GPU ring read/write pointers are now mirrored into guest-visible CP registers;
  the game's D3D dump now sees drained matching ring pointers.
- The donor A64 backend now compiles into the Android native core for
  `arm64-v8a` and `x86_64`, which is build progress, not a compatibility
  claim.
- It still does not reach title or playable gameplay.
- The next main lane is running this imported A64 backend on Thor, capturing the
  first Blue Dragon runtime failure, and fixing real donor-backend integration
  bugs instead of polishing the removed mini-JIT scaffold.
- It is not a compatibility result.

## Local Notes

Useful repo docs for this fork:

- [Android ARM64 Thor Max Port Plan](docs/porting/android-arm64-thor-plan.md)
- [Android Thor Debug Automation](docs/porting/android-thor-debug-automation.md)
- [Thor Smart Debug System](docs/porting/thor-smart-debug-system.md)
- [Thor Fast Dev Loop Options](docs/research/20260517-195019-thor-fast-dev-loop-options.md)
- [ARM64 Port Tooling and Skills](docs/research/20260517-195621-arm64-port-tooling-skills.md)
- [Faster ARM64 Port OODA Loop](docs/research/20260517-210216-faster-arm64-port-ooda-loop.md)
- [Xenia ARM64 / Android Fork Audit](docs/research/20260517-183520-xenia-arm64-fork-audit.md)
- [Blue Dragon ARM64 Mini-JIT Vector Wall](docs/research/20260518-111343-blue-dragon-arm64-mini-jit-vector-wall.md)
- [Blue Dragon ARM64 Vec128 Fallback-Zero Capture](docs/research/20260518-115238-blue-dragon-arm64-vec128-mini-jit-fallback-zero.md)
- [Blue Dragon KTHREAD Timer And GPU Watchdog](docs/research/20260518-133100-blue-dragon-kthread-gpu-watchdog.md)
- [ARM64 JIT Gap Device Checkpoint](docs/research/20260518-134832-arm64-jit-gap-device-checkpoint.md)
- [aX360e A64 Hard Swap](docs/research/20260518-164150-ax360e-a64-hard-swap.md)

Worklogs live in `docs/worklogs/` and research notes live in `docs/research/`.

## Legal And Content Boundary

This fork is for emulator research and legally owned test content only. Do not
use it for piracy, bypassing access controls, redistributing game content, or
posting copyrighted assets.

Again: experimental fork, no support, no promises. Just agentic AI, Android
ARM64 research, and vibes.
