# Cemu Android Comparison Lane

Date: 2026-05-25 16:27 EDT

## Sources Checked

- Mainline Cemu repository: https://github.com/cemu-project/Cemu
- SSimco Android Cemu fork: https://github.com/SSimco/Cemu
- SapphireRhodonite Android Cemu fork: https://github.com/SapphireRhodonite/Cemu

## What Changed

Updated `AGENTS.md` with a Cemu / Wii U Android comparison lane for
xenia-thor. The intent is to learn aggressively from public emulator Android
ports while keeping the work clean-room and license-aware.

Mainline Cemu is MPL-2.0 and the Android forks inherit that licensing context,
so Xenia should not copy Cemu source into this BSD-style repo without an
explicit file-level license plan. Use the public work as design pressure and as
a checklist source for architecture, profiling, Android packaging, input, and
settings UX.

## Engineering Takeaways

- Wii U and Xbox 360 similarities make Cemu Android a useful comparison:
  PowerPC-family CPU work, ATI/AMD GPU lineage, unified-memory-ish thinking,
  shader/pipeline cache needs, controller UX, and handheld Android lifecycle
  pressure.
- The hardware is still different enough that every borrowed idea must be
  re-proven on Thor. Xbox 360 has Xenon SMT behavior, VMX128, Xenos EDRAM and
  resolve behavior, and different kernel/HLE and synchronization pressure.
- The current Blue Dragon wall remains CPU/JIT. Recent Thor evidence still has
  Main Thread at or near one full core while GPU Commands are far below that, so
  Cemu Android should not trigger a broad Vulkan pivot yet.
- Useful comparison targets before new A64/JIT edits: guest-state register
  caches, block linking, fast dispatch, code-cache invalidation, host ABI
  choices, SIMD/FP lowering, shader/pipeline cache behavior, per-game hacks,
  and debug counters.
- Useful Android UX targets: a launcher-visible settings surface, per-game
  profiles, controller mapping, profile import/export, safe defaults, expert
  research-cvar page, reliable Thor built-in controls, external USB/Bluetooth
  gamepads, dead zones, trigger handling, rumble, and an input test screen.

## Open Questions For User

- Should the first UX slice be a minimal AYN Thor built-in controller fix, a
  Cemu-style settings/profile page, or both together?
- Should settings be scoped per-game first for Blue Dragon, or global first with
  later per-game overrides?
- Should external USB/Bluetooth controllers wait until built-in Thor controls
  are proven, or land in the same pass?
- Should experimental speed cvars be hidden behind an expert page, or visible
  as per-game toggles with warnings?

## Next

For the Blue Dragon speed lane, continue with the already-planned FullDeploy
and route-clean residual register-cache audit capture before generated behavior
changes. For the Android UX lane, start with the user's answer on controller
versus settings priority and then audit `EmulatorActivity` input paths and the
current launcher/settings plumbing.
