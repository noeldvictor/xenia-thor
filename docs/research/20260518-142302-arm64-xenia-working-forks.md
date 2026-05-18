# ARM64 Xenia Forks That Appear To Work

- Date: 2026-05-18 14:23 EDT
- Accessed: 2026-05-18
- Question: is there any ARM64 Xenia fork that actually works, and what should
  we copy/study for the Thor Android port?

## Short Answer

Yes, but not in the "drop it in and Blue Dragon reaches title" sense.

- aX360e is a public Android app and is the strongest evidence that an Android
  ARM64 Xenia-derived path can boot/run at least some content on modern devices.
- XeniOS/xenia-mac are strong evidence that ARM64 Xenia-derived work can boot
  games on Apple hardware, but their platform work is Metal/iOS/macOS-focused,
  not Android/Vulkan-first.
- Xenia Edge is the most useful upstream-ish technical branch to study because
  public release notes and XeniOS docs point to it as carrying Wunkolo-derived
  A64 backend work and better Linux/platform support.
- None of these should be treated as a clean, proven, compatibility-grade
  Android ARM64 backend we can blindly copy. They are evidence and source
  material, not magic.

## Sources Checked

- Xenia official quickstart:
  `https://github.com/xenia-project/xenia/wiki/Quickstart`
  - Official requirements still describe x64 Windows and x86 AVX/AVX2.
  - Minimum section says Linux/macOS are not natively supported and Linux runs
    via Wine/Vulkan.
- Google Play aX360e Free:
  `https://play.google.com/store/apps/details?id=aenu.ax360e.free&hl=en_US`
  - Describes aX360e as Android Xbox 360 emulation with support limited to a
    selection of games.
  - Says it is based on Xenia's `arm64-backend` branch and xenia-canary code.
  - Lists minimum hardware as Android 9, Vulkan, and ARM64.
- Google Play paid aX360e:
  `https://play.google.com/store/apps/details?id=aenu.ax360e&hl=en_US`
  - Same technical claims as the free listing.
- aenu aX360e site:
  `https://aenu.cc/ax360e/`
  - Describes aX360e as an Android-based Xbox 360 emulator ported from Xenia.
  - Links the GitHub repository.
- aenu1/ax360e GitHub:
  `https://github.com/aenu1/ax360e`
  - Public experimental Android XBOX360 emulator based on Xenia/Xenia Canary.
  - Repository contains Android app code and `app/src/main/cpp/xenia`.
- Android Authority aX360e article:
  `https://www.androidauthority.com/ax360e-xbox-360-emulator-3625517/`
  - Reports aX360e as a Play Store beta, based on Xenia, using an incomplete
    ARM64 backend, with limited compatibility.
- XeniOS site:
  `https://xenios.jp/`
  - Describes Xbox 360 emulation on iPhone, iPad, and Mac.
  - States alpha status, crashes/missing features/game variance.
  - Shows some compatibility entries such as playable/in-game/load states.
  - Says XeniOS is based on Xenia Edge.
- XeniOS FAQ:
  `https://xenios.jp/faq`
  - Describes XeniOS as an experimental Apple-focused Xenia fork based on Xenia
    Edge, with platform changes that may benefit ARM64 Windows/Linux/Android.
  - Says JIT is required for games on iPhone/iPad.
- has207/xenia-edge release note mirror:
  `https://newreleases.io/project/github/has207/xenia-edge/release/0980958`
  - Notes `[A64] Merge Wunkolo's ARM64 Backend and additional fixes for MacOS`
    in xenia-edge.

## Evaluation

### aX360e

This is the closest thing to "working on Android." It exists on Google Play,
claims ARM64/Vulkan support, has public source, and has enough user-visible
surface to suggest some games can run. It is still early, compatibility-limited,
and not proof that Blue Dragon will work on Thor.

What to study first:

- Android app shell and lifecycle.
- SAF/document access.
- Android HID/input.
- Audio backend.
- Adreno/Vulkan options and `libadrenotools`.
- Its Xenia integration shape under `app/src/main/cpp/xenia`.
- Any A64 code cache / backend files, but only with license attribution.

### Xenia Edge / Wunkolo-Derived A64 Backend

This is likely the best backend reference. It appears to carry Wunkolo's A64
backend work plus additional platform fixes. It should be treated as a
technical source to diff and port carefully, not as an app-level Android answer.

What to study first:

- AArch64 emitter structure.
- Host-to-guest and guest-to-host thunks.
- Function resolve thunks / indirect calls.
- Code cache and executable-memory handling.
- Register allocation and ABI.
- Any Linux/POSIX signal or unwind handling.

### XeniOS / xenia-mac

This is evidence that ARM64 Xenia-derived execution can reach game states on
Apple hardware. It is not directly Android because the graphics/platform layer
is Apple/Metal/JIT-entitlement focused. Still useful for CPU backend and
platform packaging ideas.

What to study first:

- How they keep ARM64 JIT enabled and mapped to app lifecycle.
- Any changes around A64 backend stability.
- Compatibility tracking and crash reporting.
- Avoid copying Metal-specific graphics work into Android/Vulkan unless it
  reveals a general GPU contract bug.

## Recommendation For xenia-thor

Use a hybrid path:

1. Keep our helper mini-JIT as the bring-up scaffold and fallback. It gives us
   logs, device launches, and a correctness oracle while we replace pieces.
2. Start reshaping toward the real AArch64 emitter backbone now. The audit
   already shows the missing x64-grade backend services.
3. Prioritize correctness coverage first, but with enough native hot-path work
   that Blue Dragon does not trip watchdogs only because the CPU backend is
   crawling.
4. Study/import from aX360e, Xenia Edge, and XeniOS only where license and
   provenance are clean. Preserve attribution. Do not import random Android
   APK code, obfuscated code, or unverifiable snippets.

## Answer To The User's Core Question

There are ARM64 Xenia forks/apps that appear to "actually work" in the sense
that some commercial games reach load/in-game/playable states on Android or
Apple ARM64 hardware. There is not a known clean drop-in fork that turns our
Thor branch into a working Blue Dragon Android port immediately.

The fastest serious move is to use these projects as references for a batched
backend conversion: thunks, code cache, A64 emitter, differential tests, then
Thor milestone runs.
