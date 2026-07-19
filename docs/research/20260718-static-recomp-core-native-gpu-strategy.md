# Static-recomp core + native GPU port — strategy assessment (2026-07-18)

Research into "rework static recompilation into the core of the emulator, then
port the GPU like ReXGlue." Latest state of the Xbox 360 static-recomp scene
(ReXGlue, XenonRecomp), what it validates, and the specific implications for
xenia-thor on the ARM64 AYN Thor.

---

## TL;DR / recommendation

**The plan mirrors ReXGlue's exact, shipping architecture — and it is already
this project's named CPU direction ("AOT-LLVM, RexGlue-style, NO JIT/dispatch at
gameplay").** ReXGlue adopted Xenia's codebase wholesale, replaced the JIT with
ahead-of-time static recompilation of PPC → C++ (compiled by Clang), kept
Xenia's kernel layer as the runtime, and **still uses Xenia's Xenos GPU backend**
— intending to replace it with native rendering later. Blue Dragon already runs
at higher-than-console framerates *even with the emulated GPU*.

**Two hard caveats for the Thor:**
1. **ReXGlue and XenonRecomp are x64-ONLY today** (ReXGlue SDK: "x64 architecture
   only"; XenonRecomp emits x86-intrinsic VMX, ARM64 only via the SIMDe shim).
   Adopting this on ARM64/Android is a **pioneer path** — you would be building
   the ARM64 recomp runtime, not adopting a ready-made one.
2. **The GPU wall persists** until the native port. ReXGlue keeps the Xenos LLE;
   its native-GPU replacement is future work. For this project, *that native GPU
   port is the DXVK-style D3D9→Vulkan HLE already in progress* — so the two halves
   of the plan connect cleanly.

**Verdict:** architecturally correct and well-validated; the CPU half solves the
residency/dispatch/ARM64-codegen pain the JIT fights; the ARM64 runtime is real
new work; the GPU half is the project's existing Vulkan-HLE track. Net: this is a
coherent consolidation of the project's own committed direction, with a concrete
external reference to copy.

## What ReXGlue is (the reference architecture)

- **Static AOT, not JIT.** "Every PowerPC instruction in the original binary is
  converted to native C++ ahead of time through our codegen pipeline and compiled
  with Clang." Three phases: analysis (map functions/data/control-flow from the
  XEX) → codegen (PPC → C++ with register state, condition flags, exception
  handling) → compile (CMake + Clang 18+, C++23, linked against the runtime).
- **Dispatch:** "the function dispatch table is populated once at startup with
  native function pointers. When a thread needs to execute a guest function, it
  looks up the address, gets a C++ function pointer, and calls it directly. No
  translation, no cache, no interpretation." Standard debuggers/profilers work on
  the recompiled code.
- **Runtime = adapted Xenia kernel layer:** memory layout, kernel objects,
  threading, filesystem, input — "real systems that need to exist regardless of
  whether the CPU code is JIT-compiled or statically recompiled."
- **GPU = Xenia's Xenos backend, temporarily.** "The GPU backend is a rendering
  service that the recompiled code talks to… one we fully intend to replace with
  native rendering over time." Abstract interface injected at build time.
  Platform backends: D3D12 (Windows), Vulkan (Linux).
- **Not automated / per-game.** "An automated 'Recompile All My 360 Games'
  solution is not on the horizon." Each title is a separate project (function-
  boundary analysis, jump tables, SMC, quirks). Blue Dragon is the pilot.
- **Ports in flight:** Blue Dragon, **Lost Odyssey**, Banjo Kazooie: Nuts &
  Bolts, Ninja Gaiden 2, Halo 3 (beta), Crackdown 2, Viva Piñata. Community:
  Fable2Recomp, GoldenEye, AC6.

## Why it fits this project's goals (the wins)

1. **Inherently solves the residency trap** (see the 2026-07-18 perf-research
   codebase cross-check). Static PPC → C++ → Clang compiles whole functions with
   normal calling conventions; guest registers become C++ locals the optimizer
   keeps resident *across the native call graph*. The JIT's WriteBackCtxRegs /
   ReloadCtxRegs round-trip at every call disappears. XenonRecomp measured this
   in a shipping title (Unleashed Recompiled): ~20 MB smaller binary, frame times
   cut by several ms.
2. **Removes the whole JIT translation/compile/cache/dispatch layer at runtime.**
   The project's own profiling found per-call resolution was a real BD cost
   (resolve-cache = +25% field fps). AOT populates the dispatch table once at
   startup; guest calls are direct native calls.
3. **Sidesteps the ARM64 JIT-codegen saga.** The JIT-LLVM-on-ARM64 effort fought
   SVE-SIGILL traps, the tbl2 AsmPrinter crash, host-stack overflow, and the
   opt=2 inline-cache crash. Emitting C++ and letting Clang's mature ARM64
   backend compile it obviates that entire class of backend bugs.
4. **Reuses the project's two biggest assets:** the ARM64 kernel HLE (done) as
   the ReXGlue "runtime," and the in-progress D3D9→Vulkan HLE as the eventual
   native GPU backend. This project is unusually well-positioned to build the
   ARM64 variant.
5. **Proven faster-than-console even with the emulated GPU** — directly on-point
   for the BD-30fps goal on a CPU/host-bound title.

## The ARM64 problem (the load-bearing caveat for the Thor)

- ReXGlue SDK build requirement: **x64 only**; platforms Windows (x64) / Linux
  (x64). No ARM64, no Android, no macOS.
- XenonRecomp: "limited to generating code compatible with x86 platforms,
  leveraging x86 intrinsics"; ARM64 support is via **SIMDe** (x86-intrinsic →
  NEON shim), i.e. portability over optimality for VMX.
- Community ARM/Android interest exists (Fable2Recomp lists "x86_64, ARM /
  Windows, Linux, Mac, Android?") but is **aspirational, not implemented**.

**Implication:** the *concept* (PPC → portable C++ → Clang) compiles to ARM64
fine for scalar code; the friction is (a) VMX/AltiVec — either accept SIMDe/NEON
via the shim (fast to ship, matches the project's existing NEON-only, no-SVE
constraint) or hand-write NEON (fast to run, big effort), and (b) an **ARM64
recomp runtime** (the guest↔host ABI, the dispatch table, SMC/indirect-branch
escape hatch, the guest 32-bit-pointer base model) that no upstream project
provides. You would be building the first ARM64 360-recomp runtime — leveraging
the project's existing ARM64 kernel HLE and NEON VMX work from the JIT backend
(the VMX NEON helpers, the fault-decodable 32-bit vector mem trick, the
denorm/NaN fixups are all reusable).

## The two halves connect: CPU AOT + the project's Vulkan HLE = the GPU port

ReXGlue's GPU is an **abstract rendering service** the recompiled code talks to,
currently backed by Xenos LLE. The N64 analogue (N64Recomp + RT64) shows the
mature pattern: the recompiled game issues its graphics commands natively to a
modern renderer that translates them to host GPU APIs, plus a patch system to
feed the renderer extra per-object info. **For 360, the native rendering service
= a D3D9/Xenos → Vulkan HLE — which is exactly the resource-keyed, GMEM-resident,
few-pass renderer this project is already building.** So:

- **CPU:** adopt the ReXGlue-style static AOT recomp core (replaces JIT-LLVM).
- **GPU:** plug the project's DXVK-style D3D9→Vulkan HLE into the abstract GPU
  interface, replacing the Xenos LLE (the ~95-pass EDRAM wall) — the committed
  multi-month rewrite, now with a clean architectural seam to attach to.

This is the whole strategy in one line: **static-AOT CPU core + native Vulkan GPU
service, both behind Xenia's existing kernel + resource abstractions.**

## Lost Odyssey specifically

LO is on ReXGlue's own port list, but note: LO's current xenia blocker is a
**game-logic / kernel-HLE stall** (main loop blocked on an unsatisfied per-tick
condition — render/HSIO layers already eliminated as symptoms, per the LO
diagnosis memory), *not* a CPU-translation problem. Static-recompiling LO's CPU
code will **not by itself** fix that stall — the same missing kernel event/
interrupt/asset-gate remains. **But** the recomp model turns the diagnosis from
device-only JIT profiling into *native code under a standard debugger/profiler*
("standard debuggers and profiling tools analyze recompiled code directly"),
which is exactly the tool LO's deep game-logic stall has been missing. So the
real LO win from this direction is **diagnostic leverage**, not an automatic fix.

## Recommended build path (if pursuing)

1. **De-risk ARM64 codegen first (cheap, decisive):** take XenonRecomp's C++
   output for one small already-working title, compile it with Clang for
   aarch64-android (SIMDe for VMX), link against a minimal shim, and confirm it
   runs a few functions correctly on the Thor. This proves the ARM64 path before
   committing to the runtime.
2. **Reuse, don't rebuild:** the runtime = the project's existing ARM64 kernel
   HLE; the VMX = the JIT backend's NEON helpers; the GPU service = the Vulkan
   HLE. The genuinely new piece is the AOT dispatch table + guest↔host ABI + SMC
   escape hatch.
3. **Keep a JIT/interpreter fallback** for SMC and unanalyzable indirect targets
   (XenonRecomp/ReXGlue handle these with analysis + manual patches; a hybrid
   AOT+small-JIT is the pragmatic 360 answer).
4. **Attach the Vulkan HLE to the abstract GPU seam** as the native rendering
   service — this is where the Thor's TBDR wins (GMEM-resident RTs, pass fusion)
   actually land, per the 2026-07-18 GPU findings.

**Open decision for the user:** ReXGlue is explicitly *per-game ports*, not a
general emulator. Adopting its model shifts xenia-thor from "one emulator runs
any title" toward "a toolkit that produces fast native builds of specific
titles." That is the source of the speed (whole-program native compile), but it
is a real philosophical change in what the project is. Worth deciding
deliberately before committing the multi-month effort.

## Sources
- ReXGlue interview (architecture, AOT dispatch, GPU-as-service):
  https://readonlymemo.com/rexglue-xbox-360-recompilation-interview/
- ReXGlue SDK docs (x64-only, Clang 18+/C++23, D3D12/Vulkan backends):
  https://rexglue-rexglue-sdk.mintlify.app/introduction and
  https://github.com/rexglue/rexglue-sdk
- Xbox 360 recomp progress (game list incl. Lost Odyssey, faster-than-console):
  https://thepixelspulse.com/posts/xbox-360-recompilation-progress/
- ReXGlue overview: https://abit.ee/en/games/rexglue-xbox-360-static-recompilation-emulation-pc-port-powerpc-c-en
- XenonRecomp (x86-target, SIMDe for ARM64): https://github.com/hedge-dev/XenonRecomp
- N64Recomp + RT64 native-GPU pattern: https://github.com/N64Recomp/N64Recomp
