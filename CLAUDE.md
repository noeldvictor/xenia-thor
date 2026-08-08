# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## 🧭 WHAT ARE YOU ABOUT TO DO? — read the matching section BEFORE starting
This file is long because most of it is things that were tried and DIED WITH DATA. The index exists so you read
the two sections that apply instead of skimming all of it. **Every line below cost device time or a bad commit.**

| about to... | read first | why |
|---|---|---|
| **benchmark anything** | `NEVER BENCHMARK A MOVIE`, `Measurement is the #1 trap` | attract mode invalidated a whole day; drift here is ~2.8%, larger than most wins |
| **trust a measurement** | `CHECK THE PERSISTED DEVICE CONFIG`, `A BARE am start DOES NOT TEST WHAT SHIPS` | a stale `xenia.config.toml` cost 2.88%; headless launches run WITHOUT LLVM/AOT |
| **A/B a cvar** | `A DEFAULT-OFF PATH IS NOT A CONTROL` | the off-branch may be untested code; ours crashed in 1s and would have inverted the conclusion |
| **pick a CPU lever** | `ARM64 PERFORMANCE PLAYBOOK` (the five rules), `docs/reference/arm/` | the port model was wrong until checked against the manual; measure applicability before building |
| **claim an instruction is too slow/strong** | `docs/reference/arm/` + diff the emitted asm | one `clang -S` killed an implemented `__sync_*` optimisation that was a pure no-op |
| **touch GPU / EDRAM / render passes** | `THE BD EDRAM / D3D9-HLE ERA IS ARCHIVED`, `BD FIELD IS CPU-BOUND` | that entire era optimised the wrong processor; the archive lists what is already dead |
| **port from XenDroid** | `XENDROID IS THE BAR`, `XENDROID UPSTREAM PORT TRACK`, `APU/BASE SWEEP TRIAGE` | their tree is heavily diverged - port the idea, not the patch, and expect genuine N/A results |
| **fire the device** | `Never thrash the Thor`, `BLACK SCREEN? CHECK THE DISPLAY`, `TURNIP IS MANDATORY` | thermal limits are real; a sleeping panel looks exactly like a rendering bug; a bare launch uses the WRONG driver |
| **chase a crash** | `BURNOUT ... LLVM WRITING x20`, `GEARS SIGTRAP`, `Android fault diagnosis` (memory) | decode the instruction before blaming a subsystem; x20/x21 state is the discriminator |
| **hunt x86-shaped bugs** | `THE x86→ARM64 SWEEP: MEMORY ORDERING IS THE BUG CLASS` | TSO hides missing fences; look for an atomic index guarding a plain buffer, and check the POSIX `#elif` nobody profiles |
| **work on POWER / HEAT / watts** | `MANUAL REVIEW #4` (little-core pin), `#6` (FPCR barriers), `ONE GLOBAL CONDVAR`, `HOW TO ACTUALLY MEASURE WATTS` | the main guest thread was HARD-PINNED to a 2.0GHz A510 while the X3 idled; every guest signal woke every parked thread; watts are unmeasurable over USB |
| **conclude a spin loop is burning watts** | `ONE GLOBAL CONDVAR` (the swept-and-clean list) | swept 2026-08-07: the `MaybeYield` sites in `xobject.cc`/`guest_scheduler.cc`/`NtYieldExecution` are all CLEAN - one-shot yields or deliberate parks, not spins. Do not re-derive |
| **touch the register allocator** | `MANUAL REVIEW #1`, `#2`, `#3`, `#5`, `THE MEASUREMENT ... ALREADY EXISTED` | 32 guest GPRs into 7, 128 guest vectors into 28; Arm says spill GPRs to VECTOR regs, we spill to memory; and the census you want already exists |
| **touch FP / VMX / FPCR** | `MANUAL REVIEW #6`, `THE THREE NEW a64 LEVERS ARE NOT INDEPENDENT` | every FPCR mode switch is a PIPELINE BARRIER (Table 4-3 note 2) - the Xenon has two FP mode registers, ARM64 has one |
| **enable any new a64 lever** | `THE THREE NEW a64 LEVERS ARE NOT INDEPENDENT` | `a64_fpcr_single_mode` SILENTLY disables `a64_vmx_fp_no_operand_copy`; measure one at a time |
| **read a CPU manual** | `docs/reference/arm/README.md`, and §4 "Special considerations" of the SWOGs FIRST | §4 is where the actionable advice is - the instruction tables only price things you already suspect |
| **edit this file with a script** | `Config + git rules` | I truncated it to 0 bytes and pushed it once - encode before opening, gate `git add` on size |

## Goal
Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740). **Blue Dragon → 30fps @ 720p
full foliage; Burnout/Gears/Lost Odyssey/Banjo → 30-60.** Ship every win as a cvar-gated, per-game
`GameProfiles` / `XeniaOptimizations` toggle (default-off until validated).

## ⚙️⚙️⚙️ STANDING DIRECTIVE (user 2026-07-31): AOT + LLVM IS THE DEFAULT — every game, from the GUI, no flags
**The standard configuration for ALL games launched from the app GUI: full AOT precompile BEFORE the game starts
(RPCS3-style full-screen compile progress in EmulatorActivity, driven by the native precompiler's log lines) +
the LLVM whole-function recompiler as the standard CPU backend.** Made safe 2026-07-31: `cpu_llvm_no_runtime_
compiles` (default TRUE, llvm_assembler.cc Assemble gate on `Processor::is_aot_runtime_phase()`) confines libLLVM
to the load window — any function first discovered during gameplay compiles on the a64 fallback, so the known
intermittent libLLVM codegen crash (killed Burnout mid-race, 2026-07-31) structurally cannot fire in gameplay.
GUI wiring: `opt_aot_precompile` (cpu_aot_maximize) + `opt_llvm_backend` (cpu_backend_llvm) + `opt_xendroid_parity`
(the 5 XenDroid-shipping GPU levers) are ALL defaultEnabled=true in XeniaOptimizations; GameProfiles adds
per-title fixes on top. a64 compile speed is real (~15k fns/1.2s device-measured, Burnout) — LLVM is the slow/
quality compile, a64 the fast/stable one; both AOT at load. VRS removed from Burnout (visible quality loss).
Game profiles ship IN the APK — updating them = rebuild + reinstall.

## 🧭🧭🧭 STANDING DIRECTIVE (user 2026-07-31): XENDROID/XENIA-EDGE IS THE COMPAT REFERENCE — USE IT TO FIX OUR SHIT
**XenDroid (github.com/rfandango/XenDroid, Kotlin shell + vendored xenia-edge, Canary lineage) runs games on the
SAME AYN Thor that our fork breaks (Lost Odyssey boots there, stalls here; user: "xendroid works so well but our
shit sucks/we crash games"). Our fork is upstream-MASTER-based (leaner, enabled the perf work: BD native renderer,
LLVM AOT, custom a64) — the cost is Canary's years of kernel/XAM compat fixes that master never had. THE RULE:
when a title misbehaves in xenia-thor but works in XenDroid, DIFF THE EDGE KERNEL AND PORT THE DIVERGENCE —
don't debug from scratch.**
- **Reference sources in-tree:** git remote `edge` (active branch `edge/edge`, NOT edge/master) — use
  `git show edge/edge:src/xenia/...`; clones at `reference/XenDroid` + `reference/xenia-edge`. Compat lives in
  `src/xenia/kernel/{xam,xboxkrnl}` + `src/xenia/vfs` (Edge's is Canary-derived and heavily diverged from ours).
- **Proven workflow (LO session 2026-07-31):** reproduce on DESKTOP xenia.exe (pull the ISO from the device if
  needed — `scratch/lost-odyssey/lo_disc1.iso` stays local, never commit) → in-tree wait/event tracing
  (`--xboxkrnl_thread_wait_trace=true --xboxkrnl_thread_wait_trace_budget=2000000` +
  `--xboxkrnl_event_trace=true`, analyzer = begin-without-end per thid) → cdb thread-stack dumps
  (`cdb -p <pid> -c ".lines; ~*k 14; qd"`) → Explore-agent diff vs Edge → port → desktop verify → APK.
- **Already ported from the LO audit (f2ea321a6):** trimmed-path NtCreateFile/NtOpenFile (probe-hits-open-misses
  class), XamGetOverlappedResult honors bWait (was infinite non-alertable park), overlapped event Reset on arm +
  25ms dispatch (was 100), fiber-thread UnlockApc null guard, scheduler-aware XIOCompletion.
  **LO STILL STALLS after these** (main thread polls a guest flag in KeDelayExecutionThread wrapper lr 827CACFC;
  workers idle; official canary A/B on desktop = the discriminator in progress). Earlier RE trail:
  docs/research/20260530-*lost-odyssey* (zombie-join hypothesis, later refined).
- **✅ THE EDGE KERNEL PORT IS LANDED AND DEVICE-VALIDATED (merged to master 2026-08-04, f3e780d2e).**
  Burnout Revenge: title + attract mode at **59.3 fps**. Blue Dragon: title at **29.6 fps**. Both on Turnip
  26.3.0, screenshotted, no faults. Bring-up took 5 defects after the merge compiled clean — see the EXECUTION LOG
  in `docs/research/20260731-edge-kernel-port-plan.md` (the two that generalize: desktop oracles cannot validate an
  Android memory/threading port, and a wholesale subsystem merge must also take the CALLER-side changes from Edge).
  Still pending: Phase 4 IRQL unification, and the deliberate omissions listed in that doc.
  **⚠️ INTERMITTENT STARTUP STALL, seen ONCE (2026-08-04), cause unknown:** GPU init completed and the presenter
  attached, then the Emulator thread futex-waited forever with the UI thread idle in epoll, no guest threads ever
  created, 0 swaps, 0.0 fps. It did NOT reproduce - the same command line (`--es hid nop` + a button sequence) ran
  to the title at 59.1 fps on the retry, so `hid nop` is NOT implicated despite the first read. Treat as a rare
  race in the emulator-thread/UI-thread handoff; if it recurs, capture the Emulator thread's stack before killing
  the app.
- **DECIDED 2026-07-31 (user: "xendroid is still more stable and reliable"): PORT EDGE'S WHOLE KERNEL LAYER**
  (xam+xboxkrnl+vfs as a unit) onto our tree, keeping our CPU/GPU. Two titles in one day reduced to ~10
  canary-lineage kernel divergences — whack-a-mole lost. Scoping report drives the multi-session migration;
  our kernel-layer customizations to re-apply afterward: trainers, guest scheduler fiber hooks, XamSwapDisc/
  disc_playlist, user_gamertag, ntreadfile_force_complete, HLE hooks, content manager. Edge stability becomes
  the floor; our CPU/GPU perf stays on top.

## 🗑️ DECISION: DELETE THE BD NATIVE RENDERER (user, 2026-08-07: "that fucking project failed")
**🚧 BD REMOVAL IN PROGRESS - 520 LINES OUT, BUILD GREEN AT EVERY STEP (2026-08-07).**
| step | result |
|---|---|
| 1. render-target-cache BD block | 34 lines - done |
| 2. the 14 `gpu_bd_native_renderer`-gated blocks in vulkan_command_processor.cc | **431 lines** (13,690 -> 13,259) - done |
| 3. six more gated blocks in vulkan_command_processor.cc | **55 lines** (13,259 -> 13,204) - done |
| 4a. the three now-CONSTANT entry points + their callers + orphans | **~4.0 KB** across 4 files - done |
| 4b. the dead L5 present path in `IssueSwap` | **134 lines** - done, build-green |
| 4c. last cmd-processor block + **`bd_native_renderer.{cc,h}` DELETED (1,511 lines)** | done, build-green |
| 4d. the 15 `gpu_bd_` cvars with no reader left | **154 lines** - done, build-green |
| 4e. the L4/L6 colour-lifetime cluster in the RTC | **STOPPED - needs the device**, see below |
**Running total: ~2,511 lines.** `gpu_bd_native_renderer` - the master gate this whole removal was justified
against - no longer exists, because after 4c nothing read it. The 15 orphans were found by COUNTING `cvars::`
readers per cvar across every .cc/.h, not by eye; 33 of the 48 are still live and were left alone. Checked the
`--ez` allowlist first: none of the 15 appear in `EmulatorActivity` or anywhere under `android/`.
**🛑 4e IS DELIBERATELY NOT DONE, AND THE REASON MATTERS.** 4b/4c were self-contained dead BLOCKS with clean
boundaries; the L4/L6 gates are **19 sites interleaved with live rendering code** inside `CreateRenderTarget`,
the transfer paths and the resolve paths - in `vulkan_render_target_cache.cc`, the file where our fork diverges
most from upstream. Build-green-but-unvalidated was a defensible trade for a self-contained block; for an
interleaved cut in the GPU path, where the failure mode is WRONG PIXELS, it is not. **Do 4e with the device
free**, in sub-slices, screenshotting each.
**🔎 A FOLLOW-ON NOBODY HAS CHASED: SEVERAL STILL-"LIVE" `gpu_bd_` CVARS ARE NOW INERT.** Their own descriptions
say they *"need gpu_bd_native_renderer"* - which no longer exists - so they still have `cvars::` readers but
their effect path is gone. That makes them the next removal candidates, and it means "has a reader" is NOT
sufficient evidence a lever does anything. Candidates visible from the help text alone: `gpu_bd_whole_frame`,
`gpu_bd_native_drop_transfers`, and the aux-RT-dependent transfer levers. **Trace each before deleting** - the
15 above were safe because they had zero readers, which is a much stronger fact than "looks inert".
**🎉 `bd_native_renderer.{cc,h}` ARE GONE (2026-08-07).** `BdNativeRenderer` no longer exists outside two cvar
DESCRIPTION STRINGS in command_processor.cc; `NativeSurface` references in `vulkan_command_processor.cc` are
**zero**; the member, the forward declaration and the `#include` are removed.
**🔁 DELETING SOURCES REQUIRES THE PREMAKE REGEN, same as changing flags.** `src/xenia/gpu/vulkan/premake5.lua`
uses `local_platform_files()`, which GLOBS the directory - so there was no premake entry to edit - but the
GENERATED `build/xenia-gpu-vulkan.prj.Android.mk` names each file explicitly and still listed the deleted ones.
`./tools/build/bin/premake5.exe --file=premake5.lua --os=android androidndk` (the `--os=android` is required)
drops them. Without it the build fails on missing sources, which at least fails loudly - unlike the flag case,
where a stale .mk silently builds the old flags.
**✅ DEVICE-VALIDATED 2026-08-07 (4a + the two a64 NaN fixes, one APK).** Gears of War, headless with
`--es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true`:
`Turnip Adreno (TM) 740` (correct driver, NOT the Qualcomm blob) - `Title name: Gears of War` reached -
**0 faults / SIGTRAP / Scudo errors** - **28,776 `LLVMobjload`** (object cache fully warm). Peak 72C in ~30s,
force-stopped at the thermal limit. **⚠️ That validates BOOT AND RUN, not PIXELS** - 4a is a GPU change whose
failure mode is wrong output, not a crash, so a screenshot check is still owed.
**Slice 4a:** `BdNativeSurfaceServes` (always `false`), `BdNativeDepthResolveImage`
(always `VK_NULL_HANDLE`, and it had **zero callers**) and `LogBdNativeSurfaceKeys` (empty body) were deleted
along with their 2 live call sites, which simplified two conditions in the RTC (a whole drop-resolve `if`, and
`native_served` out of a 4-way OR). That orphaned `AddBdNativeResolveDropped`, `bd_native_resolves_dropped_` and
the `gpu_bd_native_drop_resolves` cvar, all removed too. **Note the ordering that made this safe: one of the two
log call sites WAS the sole body of a `gpu_bd_depth_xfer_census` `if`** - exactly the entry-guard shape that
caused the null derefs above - so the `if` went with it rather than being left with an empty body.
**An audit worth repeating before 4b:** a script that walks every `VulkanCommandProcessor::` definition and
checks each body for an unguarded `bd_native_renderer_->` now reports **exactly one function (`IssueSwap`), and
it is guarded**. That is the whole-file version of the check that a 60-line regex window got wrong.
**4b is safe by the same construction as 1-3, and it is CONFIRMED not merely assumed:** `gpu_bd_native_color_
lifetime_hle` is `DEFINE_int32(..., 0)` AND reads `0` in the live device config, so every L4/L5/L6 path
(`bd_native_color_*`, `bd_native_depth_*`, `BdL5PublishAlias`/`BdL5LookupAlias`/`BdL5DropSafe`) is unreachable.
The remaining `gpu_bd_*` cvars that read non-false in the config (`aux_fmt37`, `tex_bind`, `field_convert`,
`native_rt_width`, ...) are all SUB-options of master gates that are themselves off - do not mistake them for
live paths.
**Why this is safe by construction, not by argument:** `gpu_bd_native_renderer` is `DEFINE_bool(..., false)`
(command_processor.cc:82) and reads false in the live device config, so the entire path was ALREADY dead at
runtime. Deleting dead code cannot change behaviour - the opposite of the usual GPU risk.
**🔑 TWO THINGS THE REMOVAL REVEALED, both worth knowing before continuing:**
1. **`bd_native_renderer_` is now NEVER CONSTRUCTED** - its construction lived inside one of the 14 deleted
   blocks. So the member is permanently null and the remaining 22 uses are all dead. Same for
   **`bd_native_gate`**, which is declared `= false` and appears at only 3 lines (init + 2 reads): the code that
   set it true was also deleted, so `if (bd_native_gate)` at ~4831 is unreachable.
2. **CHECK FOR NULL-DEREF AFTER EACH CUT, NOT AT THE END.** Line ~4833 dereferences `bd_native_renderer_`
   with no local null check - it is safe ONLY because `bd_native_gate` is now always false. A heuristic scan
   flagged 5 apparently-unguarded derefs; all 5 turned out guarded (two by a block guard 48 lines up, one by a
   guard phrased `bd_native_renderer_)` rather than `&&`, two by the dead gate). **A short-window regex is not
   sufficient here - read the enclosing block.**
   **⚠️⚠️ AND THE "ALL 5 WERE FINE" RESULT MADE ME COMPLACENT - STEP 3 THEN CREATED TWO REAL NULL DEREFS.**
   Deleting the gated blocks also deleted the EARLY-RETURN GUARDS at the top of two whole functions, leaving
   their bodies dereferencing a permanently-null pointer:
   ```
   VkImage ...::BdNativeDepthResolveImage(uint32_t dest_base) {
     return bd_native_renderer_->LookupDepthResolveImage(dest_base);   // guard deleted
   }
   bool ...::BdNativeSurfaceServes(uint32_t dest_base) {
     NativeSurface* surface = bd_native_renderer_->FindSurface(dest_base);   // guard deleted
   ```
   Both had `if (!cvars::gpu_bd_native_* || !bd_native_renderer_) return ...;` as their FIRST statement, which
   matches the step-2 filter exactly ("every `cvars::` in the condition starts with `gpu_bd_`") - so the
   mechanical filter is CORRECT for a gated block and WRONG for a function's entry guard. **`BdNativeSurfaceServes`
   is still called from `vulkan_render_target_cache.cc:2445` and `:9842`, so this was a live crash, not dead code.**
   Fixed by making both return the not-serving value (`VK_NULL_HANDLE` / `false`) instead of a guard.
   **🔑 THREE THINGS TO CARRY INTO STEP 4:**
   - **The filter needs one more condition: never delete a block whose `return`/`{` is the function's own entry
     guard.** Check what the block PROTECTS, not just what gates it.
   - **The build proves nothing here.** All of this compiled clean; a null deref is a runtime fault.
   - **I let `git commit` run before reading the checker's output** - the same newline-instead-of-`&&` mistake
     the git rules already record from the CLAUDE.md truncation, and the commit message asserted "no unguarded
     dereferences remain" while the output above it said `[2931, 9510, 9514]`. **Read the tool output before
     writing the claim, not after.** (2931 was genuinely fine - guarded 89 lines up, outside a 60-line window.)
**The filter that makes step 2 mechanical:** delete a block only when EVERY `cvars::` in its condition starts
with `gpu_bd_`. Note this also took a compound gated on `gpu_bd_field_decouple || gpu_bd_native_renderer` -
in scope (it is BD code) but not master-gated, so it is called out rather than silently included.

**Agreed and consistent with the evidence** - the archive above records the whole EDRAM/HLE era as superseded by
the measurement that BD's field is CPU-bound. The renderer is correct but is not the fps lever, and it is now
pure carrying cost.
**MEASURED SCOPE (2026-08-07) - this is a session of its own, NOT a delete:**
```
bd_native_renderer.cc/.h        1,511 lines
references across the tree        888 in 16 files
  of which vulkan_command_processor.cc  278
gpu_bd_* cvars in gpu_flags.cc      4
also touches: processor.cc, command_processor.{cc,h}, d3d12_command_processor.cc,
              vulkan_render_target_cache.{cc,h}, vulkan_command_processor.h
```
**⇒ IT IS WORTH DOING FOR A SECOND REASON:** it shrinks the traditional render-pass surface that currently
blocks the dynamic-rendering prerequisite (~115 API sites, 14 `kColorDrawLayout` uses - several of them BD
custom-resolve passes). **Delete it FIRST, then port dynamic rendering** - the order matters, because removing
888 references from a layer you are also replacing is strictly harder than removing them first.
**SUGGESTED ORDER (each step compiles and is independently revertable):**
1. Flip every `gpu_bd_*` cvar to a hard `false` and rebuild - proves nothing live depends on the path.
2. Delete the 278 call sites in `vulkan_command_processor.cc` (the bulk; they are gated blocks).
3. Delete the render-target-cache and command-processor hooks.
4. Delete `bd_native_renderer.{cc,h}`, the 4 cvars, and the premake entry.
**⚠️ Do NOT start this at the tail of a session.** It is the GPU path, where mistakes are wrong pixels rather
than crashes, and a half-applied delete leaves the tree unbuildable.

## 📦 THE BD EDRAM / D3D9-HLE ERA IS ARCHIVED — `docs/research/20260807-bd-edram-hle-era-archive.md`
**Roughly 230 lines of July 2026 GPU narrative moved out of this file.** It was the largest thing loaded into
every session and it is superseded by the section directly below: BD's field is **CPU-bound**, so the whole
EDRAM-removal / native-Vulkan-HLE / pass-fusion effort was optimising the wrong processor.
**Read the archive before reopening ANY of it** — it is a long list of things that are DEAD WITH DATA
(whole_frame, drop-all-transfers, depth-copy, force-1-tile, tiling-HLE, every early-Z/LRZ reject, the
RT-cache width override) and re-deriving them costs device time we have already spent. What stayed here:
the CPU-bound verdict, `TURNIP IS MANDATORY`, the desktop build recipe and gotchas, and the RenderDoc setup.
**The HLE work itself is not "wrong"** — it renders correctly and deletes real EDRAM passes on PC — it is
simply not the fps lever on this device. If GPU work resumes, the live track is the XenDroid in-pass resolve
chain and its dynamic-rendering prerequisite, not the 2026-07 HLE.

## 🟢🟢🟢 TURNIP IS MANDATORY FOR THE AYN THOR (user 2026-07-06, verbatim: "TURNIP IS MANDATORY FOR AYN THOR ADRENO STUFF")
**The Thor's DEFAULT/system Vulkan driver is the QUALCOMM PROPRIETARY Adreno driver (`vulkan.adreno.so`,
`vendorID=0x5143`, `driverID=8` = VK_DRIVER_ID_QUALCOMM_PROPRIETARY, device="Adreno (TM) 740") — confirmed
on-device 2026-07-06 via `GetVulkanDevice()->properties()`. This is the WRONG driver: BD/Adreno work MUST run on
TURNIP (Mesa's open-source Adreno driver, `driverID=VK_DRIVER_ID_MESA_TURNIP`). Why it matters:**
1. **The Qualcomm proprietary driver CRASHES on the native renderer** (null-deref inside `vulkan.adreno.so`; it
   was misdiagnosed as a CPU/JIT crash until `/proc/PID/maps` located the host pc in the driver .so). Turnip is a
   different driver with different behavior — the whole TBDR optimization strategy (minimize render passes, GMEM
   residency, LRZ, load/store_op) is premised on TURNIP, and RE2-on-GameNative (the 30fps benchmark) runs on
   Turnip (GameNative ships a custom Turnip driver).
2. **`IsAdreno()` fork point:** `BdNativeRenderer::IsAdreno()` (via device properties) branches Adreno-strict /
   super-optimized paths. Use it + check `driverID` to require/prefer Turnip. Desktop Vulkan (NVIDIA/AMD, lenient)
   hides driver-strictness bugs — validate on Turnip.
3. **To get Turnip on the Thor:** load a custom Turnip `libvulkan_freedreno.so` (as GameNative/emulators do), not
   the system driver. Confirm at runtime via the logged `driverID` (must be MESA_TURNIP, not 8).
4. **VULKAN VALIDATION is now bundled** (`app/src/main/jniLibs/arm64-v8a/libVkLayer_khronos_validation.so`,
   allowlisted cvar `vulkan_validation`) — it found the on-device crash as `VUID-VkRenderPassBeginInfo-
   clearValueCount-00902` (native CLEAR pass begun with clearValueCount=0 → driver reads null pClearValues →
   null-deref). ALWAYS run `--vulkan_validation=true` on-device when a driver crash is suspected; strict drivers
   crash where desktop tolerates.

**🎯 GOAL (user 2026-07-05, THE mandate): BD→30fps via a FULL D3D9→VULKAN HLE — the DXVK-for-360 native renderer,
NOT levers (all dead).** RE2 (heavier) hits 30fps on this Thor via DXVK ⇒ HW is fine, xenia's 95-pass EDRAM LLE
emulation is the wall. BUILD a separate native Vulkan renderer (seam 0x82489F40 → own full-surface RT, few
passes, native vertex-input/shaders/ROP-blend, early-Z reject) that BYPASSES the PM4/EDRAM/tiling back-end.
Develop STRUCTURE on desktop `--gpu=vulkan`; **SUPER-OPTIMIZE for the Thor's Turnip/Adreno TBDR** (Thor Vulkan ≠
desktop Vulkan — minimize passes/GMEM flushes, GMEM-resident RT, Turnip extensions). Also AOT-LLVM (RexGlue-style
PPC→native) for CPU/thermal. Blueprint: `docs/research/20260705-native-vulkan-renderer-plan.md`. Be creative,
novel, research (arxiv/DXVK/Cemu). Convert the WHOLE pipeline at once — do NOT do one lever at a time (all dead).

## 🚨🚨🚨 DECISIVE (2026-07-08, on clean Thor): BD FIELD IS **CPU-BOUND**, NOT GPU-BOUND — the whole GPU/EDRAM era was the wrong processor
**MEASURED on the clean Thor (busycheck.ps1, Turnip, field rendering): GPU busy% = 10-48% (avg ~28%) at the
MINIMUM clock 401MHz (Adreno 740 max ~680MHz). The GPU is IDLE 60-85% of every frame + downclocked = it has
nothing to do = the CPU can't feed it fast enough. THIS IS CPU-BOUND.** Confirmed by every GPU lever being inert
on-device THIS session: drop ALL EDRAM ops (resolves+all color transfers) = 9.9fps (no change); force_no_color_write
(skip ALL pixel shading) = ~15fps (small, cross-run-confounded); gpu_bd_skip_foliage_shadows (skip foliage
shadow-casters) = no change; near-EMPTY scene (fence+1 bush) = SAME 9.9fps as the dense field. A GPU-bound title
would show ~99% busy + boosted clock + scene-dependent fps. BD shows the opposite. => **The EDRAM/HLE/pixel/shadow
work does NOT and CANNOT get BD to 30fps — the bottleneck is the CPU (guest-code emulation + the CP-thread GPU
command translation, ~190 HLE redirects/frame). The path to 30fps is CPU: the LLVM-JIT backend, residency
write-back (#1 CPU lever per memory), XMA idle-skip, global-lock lock-free, cross-block optimizer — the committed
CPU direction. The HLE is CORRECT + shippable (renders the field right, EDRAM ops deletable) but is NOT the fps
lever.** Temp hit 63C at min GPU clock = the HEAT is the CPU cores working = CPU is the hot busy component. ⚠️ Cross-
run fps is scene-confounded (memory rule); the GPU-busy%/clock reading is the RELIABLE signal and it is unambiguous.

## 🔬 EXPERIMENT DB — check before running, record after (anti-repetition RAG)
`python tools/exp_ledger.py check "<keyword>"` BEFORE any device experiment or new lever; `add` after every
result. If DEAD/FLAT, do NOT re-run — build on the note. Skill: **xenia-experiment-ledger**. DB:
`docs/research/experiments.db` (human narrative: `docs/research/experiment-ledger.md`). Exists because we
repeatedly burned device runs re-deriving dead ends (grep-the-markdown kept missing them).

## 🎬🎬🎬 NEVER BENCHMARK A MOVIE (user, verbatim 2026-08-06: "don't benchmark movies")
**A benchmark scene must be REAL GAMEPLAY. Attract modes, demo replays, title screens, menus and FMV are not.**
Worst to best: **FMV/video** (measures XMA decode + a blit — none of the code you changed) → **title screen** →
**menus** → **attract/demo replay** → **ACTUAL GAMEPLAY** ← the only tier that counts.
- **An attract mode is a scripted replay, not the game.** No player input processing, no full vehicle AI/physics
  load, no HUD/UI work, no audio mix under load, and none of the thread contention that only appears when all of
  that runs at once. Usually frame-capped too, which hides CPU headroom outright.
- **This invalidated a whole day (2026-08-06).** Every CPU A/B that day ran on Burnout attract. Ubiquitous changes
  survive it (they fire in any code, and are probably UNDERSTATED since gameplay is more CPU-bound); scene-dependent
  ones are simply unmeasured — a "FLAT" verdict from attract means "flat in attract", nothing more.
- **Am I actually in gameplay?** Temp climbing hard + uncapped fps well below the cap + guest `entry_delta`
  materially above the attract baseline (~122-128M/5s on Burnout). If it looks like attract, it is attract.
- **Get there repeatably BEFORE the A/B:** `--es hid nop --es hid_nop_button_sequence '<seq>'` driven into a started
  race (Burnout) or the field (BD, ~120-135s). Skill: **`xenia-blue-dragon-route-capture`**. A captured route is a
  PREREQUISITE for a CPU measurement, not an optional extra.

## 🧾 XENDROID APU/BASE SWEEP — TRIAGE (2026-08-07). 43 new upstream commits fetched; APU batch triaged.
**Do the sweep, not just the blocked GPU chain.** The in-pass resolve chain is stuck behind a dynamic-rendering
prerequisite we do not have, but the 43 commits fetched 2026-08-07 include a 10-commit APU/base batch that is
completely independent of it — and it is where the CPU/power findings came from.
| upstream | verdict |
|---|---|
| `902af401d` XMA release fence | **✅ PORTED** (`e5398cac8`) — textbook x86-TSO bug, 3 sites, builds clean |
| `92ca0d563` realtime callback off global mutex | **🔴 CONFIRMED PRESENT HERE, not yet fixed** — see the section below; port WITH the condvar refactor |
| `2c0ac5847` wake multi-waiters selectively | **⚠️ NOT PORTABLE AS-IS** — ours is worse (one static condvar); see the thundering-herd section |
| `366f38da8` fill the whole AAudio request | **❌ N/A — already our behaviour.** `FillAudio` loops queued frames + zero-fills on underrun |
| `6d56383fd` cap gap concealment at one block | **❌ N/A — feature absent.** They ship their own `xe_aaudio_audio_driver.cpp` with `ConcealGap`/`last_block_`; ours is `android_audio_driver.cc` and zero-fills, so there is no overread to cap |
| `5ada02239` resolve XMA kicks by sequence | **❌ N/A — mechanism absent.** We have no `SignalWorkDone`, `work_completion_event_` or kick handshake at all (0 hits across xma_context.{h,cc} + xma_decoder.cc); it fixes a race in synchronisation THEY built |
| `bc257ce49` audio worker priority | **✅ PORTED** (`306a4a68a`) — set from inside the thread; XThread overwrites anything set at the creation site |
| `d398a8762` verify the nice took | **✅ PORTED** (`c7205d89d`) — reads back with `getpriority()`; matters here because our `set_priority` already EPERMs on Android |
| `6ec67de7b` / `138cb65f0` / `46fcd881f` | instrumentation/counters, take only if chasing the above |
**BATCH COMPLETE — every APU commit has a verdict: 3 ported, 3 N/A, 1 not-portable-as-is, 1 confirmed-present-but-
needs-the-condvar-refactor, 3 counters left. The N/A results are the ones worth keeping: their APU tree carries a
master/new/old/fake `xma_context` split, its own `xe_aaudio_audio_driver.cpp` with gap concealment, and a kick
handshake — none of which exist here, so several plausible-looking commits fix bugs we cannot have.**
**⚠️ Their APU tree is heavily diverged** (master/new/old `xma_context_*` split, own AAudio driver), so expect
"port the idea, not the patch", and expect genuine N/A results — record them so they are not re-checked.

## 🔊 GEARS AUDIO BUZZING = 8,221 XMA DECODE ABORTS (user-reported + log-confirmed 2026-08-07)
**User playing Gears reported buzzing audio, 72C and 8W.** Captured from that session's logcat:
```
8,221 XmaContext warnings, dominated by:
  5,002  XmaContext 27: non-forward input read offset
  2,682  XmaContext 32: non-forward input read offset
    537  XmaContext 33: non-forward input read offset
```
The guard is `xma_context.cc:789` and it ends in **"stopping decode of this buffer"** - so the decoder aborts
thousands of times per session, leaving gaps in the stream. **That is the buzzing.** It is a decode-side
read-offset failure, NOT (or not only) a callback-side underrun.
**⚠️ THIS IS THE SAME WARNING I SAW EARLIER TODAY AND EXPLICITLY DECLINED TO LINK TO THE AUDIO WORK** (see the
real-time-callback section below, where I wrote 'NOT asserted ... they could be unrelated'). With a user report
of audible buzzing plus 8,221 aborts in one session, the link is now evidence rather than speculation - but
note what it means: **the XMA release fence (`e5398cac8`) and the audio-priority fixes (`306a4a68a`,
`c7205d89d`) are all INSTALLED and the buzzing persists.** So none of them is the cause.
**⇒ START AT `xma_context.cc:789`, not at the driver.** The condition is an input read offset that fails to
advance (`offset >= end`), which is a producer/consumer disagreement about the guest's XMA ring - a decode-side
bug. The still-unfixed callback-takes-the-global-mutex issue below is a real defect and may make it worse under
load, but it cannot by itself produce a 'non-forward read offset'.
**🔑 WE TRADED A CRASH FOR BUZZING, AND THE MITIGATION'S CORE ASSUMPTION IS WRONG.** The guard came from
`b5afa11e3` ("don't SIGABRT on a non-forward XMA input read offset"). It replaced an `assert_true` that was
**SIGABRTing the whole emulator, device-observed killing Gears ~85s in**. Its comment reasons: *"Only reached
when the old assert would have fired, so working audio is byte-for-byte unaffected."*
- **That reasoning is sound but rests on the case being RARE.** The user's session fired it **8,221 times**
  across 3 contexts. At that rate it is not an edge case, it is the steady state - so the mitigation is
  converting a crash into continuous audio dropout, which is exactly what buzzing is.
- **⇒ The ROOT CAUSE was never fixed, only made non-fatal.** The real bug is whatever makes the recomputed
  frame offset fail to advance past `input_buffer_read_offset` - a producer/consumer disagreement about the
  guest's XMA packet stream. `b5afa11e3` is the right call for stability and should STAY; it just is not a fix.
- **Where to start:** the offset is recomputed as `GetPacketFrameOffset(packet) + packet_idx * kBitsPerPacket`
  just above the guard. Log `packet_idx`, `current_input_packet_count`, `input_buffer_read_offset` and the
  computed `offset` on the first N fires and see whether `packet_idx` is stuck, wrapping, or running past the
  buffer - the `// TODO buffer bounds check` sitting immediately above the guard is a strong hint nobody has
  validated that path.
- **⚠️ Do NOT 'fix' it by loosening the guard back toward the assert** - that restores the crash.
**8W and 72C are the user's own numbers** and are the first valid power figures we have - USB-attached ADB
cannot measure this (see the watts section). Against rpcsx's reported 3-5W on the same handheld, that is the
gap to close, and it is CPU, not GPU.

## 🔊🌩️ THE REAL-TIME AUDIO CALLBACK TAKES THE GLOBAL WAIT MUTEX AND WAKES EVERY GUEST THREAD (2026-08-07)
**The single most concrete CPU/power finding of the XenDroid sweep, and it needs no device to see. Traced:**
```
AAudio DataCallback  (real-time, every few ms)
  -> AndroidAudioDriver::FillAudio()            android_audio_driver.cc:153, takes frames_mutex_
    -> ReleaseConsumedFrameLocked()             :186
      -> semaphore_->Release(1, nullptr)
        -> lock(PosixConditionBase::mutex_)     threading_posix.cc:360  <- GLOBAL static mutex
        -> cond_.notify_all()                   threading_posix.cc:363  <- GLOBAL static condvar
```
- **Two distinct defects in one path.** (a) **A real-time audio callback must never block on an unbounded lock** —
  here it waits on the one mutex every guest thread takes for every wait/signal, so any guest thread holding it
  stalls the audio callback and underruns the stream. (b) it then **notify_all()s the global condvar**, waking
  every parked guest thread (see the thundering-herd note below) **at audio-callback frequency**.
- **This is the shape of XenDroid `92ca0d563`** ("Keep the realtime audio callback off the global multi-wait
  mutex"). Port it WITH the condvar refactor, not before — on our tree the two are the same root cause.
- **Plausibly relevant to the XMA warnings we actually see** (`XmaContext {}: non-forward input read offset ...
  stopping decode of this buffer`, xma_context.cc:789, seen throughout a Gears run). **NOT asserted** — that is a
  decode-side offset guard and this is a callback-side stall; they could be unrelated. Verify before linking them.
- **Already checked and NOT applicable:** `366f38da8` ("fill the whole AAudio request instead of one guest block
  per callback") — our `FillAudio` already loops over queued frames and zero-fills on underrun.

## 🌩️🌩️ ONE GLOBAL CONDVAR WAKES EVERY WAITING GUEST THREAD ON EVERY SIGNAL (found 2026-08-07, NOT fixed)
**`PosixConditionBase::cond_` and `::mutex_` are `static`** (threading_posix.cc, bottom of the class) — **a SINGLE
condition variable shared by every event, semaphore, mutant and timer in the emulator.** `WaitMultiple` (:242)
parks on that shared condvar with a predicate over its handles, and **all 9 `cond_.notify_all()` sites** (:322,
:363, :373, :397, :429, :839, …) therefore wake **every** parked thread — each of which re-evaluates handles it
has no interest in, finds nothing, and parks again. A classic thundering herd, paid on every guest signal.
- **Why it matters here specifically:** wasted wakeups are wasted CPU and therefore wasted WATTS, which is the
  open question raised by `noeldvictor/rpcsx-ui-android-thor` doing PS3 emulation at 30fps / 3-5W on this same
  device. It is also pure overhead — no guest work is being done in those wakeups.
- **XenDroid hit the same class and fixed a MILDER version:** `2c0ac5847` gates their `PokeMultiWaiters()` behind
  an atomic `multi_wait_refs_` counter, registered by a scoped `MultiWaitRegistration` over the handles being
  waited on, so a signal only pokes the shared condvar when some parked WaitMultiple is actually watching that
  object. **Their fix is NOT directly portable** — they have per-object condvars plus a separate multi-wait poke;
  we have one static condvar doing both jobs, so the same counter would gate nothing.
- **⚠️ DO NOT PATCH THIS CASUALLY.** It is core threading used by every guest wait, the existing code carries a
  `TODO(bwrsandman, Triang3l)` about a known deadlock hazard (issue #1677) if a thread is suspended between
  locking and waiting, and a mistake here is a hang rather than a wrong pixel. The real fix is per-object condvars
  with a gated shared poke (XenDroid's shape), which is a refactor, not a patch.
- **Measure before building** (rule 4): count wakeups vs. useful wakeups on the shared condvar under a real
  gameplay scene first. If the ratio is near 1 this is theoretical; the shape of the code says it will not be.

**🧹 FIRST, A NEGATIVE RESULT THAT SAVES THE NEXT SESSION A SWEEP: THE SPIN-WAIT HYPOTHESIS IS DEAD.**
The obvious power theory — "a 'waiting' guest thread is actually `sched_yield()`-looping at 100% duty, burning a
core for zero work, invisible to fps" — was checked against every `MaybeYield`/spin site and **does not hold**:
`xobject.cc:484/572/703/735` are **one-shot yields on the timeout RETURN path**, not loops; `GuestScheduler::
SpinYield` explicitly calls `BlockCurrentThread()` with the comment *"parking rather than re-queueing, so a lone
fiber idles instead of spinning its dispatch thread at full speed"*; `NtYieldExecution` is a guest-REQUESTED
yield; and `xeKeKfAcquireSpinLock`'s backoff is already measured completely uncontended (0 of 16,673 acquires).
**The wait paths block properly.** Do not re-run this sweep — the waste is in the WAKEUPS, not the parking.

**✅✅ IMPLEMENTED 2026-08-07 (`84fff766c`), cvar `threading_per_object_condvar`, DEFAULT OFF, allowlisted.**
**🔑 THE "NOT PORTABLE" VERDICT ABOVE WAS RIGHT ABOUT THE FACT AND WRONG ABOUT THE CONCLUSION — AND THE REASON
GENERALISES.** It says their counter "would gate nothing" because *we have one static condvar doing both jobs*.
That diagnosis is exactly correct, and it is also the entire solution: **the gate is useless only BECAUSE the two
jobs share a condvar.** Split the jobs and the gate becomes meaningful. So the blocker was never portability, it
was that only half of their design had been considered. **When an upstream fix is rejected as "not portable
because our structure differs", check whether their OTHER change is the one that creates the structure.**
| | before | after |
|---|---|---|
| single-object `Wait()` | shared static `cond_` | **per-object `local_cond_`** |
| `WaitMultiple()` | shared static `cond_` | shared `cond_`, under a scoped `MultiWaitRegistration` |
| all 7 signal sites | bare `cond_.notify_all()` | `NotifyWaiters()` — always wakes `local_cond_`, pokes the shared one **only while `multi_wait_refs_ != 0`** |
**THREE DELIBERATE CONSTRAINTS, because the failure mode is a HANG:**
1. **The shared MUTEX is untouched** — only the condvar changes. All locking order is preserved, and the #1677
   hazard is neither fixed nor worsened. Do not bundle that refactor in.
2. **The cvar is read EXACTLY ONCE** (`PerObjectCondvars()`, function-local static). If a waiter and a signaler
   could ever disagree about which condvar an object uses, the wakeup goes to a condvar nobody is on.
3. **`MultiWaitRegistration` is declared AFTER the lock**, so it is destroyed BEFORE the lock releases.
   Registering under `mutex_` is what makes the gate race-free — the signaler holds the same mutex when it reads
   the count, so it either sees the registration or has not yet changed the state the waiter re-tests.
**VALIDATED DEVICE-FREE — and note the host suite structurally COULD NOT do it: Windows builds
`threading_win.cc`, so `threading_test.cc` never compiles this file.** `tools/qemu/condvar_herd_equiv.c` distils
the algorithm and stresses it under WSL g++ (8 objects, 16 single-waiters, 2 multi-waiters, 3200 signals):
**3/3 PASS, `consumed + outstanding == effective signals` exactly in both modes, wasted wakeups 8296 → 4663
(42.7–45.1% fewer).**
**🪤 THE TEST FAILED FIRST AND THE TEST WAS WRONG, NOT THE CODE.** Its first version counted raw signals and
reported both modes "losing" hundreds. **Setting an already-set auto-reset event is a no-op, so signals
legitimately coalesce** — raw signal count is not a liveness invariant. Corrected to count state-CHANGING
signals, which closes exactly. *A stress test that fails on BOTH arms is usually measuring itself.*
**⚠️ UNMEASURED: any watt or temperature figure, and the real-world ratio.** The scenario keeps a multi-wait
registered almost continuously so the gate rarely closes; the 44% comes entirely from single waiters no longer
being woken. **Whether guest multi-waits are that persistent is unknown** — if they are rarer than in the test,
the win is larger; if a multi-wait is always outstanding, the shared poke never stops and only the single-waiter
half pays off. Read `entry_delta` AND thermals on the A/B, not fps.

## 📊 THE AOT PROGRESS BAR IS PROVABLY FROZEN — COMPILE IS FINE, THE BAR IS NOT (2026-08-07)
**User reported it three times ("no bar movement"); now confirmed with evidence that does not depend on logging.**
Gears with `--ez cpu_aot_maximize true`: the overlay renders correctly ("Compiling game code… / Starting…", with
the ANR-warning text), and **three screenshots minutes apart are BYTE-IDENTICAL (same MD5, 62373 bytes)** while
`top` shows the process at **261-340% CPU and 16+ minutes of CPU time**. So the compile is genuinely running and
the bar genuinely never advances. It is NOT a hang.
- **⚠️ TWO WRONG DIAGNOSES I MADE FIRST — do not repeat them.** (a) "`opt_aot_precompile` never sets
  `cpu_precompile_guest_functions`" — **FALSE**, `xex_module.cc:1416` ORs it with `cpu_aot_maximize`, so the GUI
  wiring is correct. (b) "there are no `AOT precompile progress:` lines" — **UNSAFE**: that run had
  `--es disassemble_function_filter` on, which flooded logcat with **12,488** `Filtered function dump` lines and
  evicted everything older than ~2 minutes. The emitter exists and matches the parser exactly
  (`xex_module.cc:1548` prints `"AOT precompile progress: {} / ~{} functions"`, throttled to every 256 fns via
  `(done & 0xFF) == 0`; `EmulatorActivity.java:1132` parses that exact string).
- **⇒ THE REMAINING QUESTION IS EMITTER-vs-WATCHER, and it needs ONE CLEAN RUN with no disassembly filter.**
  Strong candidate: the overlay's watcher is its own `logcat --pid=<self> -T 0 -s xenia:*` child process, so ANY
  high-volume xenia logging can starve it — which also means the bar may break for reasons unrelated to AOT
  whenever logging is heavy.
- **🔥 SEPARATE AND ARGUABLY BIGGER: the AOT pass wrote NOTHING to the object cache.** 15 minutes of compiling at
  261-340% CPU left `files/objcache` at exactly 264M, unchanged. The cache directory is `objcache_v2_opt2` — an
  **LLVM** object cache (opt2 = `cpu_backend_llvm_opt`) — so an a64-backend AOT pass has no persistent store and
  **recompiles the entire game on every launch**. The overlay's own text admits it: *"This runs once per launch."*
  That is the real cost: ~15min of 3-core compile and 42->67C of heat, discarded, every single launch. **If AOT is
  to be the default per the standing directive, it needs a cache on the a64 path too — or LLVM must actually be
  on.** Not yet investigated; recorded because it explains both the startup time and the thermals.

## 🪤 GEARS SIGTRAP IN JIT CODE AT GUEST 0x8227EE6C (new signature, 2026-08-07, unresolved)
**Seen twice.** Gears of War reaches `Displayed ... +212ms`, then ~3s later:
`Fatal signal 5 (SIGTRAP), code 1 (TRAP_BRKPT), fault addr 0x2a000025c, tid "Main XThread"`, with
`#00 pc 0x25c /dev/ashmem/xenia_code_cache_...` — i.e. **a `brk` OUR OWN JIT emitted**, not a host library fault.
- **Registers rule out the known suspects:** `x20 = 0x77a004ee40` (guest context **VALID**, so this is NOT the
  LLVM-writes-x20 bug documented above, whose signature is `x20_ctx=0`), `x21 = 0x100000000` (membase valid).
  `x0 = 0x8227ee70` and `x23 = 0x8227ee6c` are **guest addresses inside Gears' code range** (82170000-82B00000,
  Module Hash 1B591620508434A2).
- **Read the guest site before theorising:** `--es disassemble_function_filter 8227EE6C`. A PPC `tw`/`twi` is a
  real guest trap instruction, and Burnout's D3D wait helper is already known to `tw` when its timeout expires —
  so this may be the GUEST deliberately trapping (e.g. a GPU-completion timeout) rather than a codegen defect.
  Do not assume it is our bug until that disassembly says so.
- **Not attributed to the `-mtune=cortex-a710` build:** an earlier launch of that same APK reached
  `Title name: Gears of War` and ran 300s. Intermittent. If it needs bisecting, the cheap arm is
  `--ez cpu_backend_llvm false`, per the LLVM/a64 discriminator above.

## 🛑🛑🛑 A BARE `am start` DOES NOT TEST WHAT SHIPS — IT RUNS WITHOUT LLVM OR AOT (2026-08-07, user caught it)
**Every headless `adb shell am start` measurement is taken on the a64 backend with NO AOT precompile, unless you
pass the CPU flags explicitly.** Verified live: 0 LLVM log lines across a whole session of runs, with
`cpu_backend_llvm = false`, `cpu_aot_maximize = false`, `cpu_llvm_target_features_native = false` in the persisted
config. **This is the same class as the "bare `am start` runs the Qualcomm driver" trap, for the CPU** —
`opt_llvm_backend` and `opt_aot_precompile` are `defaultEnabled=true` in `XeniaOptimizations`, but that registry is
applied by the **GUI launch path**, and a bare `am start` never touches it, so the persisted `false` wins.
- **The standing directive says AOT+LLVM is the default for every game.** A headless run that omits them is not
  measuring the shipping configuration, whatever else it proves.
- **Pass them explicitly** (all three ARE allowlisted): `--ez cpu_backend_llvm true --ez cpu_aot_maximize true
  --ez cpu_llvm_target_features_native true`. `cpu_llvm_no_runtime_compiles` is **NOT** allowlisted (it is default
  true, so this rarely bites, but `--ez` on it silently no-ops).
- **⚠️ The object cache is LLVM-only:** the directory is `files/objcache/objcache_v2_opt2` (opt2 =
  `cpu_backend_llvm_opt`). **An a64-only run cannot use it**, so every LLVM-less headless launch recompiles from
  scratch — plausibly why those runs heat the device fast, though that specific claim is NOT yet measured (a run
  that appeared to show it turned out never to have reached the foreground; see the discipline note below).
- **Best practice, from CLAUDE.md's own shipping loop: VERIFY FROM THE IN-APP GUI LAUNCH**, not `--ez`.
- **⚠️ Android will silently decline a background activity start while the user is on the device.** Two launches
  produced a live PID with **zero** `xenia` log lines and zero `EmulatorActivity` — the process existed, the
  emulator never ran, and the temperature reading from it meant nothing. **Before trusting any headless run,
  confirm it actually rendered** (`screencap`, or a `Title name:` / fps line), not merely that a PID exists.

## 🦾 HOST BUILD IS NOW TUNED FOR THE CORE, NOT GENERIC (2026-08-07, prompted by rpcsx-ui-android-thor)
**We passed no `-mtune` at all, so the WHOLE emulator — every translation unit, not just JIT-emitted guest code —
was scheduled for a generic ARM64 pipeline.** Found by comparing against `noeldvictor/rpcsx-ui-android-thor` (a PS3
emulator on this exact device; the PS3 PPU is PowerPC like the Xenon, so it is a fair comparison), whose README
specifies `-march=armv8.2-a -mtune=cortex-a715`.
- **We are AHEAD of them on atomics** (`+lse -mno-outline-atomics`, worth ~6% on Burnout per the note at
  premake5.lua:270) and were BEHIND on scheduling. Both now set.
- **⚠️ `-mtune=cortex-a715` DOES NOT COMPILE HERE.** NDK 25 is **clang 14**, which predates Cortex-A715/X3 (LLVM 16)
  and errors: *"the clang compiler does not support '-mtune=cortex-a715'"*. Tested against this toolchain:
  **`cortex-a710`, `cortex-x2`, `cortex-a78`, `neoverse-n2` are accepted; `cortex-a715` and `cortex-x3` are not.**
  Using `cortex-a710` — not a compromise, since the 8 Gen 2 physically contains A710 cores (X3 + 2×A715 + 2×A710 +
  3×A510) and the A715 is its direct successor. NDK 29 is installed and would allow a715, but that is an STL/ABI
  change and should not be bundled with a scheduling tweak.
- **`-mtune` is safe by construction**: it selects the scheduling model and cost heuristics only and cannot change
  which instructions are legal (unlike `-mcpu`, which also moves the ISA baseline).
- **🔁 CHANGING premake5.lua IS NOT ENOUGH — REGENERATE:** the flags live in generated `build/*.prj.Android.mk`
  (50 of them, untracked). `./tools/build/bin/premake5.exe --file=premake5.lua --os=android androidndk` —
  **`--os=android` is REQUIRED**. Without the regen the gradle build finishes in ~4s having changed nothing, which
  looks like success. A real flag change forces a ~15min full rebuild; a 4s "BUILD SUCCESSFUL" means it did not take.
- **UNMEASURED.** Drift here is ~2.8%; this needs a same-session A/B before any win is claimed.

## 🧪🧪🧪 A DEFAULT-OFF PATH IS NOT A CONTROL — IT IS UNTESTED CODE (device-found 2026-08-07)
**`--ez a64_three_operand_shifts false` killed Gears in under a second with `Scudo ERROR: misaligned pointer when
deallocating` (SIGABRT on the Kernel Dispatch thread, `XThread::Create()::$_1` / xthread.cc:541). The same build
at the DEFAULT ran 240s+ fine. The bug was in the OFF path, not the ON path.**
- **Cause:** `baacdeaed` added the isolation cvar so the shift rewrite could be disabled without an APK rollback,
  and in **7 of 10** gated sites the `if (i.src1.is_constant)` arm was dropped from the off branch. With a constant
  src1 it calls `i.src1.reg()` on a constant operand and never loads the constant into dest, so it shifts stale
  register contents into guest state → heap corruption. Fixed in `862410c32` (3 sites were already correct).
- **🔑 THE LESSON, which nearly cost the whole experiment:** the Gears CPU-vs-GPU isolation run had been queued for
  most of a session. Run without noticing this, the arm under test dies in a second and the natural reading —
  *"disabling the shifts breaks it, so the shifts are load-bearing"* — is **exactly backwards**. **A branch that
  has never once executed is not a control; using it as one tests the branch, not the hypothesis.** Before trusting
  any `--ez <lever> false` result, confirm the off path has actually run at least once, or read it as carefully as
  the on path. This generalises to every isolation cvar in this repo.
- **Corollary for how these get written:** when adding an isolation cvar, the off branch must be the ORIGINAL code
  moved verbatim, not retyped. Retyping is where the arm went missing, and it type-checks either way because
  `.reg()` on a constant operand compiles fine.

## 🔋 HOW TO ACTUALLY MEASURE WATTS (2026-08-07) - you CANNOT do it over USB
**The rpcsx-ui-android-thor comparison is stated in POWER (30fps at 3-5W), and power is the one metric our whole
measurement protocol never covered. It is measurable on this device, but only under one condition.**
- **Nodes exist:** `/sys/class/power_supply/battery/{current_now,voltage_now,power_now,status}` -
  `current_now` in uA, `voltage_now` in uV, so watts = `(current_now/1e6) * (voltage_now/1e6)`.
- **🛑 BUT ADB OVER USB MAKES IT MEANINGLESS.** With the cable in, `status=Charging` and `current_now` is
  dominated by CHARGE current, not app draw - two consecutive samples on an IDLE device read `+99245uA` then
  `-261114uA`, i.e. the sign flipped. Any wattage quoted from a USB-attached session is fiction.
- **The protocol that works:** unplug, use **ADB over wifi** (`adb connect <ip>:5555`), confirm
  `dumpsys battery` says `USB powered: false` and `status: Discharging`, then take an IDLE baseline for ~60s and
  the RUNNING draw for ~60s and report the DELTA. Absolute draw includes the panel, wifi and the SoC floor, so
  only the delta is comparable to another emulator's figure - and their number is a total, so say which you are
  quoting.
- **Pair it with the gameplay rule:** a power number from a menu or an attract mode is as worthless as an fps
  number from one. Measure power in the same real-gameplay scene the CPU protocol already requires.

## ⚠️ Measurement is the #1 trap
BD's GPU scene complexity swings ~4×/second → **cross-run fps / gpu_frame_us is CONFOUNDED (worthless)**. Only
trust: single-run in-place alternating A/B on a GPU-busy frame (`gpu_freeze_ab_alternate_vrs`,
vulkan_command_processor.cc ~2280); screenshot correctness; qemu-a64 byte-identical; code facts. `gpu_frame_us`
absolute = unreliable (includes idle). **OSD fps box (screenshot it) = the truth for ONE scene.** Post-temp
change confirms the field was reached (no heating = idle/menu scene = discard run). **Derive the structural
cause from code/arch FIRST; fire the device only to CONFIRM one hypothesis, never to explore.**

## 🖤🖤🖤 BLACK SCREEN? **CHECK THE DISPLAY IS AWAKE FIRST** (cost most of a session, 2026-08-03)
**`adb shell dumpsys power | grep mWakefulness` MUST say `Awake`. If the panel is asleep the Android activity is
stopped, its SurfaceView never gets a surface, the presenter has no surface, and it silently DROPS every guest
frame — and `screencap` returns a fully black PNG (~10.9 KB at 1920x1080). The emulator is meanwhile perfectly
healthy: guest running, GPU hot, swaps issued.** Launching over adb does NOT wake the screen. Wake it with
`adb shell input keyevent KEYCODE_WAKEUP` (then `input keyevent 82` to dismiss the keyguard) BEFORE any visual
check — `tools/thor_launch.sh` does this automatically now.
- **Screenshot size is the instant tell:** ~10.9 KB = a black panel; hundreds of KB = real content.
- Burnout Revenge on the merged Edge kernel + Turnip 26.3.0: **title screen at 59.2-59.3 fps** (screenshotted).
- The presenter now SAYS so instead of failing mutely: `Presenter: guest output ready but paint mode is kNone -
  frame N dropped, nothing will be shown` (with surface/window state). Grep that first on any black screen; the
  paint chain is also traceable via logcat tag `XeniaPaint` (surfaceCreated/surfaceChanged/postInvalidate/onDraw).

## ⚠️ Never thrash the Thor (hard safety)
Before ANY launch read `/sys/class/kgsl/kgsl-3d0/temp` (milli-°C) + `gpu_busy_percentage`; launch only if
temp < 50-55°C. Force-stop `jp.xenia.emulator.github.debug` past ~70°C.
- **🔋🔋 CHECK THE BATTERY TOO - the temp+idle checklist MISSES IT, and that gap bit us 2026-08-07.** The user
  had to interrupt with *"device power is low dont use it again until i approve"* after a session of repeated
  launches: every one of them passed the documented pre-flight (temp < 55C, no other process, screen awake)
  because **charge was never part of the check**. An emulator run at 260-340% CPU is one of the heaviest
  possible drains, and unlike temperature there is no automatic recovery - the device just dies.
  **Add to pre-flight:** `dumpsys battery | grep -E "level|status|USB powered"`. Do not launch below ~30%
  unless charging, and say the level out loud when firing so the user can veto.
  **⚠️ Note the direct conflict with the WATTS protocol below**, which requires running UNPLUGGED on wifi adb
  to get a meaningful power number. Those runs drain fastest and are exactly the ones that need a charge floor
  and a stated budget BEFORE starting - not a discovery halfway through.
  **And when the user says stop using the device, that includes POLLING it** - a cooldown/temperature loop is
  still adb traffic keeping the device awake. Kill the background task, do not just stop launching. Device degrades under heavy firing
(boot stalls after ~6 launches). Batch fixes, build once, fire once, fill cooldowns with device-free work.
**DO NOT reboot the device; never blame it for a result.**
- **🔀 THE THOR IS SHARED — another Claude Code session may be driving it (user, 2026-08-05).** Before firing, check
  the device is idle (`adb -s c3ca0370 shell pidof jp.xenia.emulator.github.debug` + the temp read above); a title
  already running is someone else's measurement, and installing an APK under it invalidates their run as well as
  yours. Assume any unexplained fps/thermal reading may be another session's run, and re-measure rather than
  reasoning about it. The same applies in reverse — say what you are firing before you fire it.
- **Check `git log` before starting an optimization.** Two sessions independently mining the same source will
  implement the same lever twice; on 2026-08-05 the RPCS3 ARM64 list was already ~half landed (ISB, LLVM target
  features, SHA3 detection, MIDR classification) by a parallel session within hours.

## 🦾🦾 ARM64 / SNAPDRAGON 8 GEN 2 TUNING — the RPCS3-parity track (opened 2026-08-05)
**Source of truth: `docs/research/20260805-rpcs3-arm64-optimizations-applicable.md`** — Whatcookie's RPCS3 ARM64 work
("PS3 emulation is fast on ARM now"), mined to a local transcript and cross-checked against the merged PRs, not the
video alone. **It was measured on an AYN Odin 2 = the SAME Snapdragon 8 Gen 2 as the Thor**, and the PS3 PPU and the
Xenon are both PowerPC+VMX (several items are literally the same guest instruction), so these transfer DIRECTLY, not
by analogy. Their claim — **theirs, not ours, unverified by us**: ~60% faster at ~75% power. Never restate as ours.
- **Claude cannot watch video → use the `video-transcript-mining` skill** (yt-dlp auto-captions in seconds; whisper
  only as a fallback). Guessing a talk's contents from its title, or from the author's commits, yields confident
  nonsense. Transcript first, always.
- **Device feature reality (read from /proc/cpuinfo, 2026-08-05):** `asimddp i8mm bf16 fphp asimdhp atomics lrcpc
  ilrcpc sha3`. **NO SVE/SVE2** — Qualcomm shipped 8 Gen 2 as ARMv9 without it, so every RPCS3 SVE PR (XAR, SVE
  multiplies, SVE2 FMS) is N/A to us. `sha3` DOES apply and brings EOR3/BCAX/RAX1/XAR (useful far outside crypto).
- **⚠️⚠️ THE BUG CLASS TO SWEEP FOR: `#if XE_ARCH_AMD64` guards that leave ARM64 with NOTHING.** Found 2026-08-05 in
  `xeKeKfAcquireSpinLock` (xboxkrnl_threading.cc, a `kHighFrequency` export): the remote-holder backoff loop body was
  `_mm_pause()` inside an AMD64 guard, so on ARM64 the "16-try brief spin budget" was an **empty loop** that elapsed in
  nanoseconds and fell straight through to the fiber reschedule it existed to avoid. Fixed via the new
  `xe::threading::SpinLoopHint()` (base/threading.h). **Grep for this shape before assuming any lever is live on ARM.**
  **⛔ BUT THE LEVER IS FLAT — MEASURED 2026-08-05, do NOT tune guest spinlock backoff.** Instrumented run
  (`--ez kernel_spinlock_stats true`, Burnout on verified Turnip, 90s, 34→54°C): `calls` climbed 11733→16673
  (~198/s, steady) with **`contended=0 remote_spin=0 spin_won=0 spin_lost=0` for the entire run** — the CAS never
  failed ONCE in 16673 acquires. The guest spinlock is completely uncontended, so the spin path never executes and
  the empty ARM64 loop never ran. The fix stays in as a **latent-bug fix and is NOT a perf win**; no spin-budget A/B
  is worth firing. Re-measure with `scratch/thor-debug/measure_spinlock_stats.sh` before reopening this.
  ⚠️ Do NOT confuse this with Burnout's ring-read-pointer busy-wait — that is a DIFFERENT spin (GPU ring pointer,
  a gameplay behaviour), not `KfAcquireSpinLock`.
  ✅ **SWEEP DONE 2026-08-05 — the spinlock was the ONLY one; do not re-run it.** Scanned every `#if XE_ARCH_AMD64`
  in `src/xenia` with no `#else`/`#elif`: 7 hits, 6 benign (x64 backend include + `cpu == "x64"` selection in
  emulator.cc; `<xmmintrin.h>` includes; `XE_CLOCK_RAW_AVAILABLE`, which is fine because ARM64 reads cntvct_el0
  through the *platform* path already; the `m128_f32/i32/f64/i64` SSE utility templates in math.h, which are x64-only
  by nature and fail LOUDLY at compile time rather than silently); and `primitive_processor.h:455`, which looks bare
  but is only a helper *declaration* — the loop below it has a real `#elif XE_ARCH_ARM64` NEON branch.
  The dangerous shape is specifically **an AMD64-guarded STATEMENT as the entire body of a loop or function**, not a
  guarded declaration or include. Re-run only if new AMD64 guards land.
- **Spin counts are WALL-CLOCK budgets, not iteration counts.** x86 `PAUSE` ≈140 cyc (Skylake+) vs ARM `ISB` ≈10-30,
  so an x86-tuned constant under-spins by ~4-8× on ARM (RPCS3 PR 18055 hit exactly this). `kRemoteHolderSpinTries` is
  now arch-split: 96 on ARM64, 16 on x64.
- **⚠️ `yield` IS A NO-OP on every core in this SoC** (X3/A715/A710/A510) — it is a hint that retires as a NOP, NOT an
  x86 `pause`. `ISB` is the closest working backoff. But see the CONFOUNDED result below before assuming it wins.
- **STATUS — do NOT redo these:** ✅ inline CNTVCT_EL0 guest timer (clock_posix.cc + a64_emitter.cc:5112; the comment
  records clock_gettime was ~15% of CPU on timing-heavy titles) ✅ VPERM→TBL (a64 emits `tbl`; LLVM emits 2×TBL1 OR'd,
  deliberately — the TBL2 intrinsic needs a consecutive register pair we cannot satisfy) ✅ MIDR_EL1 core
  classification (platform_arm64.cc) ✅ LLVM target features ✅ FEAT_SHA3 detection ✅ `fmax`/`fmin` native in a64.
  **THE VMX FLOAT PATH SPENDS ~8 ASIMD uOPs OF SCAFFOLDING PER OPERATION (priced 2026-08-07).**
  Counted in our source, priced from the A710 SWOG (ASIMD logical and ASIMD compare are both latency 2 /
  throughput 2, pipe V):
  | per `vmaxfp`/`vminfp` | ASIMD uOPs | issue cycles on 2 pipes |
  |---|---|---|
  | `PrepareVmxFpSources` staging copies | 2 (`mov`) | 1 |
  | `FixupVmxMaxMinNan` | 6 (2x `fcmeq`, 2x `orr`, `bsl`, `mov`) | 3 |
  | the actual `fmax` | 1 | 0.5 |
  **The scaffolding costs ~8x the operation it wraps**, and `PrepareVmxFpSources` is on **9** VMX float
  sequences, not just max/min. Only 2 pipes accept ASIMD uOPs on the mid-cores, so this is the scarce
  resource going to bookkeeping.
  **=> This makes two existing items far more valuable than they looked, AND THEY ARE THE SAME JOB:**
  **🐞 BEFORE ENABLING `a64_vmx_fp_no_operand_copy`, KNOW THAT ITS ON-PATH WAS BROKEN UNTIL 2026-08-07.**
  Found by reading, not running - an audit of all 9 `PrepareVmxFpSources` call sites. At the VMX FMA sequence
  (`a64_sequences.cc` ~5792) the code called `PrepareVmxFpSources` and then did
  `e.str(QReg(0), ...)` / `e.str(QReg(1), ...)` to stash "the sources" for `FixupVmxNan_V128_Fma`. **But
  `PrepareVmxFpSources` only writes v0/v1 on its COPY path** - with the no-copy lever ON it returns the
  ALLOCATED registers (v4+) and never touches v0/v1, which at that point still hold denormal-flush temporaries
  from the s3 flush just above. So the FMA NaN fixup decided propagation from GARBAGE. The same block's `fmla`
  already used `VReg(s1)/VReg(s2)`, so the sequence was internally inconsistent. Fixed to `QReg(s1)/QReg(s2)`,
  which is byte-identical on the copy path (there s1==0, s2==1).
  **⇒ THIS IS THE `a64_three_operand_shifts` LESSON AGAIN, IN THE OTHER DIRECTION:** that was a default-off
  lever whose OFF path was untested; this is a default-off lever whose ON path was untested. **Any lever that
  has never executed is unvalidated code in whichever direction nobody runs.** Had the "saves 6 uOPs" framing
  been acted on without the audit, the result would have been subtle FMA float corruption - no crash.
  **⚠️ STILL DEFAULT-OFF.** This fix removes a real blocker but is NOT a substitute for the lever's own
  validation; a register-allocation change cannot be checked by a C-level qemu test, so it needs an emitter-level
  differential or a device run. One false positive to note for whoever does the audit next: a 60-line scan also
  flags the site at ~5657, but that block `return`s and the `str(QReg(0))` below it belongs to a SEPARATE
  fallback that stages into v0/v1 explicitly. Read the enclosing block.
  (a) `a64_vmx_fp_no_operand_copy` (default-off, awaiting the qemu-a64 differential) removes 2 of the 8 across
  all 9 sequences; (b) `FixupVmxMaxMinNan` is 6 of the 8 **and is probably WRONG anyway** - the ARM half is
  spec-confirmed above (`fmax` propagates NaN where PPC `vmaxfp` and x86 `MAXPS` return the number, and the
  fixup only rewrites the both-NaN lane). **Fixing it correctly and fixing it cheaply are one piece of work**,
  and the correct sequence may differ from the current one entirely rather than just being trimmed.
  **Gated on exactly what it was already gated on:** the qemu-a64 differential, plus confirming the PPC
  `vmaxfp` half from a PowerPC manual rather than from memory. **Do not trim scaffolding whose correct shape
  is not yet established** - that is the wrong order of operations.
  **⚠️ VPERM→TBL IS 3× MORE EXPENSIVE ON LLVM THAN ON a64 — manual-priced 2026-08-07.** a64 emits a real
  two-table `tbl` (a64_seq_vector.cc:960/1001/1034) = **1 µOP**. LLVM emits **2×TBL1 OR'd = 3 µOPs**
  (llvm_assembler.cc:2200) because `aarch64.neon.tbl2` needs a CONSECUTIVE register pair we cannot satisfy.
  **A710 SWOG p52: `ASIMD table lookup, 1 or 2 table regs` = latency 2, throughput 2 — a TWO-table TBL costs
  EXACTLY the same as a one-table TBL.** So the workaround buys no latency and spends 3× the µOPs on the
  FP/ASIMD pipe, which is only **2 wide** on the mid-cores (vs 4 integer, 3 load) — and **LLVM is the shipping
  default**, so that is the path most VPERMs take. (3-table TBL is the costly form: latency 4, throughput 1.)
  **NOT fixed, NOT sized:** there is no census of how hot VPERM is in a real scene, so measure applicability
  before fighting LLVM's register allocator (rule 4). The manual only changes this from a guess into a known
  payoff: 3 µOPs → 1 on the scarce pipe.
  ⚠️ `a64_spin_hint_isb` = **CONFOUNDED / default-off / NO WIN measured** — frame-capped title screen + unequal
  thermal start (53.1°C vs 57.8°C); a refutation it is NOT. Retest in a real race from equal temps.
  ❌ The A510 "two of three share a vector unit" claim is **REFUTED on the Thor** by our own probe (34014db95) —
  do not re-plumb thread affinity around it.
  **OPEN — AND IT MAY BE A CORRECTNESS BUG, NOT AN OPTIMISATION (analysis 2026-08-06, NOT yet verified):**
  vmaxfp/vminfp `FixupVmxMaxMinNan` (#2). The framing "6 extra insns we can probably delete" is likely WRONG at
  both ends. What was established by reading (facts, checkable):
  - **Our x64 `MAX_V128` is a plain `vmaxps(dest, src1, src2)` with NO NaN fixup whatsoever**
    (x64_sequences.cc:507). ⇒ **The comment in `a64_seq_util.h:468` — "x64 uses maxps(a,b)|maxps(b,a)" — is
    factually wrong about our own tree**, and the a64 fixup is built to replicate something x64 does not do.
  - x86 `MAXPS(a,b)` returns **src2** whenever a comparison involves NaN. PPC `vmaxfp` is
    `(a > b) ? a : b`, and `>` is false for NaN, so it ALSO yields **b**. **They agree — which is exactly why the
    x64 backend needs no fixup.**
  - ARM `FMAX` **propagates** NaN (it is `FMAXNM` that implements IEEE maxNum and returns the non-NaN operand).
  ⇒ **Hypothesis to TEST, do not act on it yet:** for `src1 = NaN, src2 = number`, PPC and x86 both give the
  NUMBER while ARM `fmax` gives NaN — and our fixup only rewrites lanes where BOTH are NaN, so that lane class may
  be silently wrong today. The both-NaN lane is also suspicious: the fixup produces `src1|src2`, where PPC would
  give `b`.
  **✅✅ RESOLVED - AND MY PPC CLAIM WAS BACKWARDS. PEM p85 SETTLES IT** (`docs/reference/ppc/`):
  ```
  max(NaN,x) -> QNaN   where x = any value
  min(NaN,x) -> QNaN   where x = any value
  ```
  **PPC `vmaxfp` PROPAGATES NaN.** I had asserted from memory that it was `(a>b)?a:b` and therefore returned the
  NUMBER - that was wrong, which is precisely why the note said to confirm it from a manual first.
  | `max(NaN, number)` | result |
  |---|---|
  | PPC `vmaxfp` (PEM p85) | **QNaN** |
  | ARM `FMAX` (Arm ARM p11115) | **QNaN** - AGREES |
  | x86 `MAXPS` | the number - DISAGREES |
  **⇒ ARM's `fmax` ALREADY MATCHES THE GUEST. It is x86 that does not.** So `FixupVmxMaxMinNan` looks like it
  exists to reproduce an X86 workaround that ARM does not need - 6 ASIMD uOPs per op on the 2-wide pipe, plus
  the 2 staging copies, to fix something that may not be broken here.
  **✅✅✅ DONE 2026-08-07 - THE DIFFERENTIAL RAN AND THE ANSWER IS STRONGER THAN "REDUNDANT": THE FIXUP IS
  WRONG. `a64_vmx_native_fmax_nan` IS NOW DEFAULT TRUE.** Test in-repo at `tools/qemu/fmax_nan_differential.c`
  (aarch64-linux-gnu-g++ + qemu-aarch64, no device). Both open payload questions are closed:
  - **The PPC half, verbatim from PEM 3.2.5.1 "NaN Precedence"** (not the one-line p85 summary):
    *"if the element in register vA is a NaN then the result is that NaN, else if the element in register vB is
    a NaN then the result is that NaN"*, and *"if the selected source NaN is an SNaN it is converted to the
    corresponding QNaN"*.
  - **FPCR.DN is NOT set in our VMX mode**, which is what makes payload propagation the applicable behaviour:
    `DEFAULT_VMX_FPCR = (1 << 24)` = **FZ only** (a64_backend.h:110). The test sets that exact FPCR and prints
    it back (`FPCR=01000000 FZ=1 DN=0`); results identical with and without it, since FZ concerns denormals.
  - **ARM `fmax` matches PPC EXACTLY in all 8 cases**, including the two that were open:
    `(QNaN1,QNaN2) -> 7FC00001` = vA's NaN as PPC requires, and `(SNaN,num) -> 7FC00001` = quieted as PPC requires.
  - **Our fixup is wrong in 3 of the 8** - it ORs the inputs on both-NaN lanes, so it FABRICATES payloads
    (`7FC00001|7FD00002 = 7FD00003`, neither input) and, worst, `(SNaN,SNaN) -> 7F800001` **puts a SIGNALLING
    NaN into guest state** where the architecture requires a quiet one and ARM had already quieted it.
  ⇒ **The transliterated x86 workaround actively VIOLATED the PPC rule it existed to reproduce.** Deleting it is
  a correctness fix that happens to also save 6 ASIMD uOPs/op on the 2-wide pipe. `fmin` was clean in all 8.
  Also corrected: the comments at `a64_sequences.cc:4721/4860` claimed *"if either input is NaN, result = src1
  (vA)"*, which is wrong for `(num,NaN)` - the exact comment/code disagreement the experiment ledger flagged.
  **⚠️ NOT device-validated yet** (the Thor was running rpcsx). Correctness is argued from primary sources plus a
  bit-exact differential; the PERF effect is unmeasured and must not be quoted until it is.
  **⛔⛔ DO NOT GENERALISE THIS TO THE OTHER NaN FIXUP - `FixupVmxNan_V128` IS LOAD-BEARING.** The obvious
  next thought after the above is "ARM matches PPC on NaN, so delete the NaN fixups". **That is wrong for the
  ARITHMETIC ops, and the difference is one bit.** Measured the same way
  (`tools/qemu/vmx_nan_arith_differential.c`, same FPCR):
  | case | ARM | PPC | |
  |---|---|---|---|
  | PROPAGATED NaN (`QNaN + num`, `SNaN + num`, `QNaN * num`) | `7FC00001` | `7FC00001` | redundant half |
  | GENERATED NaN (`inf + -inf`, `inf - inf`, `0 * inf`) | `7FC00000` | **`FFC00000`** | **DIFFERS - sign bit** |
  **ARM's default NaN is POSITIVE; PPC's is NEGATIVE.** ARM cannot supply that, so the generated-NaN half is a
  genuine architectural difference and the fixup must stay. This is exactly why "the port carried an x86
  workaround" is a HYPOTHESIS TO TEST PER SEQUENCE, not a rule to apply across the backend - two NaN fixups that
  look identical in kind had opposite verdicts.
  **↗️ Real optimisation still on the table (not taken, unmeasured):** since only the generated-NaN half is
  needed, `FixupVmxNan_V128`'s SCALAR lane-extraction path - which exists to reproduce operand-position
  precedence ARM already implements - could collapse to a branchless vector select ("result is NaN AND neither
  input was NaN -> `0xFFC00000`"). Rule 4 first: its fast path already skips when no result lane is NaN, so it
  only pays in code that actually generates NaNs.
  **✅ THE ARM HALF IS NOW SETTLED FROM THE SPEC (2026-08-07), not eyeballed.** `docs/reference/arm/arm-architecture-
  reference-manual-a-profile.pdf`, shared pseudocode `FPMax` (p11115-11116):
  ```
  (done,result) = FPProcessNaNs(type1, type2, op1, op2, fpcr, altfp, TRUE);
  if !done then  ... value1 > value2 ...
  ```
  `FPProcessNaNs` returns `done=TRUE` and the propagated NaN, so **`FMAX` PROPAGATES NaN**. The neighbouring
  `FPMaxNum` (= the `FMAXNM` instruction) instead tests `type1_nan`/`type2_nan` explicitly and implements IEEE
  maxNum. **So the two really are different instructions with different NaN behaviour, and we emit `fmax`.**
  ⇒ For `src1 = NaN, src2 = number`: PPC `vmaxfp` and x86 `MAXPS` both yield the NUMBER; ARM `fmax` yields NaN.
  **Our `FixupVmxMaxMinNan` only rewrites the BOTH-NaN lane, so that class is unhandled.**
  **⚠️ STILL OPEN — THE PPC HALF.** The `vmaxfp` semantics above (`(a>b) ? a : b`, `>` false for NaN, hence `b`)
  are asserted from the AltiVec definition and are NOT confirmed from a PowerPC manual in this repo. **Confirm
  that before changing the sequence.** Then build the qemu-a64 differential with the four lane classes as vectors —
  (NaN, num), (num, NaN), (NaN, NaN), (num, num) — for both `vmaxfp` and `vminfp`. If it holds, this is a FIX and
  the instruction count may go UP.
  **This is what the manuals are for:** the question was unanswerable by measurement (a NaN lane is rare in real
  content and a clean run proves nothing) and took one pseudocode lookup once the Arm ARM was in-repo. ABD/ABA 3-input mid-core trick (#4, the real differentiator — nobody else has it); EOR3/BCAX
  codegen consumers (#5, detection landed but nothing reads it); UDOT byte-sum (#7).
- **Items the FIRST mining pass missed — LLVM-on-ARM codegen workarounds (from the transcript, 2026-08-05):** LLVM
  scalarizes some vector ops on ARM (fix = write the IR idiomatically to match the x86 shape; `CMTST` is a fused
  and+compare); LLVM doesn't know ARM shifts shift in zeros (use the intrinsic, ~2 insns saved per shift); a
  compare+select chain collapses to one `BSL` (15 insns → 1 in their SPU FCGT case); and **re-rolling fully-unrolled
  codegen back into a loop is ~2% on BOTH arches** via code-cache pressure — directly relevant to our AOT precompile.

## 🚨🚨🚨 CHECK THE PERSISTED DEVICE CONFIG BEFORE TRUSTING ANY MEASUREMENT (2026-08-06, cost 2.88%)
**`files/xenia.config.toml` on the device OVERRIDES compiled defaults, permanently. A cvar written there when it was
default-off STAYS off after you change the compiled default and after you set `defaultEnabled=true` in
XeniaOptimizations.** Only `--ez/--ei/--es` beats it.
- **Found by accident:** a census built to size an ARM64 `UBFM` lowering of `rlwinm` reported
  `shift=0 mask=0 general=0 generic=20480` — **100% of `rlwinm` translations on the expensive generic path**
  ((x||x) duplicate + 64-bit rotate + 64-bit mask constant). All three `ppc_rlwinm_*_fastpath` cvars were `false` in
  the device config despite compiled default `true` AND `defaultEnabled=true` in XeniaOptimizations. They had been
  written when the cvars were genuinely default-off "pending validation", and the persisted value won ever after.
- **Measured cost: +2.88% guest throughput** just from turning them on (same session, same build, equal 40°C starts,
  Burnout uncapped, 11/11 intervals favouring ON, +2.4% to +3.8%). With them on the census becomes
  `shift=6962 mask=9811 general=2072 generic=611` — **96.9% were on the slow path for nothing.**
- **`hir_known_bits_mask_fold` is also `false` there**, despite describing itself as DEFAULT-ON, bit-exact and
  differential-tested. Assume there are others; audit the whole file.
- **⇒ EVERY device measurement taken before this was on a handicapped baseline** (including all of 2026-08-05/06).
  Before any A/B: `run-as jp.xenia.emulator.github.debug cat files/xenia.config.toml` and diff the cvars under test
  against their compiled defaults. A silently-disabled lever does not just cost its own win — it moves the baseline.
- Secondary result: the `UBFM` candidate (the `general` path) is only 2072/19456 = **10.6%** of `rlwinm`s, so that
  lowering is real but modest; turning the existing fastpaths on was worth far more than building it.
- **✅ FULL CONFIG AUDIT DONE 2026-08-06 — 13 MORE were compiled-ON but device-OFF**, all now restored:
  `arm64_use_flat_membase`, `cpu_lockfree_check_global_lock`, `gpu_adpf_performance_hints`,
  `hir_algebraic_identities`, `hir_fold_and_not`, `ppc_cr_logical_self_fastpath`, `ppc_vand_self_fastpath`,
  `ppc_vsplt_swizzle_fastpath`, `vulkan_dynamic_constants_arena`, `vulkan_gate_rt_update`,
  `vulkan_hoist_request_range_lock`, `vulkan_persistent_pipeline_cache` (+`discord`, deliberately left off).
  Backup at `files/xenia.config.toml.bak-20260806`. Re-run the audit with a script that diffs every
  `DEFINE_bool` default against the device file — 700 device cvars vs 475 compiled bool defaults.
  **⚠️ But their perf is FLAT, not a second win:** same-session A/B of the 7 CPU/ARM64 levers as a group gave
  **-0.04%** with MIXED intervals (5/3/3), versus `rlwinm`'s +2.88% at 11/11 on the identical protocol. So the
  protocol discriminates; this group genuinely does not move Burnout title/attract. Several target VECTOR idioms
  (`vand`/`vsplt`) that an attract scene barely exercises, and `ppc_cr_logical_self_fastpath`'s own "~2.6%" claim was
  measured elsewhere — **retest these in BD 3D or a race, not attract.** They are restored because they are the
  compiled defaults and individually correctness-validated, NOT because they were measured to win here.

## 🥇🥇🥇 XENDROID IS THE BAR — IT IS THE BEST STABLE ANDROID XBOX 360 EMULATOR (user, 2026-08-06)
**"always compare hard to xendroid — it's the best stable android xbox 360 emu."** Treat it as the reference
implementation for BOTH stability and image quality, not just compatibility. If a title looks or behaves worse here
than in XenDroid, that is a BUG IN OUR FORK, and the first move is to diff against XenDroid — never to debug from
scratch and never to rationalise the difference away.
- **The comparison is legitimate and cheap:** same device, same Turnip driver, same games. `reference/XenDroid` is a
  clone — `git fetch origin` then `git log --oneline HEAD..origin/HEAD`, and read the relevant subsystem directly.
- **When a game looks glitchy/wrong, the order is:** (1) what did WE change most recently that XenDroid does not
  have — our own new levers and codegen are the newest variables and the likeliest cause; (2) what does XenDroid do
  differently in that subsystem; (3) only then debug it as a novel problem.
- **⚠️ THE PATTERN THAT BIT US TWICE IN ONE DAY: shipping a lever that was DEFAULT-OFF "pending validation".**
  BD's image quality was degraded by VRS 4x4 + fp10 bloom clamp + an MSAA cap (all removed 2026-08-06 on user report
  "gfx are busted"; XenDroid ships full quality). Then `arm64_offset_memory_address_fastpath` was flipped default-on
  the same day and reverted after Gears was reported glitchy. **"Default-off pending validation" means exactly that
  — do not flip it because the code looks equivalent by construction.** Looking correct is not being measured
  correct, and a wrong address fold or a coarse shading rate shows up as WRONG PIXELS, not a crash.
- **Quality levers are NOT free perf.** VRS, fp10-as-unorm10, forced MSAA caps and resolution clamps all trade image
  quality for fps. If XenDroid ships full quality and we do not, we are not faster — we are rendering less.
- **❌ CORRECTION 2026-08-06: the Gears glitching was MIS-ATTRIBUTED to `arm64_offset_memory_address_fastpath`.**
  The revert is live and device-confirmed (`arm64_offset_memory_address_fastpath = false` in the persisted config)
  and **Gears is still broken** — menu entries render as horizontal white/black smears over a correct background
  (`scratchpad/menu.png`, 23.7 fps, live user session). So that cvar was never the cause and the revert fixed
  nothing. **This is not a regression from 2026-08-06's codegen work at all** — treat it as a standing GPU-side
  divergence from XenDroid, which renders the same menus correctly.
  - Ruled out by reading + live config, NOT by guessing: `a64_vmx_fp_no_operand_copy = false` (off), and
    `cpu_backend_llvm = false` so the a64 backend is what runs. The one always-on new codegen lever is
    `a64_three_operand_shifts = true`, and its diff is semantically identical to what it replaced
    (`mov w0,src2; mov dest,src1; lsl dest,dest,w0` → `lsl dest,src1,src2`; the constant path stages through `w0`,
    which the allocator never hands out — it only allocates x22-x28). No defect found by reading.
  - **The symptom shape points GPU-side:** correct background + horizontally smeared glyphs confined to the menu
    bars is the signature of wrong row pitch / stride when sampling or resolving a small text surface — i.e. the
    EDRAM resolve + texture-tiling area where we diverge most from XenDroid. **Not asserted, not yet measured.**
  - **Next step is one isolation launch, not more reading:** `baacdeaed` made today's always-on codegen isolable
    (`--ez a64_three_operand_shifts false` etc.). If the smears survive with all of it off, it is GPU-side and the
    in-pass resolve port is the place to look.

## 🧲🧲🧲 XENDROID UPSTREAM PORT TRACK (swept 2026-08-06, clone was 27 commits behind)
**`reference/XenDroid`, `git fetch origin` then `git log --oneline HEAD..origin/HEAD`.** XenDroid vendors
xenia-edge (Canary-derived) and our GPU has diverged heavily (BD native renderer, ROAA path, EDRAM work), so
**cherry-picks do NOT apply — port by hand.**
- **✅ TAKEN:** `4b416cd83` keep-screen-on (`FLAG_KEEP_SCREEN_ON`, 3 lines). Gamepad play emits no touch events so
  the panel sleeps; here that stops the activity, the SurfaceView loses its surface, and the presenter silently
  DROPS every guest frame while the emulator runs on — reads as a hang, already cost a full session.
- **❌ REJECTED, not applicable:** `428008f0e` cached-band process pinning. XenDroid runs the emulator in a separate
  `:emu` process and binds it to a `MainAliveService` with `BIND_IMPORTANT` so lmkd stops reaping the launcher. **We
  already run in the main process**, so there is no cached-band launcher to protect.
- **🛑🛑🛑 STOP — THE IN-PASS RESOLVE CHAIN HAS AN UNLISTED PREREQUISITE WE DO NOT HAVE: DYNAMIC RENDERING.**
  **Discovered 2026-08-06 while porting `a0aec42ae`; it rescopes this entire track and supersedes the 16-commit
  plan below.** `VK_KHR_dynamic_rendering_local_read` is an extension *to* `vkCmdBeginRendering` — it lets a
  fragment shader read the CURRENT attachment on-tile via `VkRenderingInputAttachmentIndexInfoKHR`. **It has no
  meaning inside a traditional `vkCmdBeginRenderPass`.** And measured in our tree:
  - **We have ZERO dynamic rendering.** No `vulkan_dynamic_rendering` cvar anywhere, and **zero
    `vkCmdBeginRendering` call sites**. We use traditional render passes in 9 places
    (`vulkan_command_processor.cc` ×5, `vulkan_render_target_cache.cc` ×2, `bd_native_renderer.cc`,
    `deferred_command_buffer.cc`).
  - **XenDroid uses dynamic rendering** (`CmdVkBeginRendering` in their command processor + deferred buffer), and
    their `vulkan_dynamic_rendering` cvar traces back to **`38e2332e7`, a tree-RENAME commit** — i.e. it is
    long-standing xenia-edge/Canary infrastructure that **predates the 27-commit sweep entirely**. It is not one of
    the 16; it is a foundation their fork always had. **This is the master-vs-Canary gap again, exactly as the
    directive above describes.**
  - **Concrete blast radius:** `VulkanRenderTarget::kColorDrawLayout` is used in **14 places in our fork versus 1
    in theirs** — BD custom-resolve passes, producer/resolve subpass refs, explicit `initialLayout`/`finalLayout`
    pairs. All of that is traditional-render-pass machinery that the local-read model replaces rather than extends.
  - **⚠️ QUALIFY THE STEP-1 CLAIM:** `7c38a62b3` was recorded as "device-verified", and the *extension and feature
    bits* genuinely are enabled on Turnip 26.3.0 — but **we enabled a capability we cannot structurally use yet**,
    and we took only its device half. Its render-target-cache half (+28 .cc / +45 .h — `local_read_attachments_`,
    `color_draw_stage_mask_/access_mask_/layout_`, the `vulkan_in_pass_resolve` default-off cvar, and a
    static→instance `GetDrawUsage` refactor) was **never ported**, which is why `a0aec42ae`'s Initialize block
    references members that do not exist here. Good news if it is resumed: we have only **ONE** caller of the
    static `GetDrawUsage` form (`vulkan_render_target_cache.cc:2962`), so that refactor is cheap.
  - **⇒ THE REAL ORDER IS: port dynamic rendering FIRST (a large, separate track), then step 1's RTC half, then
    the chain below.** Do not start `a0aec42ae`'s two logic blocks until dynamic rendering exists — they cannot
    work without it, and compiling is not the same as functioning.
  - **✅ STEP 0 OF THE PREREQUISITE IS DONE (`7fcc4b8c1`): `dynamicRendering` is now REQUESTED and EXPOSED.**
    It is Vulkan 1.3 core and we already read `features_1_3.supported.dynamicRendering` — but only to print it in
    a log line. **So step 1 (`7c38a62b3`) enabled `dynamicRenderingLocalRead` while the feature it EXTENDS was
    never requested.** Now it is a real `Properties` bit, so the resolve work gates on a capability instead of an
    assumption. Additive, no callers, zero behaviour change, ~115 render-pass sites untouched.
  - **➡️ THE NEXT ADDITIVE SLICE (still no layer swap):** `CmdVkBeginRendering(const VkRenderingInfo*)` +
    `CmdVkEndRendering()` in `deferred_command_buffer.{h,cc}`. **The work is the SERIALIZER, not the call** —
    `VkRenderingInfo` is variable-length (`pColorAttachments` array + optional depth/stencil), so it must be
    flattened into the command stream the way `CmdVkPipelineBarrier` already flattens its three barrier arrays.
    **Copy that function's shape**; do not hand-roll a new pattern. Still zero callers afterwards.
  - **📏 THE PREREQUISITE, SIZED (2026-08-07) — so "blocked" is a cost, not a hand-wave.** Our traditional
    render-pass surface across `src/xenia/gpu/vulkan`: **9** `CmdVkBeginRenderPass` + **16** `CmdVkEndRenderPass`,
    **18** `vkCreateRenderPass`, **10** `VkRenderPassCreateInfo`, **15** `VkSubpassDescription`, **13**
    `vkCreateFramebuffer`, **34** `VkFramebuffer` — roughly **115 direct API sites**, plus **243 `render_pass`
    references inside the 12,708-line `vulkan_render_target_cache.cc` alone.
    **Dynamic rendering deletes the render-pass/framebuffer/subpass objects entirely** (`vkCmdBeginRendering`
    takes attachments inline), so this is not additive — it replaces a layer that our BD native renderer, custom
    resolve passes and ROAA path are all built on. That is why it is a track and not a patch: ~115 call sites is
    tractable, but every one of them is load-bearing for a fork whose GPU work is its main divergence from
    upstream. **Sequence it as its own multi-session unit with its own cvar and rollback, never bundled with the
    resolve chain it unblocks.**
  - **Already landed and still valid, just blocked behind this:** `e4de1497f` (shaders, bytecode byte-identical)
    and `470e505bf` (declarations + local-read command + function pointer, no callers, behaviour-neutral).
- **🚧 THE CHAIN ITSELF (blocked on the above): in-pass EDRAM resolves (16 of the 27 commits).** Step 1's device
  half landed: `dynamic_rendering_local_read=true` on Turnip 26.3.0.
  **Why it is the biggest GPU win available:** xenia's EDRAM resolve ENDS the render pass, copies, and begins a new
  one. On a TBDR every pass begin is a GMEM store+reload — which our own pass budget already fingered (61 passes in
  a BD field frame, **45 of them single-draw**). `local_read` reads the CURRENT attachment on-tile so the pass never
  breaks. This is the mechanism the `[[xendroid-xenia-edge-findings]]` memory blames for XenDroid outrunning us.
  **Port order:** `7c38a62b3` (✅ done, extension+feature) → `ef3b90b1d` (in-pass fragment resolve shaders reading the
  source attachment on-tile; xesli/glsl split + `gen_android_spirv`) → `a0aec42ae` (~632 lines, the resolve path
  itself + deferred_command_buffer support) → hardening: `1f24328cf` (reject resolves whose mapped rect leaves the
  render area), `81f051616` (eligibility + local-read layout consistency), `d3b9ddef0` (keep loadOp discard bits out
  of framebuffer/pipeline keys), `cc0753a8c` (scope input-attachment index mapping per draw), `554b1f4e8`/`5e6ec4915`/
  `2ec7ee046` (destination matching, strip row offset from dst bpp, 2D texel origin), `c13b9be1f` (gate by proven
  roundtrip class **and count every rejection**).
  **✅ SHADER STEP `ef3b90b1d` IS PORTED AND COMPILE-VALIDATED (2026-08-06) — see the corrected recon below.**
  **🔧 SHADER-LAYER DIVERGENCE — THE REAL RULE (corrects the first recon, which was wrong):** the shader sources
  are the same size but differ throughout. My first pass called this "positional vs named args" — **that was
  wrong**. The actual divergence is that **XenDroid's xesl resource macros carry one EXTRA trailing `msl_index`
  parameter**, because XenDroid also targets Metal and we do not. Compare the impls:
  ours `byte_buffer_wo_declare_xe_impl(value_type, name, glsl_set, glsl_binding, hlsl_t, hlsl_t_space)` expanding
  to `layout(std430, glsl_set, glsl_binding)`; theirs adds `, msl_index` and expands to
  `layout(std430, set = glsl_set, binding = glsl_binding)`. **So the translation rule is mechanical: drop the
  trailing arg and pass `set=N, binding=M` instead of bare `N, M`.** Nothing else changes.
  **Everything else ports VERBATIM** — type macros (`float4_xe`, `uint2_xe`, `saturate_xe`,
  `float_bits_to_uint_xe`, `pack_half_2x16_xe`), the Xenos format enums, `push_const_xe`, `pass_push_consts_xe`,
  `XeResolveGetInfo`, `XeResolveDestPixelAddress`, `XeResolveSwapRedBlue_*`, `XeEndianSwap32/64` and the
  `byte_buffer_align*_store*_xe` stores all exist here under the same names.
  **⚠️ Our shaders are CRLF; the Write tool emits LF.** Convert new shader files or the diff is unreadable and the
  extraction guards misfire.
  **How `ef3b90b1d` was validated with NO device:** it is mostly a REFACTOR — 95 lines move out of
  `resolve_host_color.xesli` into a new `resolve_host_color_pack.xesli` so the compute and in-pass paths share
  them. Our lines 180-273 were **byte-identical** to their extraction, so the move was mechanical; then
  `python tools/build/gen_dhr_spirv.py` recompiled all 90 existing variants and **every generated bytecode header
  came out byte-identical**, which is hard proof the refactor is behaviour-preserving. The new fragment entry was
  then compile-checked in all 8 variants (32/64bpp × MSAA × scaled) plus the vertex shader. **Use this pattern for
  the rest of the port: regenerate and diff the bytecode — an unchanged header set is a real regression test.**
  **Deliberately NOT done yet:** the new shaders are NOT wired into `gen_dhr_spirv.py`'s `variants()`, so they
  compile to nothing in a normal build and add zero build surface. Wire them up IN the consumer commit
  (`a0aec42ae`), not before.
  **Toolchain is present:** `glslc.exe` ships in the NDK at
  `~/AppData/Local/Android/Sdk/ndk/25.0.8775105/shader-tools/windows-x86_64/`, and `tools/build/gen_dhr_spirv.py`
  already drives shaderc (glslc/spirv-opt/spirv-dis) to emit the `shaders/bytecode/vulkan_spirv/*.h` headers we
  `#include`. There is no `gen_android_spirv.py` here and we do not need one.
  **⚠️ Port `c13b9be1f`'s rejection counter WITH the path, not after** — it is how they made this safe to ship
  default-on. A resolve that silently declines is invisible; one that counts its rejections is debuggable.
  **🗺️ PORT MAP FOR `a0aec42ae` (the consumer) — scoped 2026-08-06, do not re-derive this.**
  Divergence measured, so nobody wastes time on `git apply` again: our `vulkan_render_target_cache.cc` is
  **12,708 lines vs their 8,000** (~20,700 differing lines), and `vulkan_command_processor.cc` is 13,668 vs 9,193.
  **A patch-apply is impossible.** But the port is far more tractable than that implies, for two reasons:
  1. **All 7 anchor functions exist in our file, exactly once each** — `Initialize` (our :564),
     `Shutdown` (:1437), `GetDirectHostDepthResolvePipeline` (:1996), `~VulkanRenderTarget` (:3926),
     `CreateRenderTarget` (:3968), `GetTransferShader` (:6719), `Resolve` (:321). Our 4,700 extra lines are the BD
     native renderer / EDRAM work living elsewhere in the file, NOT rewrites of these.
  2. **466 of the 632 lines are TWO self-contained new blocks**, not scattered edits: **+284** after
     `GetDirectHostDepthResolvePipeline` and **+182** after `GetTransferShader`. The rest is small and
     mechanical — `Initialize` +84 (setup), `CreateRenderTarget` +37/-4, `Shutdown` +19+1 (teardown),
     `Resolve` +10/-1 (the actual hook), `~VulkanRenderTarget` +5, `namespace shaders` +5.
     Supporting files are tiny: `deferred_command_buffer.{cc,h}` +17/+19, `vulkan_shared_memory.h` +15,
     `vulkan_command_processor.{cc,h}` +25/-7 and +2.
  **✅ FOUNDATION SLICE OF `a0aec42ae` IS LANDED AND COMPILE-VERIFIED (2026-08-06, +96 lines, BUILD SUCCESSFUL).**
  Everything except the two big `.cc` blocks: the `vulkan_render_target_cache.h` declarations (+35, matching their
  diffstat exactly), the `deferred_command_buffer` local-read command, `VulkanSharedMemory::MarkInPassWrite`,
  `kStorageBufferFragment` + `in_render_pass()`, and the device function-pointer plumbing. Nothing calls any of it
  yet, so it is behaviour-neutral by construction. **Two real divergences the compiler caught — both are traps for
  the remaining work:**
  1. **⚠️ THE KHR-SUFFIX TRAP, CONFIRMED AGAIN (it also bit step 1).** Our `third_party/Vulkan-Headers` is **1.3,
     `VK_HEADER_VERSION 278`**, so ONLY the suffixed spellings exist: `vkCmdSetRenderingInputAttachmentIndicesKHR`,
     `VkRenderingInputAttachmentIndexInfoKHR`,
     `VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO_KHR`. XenDroid is on 1.4 headers and uses the
     UNSUFFIXED core names throughout — **every one of them must gain `KHR` when ported.** Grep the upstream hunk
     for unsuffixed Vulkan 1.4 symbols before compiling, not after.
  2. **We have no `in_render_pass_` bool.** XenDroid keeps one; our fork tracks the pass by handle
     (`current_render_pass_`, vulkan_command_processor.h:2446), and our own code already spells the test as
     `current_render_pass_ != VK_NULL_HANDLE` (vulkan_command_processor.cc:5277). `in_render_pass()` derives it.
     Expect more of this shape in the remaining blocks — upstream state that we model differently.
  **Also new: `functions/device_khr_dynamic_rendering_local_read.inc`** — we did not load the entry point at all
  before, only the extension/feature bits from step 1. It is gated on `ext_1_4_KHR_dynamic_rendering_local_read`.
  **⛔ DO THE REMAINING TWO BLOCKS ONLY IN A SESSION WITH THE DEVICE FREE.** They are ~632 lines going into
  the single file where our fork diverges most, and it is exactly the change class that already burned us twice in
  one day (VRS/fp10/MSAA quality levers, then `arm64_offset_memory_address_fastpath`) — wrong pixels, not a crash.
  A gradle build compiles it, but compiling proves nothing about a resolve path. Land it default-off, with
  `c13b9be1f`'s rejection counter in the same commit, and validate on-device before flipping.
- **✅ `904374971` IS FULLY PORTED (2026-08-06, BUILD SUCCESSFUL) — but `vulkan_hoist_shmem_uploads` is DEFAULT-OFF
  and NEEDS ONE DEVICE CHECK.** Upstream ships it ON; it is off here per the standing rule, because reordering an
  upload relative to a render pass fails as WRONG PIXELS, not a crash — the same failure mode as the two
  regressions of 2026-08-06. **Flip it to true after checking one title with `--ez vulkan_hoist_shmem_uploads
  true`.** It is independent of the blocked dynamic-rendering track (grep of the upstream commit for
  BeginRendering/local_read = 0 hits).
  Two more of our-fork-differs-from-theirs, both caught by the compiler: **we have no `command_stream_size_`
  scalar** (our `DeferredCommandBuffer` keeps a `command_stream_` vector, so `empty()` tests
  `command_stream_.empty()`), and **we have no `VkFrameSyncStats`**, so that commit's public accessor for it was
  skipped as XenDroid-specific instrumentation. Our `UploadRanges` also takes a vector rather than pointer+count
  and has no debug markers, so the hoist branch was adapted rather than copied.
- **⭐ STANDALONE, NOT part of that chain — take it independently:** `904374971` hoist shared-memory uploads out of
  render passes. Pages never invalidated **since the current GPU submission opened** cannot have been read by an
  already-recorded command, so their upload can be reordered to the submission HEAD and the render pass never
  breaks. Needs: submission-scoped `invalidated_in_submission` tracking + `OnGpuSubmissionOpened` in shared_memory,
  a submission-head deferred command buffer, and routing eligible uploads to it (~162 lines).
- **❌ `83cf6fa0d` REJECTED, NOT APPLICABLE (2026-08-06).** It makes `disable_context_promotion` also turn off
  `context_promote_vec128`. **We have NEITHER cvar** — both are absent tree-wide. Our `ContextPromotionPass` is
  added unconditionally (`ppc_translator.cc:207`) and gates on entirely different cvars
  (`ppc_cross_block_*`, `arm64_context_promotion_*`). Porting it would mean inventing both levers first, which is
  writing a feature, not taking a fix.
- **✅ `1c0285b47` ADAPTED AND LANDED (2026-08-06, BUILD SUCCESSFUL) — frontend launch.** Theirs is Kotlin/Compose
  against XenDroid-only infrastructure (`FrontendLaunch`, `ACTION_LAUNCH_GAME`), so the code does not transfer;
  the capability does. Ours is an `ACTION_VIEW` intent-filter (file+content schemes, iso/xex/m3u/zar) plus
  `maybeLaunchFromFrontendIntent()` in `LauncherActivity`.
  **🔑 THE POINT THAT MATTERS: the filter is on `LauncherActivity`, NOT `EmulatorActivity`.** `launchGame()` is
  where title-ID resolution, `GameProfiles` and `XeniaOptimizations` are applied, so a frontend launching the
  emulator directly would **silently bypass every per-game fix** — exactly the stale/skipped-lever class this file
  keeps warning about. Handles re-launch via `onNewIntent`, consumes the intent once (sets it to `ACTION_MAIN`) so
  a configuration change cannot double-fire it, and reuses the existing `persistReadPermission()` helper for
  `content://` grants. **Untested on device** (Thor was offline): needs one launch from Daijishō/ES-DE.

## 💥 BURNOUT MID-GAMEPLAY FAULT STORM = LLVM WRITING x20 (2026-08-06) — how to tell it from a JIT bug
**Symptom:** frozen half-drawn frame, `0.0 FPS`, and `UNHANDLED host fault ... re-fault (signal storm)` repeating at
one pc until the thread parks. Looks like "the renderer broke"; it is not — the emulator died mid-frame and the last
frame stayed on screen.
- **Decode the instruction before blaming anything.** `insn=0xA944D296` = **`LDP x22, x20, [x20, #72]`**, with
  `x20_ctx=0` and `fault_addr=0x48` (=72). So x20 was ALREADY null; that instruction is merely where it faults.
  `x21_membase` was still VALID, which rules out a membase/addressing bug.
- **x20 is the RESERVED guest-context register.** LLVM is handed `+reserve-x20,+reserve-x21` so it can never use it.
  The faulting instruction uses x20 as a BASE and WRITES it via Rt2 — a reserved register used as general-purpose.
- **🔑 THE DISCRIMINATOR — this is how you prove it is LLVM and not the a64 JIT:** the a64 backend references x20 in
  **exactly one shape**, `stp/ldp(x19, x20, ptr(sp, 0x00))` in the host↔guest thunk (`a64_backend.cc:1177/1234`),
  **always with SP as base**. It never emits an `ldp` using x20 as a base. So any fault whose instruction bases on
  x20 did NOT come from a64. Use this before suspecting a64 codegen changes.
- **⚠️ `cpu_llvm_no_runtime_compiles=true` does NOT protect against this** (it is set, on device and by default). It
  stops LLVM *compiling* during gameplay; this is bad code LLVM produced during the LOAD WINDOW and executed later.
  **That is a real gap in the mitigation**, not a misconfiguration.
- **Bisect in one flag:** relaunch with `cpu_backend_llvm=false` (a64 only). Stable ⇒ LLVM-side, confirmed.
- **Fix directions:** verify the `target-features` attribute reaches EVERY function (it is applied per-function via
  `fn->addFnAttr`) and that no pass drops it; and/or add a structural guard that scans LLVM-emitted code for writes
  to x20/x21 and rejects those functions to the a64 fallback.
- **Do not change LLVM instruction selection while triaging this** — `cpu_llvm_target_features_native` was flipped
  default-on the same day and deliberately reverted to default-off for exactly this reason.

## 🧩 THE STALE-CONFIG TRAP HAS A SECOND FORM: THE GUI NEVER SETS IT (2026-08-06)
Beyond "the persisted config overrides the compiled default", there is a second way a validated lever stays off:
**`XeniaOptimizations` has no entry for it, so a GUI launch never passes it at all.** Found with
`cpu_llvm_target_features_native`, and it was costing the whole LLVM backend.
- **`opt_llvm_backend` is `defaultEnabled=true`, so a GUI launch DOES run the LLVM recompiler** — confirmed live in
  logcat (`LLVMbegin`/`LLVMseq`) even though the persisted config says `cpu_backend_llvm = false`, because a launch
  extra beats the config. **But there was no toggle for the CPU features**, so LLVM compiled every function for
  **generic armv8-a**: no UDOT/SDOT, no EOR3/BCAX, no LSE atomics, on a SoC that has all of them.
- **`+sha3` is a NEON win, not a crypto one.** LLVM fuses `(a^b)^c` → one `EOR3` and `a ^ (b & ~c)` → one `BCAX`,
  which is exactly what VMX bitwise chains lower to. **This reframes the EOR3 DEAD verdict:** that census counted
  hand-written V128 XOR chains in the a64 backend HIR and found none. LLVM does the fusion automatically across all
  vector code it emits — a different route to the same instruction, needing no HIR pass.
- **Fixed:** default flipped to true (exact-semantics only: integer/atomic/bitwise; FP16/BF16 deliberately excluded
  per the heuristics-only rule; SVE disables always kept because SVE SIGILLs here), plus a new `opt_llvm_cpu_features`
  entry in XeniaOptimizations so the GUI actually sets it. **The persisted device config must ALSO be cleared** —
  flipping the compiled default alone does nothing while `xenia.config.toml` carries the old `false`.
- **⇒ WHEN AUDITING A LEVER, CHECK THREE PLACES, NOT ONE:** the compiled default, the persisted
  `files/xenia.config.toml`, AND whether `XeniaOptimizations` has an entry that a GUI launch will pass.

## DEVICE-CONFIRMED: `--ez cpu_backend_llvm true` DOES NOT ENABLE LLVM (2026-08-07) - and that explains the heat
  **✅ ROOT CAUSE: `cpu` DEFAULTS TO "any", AND THE "any" BRANCH NEVER CONSULTS `cpu_backend_llvm`.**
  `DEFINE_string(cpu, "any", ...)` (cpu_flags.cc:12), and emulator.cc has two branches:
  ```
  if (cvars::cpu == "arm64") {   // the ONLY branch that tests cpu_backend_llvm
      if (cvars::cpu_backend_llvm && LLVMBackend::IsAvailable()) { LLVM } else { a64 }
  }
  if (cvars::cpu == "any")   { backend.reset(new Arm64Backend()); }   // never tests it
  ```
  **So `--ez cpu_backend_llvm true` ALONE does nothing. You must ALSO pass `--es cpu arm64`.**
  **⚠️ MY ERROR, NOT A CODE DEFECT — correcting what I implied above.** `XeniaAndroidSettings.java:235` DOES
  `putString("cpu", CPU_ARM64)`, so **GUI launches reach the LLVM branch correctly** and the standing AOT+LLVM
  **✅ RECIPE VERIFIED ON DEVICE (2026-08-07).** Adding `--es cpu arm64` took llvm log lines from 0 to 2:
  ```
  LLVMBackend: AOT object cache enabled at '.../files/objcache/objcache_v2_opt2'
  LLVMBackend: ORCv2 LLJIT initialized; LLVM lowers what it can, a64 the rest.
  ```
  So the backend AND `ensureObjectCacheDefaults` both work correctly - the cache is enabled at the right path.
  **⚠️ BUT `LLVMobjload` STAYED AT 0 AGAINST A 264MB CACHE.** The cache exists, is enabled, and never hits.
  **Leading hypothesis (NOT yet confirmed): the cache KEY changed.** llvm_assembler.cc:2434 keys each object on
  `g<addr>_<codehash>_o<opt>r<residency>w<writeback>a<abi>` - i.e. `cpu_backend_llvm_opt`,
  `cpu_backend_llvm_context_residency`, `..._residency_writeback` and `..._residency_abi`. **If ANY of those
  differ from the run that built the cache, every lookup misses and the whole title recompiles** - which is
  exactly the observed 15min / 40C->68C / cache-unchanged behaviour. Next step: log the computed key on a miss
  and diff it against a filename already in `objcache_v2_opt2`.
  **📊 60,606 CACHED OBJECTS, 54,705 OF THEM GEARS', ALL KEY CVARS MATCH - AND STILL ZERO HITS.**
  Inspected the cache directly (no device run needed - the filenames carry the key):
  ```
  objects in objcache_v2_opt2 : 60,606
  addresses in Gears range    : 54,705   (82170000-82B00000)   <- RIGHT GAME
  suffixes present            : o2r0w0a0  (all 60,606)
  live cvars                  : opt=2, residency=false, writeback=false, abi=false  <- ALL MATCH
  ```
  **⇒ TWO PLAUSIBLE EXPLANATIONS ARE NOW DEAD.** It is NOT a cache built from a different title (the
  addresses are Gears'), and it is NOT a changed key cvar (all four match the `o2r0w0a0` suffix).
  **REMAINING CANDIDATES, in order of suspicion:**
  1. **The `code_hash` differs.** The key includes FNV-1a over the guest function's BYTES read via
     `TranslateVirtual`. Anything that changes those bytes between runs - an applied `.patch.toml`, in-place
     decryption, a different module base - changes every hash and misses everything.
  2. **The load path is gated off at runtime.** llvm_assembler.cc:2417 needs `cpu_llvm_object_cache` AND
     `cpu_llvm_object_cache_skip_lowering`. The 'AOT object cache enabled' log line proves only the FIRST;
     `skip_lowering` is a separate flag and the persisted config has it false.
  **The miss diagnostic added in this commit prints the wanted key next to a real filename from the directory**,
  **✅ ANSWERED (2026-08-07): THE GATE SHORT-CIRCUITS - IT IS NOT THE CODE HASH.** Ran the miss diagnostic:
  **`LLVMobjcache MISS` printed ZERO times** in a 50s Gears run with `--es cpu arm64 --ez cpu_backend_llvm true`.
  The diagnostic sits on the `!exists()` branch, so zero lines means execution **never reaches the lookup at
  all** - candidate (1), a differing `code_hash`, is eliminated, because a hash mismatch would have PRINTED.
  **⇒ One of the four conjuncts at llvm_assembler.cc:2417 is false:**
  `cpu_llvm_object_cache` / `cpu_llvm_object_cache_skip_lowering` / non-empty `..._path` /
  `function->has_end_address() && end_address() > address()`.
  The 'AOT object cache enabled at ...' log line proves the 1st and 3rd are fine, **so it is
  `skip_lowering` or `has_end_address()`.** Note `ensureObjectCacheDefaults` DOES set `skip_lowering` true, and
  the persisted config says false - so the next step is to log all four conjuncts once, not to guess between
  them. **60,606 objects and 15 minutes of compile per launch are riding on one boolean.**
  which separates these two immediately: identical-except-hash means (1); never printed at all means (2).
  **❌❌❌ ALL OF THE ABOVE IS WRONG. THE CACHE WAS NEVER BROKEN — DEVICE-MEASURED 2026-08-07, GATE LINE READ.**
  ```
  LLVMobjcache GATE: object_cache=1 skip_lowering=1 path_set=1 has_end=1 end_gt_start=1
  ```
  **All five conjuncts are TRUE.** In the same run: **28,775 `LLVMobjload` hits, exactly 1 MISS**, the whole
  AOT precompile of **28,478 functions in 9.5 seconds** (17:00:54.497 -> 17:01:04.010), title reached
  ("Gears of War", hash 1B591620508434A2), **zero faults**. Launch:
  `--es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true --ez cpu_llvm_target_features_native true`.
  **⇒ THE DEFECT WAS IN THE MEASUREMENT RECIPE, NOT THE CODE - AND THE "ZERO OF EVERYTHING" SIGNATURE IS WHAT
  MISLED ME.** The earlier diagnostic run omitted **`--ez cpu_aot_maximize true`**. Trace the structure:
  - `LLVMAssembler::Assemble` (llvm_assembler.cc:2711) returns to the **a64 fallback at :2720** whenever
    `cpu_llvm_no_runtime_compiles && processor()->is_aot_runtime_phase()`.
  - `LowerAndJit` is only called at **:2764**, past that return - and the GATE block, the cache lookup and the
    MISS diagnostic all live INSIDE `LowerAndJit` (from :2334).
  - `aot_runtime_phase_` starts **false** and is flipped true by `EnterRuntimePhase()`
    (processor.cc:1534, called from kernel_state.cc:429) **when the title's main thread launches**.
  ⇒ The LLVM window is **load-time only**. With no AOT precompile pass, nothing compiles during that window,
  every later compile is routed to a64 by the safety gate, `LowerAndJit` is never entered, and therefore GATE,
  MISS and objload are all zero **while the cache is perfectly healthy**. The safety gate was doing its job.
  **🔑 THE TRANSFERABLE MISTAKE: I read "zero of every diagnostic" as "the thing I am measuring is broken",
  when it actually meant "the code containing the diagnostics never ran."** A diagnostic that prints nothing
  cannot distinguish "the gate is false" from "we never reached the gate" - and I had already written that exact
  distinction into this file one paragraph earlier ("never printed at all means (2)") and then read it the other
  way. **Before concluding from silence, prove the enclosing function executed** - one unconditional log at
  function entry, above every early return, would have ended this on the first run.
  **⇒ THE ~15-MINUTE COMPILE WAS AN a64 AOT PASS, WHICH HAS NO OBJECT CACHE BY CONSTRUCTION** (the directory is
  `objcache_v2_opt2`, LLVM-only). That is still a real robustness issue - **any launch that lands on a64
  recompiles ~28k functions from scratch** - but it is NOT a cache bug, and **GUI launches were never affected**
  (`XeniaAndroidSettings.java:235` sets `cpu=CPU_ARM64`, and `opt_aot_precompile`/`opt_llvm_backend` are both
  `defaultEnabled=true`). Only headless `am start` runs missing `--es cpu arm64` ever hit the 15-minute path.
  **🔑 AND A CORRECTION TO THE "CHECK THE PERSISTED CONFIG" RULE:** after this run the config STILL read
  `cpu = "any"` and `cpu_backend_llvm = false`, while the log proves LLVM ran. **Launch extras override at
  runtime WITHOUT persisting.** So the config file shows what a launch INHERITS, not what a launch USED - read
  the log to know what actually ran.
  directive is honoured there. Only my headless `am start` runs were affected, because I never passed `cpu`.
  The persisted config carries `cpu = "any"`, which is what a bare launch inherits.
  **⇒ THE HEADLESS RECIPE NEEDS THREE FLAGS, NOT ONE:**
  `--es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true`. Verify with `LLVMobjload` in the log
  (llvm_assembler.cc:2457) — zero of those with LLVM supposedly on means you are still running a64.
  `XE_LLVM_BACKEND_ENABLED=1` IS set for Android-ARM64 (checked in the generated .mk), so `IsAvailable()` is not
  the blocker.
  **STRUCTURAL HAZARD FOUND WHILE CHASING THIS - THE ENTIRE `--ez` ALLOWLIST IS INSIDE ONE GUARD.**
  `EmulatorActivity.java:139`: `if (intent != null && intent.getBundleExtra(EXTRA_CVARS) == null) {` wraps the
  **whole** ~750-line allowlist block (every `copyBooleanExtra`/`copyIntExtra`/`copyStringExtra`), closing at
  :894 with `intent.putExtra(EXTRA_CVARS, launchArguments)`. **So any launch that ALREADY carries an
  `EXTRA_CVARS` bundle skips every allowlisted extra.** This is the same shape as the object-cache bug this file
  already records as fixed - `ensureObjectCacheDefaults` was deliberately moved OUT of this block for exactly
  that reason (see its comment at :898-901) - but everything else is still inside it.
  **⚠️ HONEST SCOPE: this does NOT explain the headless failure above.** A bare `am start` passes no
  `EXTRA_CVARS`, so the guard opens and the allowlist does run. The LLVM root cause is still open. But it DOES
  mean **a GUI launch (which attaches a bundle via `XeniaAndroidSettings.createLaunchArguments`) ignores every
  `--ez`-style extra**, so the two launch paths do not accept the same configuration - which is worth knowing
  before debugging any 'the cvar did not apply' symptom on either path.
**Measured, not inferred.** Gears launched headless with `--ez cpu_backend_llvm true`:
```
objcache hits (LLVMobjload) : 0
llvm mentions in logcat     : 0
A64Backend: ... init lines  : present
total xenia log lines       : 925   <- logging IS flowing, so 0 is a real zero
```
**The a64 backend ran.** So EVERY "LLVM" measurement attempted this session was actually a64, including the
thermal comparison I drew between them - that comparison is void.
**⇒ THIS IS THE HEAT.** The AOT precompile therefore runs on a64, and the object cache is **LLVM-only**
(`objcache_v2_opt2`, keyed on `cpu_backend_llvm_opt`), so an a64 AOT pass **cannot use or populate it by
construction**. That is a full recompile of the whole title on EVERY launch: ~15 min at 261-340% CPU, 40C->68C
in 30-40s, and `files/objcache` unchanged afterwards - which is now explained WITHOUT needing the
"cache not being written" theory.
**⚠️ ROOT CAUSE NOT YET FOUND.** `cpu_backend_llvm` IS allowlisted in EmulatorActivity, so the extra should
reach the native side. Candidates not yet eliminated: the persisted `cpu_backend_llvm = false` winning over the
launch bundle for this particular cvar (the documented precedence says the bundle wins - verify it actually
does), or the backend being selected before the bundle is applied. **`LLVMobjload` (llvm_assembler.cc:2457) is
the cheap probe for any future claim about LLVM being active** - it needs no new instrumentation.
**Do not quote any LLVM-vs-a64 result from this session.**

## AOT OBJECT CACHE: WHAT IS ESTABLISHED AND THE ONE CHECK THAT SETTLES IT (2026-08-07)
**Every Gears launch today climbed 40C to 68C in 30-40s and one ran ~15 minutes at 261-340% CPU. That compile
is upstream of most measurements worth taking on this device** - it dominates startup, it is the heat, and it
makes any short run unrepresentative. Worth resolving before more micro-optimisation.
**Established by reading (facts):**
- The cache fast path needs THREE things together (llvm_assembler.cc:2417): `cpu_llvm_object_cache` AND
  `cpu_llvm_object_cache_skip_lowering` AND a non-empty `cpu_llvm_object_cache_path`.
- **All three compiled defaults are OFF/empty** (llvm_backend.cc:197/216), and **the persisted device config has
  all three off/empty too**. `XeniaOptimizations` has **zero** references to any of them.
- `EmulatorActivity.ensureObjectCacheDefaults()` sets all three, runs unconditionally before `super.onCreate()`,
  and its early-return cannot be tripped accidentally - `copyBooleanExtra` only inserts a key when the extra is
  actually present (EmulatorActivity.java:1643). So on a launch that does not name these cvars, it SHOULD enable
  them.
**NOT established - and I nearly asserted it:** that the cache is not being written. `files/objcache` stayed at
exactly 264M across a 15-minute compile, which reads as "nothing was written" but is EQUALLY consistent with
"the cache was already complete for this title, so there was nothing new to write". Those imply opposite
actions. **Do not conclude from the size alone.**
**THE ONE CHECK THAT SETTLES IT:** launch and grep our own log for the cache-hit/miss path in
`llvm_assembler.cc` around :2417-2443 (add a counter there if none logs). A run that is recompiling everything
and a run that is loading 264M of objects look completely different in the log, and identical on disk.
**Also worth knowing:** the directory is `objcache_v2_opt2` - `v2` is `kLlvmObjectCacheVersion` and `opt2` is
`cpu_backend_llvm_opt`, so **it is an LLVM-only cache**. An a64-backend AOT pass cannot use it at all, which
means any run with `cpu_backend_llvm=false` recompiles from scratch by construction, cache or no cache.

## 🐌 "XENIA ISN'T RESPONDING" DURING LAUNCH = THE AOT COMPILE, NOT A HANG (2026-08-06)
**Diagnosed live: ~85 functions/sec, GPU 1%, 41°C — all CPU, nothing rendering yet.** Do not force-close it.
- **The UI thread blocks >5s in `Presenter::PaintFromUIThread`** while the emulator thread compiles, so Android
  fires an ANR and the existing AOT overlay CANNOT draw even though its logcat watcher is correct.
- **The progress counters are not monotonic:** the native estimate GREW 6665 → 10540 mid-module, and both counters
  RESET per XEX module (561, 794 for later ones). A raw `done/total` bar jumps backwards and reads as a hang.
  The overlay now shows a cumulative monotonic count and a per-module bar that cannot regress.
- **The pass ignores its own budget:** the log says `budget 1500ms` but it ran ~60s because `drain_frontier=true`
  overrides it. **Fixing the blocking paint OR honouring the budget removes the ANR** — the real fix, still open;
  the tradeoff is a shorter freeze now versus more stutter later, so it needs a deliberate decision.
- **🔑 DISCRIMINATOR CAPTURED 2026-08-06 — THE ASYNC-HANDLER FIX IS THE WRONG HYPOTHESIS, ABANDON IT.** The
  watchdog posts one SYNC and one ASYNC message each tick and reports both latencies. Live Gears session printed
  **`UI thread STALLED sync=2921ms async=2921ms`** — *identical*. Async messages BYPASS Looper sync barriers by
  construction, so **equal starvation proves there is no barrier**: the main thread is genuinely not running, not
  blocked behind a barrier. ⇒ `Handler.createAsync`/`postToUi`/`asyncMain` CANNOT fix this and no amount of message
  flagging will. The overlay is starved because the UI thread is wedged in the blocking paint (above), so the
  remaining real fixes are exactly the two named there. Do not re-attempt async message plumbing.

## 🎯🎯🎯 THE REAL DIAGNOSIS (user, 2026-08-07): THE a64 BACKEND IS A TRANSLITERATION OF THE x64 BACKEND
**"When we ported code we probably ported x64 to arm, but we should have reimagined major functions - PowerPC
to x64 to ported ARM has a ton of inefficiency." This is correct, and it is a sharper framing than the
"scattered idioms" one above. Today's findings are evidence FOR it, not against it.**
**The path that produced our code was `PPC -> (x64 lowering) -> transliterated to ARM64`.** Each x64 sequence
was rewritten instruction-for-instruction into ARM64 rather than asking *how should PPC lower to ARM64*. The
tell is that every defect found today sits in `a64_sequences.cc` / `a64_seq_util.h` and mirrors an x64
structure that has no reason to exist on ARM:
| what we emitted | why x64 needed it | ARM64 reality |
|---|---|---|
| shifts staged через scratch + `mov` | x86 shifts are DESTRUCTIVE and take the count in `cl` | `LSLV` is 3-operand, reads both sources first - **3 insns -> 1** (fixed, `02ae6ec83`) |
| `PrepareVmxFpSources` copies BOTH operands | SSE is 2-operand destructive, must stage | NEON is non-destructive - **2 wasted ASIMD uOPs on the 2-wide pipe, x9 sequences** |
| `FixupVmxMaxMinNan`, 6 uOPs | x86 `MAXPS` returns src2 on NaN, which **disagrees with PPC** | **ARM `FMAX` propagates NaN and ALREADY MATCHES PPC** (PEM p85 + Arm ARM p11115) - the fixup emulates a workaround for a problem ARM does not have |
| `// dest may alias src2` staging | real hazard in 2-operand form | **cannot occur** on a 3-operand ISA |
**The `FixupVmxMaxMinNan` row is the purest case:** PPC->x64 genuinely needed a NaN fixup because x86 disagrees
with PPC. PPC->ARM64 needs NONE, because ARM agrees with PPC. The port carried the workaround anyway, and it
costs 6 uOPs per op on the scarcest pipe. **That is not an idiom that leaked - that is a whole function that
should have been deleted rather than translated.**
**⇒ THE METHOD THIS IMPLIES:** for each hot sequence, do NOT read `x64_sequences.cc` and translate. Read the
**guest** semantics in `docs/reference/ppc/` and the **host** capability in `docs/reference/arm/`, and lower
directly. Where those two agree, the x64 scaffolding is pure waste. **Both manuals are in-repo precisely so this
is checkable rather than guessable** - and note that the two biggest finds of the session came from reading them
AGAINST each other, not from reading our code.
**⚠️ Still measure applicability first (rule 4).** "Reimagine every sequence" is unbounded; the profiler says
per-CALL overhead and a 21M/sec guest busy-wait dominate, so start with sequences that are actually hot.

## ⚠️⚠️ THERE IS NO x86/x64 LAYER TO REMOVE - READ THIS BEFORE PLANNING ANY "STRIP OUT x86" WORK
**Verified in the build 2026-08-07:** `xenia-cpu.prj.Android.mk` contains **0** `x64_` sources, and
`emulator.cc` selects the backend under `#if XE_ARCH_ARM64`. The x64 backend project exists in the tree but
**its sources are not compiled into the Android APK.** The pipeline is already **PowerPC guest -> HIR ->
ARM64 host**, directly. No x86 is emulated, translated through, or executed on the Thor.
**So "rework the x86/x64 layers out" has no target.** What the phrase actually means here - and it IS real -
is **x86-shaped ASSUMPTIONS in shared, architecture-neutral code**. Those are scattered idioms, not a layer:
- `atomic_exchange` implemented as a CAS with no retry (the Win32 branch was correct; POSIX was not) - FIXED
- XMA output published with no release fence, invisible under x86 TSO - FIXED
- `FixupVmxMaxMinNan` reproducing x86 `MAXPS` NaN behaviour that **ARM does not need** - **FIXED 2026-08-07,
  and it was a CORRECTNESS BUG, not just 6 wasted uOPs/op**: on `(SNaN,SNaN)` the fixup emitted a SIGNALLING
  NaN where PPC requires a quiet one. qemu differential + PEM 3.2.5.1; `a64_vmx_native_fmax_nan` now default on
- shifts staged through scratch for x86's 2-operand destructive form - FIXED (`02ae6ec83`)
- the LLVM 2xTBL1 workaround, 3x uOPs for no latency benefit - still present
**Removing ALL of them is worth low single-digit percentages.** They are correctness-and-tidiness wins, and the
two fixed ones were real bugs, but they are not where the 8W-vs-3-5W gap lives.
**🔥 THE MEASURED BIG WINS ARE NOT x86-RELATED AT ALL:**
1. **~~The AOT object cache never hits~~ ❌ RETRACTED 2026-08-07 - THE CACHE WORKS.** Measured with the gate
   logging: `object_cache=1 skip_lowering=1 path_set=1 has_end=1 end_gt_start=1`, **28,775 objloads / 1 miss,
   28,478 functions precompiled in 9.5s**, Gears reached, 0 faults. The zeroes came from a run that omitted
   `--ez cpu_aot_maximize true`, which leaves `LowerAndJit` (and every diagnostic inside it) unreached - see the
   AOT section for the full trace. **This was NOT the order-of-magnitude win; there was no bug to fix.**
   What survives: an **a64** AOT pass has no object cache by construction, so a launch that lands on a64 does
   recompile ~28k functions - but GUI launches always set `cpu=CPU_ARM64`, so only mis-flagged headless runs hit it.
2. **One guest function called ~21M times/sec** (Burnout's D3D wait predicate), 85% of all guest entries, with
   a fastpath hardcoded to TWO title addresses.
3. **One global condvar** woken on every signal, waking every parked guest thread - and the real-time audio
   callback takes that same global mutex every few ms.
**⇒ Spend effort on 1-3, not on hunting for a layer that is not there.**

## 🛑🛑🛑 GEARS ACT 1 GAMEPLAY **STALLS** - 5 THREADS PARKED ON AN EVENT, 0-2 FPS (2026-08-07, NEW BUG)
**Found while A/B-ing a codegen lever; it is far more important than the lever.** In the Act 1 prison sequence
the emulator collapses to **0.0-2.0 fps** with **five guest threads blocked on an EVENT** (XObject type 2 =
`Event`) for 30, 60 and 90 seconds:
```
XObject::Wait: host thread has waited 90s on a 2 (tid=00000012)
XObject::Wait: host thread has waited 90s on a 2 (tid=00000011)
XObject::Wait: host thread has waited 60s on a 2 (tid=00000007)   ... 10 such lines
```
**REPRODUCES ON BOTH ARMS** of the A/B (lever on: 2.0 fps; lever OFF: **0.0 fps**), so it is baseline behaviour,
not a codegen regression. `entry_delta` stays 2.5-3.0M, i.e. the guest is still executing - a few threads run
while the rest wait on an event nobody sets.
**⇒ THIS IS THE SAME CLASS AS THE LOST ODYSSEY STALL** already tracked in this file (threads parked, workers
idle, an un-signalled kernel object) and therefore the same class the Edge kernel port exists to fix. It is a
COMPAT bug, and on current evidence it matters more than any CPU lever: Gears is not really playable past the
opening regardless of how fast the JIT is.
**⚠️⚠️ AND IT INVALIDATES PART OF MY OWN ROUTE CLAIM FROM EARLIER TODAY.** The route DOES reach gameplay
(`entry_delta` 3.5x the title, different hot set) - but **the scene it reaches STALLS, so it is NOT a valid
benchmark scene** and no fps or throughput number from it means anything yet. "Route solved, gameplay
measurement unblocked" was half right: navigation is solved, measurement is not.
**🎯 HYPOTHESIS NARROWED (2026-08-07, code-read, no device): THEY ARE ASYNC-I/O COMPLETION EVENTS.**
Three facts line up, and none of them needed a device:
1. **A guest event does NOT need `NtCreateEvent` to exist.** `XObject::GetNativeObject` (xobject.cc:884) wraps a
   guest `DISPATCHER_HEADER` into an XEvent **on first use** and allocates the handle then, stashing it in
   `header->wait_list.blink_ptr`. So an event can be waited on while never appearing in any create trace - which
   is exactly what we observed.
2. **The stalled handles are LOW** - `F8000010 F8000018 F800004C F80000FC F8000104`. Handles are `slot << 2` plus
   a base, so low values mean EARLY allocation: long-lived subsystem objects created during init, not
   mid-gameplay churn.
3. **This tree already documents the exact churn class for this exact game.** `object_table.cc:203` comment:
   *"fires on EVERY handle add (e.g. **~135/s of async-I/O event churn during UE3 / Gears 3 asset loads**)"*.
⇒ **The five events are most likely overlapped/async-I/O completion events that our kernel never signals** -
the same defect class the Edge kernel port already partially addressed (`XamGetOverlappedResult` honouring
bWait, overlapped event Reset on arm, 25ms dispatch - see the Edge port section). **Gears Act 1 is where UE3
does its heaviest streaming, which is why the stall lands there.**
**✅ NO NEW CODE NEEDED TO CONFIRM IT - the diagnostic already exists.** `ObjectTable::AddHandle` logs
`"Added handle:{:08X} for {typeid}"` at **Debug** level (gated there deliberately, because at Info it cost a
typeid + format + logcat write inside the global critical region on every handle add). So:
```
raise the log level to Debug, run the Gears route, then:
  logcat -d -s xenia:* | grep -E "Added handle:F8000010|F8000018|F800004C|F80000FC|F8000104"
```
The `typeid` on those five lines names the owning subsystem, and the owner is the thing that owes the signal -
which IS the fix site. Do that before writing any new instrumentation.
**⚠️ Debug level is very verbose here** (~135 handle adds/sec plus everything else), so use a big logcat buffer
and expect eviction - the same trap that made the first event-trace attempt look broken.

**🔬 WAIT-TRACE DONE 2026-08-07 - THE FIVE EVENTS ARE NEVER TOUCHED BY GUEST CODE AT ALL.**
Ran `--ez xboxkrnl_event_trace true --ei xboxkrnl_event_trace_budget 6000` plus the wait trace with
`--ei xboxkrnl_thread_wait_trace_after_ms 110000` (delay matters - see the budget trap below). Stall reproduced,
**6,003 event-trace lines captured**, and the five handles the stalled threads are parked on are:
```
F8000010   F8000018   F800004C   F80000FC   F8000104
```
**Every one of them appears ZERO times in the whole trace** - not set, not reset, not pulsed, not mentioned.
The trace covers `KeSetEvent` / `NtSetEvent` / `KeResetEvent` / `NtClearEvent`, so **no guest code ever operates
on these events**, yet five threads block on them for 30/60/90s.
**⇒ THE MISSING SIGNAL IS KERNEL-SIDE, NOT GUEST-SIDE.** Something our HLE owes them - an I/O completion, a
timer, an APC, or an XAM callback - never fires. That is the same shape as the Lost Odyssey stall and squarely
in the territory the Edge kernel port targets. **Next: find who CREATES these handles** (NtCreateEvent /
ObCreateObject trace, or a breakpoint on the handle value) - the creator identifies the subsystem that owes the
signal, which is the actual fix site.
**🪤 BUDGET TRAP THAT COST A RUN: `xboxkrnl_event_trace_budget` DEFAULTS TO 160.** The first trace attempt
emitted exactly 161 lines, all consumed during boot, and showed nothing about the stall - it looked like the
trace "didn't work". It worked fine; it ran out. Raise the budget AND use
`xboxkrnl_thread_wait_trace_after_ms` to push the window onto the event you care about. (Note the event trace has
a budget but NO `after_ms`; only the wait trace has one.)
**Also: all the trace knobs ARE allowlisted** (`..._budget`, `..._after_ms`, `..._guest_tids`,
`..._objects`), so no rebuild is needed to tune them.

**Next step is a wait-trace, not a perf run:** `--xboxkrnl_thread_wait_trace=true --xboxkrnl_event_trace=true`
(the LO workflow in the Edge-port section) to find which event is created-and-waited-but-never-set.

## 🎯🎯🎯 FIRST GEARS *GAMEPLAY* CPU PROFILE (2026-08-07) — AND IT KILLS THE #1 CPU LEVER FOR THIS TITLE
**Captured with the new route, in the Act 1 prison corridor. This is the first gameplay-tier profile for Gears
and the hot set is COMPLETELY different from the title screen's.**
| # | guest fn | delta/5s | code_size |
|---|---|---|---|
| 1 | `sub_8253A088` | 403,371 | 5124 |
| 2 | `sub_825F2D40` | 295,318 | 6512 |
| 3 | `sub_825A5768` | 230,194 | 5968 |
| 4 | `sub_822153F0` | 156,990 | 1608 |
| 5/6 | `sub_82549E78` / `sub_82549F68` | 93,560 each (identical - a caller/callee pair) | 1928 / 10024 |
| 7 | `sub_821911E0` | 60,008 | 6696 |
**🔑 THE LOAD IS DISTRIBUTED, AND THAT IS THE FINDING.** Top-1 is ~403K of ~3M entries = **~13%**. Compare
Burnout, where ONE function (`sub_8238CD28`, the D3D9 wait predicate) is **85% of all guest entries at ~21M
calls/sec**. **Gears has no equivalent busy-wait.**
**⇒ THE "GENERALISE THE WAIT FASTPATH" LEVER - recorded elsewhere in this file as the highest-value CPU work
available - DOES NOT APPLY TO GEARS.** That lever exists because Burnout and Blue Dragon both hammer the same
XDK wait helper; Gears simply does not. Do not port the per-title fastpath table expecting it to help here.
**⇒ What WOULD help Gears is breadth, not a single fastpath:** the top 7 functions together are well under half
the entries, so per-call overhead and ubiquitous codegen quality (prolog, shifts, `rlwinm`, memory ops) matter
more than any one function. That also means Gears is a GOOD title for validating ubiquitous codegen changes and
a BAD one for hunting a single hot spot.
**⚠️ `entry_delta` is NOISY in gameplay** - 5.3M, 547K, 2.2M, 3.0M across consecutive 5s windows (the 547K
window is a load/transition). Average over several windows; do not quote a single sample.
**Thermals for this route: 40C cold -> 74C at 150s.** One run per cooldown, as recorded with the route.

## 🌡️ THERMAL DECOMPOSITION 2026-08-07: THE STARTUP HEAT IS THE AOT COMPILE, NOT THE GUEST
**Measured, and it reframes the watts question.** Gears reaches **70-72C from a 36C cold start in ~2 minutes**
of TITLE SCREEN, cinematics and MENUS - no gameplay at all. But the profiler over that same window shows the
guest barely running: **`entry_delta` ~1.1M per 5s** (Burnout attract, for scale, is ~122-128M/5s - a hundredfold
more), 4,004 functions, top function `sub_822153F0` at delta=97,234.
**⇒ Guest execution CANNOT be what heats the device at the title screen.** The plausible source is the ~9.5s AOT
precompile at load, which runs at 261-340% CPU, plus the GPU holding 30fps - and the panel never gets a chance to
shed it. **So "46->72C in 30s" was never a gameplay figure; it is mostly the compile.**
**⇒ THE NEXT THERMAL MEASUREMENT MUST START AFTER THE COMPILE SETTLES**, not at launch, or it measures the
precompile and calls it gameplay. Take a steady-state baseline once `AOT precompile progress` stops, THEN drive
into a scene.
**✅ SOLVED SAME DAY - GEARS GAMEPLAY ROUTE, device-verified: `tools/thor/gears_gameplay_route.sh`.**
`--es hid nop --es hid_nop_button_sequence 'start@40000:1200;start@47000:1200;start@53000:1200;start@59000:1200;
start@65000:1200;start@71000:1200;a@79000:1200;a@86000:1200;a@93000:1200;a@100000:1200;a@107000:1200;a@114000:1200'`
lands in the **Act 1 prison corridor at ~125-150s**. Format is `buttons@delay_ms:hold_ms`, delays ABSOLUTE from
launch, injected into the GUEST - which is why it works where adb keyevents did not.
**Proof it is gameplay and not a menu:** `entry_delta` **3.6-3.9M per 5s vs ~1.1M at the title (3.5x)**, and the
hot function changes completely (`sub_825A5768` delta=316,123 vs the title's `sub_822153F0` delta=97,234).
**⏱️ BUDGET ONE RUN PER COOLDOWN: 38C cold -> 73C at 150s -> 78C just after.** An A/B is two cooldowns, not two
back-to-back runs - which makes the same-session A/B protocol expensive on this title and worth planning around.
**❌ MY OWN NOTE HERE WAS WRONG, CORRECTED SAME DAY - THERE IS NOTHING TO INVESTIGATE.** I recorded
"`--ei gpu_frame_limit_fps 0` does not uncap Gears (29.6/28.8 fps)" as an open defect. **It is not a defect:
GEARS OF WAR IS A 30FPS-NATIVE UE3 TITLE**, which this repo already documented at `GameProfiles.java:258`
("a 30fps-native UE3 title") before I wrote the note. Uncapping the HOST limiter cannot make a guest that
presents 30 times a second present faster, and the pacing code is plainly correct - `if (frame_limit_fps)` simply
does not pace when the value is 0, and `gpu_adpf_thermal_throttle` (the only cvar that composes with it) is
default-off.
**⇒ The real consequence is the one the protocol already states: FPS IS USELESS AS A CPU METRIC ON A CAPPED
TITLE, so use `entry_delta`** - which demonstrably discriminates here (1.1M title -> 3.9M gameplay, 3.5x).
**The lesson is the recurring one:** I filed "unexplained observation" as "suspected defect" without checking
whether the behaviour was already explained in-tree. One grep of GameProfiles would have answered it.
**Why keyevents failed, so nobody retries them:** alternating START/B skips the cinematics but B then BACKS OUT
of the main menu to the title, and A does nothing on the title. Two attempts, ~35C of headroom each, no gameplay.

**🚧 (superseded) REACHING GEARS CAMPAIGN GAMEPLAY WAS AN UNSOLVED NAVIGATION PROBLEM.**
Two attempts failed: alternating START/B skips the cinematics but then B BACKS OUT of the main menu to the title,
and plain A presses do nothing on the title. Each attempt costs ~2 minutes and ~35C of headroom, so this is not
something to brute-force. **Capture a route first** (`--es hid nop --es hid_nop_button_sequence`, skill
`xenia-blue-dragon-route-capture`) - a Gears route does not exist yet and is a prerequisite for ANY gameplay-tier
CPU or power number on this title.
**🔌 WATTS ARE STILL UNMEASURABLE AND THE REASON IS CONFIRMED FRESH.** `dumpsys battery` reported `status: 2`
(Charging) with USB attached, and five consecutive `current_now` samples on an IDLE device flipped sign:
`-36988, +225591, +165897, +224859, -16846 uA`. Any wattage from a plugged-in session is fiction, exactly as the
watts section says. **adb-over-wifi is now configured (`192.168.1.33:5555`)** so the measurement can be taken the
moment the cable comes out - that is the only remaining blocker for a real 3-5W comparison.

## ✅✅✅ PIXEL-VALIDATED 2026-08-07: BD REMOVAL (~2,511 LINES) + 6 CPU FIXES, GEARS, SCREENSHOTTED
**One APK carrying every change of the session. Gears of War, Turnip, `--es cpu arm64 --ez cpu_backend_llvm true
--ez cpu_aot_maximize true`. Screenshots READ, not inferred from file size:**
| checked | result |
|---|---|
| title screen | correct - crisp logo, clean "PRESS START", correct background |
| opening cinematic (complex 3D city) | correct - geometry, lighting, crisp subtitles |
| combat cinematic | correct - character models, muzzle-flash particles |
| **MAIN MENU** | **correct - NO SMEARING** (see the attribution note below) |
| faults / SIGTRAP / Scudo | **0** across the whole run |
| AOT gate | `object_cache=1 skip_lowering=1 path_set=1 has_end=1 end_gt_start=1`, 28,776 objloads |
| thermals | 35C cold start -> 73C, force-stopped at the limit |
**⚠️ ATTRIBUTION NOT ESTABLISHED FOR THE MENU.** The 2026-08-06 report was menu entries rendering as horizontal
white/black smears; they render correctly now. **That is an OBSERVATION, not a fix I can claim.** Many changes
landed between the report and this run - the whole BD removal, six CPU correctness fixes, and work from other
sessions - and no bisect was done. Do NOT record the menu bug as "fixed by X" without one.
**🔊 THE XMA BUZZING IS NOT FIXED AND IS WORSE THAN RECORDED: 13,534 `non-forward input read offset` warnings in
a ~5 MINUTE run** (the user's whole session was 8,221). Same guard at `xma_context.cc:789`. The release fence and
the audio-priority ports are all installed and this is unaffected by them, exactly as the section below predicts.
**This is now the largest un-fixed defect with a confirmed user-visible symptom.**

## 🔥🔥🔥 MANUAL REVIEW #4 - **THE MAIN GUEST THREAD WAS HARD-PINNED TO A 2.0GHz LITTLE CORE** (FIXED)
**The best lead on the power/heat gap in the whole review, and the purest example of the x86->ARM64 port
diagnosis. Fixed 2026-08-07, NOT yet device-validated.**
`XThread::SetActiveCpu` default path was `thread_->set_affinity_mask(uint64_t(1) << cpu_index)` - guest CPU N to
host CPU N. **On homogeneous x86 that is correct and obvious. This SoC is big.LITTLE** (`thor_topology.h`):
```
kLittleMask = 0x07   cpu0-2 = 3x Cortex-A510 @2.0GHz
kBigMask    = 0xF8   cpu3-6 = 2x A715 + 2x A710 @2.8GHz
kPrimeCore  = 7      Cortex-X3 @3.19GHz
```
| guest CPU | got pinned to | which is |
|---|---|---|
| **0** (on the 360, conventionally the **MAIN GAME THREAD**) | cpu0 | **A510 @2.0GHz** |
| 1, 2 | cpu1, cpu2 | **A510 @2.0GHz** |
| 3, 4, 5 | cpu3, 4, 5 | A715 / A710 |
| - | **cpu7, the X3 @3.19GHz** | **NEVER GIVEN GUEST WORK** |
**⇒ Half the guest CPUs were on little cores and the fastest core in the chip was idle of guest work.**
**🔑 AND IT IS A HARD PIN, NOT A PREFERENCE - THAT IS WHAT MAKES IT BAD.** `set_affinity_mask` is
`sched_setaffinity`, so this did not merely *suggest* a little core, it **forbade Android's EAS scheduler from
ever migrating the main guest thread to a big one.** The OS could not fix it.
**Why it is a POWER bug and not only a speed bug:** a little core at max voltage grinding the hot thread, big
cores idle, every frame taking longer, is strictly worse energy than the same work raced to idle on a big core.
This is the shape of "too much power and heat for how slow".
**FIX (`759e2b59d`):** remap the six guest CPUs onto the big cluster, preserving the distribution the game asked
for so threads it deliberately separated stay separated - `guest 0 -> cpu7 (prime)`, `guest 1..5 -> cpu3..6`.
Guarded by `ThorTopology::IsThorBuild()`. `thor_guest_thread_affinity_mask` already existed as a workaround but
**defaults to 0**, so the 1:1 map is what shipped.
**✅ FULL HARD-PIN AUDIT DONE - the other two are FINE, do not "fix" them:**
- `command_processor.cc:874` pins the CP worker **only when `thor_gpu_thread_affinity_cpu >= 0`, default -1**,
  so it FLOATS - and we ship `gpu_adpf_performance_hints` ON, which tells the OS to boost it. **Float + ADPF is
  BETTER than a hard pin here**: the scheduler can react to thermals, a pin cannot. Hard-pinning the CP thread
  would repeat the mistake above in the other direction.
- `xthread.cc:1235` is the explicit `thor_guest_thread_affinity_mask` path, opt-in.
**⚠️⚠️ A CAVEAT ON MY OWN FIX, WHICH THE A/B MUST SETTLE: `guest 0 -> cpu7` OPTIMISES SPEED, AND THE GOAL IS
ALSO POWER.** The X3 prime is the highest-PERFORMANCE core in the SoC and also the highest-POWER one. The stated
target is rpcs3-like behaviour - **5W and 50C** - not maximum framerate.
- **Race-to-idle favours the big core** for bursty work: finish sooner, drop to idle, spend less total energy.
- **But the guest main thread is not bursty** - it is a ~100%-duty thread for the whole session. For a sustained
  full-duty load the mid cores (A715) usually win on perf/watt, because the prime core's last few hundred MHz
  cost disproportionate voltage.
⇒ **So the fix is unambiguously right about NOT using the A510s, and genuinely uncertain about X3 vs A715 for
guest 0.** The A/B should therefore be THREE arms, not two, and must read thermals and power - not just
`entry_delta`:
| arm | mapping |
|---|---|
| A (before) | 1:1 - guest 0-2 on A510 littles |
| B (now) | guest 0 -> cpu7 X3, guest 1-5 -> cpu3-6 |
| C (worth testing) | guest 0 -> cpu3 A715, guest 1-5 -> cpu4-7 - keeps the hot thread on the efficient tier |
**📕 MANUAL EVIDENCE THAT NOW FAVOURS B (X3) - read the X3 and A715 Table 2-1 side by side:**
| core | FP/ASIMD pipes | source |
|---|---|---|
| **Cortex-X3** | **4** (FP/ASIMD-0,1,2,3) | `cortex-x3-...pdf` Table 2-1, pdf-p12 |
| Cortex-A715 | 2 (FP/ASIMD-0,1) | `cortex-a715-...pdf` Table 2-1, pdf-p13 |
| Cortex-A710 | 2 | `cortex-a710-...pdf` Table 2-1 |
**The X3 has DOUBLE the vector issue width of either mid core**, and reviews #2/#3 establish that our guest is a
128-vector-register machine whose operands we are constantly spilling - i.e. the main guest thread is
vector-bound precisely on the pipe the X3 doubles. **That is also a POWER argument, not only a speed one:** a
vector-bound thread that issues in half the cycles is active for less time, and time-at-load is what the energy
integral multiplies. The naive "prime core = more watts" intuition assumes the same work takes the same cycles
on both, which the pipe counts say is false for THIS workload.
⇒ **B is now the better-supported default and C is the control, not the other way round.** Still measure both -
the pipe ratio is an issue-width argument and says nothing about DVFS voltage curves, which is where the X3's
cost actually lives. But do not assume A715 is the efficient choice just because it is smaller.
(Also from the same tables, for the crypto track: AES/crypto uOPs issue on **3 of 4** FP/ASIMD pipes on the X3
but only **2 of 2** on the A715/A710 - so hardware crypto, if it is ever worth writing, is also prime-core
favoured.)
**If C wins on watts at similar fps, C is the right default for this project's actual goal.** Do not conclude
from `entry_delta` alone; that metric cannot see power, and the whole point of this goal is that we are already
fast enough in bursts and too hot overall.
**⚠️ NEEDS ONE A/B**: same route, thermals + `entry_delta`, old mapping vs new. One-commit revert if it regresses.

## 🧭 THE MEASUREMENT FOR REVIEWS #1 AND #2 ALREADY EXISTED - `arm64_register_allocation_audit`
**I built `a64_vmx_pressure_census` and then found the tree already had a better instrument, already
allowlisted. Recorded so nobody repeats either the duplication or my metric mistake.**
`register_allocation_pass.cc` logs, **per register set (int / float / vec)**:
```
set=<int|float|vec> dest_values= allocation_successes= preferred_attempts= preferred_hits=
preferred_fallbacks= spill_requests= spill_successes= max_active_registers= max_upcoming_uses=
```
- **`spill_requests` / `spill_successes` are the direct answer to "do we run out of registers"** - review #1 for
  `set=int` (7 GPRs) and review #2 for `set=vec` (28 vectors), in ONE run, with no new code.
- **`max_active_registers` is the RIGHT metric and my census measures the WRONG one.** It is peak
  SIMULTANEOUSLY-LIVE values; `a64_vmx_pressure_census` counts DISTINCT registers touched per function, which
  over-counts - a function can touch 40 distinct VMX registers and never hold more than 10 live at once. Only
  the simultaneous figure decides whether the allocator spills.
- It is **host-independent** (it lives in the shared HIR allocator, not the a64 backend), so unlike my census it
  also runs on the desktop x64 build - though the register-set SIZES differ per backend, so the spill counts
  only mean something when the a64 set sizes (7/28) are in force.
- Filter to one function with `arm64_register_allocation_audit_function=<addr>`; both cvars are allowlisted.
**⇒ USE THE EXISTING AUDIT FIRST.** Keep `a64_vmx_pressure_census` only for the one thing it does that the audit
does not: telling you WHICH of the 128 guest VMX registers get touched, i.e. whether the guest's usage is
clustered (cacheable) or spread across the whole 2 KB block (review #3).
**And the spill DESTINATION, which review #5 needs:** `SpillOneRegister` calls `builder->AllocLocal()` and emits
`StoreLocal`/`LoadLocal`, so spills go to the **stack frame**, not to `PPCContext`. That is the exact site Arm's
§4.3 advice applies to - a stack local is still "the cache hierarchy". Implementing GPR-to-VPR spilling means
teaching the allocator that `vec_set` can host a spilled integer, which is shared-code surgery (it would affect
x64 too, where the same trick works via `MOVQ`), not an a64-local peephole. **Scope it accordingly.**

## 📕 MANUAL REVIEW #7 - SINGLE-WORD LANE WRITES COST A 3-CYCLE DISPATCH STALL (A710 4.1/4.2)
**Completes the §4 sweep, and it prices the FixupVmxNan_V128 rewrite that was previously "unmeasurable".**
**§4.2 Dispatch stall, verbatim:**
> *"In the event of a V-pipeline uOP containing **more than 1 quad-word register source, a portion or all of
> which was previously written as one or multiple single words**, that uOP will **stall in dispatch for three
> cycles**. This stall occurs only on the first such instance, and subsequent consumers of the same register
> will not experience this stall."*
**`FixupVmxNan_V128` is exactly that pattern.** Its slow path writes lanes one at a time -
`e.ins(VReg(2).s4[lane], e.w0)` for lane 0..3 - and v2 is then consumed as a full quad-word by the code after
it. Single-word writes feeding a multi-Q-source consumer = **3-cycle dispatch stall**, on top of the scalar
`umov`/`cmp`/branch chain and the stack spills the function already pays.
⇒ **This is the justification the earlier note lacked.** CLAUDE.md previously recorded the branchless rewrite
("result is NaN AND neither input was NaN -> 0xFFC00000", ~8 vector ops, no branch, no lane writes) as an
optimisation that could not be justified without measuring how often NaNs occur. **The dispatch stall is a
structural cost of the LANE-WRITE SHAPE, independent of NaN frequency** - and a branchless vector select never
writes a single word at all, so it avoids the stall by construction. The rewrite is now manual-justified.
**§4.1 Dispatch constraints, for the record:** the A710 dispatches up to 5 MOPs / 10 uOPs per cycle, but with
per-pipe caps - **at most 2 uOPs to V0 and 2 to V1**. So even ignoring execution width, no more than 4 vector
uOPs can DISPATCH per cycle on a mid core, which reinforces reviews #2/#3: vector-heavy emitted sequences are
constrained at dispatch as well as at issue.
**✅ §4 IS NOW FULLY READ** (4.1 dispatch constraints, 4.2 dispatch stall, 4.3 GPR/VPR spills, 4.4 memory
routines, 4.5 load/store alignment, 4.7 region forwarding, 4.8 branch alignment, 4.9 FPCR self-synchronisation,
4.10 special register access, 4.11 register forwarding hazards). **§4 produced every actionable finding of this
review; the instruction tables only priced things already suspected.** Read §4 first next time.

## 📕 MANUAL REVIEW #8 - THE X3 SWOG HAS SECTIONS THE A710 DOES NOT (fusion, zero-latency MOVs)
**The X3 guide's §4 is NOT a copy of the A710's** - it adds 4.5 store-to-load forwarding, 4.6 AES, 4.11
instruction fusion, 4.12 zero-latency MOVs, 4.13-4.15 cache/MTE, 4.16-4.17 SVE. Two matter here, and one of them
REFUTES a worry rather than confirming it.
**§4.12 Zero Latency MOVs - the list is ONLY zeroing idioms:**
```
MOV Xd,#0    MOV Xd,XZR    MOV Wd,#0    MOV Wd,WZR    MOV Hd,WZR
```
*"These instructions do not utilize the scheduling and execution resources of the machine."*
⇒ **REGISTER-TO-REGISTER MOVs ARE NOT FREE, even on the prime core.** I checked this expecting it might undercut
`a64_vmx_fp_no_operand_copy` - if the X3 renamed our `mov v0.16b, v4.16b` staging copies away, that lever would
buy nothing on the core the main guest thread now runs on. It does not: only zeroing forms are eliminated. **The
lever keeps its value on the X3.** Recorded because the opposite is a very natural assumption.
**⚠️ CORRECTION TO THE PARAGRAPH ABOVE - THE A715 SAYS SOMETHING DIFFERENT, AND IT IS CORE-DEPENDENT.**
The A715 guide's equivalent section is titled "Zero Latency **Instructions**" (not MOVs) and its list is far
broader (`cortex-a715-...pdf` §4.12, pdf-p64):
```
MOV Wd,Wn    MOV Xd,Xn    FMOV Sd,Sn    FMOV Dd,Dn    MOV Vd,Vn (vector)
MOVI Dd,#0   MOVI Vd.2D,#0   MOV Zd.D,Zn.D   PTRUE  PFALSE  SETFFR   + the zeroing forms
```
with the caveat *"may not be executed with zero latency under certain conditions"*.
**`MOV Vd, Vn` (vector register-to-register) is EXACTLY what `PrepareVmxFpSources` emits for its staging
copies** - and it is zero-latency on the A715 while the X3's documented list contains only zeroing forms.
| core | reg-to-reg vector MOV | source |
|---|---|---|
| Cortex-X3 | **not listed** as zero-latency | x3 SWOG §4.12 (Issue 4.0) |
| Cortex-A715 | **zero-latency** (with conditions) | a715 SWOG §4.12 (Issue 5.0) |
| Cortex-A710 | no zero-latency section at all | a710 SWOG |
**⇒ THE VALUE OF `a64_vmx_fp_no_operand_copy` IS CORE-DEPENDENT, and the affinity fix decides which core runs
the hot thread.** On an A715 the staging copies it removes are largely free already; on the X3 - where review #4
now routes guest CPU 0 - they are not, per that core's manual. **These two changes interact, and an A/B of the
no-copy lever will give different answers depending on the affinity arm it is run under.** Measure them in the
same arm, and prefer the arm the shipping default uses.
**Do NOT over-read the X3 omission**: the X3 guide is Issue 4.0 and the A715 guide Issue 5.0, so the X3 may
simply not document an elimination it performs. What is certain is what each manual states; treat "the X3 does
not eliminate vector MOVs" as UNCONFIRMED rather than established.

**§4.11 Instruction fusion - the X3 fuses these ADJACENT pairs into one operation:**
`AESE+AESMC`, `AESD+AESIMC`, `CMP/CMN (imm) + B.cond`, `CMP/CMN (reg) + B.cond`, `TST (imm/reg) + B.cond`,
`BICS (reg) + B.cond`, `NOP + Any`. *"These instruction pairs must be adjacent to each other in program code"*,
and fusion is **not** allowed for shifted/extended REGISTER forms.
**✅ VERIFIED CLEAN - we already satisfy this.** Every `EmitCmpImm32` site in the hot paths emits the compare
immediately followed by the branch (a64_emitter.cc 4984/4985, 4990/4991, 5043/5044, 5049/5050, 5361-5363), so
they fuse. The `CMN #1` form `EmitCmpImm32` picks for `0xFFFFFFFF` is in the fusion list too, and its
`CMP #x, LSL #12` form is a shifted IMMEDIATE, not a shifted register, so it is unaffected by the exclusion.
**⇒ THE STANDING RULE THIS CREATES: never insert an instruction between a compare and its conditional branch
in the a64 backend.** It looks harmless and silently costs the fusion on the prime core - exactly the sort of
thing a well-meaning peephole or a debug counter would break. `EmitAtomicIncrement64` between a CMP and a B.cond
would do it.
**Also noted for the crypto track:** `AESE+AESMC` fusion means hardware AES on the X3 is cheaper than the
per-instruction table suggests - relevant if the `xe_crypt_sha_census` ever justifies writing the intrinsics.

## ⚠️⚠️ THE THREE NEW a64 LEVERS ARE NOT INDEPENDENT - MEASURE THEM SEPARATELY
**Found by reading, before wasting a device run on it.** `a64_fpcr_single_mode` **silently disables**
`a64_vmx_fp_no_operand_copy`: turning off FPCR.FZ makes the software denormal flush mandatory
(`flush_needed = true`), and the no-copy fast path in `PrepareVmxFpSources` is gated on `!flush_needed` because
the flush is destructive. **Enable both and you measure single_mode alone, then wrongly conclude the no-copy
lever is worthless.**
| lever | default | interacts with |
|---|---|---|
| `a64_fpcr_single_mode` | off | **disables** `a64_vmx_fp_no_operand_copy` |
| `a64_vmx_fp_no_operand_copy` | off | inert while `a64_fpcr_single_mode` is on |
| `a64_spill_gprs_to_vector` (0-8) | 0 | shrinks the vector set, so it makes `set=vec` pressure WORSE - read the census with it OFF first |
**⇒ A/B protocol for these three: one at a time, from equal thermal starts.** The tempting "enable everything
and see" run cannot attribute anything, and two of the three actively fight each other.
This is the same shape as the standing rule that a `--ez` A/B must pass the FULL validated set or it is
confounded - except here the confound is between the NEW levers, not against the baseline.

## 🚨🚨🚨 MANUAL REVIEW #6 - **EVERY FPCR MODE SWITCH IS A PIPELINE BARRIER, AND WE SWITCH CONSTANTLY**
**The single biggest CPU finding of the manual review, and a textbook guest/host mismatch.**
`cortex-a710-software-optimization-guide.pdf` **Table 4-3, "Special-purpose register write accesses"**:
| Register Write | Non-Speculative | In-Order | Flush Side-Effect |
|---|---|---|---|
| **FPCR** | **Yes** | **Yes** | **Maybe** |
> **Note 2:** *"If the FPCR/FPSCR write is predicted to change the control field values, **it will introduce a
> barrier which prevents subsequent instructions from executing**. If the FPCR/FPSCR write is predicted to not
> change the control field values, it will execute without a barrier but **trigger a flush if the values
> change**."*
And §4.10 explains why: *"most special-purpose registers are **not renamed**"* - so unlike NZCV and SP (note 1:
"fully renamed"), FPCR cannot be speculated around.
**WE WRITE IT ON EVERY SCALAR-FP <-> VMX TRANSITION.** `A64Emitter::ChangeFpcrMode` (a64_emitter.cc:6222) emits
`msr FPCR, x0`, loading `fpcr_vmx` or `fpcr_fpu` from the backend context. Those two values DIFFER -
`DEFAULT_VMX_FPCR = (1 << 24)` (FZ set) vs `DEFAULT_FPU_FPCR = 0` - so **every transition is the
"predicted to change" case: a full barrier on a 13-wide out-of-order core (17-wide on the X3).**
**🔑 THE ROOT CAUSE IS A CLEAN PPC->ARM64 MISMATCH, not a coding mistake:**
| | scalar FP mode | vector FP mode |
|---|---|---|
| **Xenon (guest)** | `FPSCR` | `VSCR.NJ` - **a SEPARATE register** |
| **ARM64 (host)** | `FPCR` | `FPCR` - **the SAME register** |
The guest can run scalar FP in IEEE mode and VMX in flush-to-zero mode SIMULTANEOUSLY, because they are
different registers. On ARM64 there is one FPCR, so we emulate the guest's two modes by SWITCHING it - and the
manual says each switch is a barrier. **A machine with 32 FPRs and 128 VMX registers interleaves these two
worlds constantly**, so this is not an edge case.
**⚠️ COST NOT YET MEASURED - `a64_fpcr_switch_census` ADDED (default off, allowlisted).** It counts emitted
`msr FPCR` barriers per compiled function and reports total, per-function average and worst function every 4096
functions. The existing tracking (`if (fpcr_mode_ == new_mode) return false`) already suppresses redundant
switches WITHIN a block, so the census measures what actually survives that.
**⇒ IF THE COUNT IS HIGH, THE FIXES ARE ARCHITECTURAL AND WORTH REAL WORK:**
1. **Find a single FPCR value that serves both** - if the FZ difference can be handled another way (software
   denormal flush on the rarer path, which `PrepareVmxFpSources` ALREADY has via `kA64FZFlushesInputs`), the
   barrier disappears entirely.
2. **Batch by mode** - schedule VMX and scalar FP work into runs at the HIR level so transitions per function
   drop, rather than alternating.
3. **Hoist the mode out of the function** - if a function is entirely one mode, set FPCR once at entry.
**This is exactly the class the user asked for: it exists ONLY because the port maps a two-register guest model
onto a one-register host, and nothing about the x64 backend would have surfaced it** (x86 has MXCSR, one
register too, but the x64 backend handles denormals differently and never took this shape).

## 📕🔧 MANUAL REVIEW #5 - **ARM TELLS US TO SPILL GPRs INTO VECTOR REGISTERS, AND WE SPILL TO MEMORY**
**The most directly actionable thing in the SWOGs, and it needs no census to justify - the manual states the
principle outright.** `cortex-a710-software-optimization-guide.pdf` **§4.3 "Optimizing general-purpose register
spills and fills"**, verbatim:
> *"Register transfers between general-purpose registers (GPR) and ASIMD registers (VPR) are lower latency than
> reads and writes to the cache hierarchy, thus it is recommended that **GPR registers be filled/spilled to the
> VPR rather to memory**, when possible."*
**We do the opposite.** Review #1: 7 allocatable GPRs against the guest's 32, and everything else spills to
`PPCContext` - i.e. to the cache hierarchy, which is exactly what §4.3 says not to do.
**The numbers, from the A710 instruction tables:**
| operation | Exec Latency | Throughput | Pipe | table |
|---|---|---|---|---|
| **fill from vector** `UMOV`/`SMOV` (element -> gen) | **2** | 1 | V | 3-33, pdf-p52 |
| fill from memory `LDR` | **4** | 3 | L | 3-13, pdf-p27 |
| **spill to vector** `FMOV` (gen -> low D) | 3 | 1 | M0 | 3-21, pdf-p36 |
| spill to vector `INS` (gen -> element) | 5 | 1 | M0, V | 3-33, pdf-p52 |
⇒ **A fill from a vector register is HALF the latency of a fill from L1 (2 vs 4), and it never touches the cache
at all** - which matters doubly here, because review #3 shows our 2 KB context block is already competing for a
32 KB L1 with a guest that was tuned for a 32 KB L1 of its own.
**Capacity, CORRECTED by test:** one 64-bit slot per vector register, so 28 registers = **up to 28 GPR
slots** against a guest with 32 - not 56. An earlier version of this section claimed 2 lanes per register;
`tools/qemu/gpr_vector_spill_roundtrip.c` shows `FMOV Dn, Xs` ZEROES the upper 64 bits (all AArch64 scalar
FP writes zero the upper lanes), so lane 1 cannot be a second slot unless it is written with `INS`
(latency 5) and lane 0 is then never refilled with FMOV. The implementation already assumed one slot per
register; only the documentation was wrong.
**⚠️ THE HONEST CAVEAT - THROUGHPUT, and it is why Arm wrote "when possible":** `LDR` is throughput **3/cycle**
on the 3-wide load pipe; `UMOV`/`FMOV` are throughput **1/cycle**. So memory wins for BULK spill/fill, VPR wins
for LATENCY-critical single values. The right implementation is therefore selective, not wholesale: spill
short-lived, latency-critical values to VPR and leave bulk state in the context.
**⇒ THIS IS THE CONCRETE "REWORK MAJOR PIECES" ITEM FOR THE INTEGER SIDE**, and unlike reviews #1-#3 it does not
wait on the census - Arm states the principle. It DOES interact with review #2: vector registers are only free
to borrow when the function is not already vector-bound, which is exactly what `a64_vmx_pressure_census`
measures. **So the census now decides scope, not whether to do it at all.**
**Also found in §4.7 "Region based fast forwarding", and it prices something we already suspected:** the A710
splits NEON forwarding into regions and warns *"it is not advisable to interleave instructions belonging to
different regions"*, with a worked example where a `MOV v27.s[1], v20.s[1]` placed between FP ops **costs an
extra cycle**. Our `PrepareVmxFpSources` staging copies are exactly that shape - `mov` interleaved with `fmax`/
`fcmeq` - so they cost MORE than their nominal uOP count, which strengthens `a64_vmx_fp_no_operand_copy`.

## 📕📕📕 MANUAL-DERIVED ARCHITECTURE REVIEW #1: THE REGISTER ALLOCATOR HAS ONE CLASS, AND IT IS THE SMALL ONE
**User directive 2026-08-07: "still way too much power and heat for how slow... rethink major pieces from
xbox360 straight to ARM64. USE THE FUCKING MANUALS." This section is manual-first: every number below is quoted
from a primary source in `docs/reference/`, not from memory.**

**THE THREE REGISTER FILES, from the manuals:**
| | source | integer registers |
|---|---|---|
| GUEST Xenon (PowerPC) | `ppc/powerpc-user-isa-book1.pdf` Figure 2, pdf-p16: `GPR 0 ... GPR 31` | **32** |
| HOST ARM64 | AAPCS: x0-x30 | **31** (10 callee-saved: x19-x28) |
| the x64 host the port came from | - | **16** |

**WHAT WE ACTUALLY ALLOCATE (`a64_emitter.h:79`, `a64_emitter.cc:2306`):**
```
GPR_COUNT = 7      // x22..x28
VEC_COUNT = 28     // v4..v31
// Reserved: x19 backend ctx, x20 guest context, x21 membase
// "Scratch: x0-x18 (caller-saved)"      <- 19 registers, never allocated
```
**❌ FIRST HYPOTHESIS, AND IT IS WRONG - RECORDED SO NOBODY RE-RAISES IT.** `x64_emitter.h` also says
`GPR_COUNT = 7`, which looks like the port copied x64's budget onto a machine with twice the registers. **It did
not.** ARM64 has exactly 10 callee-saved GPRs; minus our 3 reservations that is **exactly 7**. The match with
x64 is a coincidence, and the vector set WAS widened correctly for ARM64 (x64 `XMM_COUNT = 12` -> a64
`VEC_COUNT = 28`). Do not "fix" GPR_COUNT by raising the number - x29/x30/SP are not yours to take.
**✅ THE REAL FINDING: WE HAVE A SINGLE REGISTER CLASS, AND IT IS CALLEE-SAVED-ONLY.** All 19 caller-saved
integer registers (x0-x18) are used ONLY as fixed scratch, never given to the allocator. A conventional
allocator carries two classes - callee-saved for values live across a call, caller-saved for short-lived
temporaries - so a temporary that dies before the next call can use a caller-saved register for free. Ours
cannot: every value competes for the same 7.
**Why that costs, quoted from `arm/cortex-a710-software-optimization-guide.pdf`:**
- §2.1 pdf-p12: the A710 dispatches into **thirteen issue pipelines** after **register renaming**; the X3
  (`cortex-x3-...pdf` §2.1 pdf-p11) into **seventeen**. **Both cores rename**, so ARCHITECTURAL registers are
  cheap - the physical file is much larger - while a spill becomes a real memory dependency renaming cannot undo.
- Table 3-13 (Load instructions, pdf-p27): `LDR` = **Exec Latency 4, Throughput 3**, pipeline L.
- Table 3-11 (Misc data-processing, pdf-p26): `MOVZ/MOVK` , `SBFM/UBFM`, `LSLV/LSRV/ASRV/RORV` = **latency 1,
  throughput 4**, pipeline I.
⇒ **A spill reload costs 4x the latency of the ALU op it feeds**, and the guest has 32 GPRs to squeeze into 7.
**⚠️ NOT MEASURED, AND THERE IS A REAL COUNTER-ARGUMENT.** Caller-saved registers must be spilled around every
guest->host call, and this file already measures **~24.4M guest function entries/sec on Burnout**, i.e. calls are
extremely frequent - so values rarely live long, and a second class might buy little while costing save/restore.
**Do the census before building it** (rule 4): count how many HIR values are live across a call vs die before
one. That counter is cheap; a two-class allocator is not.

## 📕📕📕 MANUAL-DERIVED ARCHITECTURE REVIEW #2: VMX128 IS A 128-REGISTER ISA RUNNING ON 28 REGISTERS
**This is the sharpest guest/host mismatch found so far, and unlike the integer case there is no slack to find.**

**The guest vector file, confirmed in THREE Xbox 360 manuals** (`docs/reference/xbox360/`):
| source | quote |
|---|---|
| Hot Chips 17, pdf-p5 | *"**128 128-bit vector registers per thread**"*; *"2-issue per cycle, **in-order**, decoupled Vector/Scalar issue queue"*; *"VMX128 Units: Floating Point Unit, Permute Unit, Simple Unit"* |
| IEEE Micro 2006, pdf-p3 | *"VMX128 includes **128 registers, of 128 bits**"* |
| CIS501 lecture, pdf-p2 | *"Superscalar: **two-way issue**"*, *"VMX-128 instructions"* |

**The host: ARM64 NEON has 32 vector registers TOTAL. We allocate 28 (v4-v31; v0-v3 scratch).**
⇒ **128 guest -> 28 host is a 4.6:1 squeeze, and there is nowhere to get more.** The integer story (review #1)
at least has 19 unallocated caller-saved GPRs to argue about; here ARM64 simply has 32 registers and we already
use 28 of them.

**🔑 WHY THE GUEST CODE IS REGISTER-HUNGRY BY DESIGN - this is the part that matters.** The Xenon is **in-order**
(Hot Chips 17 p5). An in-order machine hides latency only through software pipelining and unrolling, which is
exactly what a compiler does by keeping many values live at once - and Microsoft gave it 128 vector registers so
it could. **So Xenon-targeted game code does not merely happen to use many vector registers; it was compiled to
use them, deliberately, to cover latency the hardware could not.** We then run that code on a host with 28.

**What a vector spill costs, from `arm/cortex-a710-software-optimization-guide.pdf`:**
| operation | Exec Latency | Throughput | Pipelines |
|---|---|---|---|
| `LDR` vector reg (Q) - a spill RELOAD, Tbl 3-30 pdf-p37 | **6** | 3 | L |
| `ST1` 1 element - a spill STORE, Tbl 3-37 pdf-p57 | 2 | 2 | **L01 + V** |
| (integer `LDR` for comparison, Tbl 3-13) | 4 | 3 | L |
**A vector reload is 6 cycles - 50% worse than an integer reload - and the spill STORE burns one of the only
TWO FP/ASIMD (V) pipes the A710/A715 have** (Table 2-1; the X3 has 4). So vector register pressure does not just
add memory traffic, it competes for the scarcest issue resource on the mid-cores.

**⇒ THE STRUCTURAL STATEMENT, which is what the "straight to ARM64" rethink has to answer:** we are running a
128-register in-order vector ISA on a 32-register out-of-order one. The host's out-of-order engine gives back
some of what the guest's unrolling was buying (we do not need software pipelining), but it cannot give back
REGISTERS, and every value the guest kept live costs a 6-cycle reload plus a V-pipe slot.
**⚠️ NOT YET MEASURED - and the census is the cheap next step, not a rewrite.** Count, per translated function:
peak live V128 values, and how many VMX register reads/writes go to `PPCContext` memory rather than an allocated
register. If most guest functions keep <=28 vectors live, this is theoretical; if the hot ones keep 40+, it is
the dominant cost on the vector path and would reframe the whole VMX effort (which so far has been about
shaving 2-6 uOPs per op - irrelevant next to a 6-cycle spill on every operand).

## 📕📕📕 MANUAL REVIEW #3: THE GUEST'S REGISTER FILE BECAME A 2 KB MEMORY BLOCK - THAT IS THE REAL PORT COST
**This is what reviews #1 and #2 add up to, and it is the sharpest way to state the whole problem.**

**On real hardware the Xenon's registers are IN THE REGISTER FILE** - 32 GPR + 32 FPR + **128 VMX**, zero cache
cost, 2-cycle access. Quoted: Hot Chips 17 pdf-p5 *"128 128-bit vector registers per thread"*, *"L1 Caches: 32K
2-way I$ / 32K 4-way D$"*, *"Shared 1MB L2 cache, 8-way set associative"*; IEEE Micro 2006 pdf-p2 *"The shared L2
allows fine-grained, dynamic allocation of cache lines between the six threads"* and *"The write-through data
cache does not allocate cache lines on writes"*.

**In our port those registers are a STRUCT IN MEMORY.** From `ppc_context.h` (its own offset comments):
```
r[32]    @ 0x020    256 B
f[32]    @ 0x120    256 B
v[128]   @ 0x220   2048 B   <-- the VMX file
...                          PPCContext continues past 0xA20 = 2592 B
```
**The guest's 128 vector registers are 2 KB of memory = 32 cache lines, per guest thread.** We hold 28 of them in
real registers (review #2); the other 100 live in that block and are reached with `LDR`/`STR`.

**⇒ THE STRUCTURAL COST OF THE PORT, stated precisely:** on Xenon, VMX register access was a register-file read.
Here, most of it is a **6-cycle L1 load** (A710 SWOG Tbl 3-30) against a **2 KB working set that competes for L1
with the guest's actual data** - on a guest whose own L1D was only 32 KB and whose L2 was 1 MB shared six ways,
i.e. code already written to be cache-frugal. We added 2 KB of pseudo-register traffic per thread to a workload
that was tuned for a small cache.
**This reframes every micro-optimisation in this file.** Shaving 2-6 ASIMD uOPs off a VMX sequence (the NaN
fixups, the staging copies) is real but second-order next to whether the operands were in registers at all.
**⚠️ MEASUREMENT, NOT REWRITE, IS THE NEXT STEP** (rule 4). The census that settles reviews #1-#3 together:
per translated guest function, (a) peak live V128 values, (b) count of `PPCContext.v[]` loads/stores emitted vs
allocated-register accesses, (c) same for `r[]`. If the hot functions sit under 28 live vectors this is
theoretical; if they run 40+, context traffic is the dominant cost on the vector path and the fix is a
register-allocation/caching strategy, not more peephole work.
**📕 WHAT THE MANUALS CANNOT TELL US, and why the "Qualcomm chipset manual" hunt is a dead end:** the Arm SWOGs
are OPTIMISATION guides - they give per-instruction latency/throughput/pipe, which is exactly what we need for
codegen - but they deliberately do NOT give cache SIZES, because those are chosen by the SoC integrator.
Qualcomm does not publish a CPU TRM for the SM8550 (only a product brief and datasheets), and it does not matter:
**the 8 Gen 2 uses STOCK Arm cores (X3/A715/A710/A510), not custom Kryo**, so the four Arm SWOGs in
`docs/reference/arm/` ARE the authoritative microarchitecture manuals for this chip. The "huge manual" is the
**Arm ARM (DDI 0487H.a, 11,530 pages)**, already in-repo. Read L1/L2 sizes off the DEVICE
(`/sys/devices/system/cpu/cpu*/cache/`) instead of hunting a document that is not published.

## 🧨🧨🧨 THE CONSTANT-OPERAND BUG FAMILY — FOUR FOUND, SWEEP NOW COMPLETE (2026-08-07)
**`SrcVReg` returns the SCRATCH INDEX when an operand is constant** (a64_seq_util.h:271-277): it materialises
the constant into v0/v1 and returns 0/1. So `s1` can BE v0 and `s2` can BE v1 — and any sequence that later uses
v0/v1 as a TEMP destroys its own source. Allocated operands (the allocator only hands out v4-v31) are never
affected, **which is exactly why these survive: the constant path is the one nobody exercises.**
| sequence | what it did | symptom |
|---|---|---|
| `VECTOR_DOT_PRODUCT` f32 fallback ×4 | `fcvtl(v0, s1.low)` then later `fcvtl2(v2, s1.high)` | high half read f64 product bits — **wrong dot product** |
| `VECTOR_ROTATE_LEFT` ×3 type cases | `movi(v0, 8/16/32)` then `ushl(v0, s1, v0)` | **rotated by the shift amount instead of the value** |
| `VECTOR_DENORMFLUSH` | `cmeq(v0, v0, 0)` then two more `VReg(s)` reads | sign bits + "original" read out of the **compare mask** |
| `a64_three_operand_shifts` (earlier) | dropped the `if (i.src1.is_constant)` arm in 7 of 10 sites | heap corruption, Gears dead in <1s |
**All fixed. None crash — they silently compute the wrong number**, which is why no amount of running found them.
**✅ SWEEP IS COMPLETE, DO NOT RE-RUN IT.** Detector: flag `sN = SrcVReg(e, op, K)` where `VReg(K)` is written on
an EARLIER line than a later read of `VReg(sN)`. **Two filters are essential or it is all false positives:**
(1) **same-line write+read is SAFE** — an instruction reads its sources before writing its destination, so
`ushr(VReg(0), VReg(s), n)` with `s==0` is fine; (2) the scan window overruns short sequences, so every hit
needs the enclosing block READ before believing it. After filtering: 3 real bugs, everything else clear.
**Also swept and CLEAN: `ComputeMemoryAddress`**, which has the identical signature hazard (returns `e.x0` for a
constant guest address). 3 candidates, all false positives — two matched the word "addr" inside a COMMENT, and
`add(x0, membase, addr)` with `addr==x0` is same-instruction read-then-write.
**⇒ THE STANDING RULE: any helper that returns a SCRATCH register for constants creates this hazard for every
caller.** There are two such helpers (`SrcVReg`, `ComputeMemoryAddress`). If a third is added, sweep it.

## 🔬🔬🔬 THE x86→ARM64 SWEEP: MEMORY ORDERING IS THE BUG CLASS (2026-08-07)
**x86 is TSO. Stores cannot be reordered with stores, loads cannot be reordered with loads, and almost every
missing fence is INVISIBLE. ARM64 is weakly ordered, so the same code races. This is where the real x64-shaped
bugs live — not in instruction selection.** Two found and fixed in one sweep, both by reading rather than testing,
because a dropped-write or stale-read race is intermittent and a clean run proves nothing.
- **✅ FIXED `1e6db18f5` — `atomic_exchange` silently dropped writes on POSIX.** It was
  `__sync_val_compare_and_swap(value, *value, new_value)`: a NON-ATOMIC read of `*value`, then a CAS against that
  snapshot **with no retry loop**. Another thread writing in between made the CAS fail, store NOTHING, and still
  return a plausible old value. **The Win32 branch was always correct** (`_InterlockedExchange` is an
  unconditional swap) — so the x64 path was right and the POSIX path we ship on Android was not. Both call sites
  are `Processor::RaiseIrql`/`LowerIrql`, the guest interrupt priority level. Now `__atomic_exchange_n(...,
  __ATOMIC_ACQ_REL)`.
  **🔑 THE GENERALISABLE LESSON: check the PLATFORM BRANCH NOBODY PROFILES ON.** Upstream develops and benchmarks
  on Windows/x64; a defect that only exists in the POSIX `#elif` can survive indefinitely there.
- **✅ FIXED `e5398cac8` — XMA output published without a release fence** (XenDroid `902af401d`, whose own message
  says *"x86 never shows this (TSO)"*). The guest mixer polls `output_buffer_write_offset` from another thread and
  reads the PCM behind it; nothing ordered the ring writes against the offset store.
- **❌❌ DEAD, WITH ASM PROOF — "`__sync_*` costs us a full `DMB ISH` on ARM64" IS FALSE. DO NOT REDO THIS.**
  I claimed it, implemented the conversion (`__sync_*` → `__atomic_*` with `ACQ_REL` across
  `atomic_inc`/`dec`/`exchange_add`/`cas`), built it clean — and then checked the emitted assembly instead of
  shipping the claim. **The two forms are byte-identical, and NEITHER emits a `dmb`:**
  ```
  __sync_bool_compare_and_swap  ->  casal   w8, w1, [x2]
  __atomic ACQ_REL CAS          ->  casal   w8, w1, [x2]
  __sync_add_and_fetch          ->  ldaddal w8, w8, [x0]
  __atomic ACQ_REL add          ->  ldaddal w8, w8, [x0]
  ```
  (NDK 25 clang 14, `-march=armv8.2-a+lse -mno-outline-atomics -mtune=cortex-a710 -O2`.)
  **Why:** with FEAT_LSE, clang lowers seq_cst straight onto the acquire-release LSE forms — `casal`/`ldaddal` —
  which are sufficient for seq_cst on ARM64. The `+lse -mno-outline-atomics` flags we already ship are what buys
  this; there is no second win hiding behind the builtin choice.
  **⇒ REVERTED.** The change bought exactly nothing while formally weakening seq_cst to acq_rel — an unjustified
  reduction in correctness margin for zero measured benefit, which is the specific mistake this file keeps warning
  about. **The `atomic_exchange` fix (`1e6db18f5`) is unaffected and stays: that one fixed a real dropped write,
  not an ordering strength.**
  **🔑 THE TRANSFERABLE BIT: for any "this barrier is too strong" hypothesis, DIFF THE EMITTED ASM FIRST.** It
  costs one `clang -S` against a 10-line file, and here it turned a plausible, well-argued, already-implemented
  optimisation into a no-op before it shipped.
- **✅ CHECKED AND CLEAN — do not re-derive these.** Both looked like textbook races and are not:
  `AndroidAudioDriver::shutdown_` is a plain `bool` read in the REAL-TIME AAudio callback, but all four accesses
  (`android_audio_driver.cc:90/110/113/181`) are under `frames_mutex_`, so the mutex supplies the ordering.
  `AudioSystem::paused_` is a plain `bool` written on the emulator thread (`:430`) and read on the audio worker
  (`:165`) in a publish-then-signal shape — `paused_ = true; shutdown_event_->Set();` — which on ARM64 would be a
  classic reordering bug, EXCEPT `Event::Set()` goes through `PosixConditionBase`'s mutex and the waiter
  re-acquires it, giving a real happens-before edge. The only unsynchronised read is the public `is_paused()`
  accessor (audio_system.h:63): a data race by the letter of the standard, benign for a bool, **not worth
  "fixing" blind.**
  **⇒ The lesson for the rest of the sweep: a plain `bool` shared across threads is a CANDIDATE, not a verdict.
  Trace whether a mutex or condvar already carries the edge before changing anything** — an unnecessary atomic on
  a hot flag costs, and a wrong "fix" here is indistinguishable from the bug it claims to fix.
- **🎯 THE ACTUAL SHAPE TO HUNT (refined after checking several false leads).** Plain `std::atomic<T>` with bare
  `.load()`/`.store()` defaults to **`seq_cst`** and is therefore SAFE on ARM64 — verified on
  `CommandProcessor::write_ptr_index_` (gpu/command_processor.h:349), which is a genuine cross-thread ring
  publish and needs no change. **So the bug is almost never "an atomic with weak ordering".** It is:
  **an ATOMIC offset/pointer/flag that correctly publishes, guarding BULK DATA that is NOT atomic and has no
  fence.** That is exactly the XMA case — `output_buffer_write_offset` was fine; the PCM ring writes behind it had
  nothing ordering them. Look for "atomic index + plain buffer", not for atomics in isolation.
  Secondary shape: raw `__sync_*`/`__atomic_*` with an explicitly chosen ordering (those bypass the safe default),
  and hand-rolled primitives like the `atomic_exchange` above.
- **🔴 CONFIRMED RACE (traced 2026-08-07, NOT yet fixed): the GPU register file is written by the GUEST thread and
  read by the CP thread, with no ordering on either side.**
  - **Writer — GUEST thread:** `GraphicsSystem::WriteRegister` ends in `register_file_.values[r] = value;`
    (graphics_system.cc:364), a **plain non-atomic, non-volatile store**. It is reached from
    `WriteRegisterThunk(void* ppc_context, ...)`, i.e. an MMIO handler on the guest CPU thread — the comment at
    graphics_system.cc:319 says "(guest thread)" outright.
  - **Reader — CP WORKER thread:** `CommandProcessor` holds a POINTER TO THE SAME OBJECT
    (`command_processor.cc:971`: `register_file_(graphics_system_->register_file())`) and reads it through
    `const_cast<volatile uint32_t&>` at 8 sites. **`volatile` is not a fence** — it forbids compiler caching, and
    guarantees nothing about hardware ordering.
  - **Why x86 never showed it:** TSO plus naturally atomic aligned 32-bit access. On ARM64 the value still will
    not tear, but the CP thread can observe these register writes **stale or reordered** relative to whatever the
    guest did around them.
  - **⚠️ SCOPE HONESTLY: this is the DIRECT-MMIO register path, not the main one.** Most register state reaches
    the CP inside PM4 packets that the CP thread itself decodes (no cross-thread hazard), and the important
    handoff — `CP_RB_WPTR` — is correctly routed through `UpdateWritePointer` into the `seq_cst`
    `write_ptr_index_`. So this is a narrow window, not a constant corruption source, which is consistent with it
    never having been chased.
  - **The fix is a release/acquire PAIR on the existing boundary, not atomics sprinkled over the array.** Making
    `values[]` `std::atomic` would tax every register access in the PM4 decode path — the hot one — to close a
    hazard that exists only at the MMIO boundary. Prefer: release store on the guest side write, acquire on the
    CP side's first read after a wake. **Measure before changing** (rule 4) — and note a wrong weakening here is
    an intermittent GPU-state bug, i.e. wrong pixels, the worst thing to debug.
- **🔎 PRIOR NOTE, now superseded by the confirmation above — kept for the reasoning:**
  `RegisterFile::values[]` is a plain `uint32_t[kRegisterCount]` (register_file.h:41) and the command processor
  reaches it through `const_cast<volatile uint32_t&>` at **8 sites** (command_processor.cc:1299, 1301, 1324, 1326,
  1357, 1404, 1439, 1709). **`volatile` gives neither atomicity nor ordering in C++** — it is not a fence. On x86
  this works by accident: TSO plus naturally atomic aligned 32-bit access. On ARM64 an aligned 32-bit access is
  still single-copy atomic, so values will not tear, but **nothing orders those accesses against surrounding
  ones.**
  **Do NOT "fix" this yet.** The register writes are almost certainly ordered by the ring-buffer write-pointer
  publish, which IS a `seq_cst` atomic (`write_ptr_index_`) — i.e. the edge may already exist, exactly like
  `AudioSystem::paused_` above. **What to do first: establish which THREAD writes each of the 8 sites.** CLAUDE.md
  already records that `IssueDraw` and `register_file_` are CP-worker-owned, so if all 8 are CP-thread-only this
  is a non-issue and should be recorded as such. Only if a guest/MMIO thread writes while the CP thread reads is
  there anything to fix — and then the fix is an acquire/release pair on the existing publish, not sprinkling
  atomics over the register array.
- **✅ ALSO CHECKED AND CLEAN:** `SharedMemory::system_page_flags_` (the valid / valid_and_gpu_written / invalidated_in_submission bitmaps) is written by the memory-watch path and read by the GPU thread, which is the exact shape to suspect - but every access (shared_memory.cc:311, 378, 463, 485) sits under `global_critical_region_.Acquire()`, so the lock carries the edge. **Running tally of this sweep: 2 fixed, 4 cleared by tracing, 1 confirmed-and-scoped, 1 open perf item.** The 4:2 ratio of clearances to fixes is the point - most candidates already have an edge, and finding it is cheaper than adding a redundant one.
- **WHERE TO KEEP LOOKING (this sweep is not finished):** cross-thread flags that are plain `bool`/`uint32_t`
  rather than `std::atomic`; `volatile` used as if it implied ordering (it does not — it is not a fence);
  publish-then-signal pairs where the publish has no release; and double-checked-locking shapes. Grep entry
  points: `volatile (bool|uint32_t|int32_t)` outside `base/atomic.h` and the MMIO paths, and any `.Store(`/
  `->Set(` that follows a buffer write on a different thread from its reader.

## 🧠🧠🧠 ARM64 EMULATOR PERFORMANCE PLAYBOOK (2026-08-06) — read this BEFORE picking a CPU lever
Distilled from the RPCS3/Whatcookie ARM64 talk (measured on an AYN Odin 2 = OUR SoC) **plus what we then measured
ourselves**. The principles are what transfer; their specific patches mostly do not (see the parity track below).

**0. 🧭 THE FIVE RULES (full synthesis: `docs/research/20260806-x64-shaped-code-to-rethink-for-arm64.md`).**
The individual findings are disposable; these generated them and will generate the next ones.
1. **Ask what the ISA the code was written for COULD NOT DO.** Nearly every finding traces to an x86 constraint, not
   an ARM decision: 2-operand destructive ops → staging copies; shift count in `cl` → scratch staging; free large
   immediates → no materialisation strategy; `base+index+disp` in one mode → displacement never folded; no
   rotate-and-mask → `rlwinm` modelled as rotate+AND (ARM64's `UBFM` **is** rotate-and-mask); `MOVBE` → endianness
   treated as free (we pay `REV`, 65 sites). **The tell is a comment explaining a workaround** — "dest may alias
   src2" described a hazard that cannot occur on ARM64.
2. **Optimise the dependency graph and port mix, NOT the line count.** 3 load ports vs 2 arithmetic: arithmetic is
   scarce. Proven painfully — `ORR`+`STP` packing cut 18 insns to 13 and measured SLOWER by serialising two loads
   through arithmetic into one gated store. Corollary: **loading a constant can beat computing it.**
3. **Fusion belongs in HIR; elision belongs in the backend.** Two-ops-into-one (`EOR3`, `UBFM`) MUST be a HIR pass
   before regalloc — a sequence peephole is always too late, since the inner op is already emitted and allocated.
   Not-emitting a redundant `CMP` the previous instruction already did IS backend-able, because it belongs to the
   later sequence. Getting this backwards wastes the whole implementation.
4. **Measure applicability BEFORE building.** `EOR3` counter: **0 of 1** fusable — the pass would have folded
   nothing. The `rlwinm` census, built to size `UBFM`, instead found 100% of rlwinms on the slow path (**+2.88%**).
   A counter costs one cvar and one run; a pass costs days.
5. **A lever can be correct, allowlisted, and still never run.** Three separate ways this bit us: stale persisted
   config (rlwinm, -2.88%); no `XeniaOptimizations` entry so a GUI launch never passes it (LLVM built generic
   armv8-a); and a defaults block gated on `getBundleExtra(EXTRA_CVARS) == null` while the launcher always attaches
   one (object cache off for EVERY real launch → full recompile every time). **Verify a lever RUNS before
   optimising it** — compiled default, persisted config, GUI registry, and which launch path sets it.

**1. THE HARDWARE MODEL — NOW FROM THE PRIMARY SOURCE, AND THE OLD SUMMARY WAS HALF WRONG.**
**📕 THE MANUALS ARE IN-REPO: `docs/reference/arm/cortex-x3-software-optimization-guide.pdf` (66pp) and
`cortex-a710-software-optimization-guide.pdf` (92pp). READ THEM INSTEAD OF REPEATING FOLKLORE.** They carry the
per-instruction Exec Latency / Execution Throughput / issue-pipeline tables — the actual answer to "is this
instruction cheap on this core". Extract text with `pypdf` (installed); the `Read` tool cannot render them (no
poppler). Section 2.1 is the pipeline, Table 2-1 the pipe→operation map, section 3.x the per-instruction tables.
**Issue pipelines, quoted from Table 2-1 of each guide:**
| | Cortex-X3 (prime, 17 pipes) | Cortex-A710 (mid, 13 pipes) |
|---|---|---|
| Load (`Load/Store 0/1` + `Load 2`) | **3** | **3** |
| Integer ALU (`Single-Cycle` + `Single/Multi-Cycle`) | **6** (4 + 2) | **4** (2 + 2) |
| **FP/ASIMD (vector)** | **4** | **2** |
| Store data | 2 | 2 |
| Branch | 2 | 2 |
- **✅ "3 load ports" is CONFIRMED** on both cores — `Load/Store 0`, `Load/Store 1` and a load-only `Load 2`.
- **❌ "only 2 arithmetic ports" was WRONG as stated.** The 2 is **FP/ASIMD on the A710**, not arithmetic in
  general. **Integer ALU is 4 wide on the A710 and 6 wide on the X3 — it is ABUNDANT, not scarce.**
- **⇒ THE RULE MUST BE SPLIT.** "Spend loads, save arithmetic" is a **VECTOR/NEON** rule: on a mid-core only 2
  pipes accept ASIMD µOPs against 3 that accept loads, so a NEON-heavy loop starves on the vector pipes.
  **For INTEGER code the ratio is the other way round** (4-6 integer pipes vs 3 load), so replacing integer ALU
  work with a load is likely a PESSIMISATION there. Whatcookie's +38%/+21% came from an SPU comparison loop, which
  is vector code — consistent with this, and not a general licence.
- **This retro-justifies a result we already measured and could not fully explain:** the `ORR`+`STP` stackpoint
  packing was INTEGER work, cut 18 insns to 13, and measured SLOWER. With 4 integer pipes and 3 load pipes on the
  mid-cores, trading three independent stores for one gated store through an integer op was always going to lose.
- Corollary that still stands, for vector code only: **materialising a constant by loading it can beat computing
  it** — see the `GetV128ConstLabel` constant pool.
- **NO SVE/SVE2** (Qualcomm shipped ARMv9 without it). Every SVE idea is N/A. We DO have
  `asimddp i8mm bf16 fphp asimdhp atomics lrcpc ilrcpc sha3`.
- **`yield` is architecturally a hint and retires as a NOP on all 8 cores.** It is NOT x86 `pause`. `ISB` is the
  nearest real backoff — but see the measured caveat below before reaching for it.

**2. INSTRUCTION COUNT IS NOT THE OBJECTIVE — WE PROVED THIS ON OUR OWN CODE (2026-08-05).** Packing two u32 fields
with `ORR` and writing a stackpoint entry as one `STP` cut 18 emitted instructions to 13 and **measured SLOWER**: it
serialised two loads through an arithmetic op into a single gated store, where three *independent* stores are
absorbed by the store buffer. Fewer instructions, longer dependency chain, more arithmetic pressure on the scarce
port = a loss. **Optimise the dependency graph and the port mix, not the line count.**

**3. WHERE OUR TIME ACTUALLY GOES — PROFILE, DO NOT GUESS.** `--ei arm64_speed_profile_interval_ms 5000` is the tool;
it reports hot GUEST functions and total `entry_delta`. Burnout: **~24.4M guest function entries/sec, 85% of them
into ONE function.** ⇒ **per-CALL overhead dominates, not any individual opcode.** This is why the prolog win below
exists and why opcode-level micro-optimisation kept disappointing.

**4. 🔥 THE BIGGEST CPU LEVER IS THE GUEST'S OWN BUSY-WAIT (the talk's #1 finding, confirmed here).** RPCS3 found
half of all CPU time in a four-line `busy_wait`. **We have the same shape: Burnout's main thread calls a D3D9
GPU-completion predicate `sub_8238CD28` ~21M times/sec** (identified 2026-08-06 via
`--es disassemble_function_filter 8238CD28`). It reads a device flag at dev+0x28C1, takes a timestamp from
`r13`(PCR)+0x100 → +0x58, compares elapsed against 0x7D0 (2000) and returns 1 = "keep waiting"; past the timeout it
logs and hits `tw` (trap).
- **This is the SAME XDK helper as Blue Dragon's `0x8246B408`**, which we ALREADY replace with a hand-emitted native
  fastpath (`EmitBlueDragonDrawWaitFastpathBody`, `arm64_blue_dragon_draw_wait_fastpath`, default ON). Only the
  build-specific constants differ: BD flag dev+0x2A39/bit1/timeout 5000, Burnout flag dev+0x28C1/bit2/timeout 2000.
  The PCR+0x100→+0x58 time source and the 1/0 return contract are IDENTICAL.
- ⇒ **The fastpath is a title-PARAMETERISED mechanism currently wearing a hardcoded address** (`current_guest_function_
  != 0x8246B408`). Generalising it to a per-title table {wait fn addr, flag offset, flag bit, timeout} is the highest-
  value CPU work available, and it is mostly refactoring code that is already proven on-device.
- **Do NOT confuse this with `KfAcquireSpinLock`** — that one is MEASURED completely uncontended (see below) and is
  a dead end. The hot spin is in GUEST code, not our kernel HLE.

**5. WHAT HARDWARE WE STILL LEAVE UNUSED → `docs/research/20260806-arm64-hardware-exploitation-map.md`.** Each
technique mapped to VERIFIED locations in our tree with a verdict, ordered by breadth-per-risk. Headlines: `EOR3`/
`BCAX` are detected (`kA64EmitSHA3`) with **zero consumers** and need a HIR pass, NOT a sequence peephole (by the
time `XOR_V128` emits, the inner XOR is already allocated); constant materialisation via `MOVZ`+`MOVK` chains burns
the scarce arithmetic ports where a literal load would use an idle one.
**❌ BYTE-SWAP ELISION IS DEAD — ALREADY IMPLEMENTED, DO NOT BUILD IT (checked 2026-08-06).** This file used to
call `rev` "our single biggest arithmetic-port line item" and list it as open. It is not open. Both available
optimisations already exist in HIR, which is exactly where rule 3 says they belong:
`SimplificationPass::CheckByteSwap` (simplification_pass.cc:292/345) folds the `BYTE_SWAP(BYTE_SWAP(x)) == x`
identity into an assign, and `memory_sequence_combination_pass.cc` folds a swap into the load/store's
`LOAD_STORE_BYTE_SWAP` flag so the `REV` is never emitted separately at all. The actual count is **43 emission
sites** (a64_seq_memory.cc 27, a64_seq_vector.cc 12, a64_sequences.cc 4), not 65, and what remains is the
IRREDUCIBLE endian conversion at the memory boundary — ARM64 has no `MOVBE`, so a genuine big-endian guest access
costs a `REV` and there is no instruction to fold it into. **The only unharvested crumb:** `CheckByteSwap`'s own
comment says it matches within a basic block only, so a cross-block double swap is missed — and the comment also
says that is rare. Not worth a pass. **And two that do NOT apply, recorded so they stop being re-proposed:
`UDOT` and the `ABD`/`ABA` trick have no hot fixed-length-comparison site in our tree** (ours are GPU-side or
load-time), unlike RPCS3's SPU block compare where that trick earned its +38%.
**Applied so far: `A64Emitter::EmitCmpImm32`** — one place implementing "fold the constant into the compare",
used at all 8 `mov`+`cmp` sites (kernel lock fastpaths, wait fastpath, stackpoint bound). Picks `CMP #imm`,
`CMP #x,LSL#12`, or **`CMN`** for compare-against-negative (`0xFFFFFFFF` → `CMN #1`). All forms are FLAG-EXACT, so
it is safe under LO/GE and not just EQ/NE — `CMP rn,#K` is `rn+~K+1` and `CMN rn,#M` is `rn+M`, the same addition
when `M == -K`.

**6. ⛔⛔⛔ BENCHMARK GAMEPLAY, NOT ATTRACT MODE / DEMOS / MENUS / FMV (user, 2026-08-06 — I got this wrong all day).**
**Burnout's attract mode is a SCRIPTED DEMO REPLAY, not gameplay, and it is NOT a valid CPU benchmark.** Every number
taken on 2026-08-06 came from it. What attract does NOT exercise: player input processing, the full vehicle AI and
physics load, HUD/UI code, the audio mix under load, and the thread contention that only appears when all of that runs
at once. It is also frame-capped, which hides CPU headroom outright. A scene that runs a *subset* of the engine
measures a *subset* of the code you changed.
- **The tiers, worst to best:** FMV/video playback (measures XMA decode + a blit, essentially nothing under test) →
  title screen → menus → **attract/demo replay** → **actual gameplay** ← the only one that counts.
- **⚠️ I diagnosed this and then ignored it.** The `a64_spin_hint_isb` ledger entry says the busy-wait is "a GAMEPLAY
  behaviour" and that the frame-capped title screen was why that A/B was CONFOUNDED — and then every later A/B this
  session used attract anyway. Writing the caveat down is not the same as obeying it.
- **What this does and does not invalidate.** Changes that are UBIQUITOUS in guest code (the `rlwinm` fastpaths, the
  stackpoint prolog, shifts, constant materialisation) are probably still directionally right, because they fire in
  any code; expect the magnitude to be LARGER in gameplay, which is more CPU-bound. Changes that are SCENE-DEPENDENT
  are not measured at all by attract — **the "13-lever group is FLAT" verdict is really "flat in attract"**, and
  `ppc_vand_self_fastpath` / `ppc_vsplt_swizzle_fastpath` / `ppc_cr_logical_self_fastpath` target idioms an attract
  replay barely runs. Re-test those in a race before believing FLAT.
- **How to reach real gameplay repeatably:** drive the menus with `--es hid nop --es hid_nop_button_sequence '<seq>'`
  into a started race (Burnout) or the field (BD, ~120-135s). Skill: **`xenia-blue-dragon-route-capture`** exists for
  exactly this — a captured, replayable route is the prerequisite for a trustworthy CPU A/B, not an optional extra.
- **Sanity check that you are actually in gameplay:** GPU temp climbing hard, uncapped fps well below the cap, and
  guest `entry_delta` materially higher than the attract baseline (~122-128M/5s on Burnout attract). If your numbers
  look like attract, you are in attract.

**7. MEASUREMENT PROTOCOL (non-negotiable — two separate traps burned device time here).**
- **Run-to-run drift on this device is ~2.8%, LARGER than a typical codegen effect.** Comparing two BUILDS cannot
  resolve 1-2%; it once showed a clean "regression" that a control arm exposed as pure drift. **A/B WITHIN one
  session behind a cvar**, both arms from equal thermal starts, and keep an arm whose code is identical in both
  builds as a drift control.
- **fps is the WRONG metric for CPU work** — Burnout is frame-capped at 60, which hides all CPU headroom (this is
  what made the `a64_spin_hint_isb` A/B useless). Run **uncapped** (`--ei gpu_frame_limit_fps 0`) and read the
  profiler's `entry_delta` (guest entries/5s) = a direct CPU-throughput measure.

## 🔐❌ CRYPTO / CRC32 HARDWARE: SEARCHED FOR A HOT TARGET, FOUND NONE (2026-08-07)
**User asked directly: "use crypto hardware we have in arm 64 as needed too". The ISA prerequisite is now done
- `-march=...+crypto+sha3+crc+dotprod` - but a search for somewhere it would actually PAY came up empty. Recorded
as a negative so the next session does not repeat the hunt.**
| candidate | verdict |
|---|---|
| `texture_info.cc:308`, `sampler_info.cc:54` - per-resource hashes | **already `XXH3_64bits`**, which beats CRC32 for bulk hashing. Nothing to win. |
| `vulkan_command_processor.cc:8779` - per-draw FNV-1a signature, 37 mix calls, byte-at-a-time | **DEAD CODE.** It feeds `bd_native_frame_` (Blue Dragon full-native capture), gated by `gpu_bd_full_native`, which is `DEFINE_int32(..., 0)` and reads `0` in the live device config. It LOOKS like the perfect CRC32 target - a long serialized xor/multiply dependency chain per draw, where `CRC32X` does 8 bytes in one latency-2 instruction - and it never executes. |
| XEX AES decryption (`rijndael-alg-fst.c`) | load-time, once per module. Even a 10x win is invisible next to a multi-second AOT pass. |
| guest `XeCryptSha*` (`TinySHA1.hpp`) | frequency STILL UNMEASURED. `xe_crypt_sha_census` exists (xboxkrnl_crypt.cc:29) and has never been run. This is the only live question. |
**⇒ The honest state: the ISA flags are a PREREQUISITE that is now in place, and nothing yet justifies writing
intrinsics.** The one open question is guest SHA frequency, and it costs one device run to answer.
**🔑 THE NEAR-MISS IS THE LESSON.** The per-draw FNV chain passes every smell test for a hardware-hash win -
hot-sounding location, textbook-bad algorithm, exact instruction available - and optimising it would have
achieved literally nothing, because the cvar that reaches it is off. **Check the GATE before the algorithm.**
This file already says "a lever can be correct, allowlisted, and still never run"; the same applies to code you
are about to speed up.
**Also: that is more BD debris, in the draw path.** 37 byte-at-a-time mix calls per draw sit behind a
default-off Blue Dragon cvar, in a function on the hot path. Worth removing with the rest of the BD cleanup.

## 🔐 ARM64 CRYPTO EXTENSIONS — HARDWARE IS THERE, WE USE NONE OF IT, BUT COUNT BEFORE BUILDING (2026-08-07)
**Prompted by RPCS3 using crypto acceleration on ARM64. The Thor has the FULL set** (from `/proc/cpuinfo`):
`aes crc32 pmull sha1 sha2 sha3 sha512`. **We use software for every one of them:**
- `third_party/crypto/TinySHA1.hpp` backs the guest-callable `XeCryptSha*` / `XeCryptHmacSha*` exports.
- `third_party/crypto/rijndael-alg-fst.c` backs XEX decryption (`aes_decrypt_buffer`, xex_module.cc:63) and the
  guest `XeCryptAes*` exports.
- `rc4.c`, `sha256.cpp`, `des/` - RC4 has NO hardware equivalent, so it is out of scope regardless.
**⚠️ AND THE INSTRUCTIONS ARE NOT EVEN ENABLED:** premake5.lua targets `-march=armv8.2-a+lse` with **no `+aes`,
`+sha2` or `+crypto`**, so the intrinsics would not compile today. That is a one-line prerequisite, but note it
buys NOTHING on its own - the compiler does not turn a software AES round into `AESE`. It needs intrinsics or a
dispatching library.
**⇒ RULE 4 APPLIES HARD HERE. COUNT FIRST.** The cost is dominated by *where* this runs, and we have no data:
- **XEX AES decrypt is LOAD-TIME**, once per module. Even a 10x speedup saves a fraction of a second against an
  AOT compile that currently takes minutes - almost certainly noise.
- **Guest `XeCryptSha*` frequency is UNKNOWN.** Games use it for save integrity, content verification and
  anti-tamper. It could be once at boot or thousands of times a frame; nothing in this repo measures it.
  **Add a call counter (one cvar, one run) before writing any intrinsic** - the `rlwinm` census is the model.
**🔑 THE MORE LIKELY WIN IS NON-CRYPTO USE OF THE CRYPTO EXTENSIONS**, which is what usually pays off in
emulators: `FEAT_SHA3` gives **EOR3** (3-input XOR) and **BCAX** (XOR-and-not), which is exactly what VMX bitwise
chains lower to - and `cpu_llvm_target_features_native` already passes `+sha3` so LLVM fuses those automatically
across all vector code (see the note above that reframes the EOR3 DEAD verdict). `PMULL` is the other one worth
remembering, for carry-less/GF work. **Neither is about encrypting anything.**

## ✅✅ MEASURED ARM64 WIN (2026-08-05): the GUEST PROLOG is the hot path — `a64_stackpoint_prolog_fastpath` = +2.04%
**The a64 profiler (`--ei arm64_speed_profile_interval_ms 5000`) is the tool that found this; use it before guessing
a CPU lever.** Burnout on Turnip: **~24.4M guest function entries/sec, 85% of them into ONE function**
(`sub_8238CD28`, ~21M calls/sec, stable across a whole run). So anything emitted per-call is paid ~24 million times
a second — **the prolog IS the hot path**, not any individual opcode.
- **What was there:** every guest prolog emitted an 18-instruction stackpoint bookkeeping sequence (longjmp
  recovery). **Ceiling, measured: disabling the machinery entirely = +3.25% guest throughput.**
- **What landed:** tightened the ADDRESSING only, keeping longjmp safety — `cmp` against an encoded immediate
  (262144 = 64<<12) instead of MOV+CMP; shifted-register `add x8,x8,x9,lsl#4` instead of MOV+UMULL+ADD (the struct
  is 16B, power of two); and reuse of the depth already live in `w9` instead of re-loading it for the frame slot.
  **18 → 14 instructions. +2.04% guest throughput, 11/11 intervals, ~63% of the ceiling.** Default ON (validated).
  `static_assert`s pin `sizeof(A64BackendStackpoint)==16` and the field offsets so a struct change fails loudly.
- **⛔ DEAD, do not retry: packing the two u32 fields with ORR + one STP.** Fewer instructions (13) but MEASURED
  SLOWER — it serialises two loads through an arithmetic op into one gated store, where three independent stores are
  absorbed by the store buffer. It also runs against this SoC's shape: the A715/A710 have **3 load ports vs 2
  arithmetic ports**, so spend loads and save arithmetic, never the reverse. **Instruction count is not the objective.**
- **🔬 PROTOCOL, learned the hard way: run-to-run drift on this device is ~2.8% — LARGER than a typical codegen
  effect.** Comparing two BUILDS cannot resolve ~1-2%; it produced an apparent regression that the control arm
  exposed as drift. **Every codegen change must be A/B'd WITHIN one session behind a cvar**, both arms from equal
  thermal starts. Metric: NOT fps (Burnout is capped at 60, which hides all CPU headroom — the same trap that
  confounded `a64_spin_hint_isb`). Run **uncapped** (`--ei gpu_frame_limit_fps 0`) and read the profiler's
  `entry_delta` = guest entries/5s, a direct CPU-throughput measure. Harness: `scratch/thor-debug/` A/B scripts.
- **Remaining ~1.2%** needs the machinery gone, not cheaper: per-title disable for games that provably never
  longjmp, or repair the longjmp sync (edge d5956d7e3) so the stackpoint array stops leaking depth.

## CPU = AOT-LLVM (the committed CPU direction)
Whole-fn HIR→LLVM→ORCv2 recompiler. ⚠️ **`cpu_backend_llvm` is DEFAULT **OFF*** (llvm_backend.cc:55 AND the
device's `files/xenia.config.toml`) — this file previously said "default-on", which is WRONG and cost a device
A/B on 2026-07-24: a lever stack containing the LLVM-ONLY residency cvars was measured WITHOUT ever enabling
the LLVM backend, so all three were silent no-ops. **Any LLVM-lever experiment MUST pass `--ez cpu_backend_llvm
true` explicitly.** (`cpu_backend_llvm_opt` does default to 2.) Renders BD,
100% emittable-opcode coverage, hybrid a64 fallback. Model = RPCS3/ReXGlue: precompile all fns, residency,
direct calls, NO JIT/dispatch at gameplay. Levers: **residency** (`cpu_backend_llvm_residency_writeback` = WIN,
qemu byte-identical, smoother+cooler), inline-cache direct calls (~13%), NEON for VMX. Payoff: CPU-bound titles
(Burnout/Gears → 30) + BD heat/sustained-fps. Detail + build recipe: memory `[[llvm-jit-backend-build]]`. Open
bugs (fix forward): opt=2 residency crash, BD-cyan codegen `[[bd-llvm-postload-3d-cyan-bug]]`. Validate every
change: qemu-a64 differential + device render/no-fault (qemu can't catch SVE-SIGILL / q-decode hangs).

## Device
AYN Thor, ADB `c3ca0370`. Snapdragon 8 Gen 2 (QCS8550), Android 13, ~16GB UMA. ADB:
`C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`. Package `jp.xenia.emulator.github.debug`.
- **CPU:** 8× ARMv9 (1× Cortex-X3 @3.19GHz = cpu7, 4× A715/710 @2.8, 3× A510 @2.0). **NEON only (no SVE).**
  asimddp/i8mm = int8/bf16 matrix (heuristics only, NEVER guest FP32 geometry — it black-screens); fphp = FP16
  (pixel-shader only).
- **GPU:** Adreno 740v2, TBDR, Vulkan 1.3, **Turnip (Mesa)** driver. GMEM = EDRAM-emulation target. Single
  graphics+compute queue. LRZ early-Z defeated by alpha-test/discard. EDRAM resolves = per-pixel compute dispatches.
- **Driver = Mesa Turnip, BUNDLED in-APK** (`assets/drivers/turnip.zip`, auto-installs + auto-selects via
  GpuDriverManager). **⚠️ Do NOT swap to the Qualcomm blob (v840/837) — it LACKS ROAA / dynamic_rendering_local_read
  = a downgrade.** Mesa source at WSL `/root/mesa` (build-android works) for driver patches. PRESENT: ROAA,
  dynamic_rendering_local_read, multisampled_render_to_single_sampled, custom_resolve, load_store_op_none.
  ABSENT: fragment_shader_interlock (but ROAA covers same-pixel; interlock is DEAD for BD — its composites are
  neighboring-pixel bloom). Feature audit: `docs/research/20260620-adreno-turnip-feature-gap-audit.md`.

## 🟩🟩🟩 STANDING DIRECTIVE (user 2026-08-03): ALWAYS SHIP THE LATEST TURNIP — newer = more perf extensions
**"we need the latest turnips always they have extra apis for performance."** Turnip is where the Adreno perf
extensions land (ROAA, dynamic_rendering_local_read, custom_resolve, load_store_op_none all arrived this way), so we
TRACK UPSTREAM — never pin. **`python tools/update_turnip.py`** fetches the newest AdrenoTools build, validates the
zip (meta.json + the .so it names — a bad zip would silently fall back to the Qualcomm driver on every device),
writes `assets/drivers/turnip.zip`, and rewrites `GpuDriverManager.BUNDLED_TURNIP_VERSION`. `--check` reports
without writing; `--tag` pins one release. Source = `The412Banner/Banners-Turnip` (rebuilds from Mesa main on EVERY
upstream commit; take the plain non-suffixed asset = A6xx/A7xx, correct for the Thor's Adreno 740 — NOT the A8xx or
-Test variants). Run it at the start of any GPU work.
- **The driver ships INSIDE the APK ⇒ updating it = rebuild + reinstall.** Bumping the version alone does nothing
  until repackaged.
- **⚠️ These are automated bleeding-edge builds, NOT guaranteed stable.** Always device-validate after updating:
  launch a title FROM THE IN-APP GUI and confirm the log says `driverID` = `VK_DRIVER_ID_MESA_TURNIP`. If it says
  `driverID=8` / logs `AdrenoVK-0`, you are on the Qualcomm proprietary driver and every GPU measurement is invalid.
  Roll back with `--tag <older>` if a build regresses.
- **🛑 A BARE `adb shell am start` RUNS THE QUALCOMM DRIVER** — Turnip is applied by the Java launch path
  (`GpuDriverManager.applyToLaunch`), which only runs on a GUI launch. Bit me 2026-08-03: a whole Burnout
  bring-up session ran on AdrenoVK-0 before I noticed. **But the four driver extras ARE allowlisted in
  EmulatorActivity, so headless Turnip launches DO work** — pass them explicitly:
  `--es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '<files>/gpu_drivers/<id>/'` (trailing slash
  REQUIRED) `--es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '<nativeLibraryDir>'`
  (get it from `pm path <pkg>` → replace `/base.apk` with `/lib/arm64`; get `<id>` from
  `run-as <pkg> ls files/gpu_drivers/`).
  **VERIFY EVERY RUN:** the log must say `Loaded Turnip Vulkan driver ... via libadrenotools` and the physical
  device must print as **`Turnip Adreno (TM) 740`**. Plain `Adreno (TM) 740` + `AdrenoVK-0` = the Qualcomm blob and
  every GPU number from that run is invalid.
  **🛑🛑 RE-READ `pm path` AFTER EVERY INSTALL — THE APK DIRECTORY CHANGES, AND A STALE HOOKS PATH FAILS IN A
  MISLEADING WAY (2026-08-07).** Android installs each APK under a fresh randomised directory
  (`/data/app/~~<rand1>==/<pkg>-<rand2>==/`), so a `nativeLibraryDir` captured BEFORE `adb install` points at a
  directory that no longer exists after it. **The failure does not say "bad path".** It says:
  ```
  i> Loaded Turnip Vulkan driver 'libvulkan_freedreno.so' from '.../gpu_drivers/<id>/' via libadrenotools
  w> No Vulkan physical devices available
  ```
  — i.e. it reports the driver as **successfully loaded** and then finds zero physical devices, because
  libadrenotools needs `libhook_impl.so`/`libmain_hook.so` from that directory to redirect the driver's
  dependencies. The emulator then sits on a black screen with ~38 log lines, the activity foreground and
  `Displayed +213ms`, burning no CPU. Reads exactly like a hang or a GPU bug; it is a stale string.
  **⇒ Derive it in the launch command itself, never hardcode it:**
  `NATIVE=$(dirname $(adb shell pm path <pkg> | sed 's/^package://' | tr -d '\r'))/lib/arm64`.
  Note this ALSO invalidates any saved launch script from a previous session - the path in it is dead the moment
  you reinstall, which is every single time we ship a change.
- **Upgrades reach existing devices** (fixed 2026-08-03): `ensureBundledDriverInstalled` used to skip whenever ANY
  driver was installed — including the one it had installed itself — so version bumps only ever helped FRESH
  installs. It now tracks the bundle-installed id (`KEY_BUNDLED_GPU_DRIVER_ID`), replaces exactly that package,
  re-selects it if it was selected, and deletes the superseded one. Drivers the user imported are never touched.
  "Already handled" requires the marker AND the recorded package to exist on disk — a marker-only check let the two
  disagree permanently (device recorded the new version while still running the old driver).
- **NEW SINCE THE 26.0 AUDIT (device-enumerated on Turnip 26.3.0, 174 device extensions — availability only, NOT
  measured):** `VK_EXT_descriptor_buffer` (cheaper descriptor updates than our push-descriptor path),
  `VK_EXT_host_image_copy` (CPU→image upload with no staging buffer or queue submit — texture uploads),
  `VK_EXT_graphics_pipeline_library` (pipeline-creation cost / shader stutter), `VK_KHR_maintenance5/6/7`,
  `VK_EXT_attachment_feedback_loop_dynamic_state`. All five 26.0-era perf extensions still PRESENT (ROAA,
  dynamic_rendering_local_read, custom_resolve, load_store_op_none, multisampled_render_to_single_sampled).
  `fragment_shader_interlock` still ABSENT (still DEAD for BD — ROAA covers same-pixel). Re-audit on each bump.

## Build / deploy / run
- **Path has spaces** → junction: `cmd /c mklink /J C:\xt "<repo>"`, build from `C:\xt\android\android_studio_project`
  (`subst X:` is NOT visible to background gradle tasks).
- **APK:** `cmd /c "C:\xt\android\android_studio_project\gradlew.bat -p C:\xt\android\android_studio_project
  :app:assembleGithubDebug"` (native ~1-2.5min). If `mergeResources` fails, run `:app:mergeGithubDebugResources`
  once first. APK: `android/.../app-github-debug.apk`. Install: `adb -s c3ca0370 install -r <APK>`.
- **BD launch:** ISO `/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`,
  turnip driver flags, `--es hid nop --es hid_nop_button_sequence '<start/a seq>'`. Heavy field ~120-135s. Reusable
  scripts: `scratch/thor-debug/*.ps1`. Trace: `--ez vulkan_trace_draw_outcomes_per_frame true` → the
  `GPU draw outcomes/frame` logcat line (rendered, pipeline_binds, guest_ms, cpu_* buckets).

## Shipping a win — the propagation loop (do ALL or the device runs stale)
1. **NEW cvar → allowlist it in `EmulatorActivity.java` onCreate** (copyBooleanExtra/Int/String) or `--ez/--ei/--es`
   silently no-ops → you measure the wrong thing.
2. Per-game win → add to that title's `GameProfiles` profile (with the validated "why"). Cross-game → flip
   `XeniaOptimizations` `defaultEnabled=true`. Frame cap per game via `gpu_frame_limit_fps` (BD=30, Burnout=60).
3. **REBUILD + REINSTALL the APK** — the Java registry/profiles DON'T reach the device until repackaged (stale
   APK = the silent-default-off confound, on-device — root cause of a "10fps Burnout" that `--ez` read at 46).
4. **VERIFY from the in-app UI launch (not `--ez`)** — reproduces the measured fps (menus uncapped = install stale).
- **Test stack:** every `--ez` A/B must pass the FULL validated set (reuse `$opts` in
  `scratch/thor-debug/measure_const_promo.ps1`) or it's confounded (silently missing flat_membase, the fence fix,
  etc.). Effective-value layering (highest wins): launch Bundle > `files/xenia.config.toml` > compiled default.

## Device-free testing (prefer over firing)
- **Host x64 cpu-tests:** `MSBuild build\xenia-cpu-tests.vcxproj /p:Configuration="Debug Windows" /p:Platform=x64`;
  run `build/bin/Windows/Debug/xenia-cpu-tests.exe "FILTER_*"` (Catch2).
- **qemu-a64** (no device): WSL + aarch64-linux-gnu-g++ + qemu-aarch64. Recipe: `[[a64-qemu-harness]]`
  (that memory was MISSING until 2026-08-07 and is now written). Working examples committed in
  `tools/qemu/`: `fmax_nan_differential.c`, `vmx_nan_arith_differential.c`, `float_commutativity_check.c`.
  **⚠️ IT IS NOT A "real ARM64 backend" HARNESS, which is what this line used to claim.** It runs
  hand-written asm under qemu, so it proves ISA SEMANTICS (exact bit patterns, guest-vs-host agreement
  against `docs/reference/`) and proves NOTHING about our emitter - not register allocation, not operand
  staging, not sequence selection. That distinction is exactly why `a64_vmx_fp_no_operand_copy` is still
  default-off: its change is WHICH REGISTERS hold the sources, which this harness structurally cannot see.
  **Set FPCR to `DEFAULT_VMX_FPCR` (1<<24: FZ set, DN CLEAR) or the NaN answers are wrong for the right-
  looking reason.**
- **⚠️ TWO MEMORY LINKS IN THIS FILE ARE STILL DANGLING** (audited 2026-08-07): `[[bd-llvm-postload-3d-cyan-bug]]`
  and `[[llvm-jit-backend-build]]` are cited as the authoritative detail/build recipe but **no such memory
  files exist**. Do not burn time hunting them; the content was lost, not misfiled. Rewrite them from the
  code when someone next works those areas rather than trusting the pointer.
- **RE:** Ghidra 12.0.4 at `...\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC` (headless in the MAIN loop, long
  timeout, never in a subagent). **page-watch** (`cpu_watch_guest_write_page`) = the RE unblock: host-mprotect a
  guest page + emulate-on-fault at full speed. Skills: `xenia-ghidra-ooda-loop`, `xbox360-d3d-hle-recomp`.
- Mine existing captures (log-grep) instead of re-firing where possible.

## Game patches
`.patch.toml` (src/xenia/patcher/): be8/16/32/64 guest-memory writes, matched by title_id + build hash ("Module
Hash" in logcat). Applied in `KernelState::LoadUserModule` before execution. Skill: `xenia-thor-ghidra-game-patch`.

## Config + git rules
- Device persists `files/xenia.config.toml` (overrides compiled cvar defaults; only `--ez/--ei/--es` beat it).
- **Never fabricate** — every number read from device output THIS turn; unmeasured = say so; cross-run = say
  CONFOUNDED; read the screenshot before asserting a visual.
- **NEVER let a file-mutating script and `git` share a command with a NEWLINE between them - use `&&`.**
  On 2026-08-07 I truncated CLAUDE.md and pushed it: the script used `open(path,"w")`, which truncates
  IMMEDIATELY, then the encode threw before writing a byte, leaving 0 bytes - and `git commit` ran anyway
  because a newline had separated them, producing a commit whose message described content that did not
  exist. Three habits that make it structurally impossible:
  1. **Encode/serialise BEFORE opening the file for write.** `open("w")` destroys the old content before your
     code can fail. Build the bytes first, then write them.
  2. **Refuse a result smaller than the original** unless a deletion is genuinely intended, and keep a backup
     you restore on a short write.
  3. **Gate `git add` on a size/line-count check** (`[ $(wc -c < f) -gt N ]`). That catches the whole class
     regardless of cause, which the first two do not.
  **And read the diffstat before believing a commit.** The bad one said `1 file changed, 1504 deletions(-)`
  in plain sight.
- **Targeted `git add` only (never `-A`).** Never commit ISOs/keys/dumps/screenshots/config backups. Work on
  `master`. **Forward-only** (never `git revert`; fix forward). End commits:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Skills in `.agents/skills/`; knowledge in memory files (`MEMORY.md` index).
- **CONSULT CODEX gpt-5.6-sol (MAX) FOR HARD REARCH** — for any hard architecture/rearch/root-cause problem, get a
  second opinion from **`gpt-5.6-sol` at reasoning effort `max`** via the Codex CLI (the OpenAI/ChatGPT tool; the
  configured default in `~/.codex/config.toml` = `model = "gpt-5.6-sol"`, `model_reasoning_effort = "max"`). Run
  READ-ONLY: `codex exec -c model_reasoning_effort=max --sandbox read-only -` (uses the default sol; or `-m
  gpt-5.6-sol`). Pipe the prompt via stdin; give it the measured diagnosis + exact file:line context and ask it to
  cite code. It PAID OFF 2026-07-10 (caught a wrong build — the float-in-pass variant — before it was written, via
  `skip_fmt=0`; designed the color-only native HLE plan; hardened the drop gate to fail-closed).
  ⚠️ Only the PLAIN model names work on the ChatGPT-account login (`gpt-5.6-sol`, `gpt-5.6-terra`); the
  `-max`/`-high`/bare-`gpt-5.6` suffixes are rejected ("not supported when using Codex with a ChatGPT account") —
  set reasoning via `-c model_reasoning_effort=max`, NOT a name suffix. `gemini` CLI is the fallback.

## Autonomous mode (standing directive)
Pick the highest-value unit yourself, execute end-to-end (implement → build-verify → device-test → commit →
next). Don't ask which task / re-confirm direction / analysis-paralyze. A big effort is a reason to start, not
to ask. Surface only genuine external blockers. Thermal + no-fabrication rules always hold.
