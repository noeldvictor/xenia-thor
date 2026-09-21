# AGENTS.md, xenia-thor

xenia-thor is a fork of the Xenia Xbox 360 emulator for one device: the AYN Thor.
This file is the single instruction file for every agent: Claude Code, Codex, and others.
It holds the rules, the sources, and the current state. It is short by design. `CLAUDE.md` is
a pointer to this file (migrated 2026-09-20). Read this file at the start of every session and
update it when a rule or the state changes.
The full research journal to 2026-09-18 is in `docs/research/20260918-claude-md-archive.md`.
Use `docs/research/20260918-claude-md-archive-index.md` to find a section by title.
Put new findings in a dated note under `docs/research/`. Do not grow this file with journal entries.

## 1. Language rule (user, 2026-09-18). This rule applies to all future language.

Write all text in ASD-STE100 Simplified Technical English. This applies to this file, chat replies,
commit messages, research notes, worklogs, code comments, and prompts to other agents.

| rule | do this |
|---|---|
| 1 | Remove all metaphors, similes, analogies, and idioms. Use literal words. |
| 2 | Remove filler, small talk, and repeated phrases. |
| 3 | Replace vague words and jargon with exact, concrete terms. |
| 4 | Keep all facts, numbers, and intent unchanged. |
| 5 | Write short sentences in the active voice. |

STE mechanics:

| item | rule |
|---|---|
| sentence length | 20 words or fewer in an instruction. 25 words or fewer in a description. |
| one meaning | One word for one meaning. Write "start". Do not write "kick off", "fire", "spin up". |
| voice | Active voice. Write "The shader reads the image." |
| tense | Simple present, past, or future. No perfect or progressive tense. |
| articles | Use "the" and "a". |
| noun clusters | Three nouns or fewer in a row. |
| one idea | One instruction per sentence. Use a list for more than one step. |
| paragraphs | Six sentences or fewer. |

Keep the numbers. Keep the tables. Keep the reason behind each result. Write "This is not measured."
Write "I was wrong." when it is true.

Writing principle (user, 2026-09-20), the guide for every text an agent writes:

1. Be entirely literal. Replace figurative language with direct facts or clear descriptions.
2. Cut the fluff. Remove unnecessary adverbs, repeated phrases, and sentences without concrete value.
3. Keep the core meaning. Do not alter the facts, data, or arguments.
4. Keep a professional tone. Clear, objective, easy to read.

Always update this file when a rule or the state changes, and check it at the start of a session.

## 2. Mission and target

- Goal: Xbox 360 games run fast and playable on the AYN Thor and Thor Max.
- Device: Snapdragon 8 Gen 2, Adreno 740, native Android, ABI `arm64-v8a`.
- Graphics: Vulkan through the Mesa Turnip driver. The Qualcomm driver is wrong for this work.
- CPU: PowerPC guest code runs through the a64 backend and the LLVM backend. No x64 code is in the APK.
- Targets: Blue Dragon 30 fps at 720p with full foliage. Burnout, Gears, Lost Odyssey, Banjo at 30 to 60 fps.
- Ship each fix and each win as the app's default behavior. No cvar gates, no intent extras. See directive 17.
- This is a research fork. Do not send fork bugs, traces, or compatibility claims upstream.
- Legal content only. Never commit game files, keys, private data, dumps, screenshots, or config backups.

## 2b. The port is a multi-axis paradigm change (user, 2026-09-20)

xenia assumes x86-64, TSO memory, SSE/AVX, a desktop GPU with its own VRAM, Windows scheduling,
and a desktop build. The Thor changes every one of those at once. The bugs and the lost
performance come from code that still holds the old assumption on one axis. So the method is:
name the axes, write the desktop assumption on each, and for every bug first ask which axis it
crosses. Full table and the open items: `docs/research/20260920-port-paradigm-matrix.md`.

| axis | desktop assumption | the Thor |
|---|---|---|
| CPU ISA | two-operand, 16 GPRs, AVX2 | three-operand, 31 GPRs, NEON 128-bit |
| memory model | TSO | weak ordering; missing fences race |
| host SIMD | SSE intrinsics | NEON, sse2neon shims differ on rounding and denormals |
| GPU architecture | immediate-mode dGPU, cheap bandwidth | binning tiler with GMEM, UMA, expensive pass breaks |
| Xenos model | EDRAM flattened into one big buffer | GMEM is the natural EDRAM analogue |
| driver | NVIDIA/AMD | Turnip, strict, weekly builds |
| OS | Windows threads, timers, one CPU type | Android, 1+4+3 cores, thermal throttling near 95 C |
| memory manager | VirtualAlloc, no limits | Scudo, `vm.max_map_count` 65,530 |
| storage, present, input, build | NVMe, exclusive fullscreen, XInput, MSVC | flash and content URIs, SurfaceFlinger and a sleeping panel, gamepad events, NDK |

## 3. Read first, by task

Each row names a section title in the archive. Search the archive for the title.

| about to | read in the archive | reason |
|---|---|---|
| test whether a GPU change alters rendering | `THE OODA LOOP IS 1.2 SECONDS NOW`, `tools/pc/bd_trace_ab.sh` | Replay one captured frame on the PC. 1.2 s per arm. Do not use the device for this. |
| quote a Blue Dragon speed number | `THE BD SPEED HACK, MEASURED AT LAST`, `THIN PER PASS, NOT PER FRAME` | Capture a frame before you quote a speedup. A frame rate on a corrupted frame is not a frame rate. |
| reason about BD MSAA, EDRAM, or tiling | `BD's HEAVY PASS IS 2xMSAA` | The 734-draw heavy pass is 2x MSAA. "BD is 1x" is wrong. |
| A/B a lever and get a flat result | `INERT-LEVER AUDIT`, `gpu_vulkan_edram_roaa is INERT` | Six inert levers exist. Prove the lever can fire: a cvar reader, a producer, an allowlist entry. |
| benchmark anything | `NEVER BENCHMARK A MOVIE`, `Measurement is the #1 trap` | Run-to-run drift is 2.8%. That is larger than most wins. |
| trust a measurement | `CHECK THE PERSISTED DEVICE CONFIG`, `A BARE am start DOES NOT TEST WHAT SHIPS` | A stale `xenia.config.toml` cost 2.88%. Headless launches run without LLVM, AOT, and the shipping levers. |
| A/B a cvar | `A DEFAULT-OFF PATH IS NOT A CONTROL` | The off branch may be untested code. |
| pick a CPU lever | `ARM64 PERFORMANCE PLAYBOOK`, `docs/reference/arm/` | Measure applicability before you build. Read the manual. |
| touch GPU, EDRAM, or render passes | `EDRAM / GMEM / UMA: ANSWERED ON THE DEVICE` (2026-08-16), `THE BD EDRAM / D3D9-HLE ERA IS ARCHIVED` | The GPU frame is ALU-bound. Blend is free. GMEM never beats autotune. The EDRAM redesign is dead. |
| test an acceleration theory | `STANDING DIRECTIVE ... BESPOKE HARNESS` | Build a native ARM64 binary. Run it over adb. 45 s loop instead of 10 to 18 min. |
| port from XenDroid or Edge | `SWEEP 2026-08-16`, `XENDROID SWEEP 2026-08-13`, `XENDROID IS THE BAR` | The trees diverge. Port the idea, not the patch. |
| change any CPU semantics | `SWEEP 2026-08-16`, `src/xenia/cpu/ppc/testing/instr__gen_*.s` | 169,117 hardware-captured cases exist. Get a before and after failure count. |
| start the device | `Never thrash the Thor`, `BLACK SCREEN? CHECK THE DISPLAY`, `TURNIP IS MANDATORY` | Thermal limits are real. A sleeping panel looks like a render bug. A bare launch uses the wrong driver. |
| chase a crash | `BURNOUT ... LLVM WRITING x20`, `GEARS SIGTRAP` | Decode the faulting instruction before you blame a subsystem. |
| hunt x86-shaped bugs | `THE x86->ARM64 SWEEP: MEMORY ORDERING IS THE BUG CLASS` | TSO hides missing fences. Look for an atomic index that guards a plain buffer. |
| work on power or heat | `MANUAL REVIEW #4`, `#6`, `ONE GLOBAL CONDVAR`, `HOW TO ACTUALLY MEASURE WATTS` | Watts are not measurable over USB. The spin sites are swept and clean. |
| touch the register allocator | `MANUAL REVIEW #1`, `#2`, `#3`, `#5` | 32 guest GPRs map to 7 host GPRs. 128 guest vectors map to 28. |
| touch FP, VMX, or FPCR | `MANUAL REVIEW #6`, `THE THREE NEW a64 LEVERS ARE NOT INDEPENDENT` | Each FPCR mode switch is a pipeline barrier. |
| measure CPU throughput | `entry_delta IS BLIND TO THE LLVM BACKEND` | `entry_delta` counts a64 functions only. Use fps on an uncapped title. BD field is about 9.9 fps. |
| work on LLVM coverage | `SCALAR FMA LOWERING`, `ONE LLVM MODULE PER GUEST FUNCTION` | Fallbacks went 1,022 to 194 by lowering scalar FMA. |
| expect a win from register residency | `THE MANUAL RESIZES STAGE 3`, `docs/reference/arm/aapcs64-callee-saved-notes.md` | AAPCS64 preserves only the low 64 bits of v8 to v15. Vector residency across a call is impossible. |
| run anything on the Thor | `DEVICE HYGIENE`, `tools/thor/bd_fma_fps_ab.sh` | The device is shared. The idle check must abort, not print. |
| edit this file with a script | Section 11 of this file | A script truncated this file to 0 bytes once. |

## 4. Standing directives from the user

Each directive has a date. The archive holds the full text and the evidence.

1. **Top priority (2026-08-08): emulate PowerPC and the GPU on ARM64 directly.** Remove x86-shaped
   structure from the ARM64 port. No x86 code runs in the APK. The targets are decisions that only made
   sense for a 16-register, 2-operand, TSO host. The largest open item is the register budget: 7
   allocatable GPRs and 28 vectors, with the rest spilled to the `PPCContext` block.
2. **AOT plus LLVM is the default (2026-07-31).** Every game gets a full AOT precompile before start,
   and the LLVM backend as the standard CPU backend. `cpu_llvm_no_runtime_compiles` is default true.
   Functions found during gameplay compile on the a64 backend. Since 2026-09-18 these are code
   defaults on Android (`XE_ANDROID_DEFAULT` in cvar.h), not app toggles. The app keeps four user
   choices: VRS balanced, VRS performance, VRS heavy scenes, and frame generation.
3. **XenDroid and xenia-edge are the compatibility reference (2026-07-31, 2026-08-06).** XenDroid is the
   best stable Android Xbox 360 emulator. It runs on the same device. When a title misbehaves here and
   works there, diff the edge kernel and port the divergence. Do not debug from scratch. If a title looks
   worse here, that is a bug in this fork. Quality levers are not free performance.
4. **Turnip is mandatory (2026-07-06).** Confirm `driverID` is `VK_DRIVER_ID_MESA_TURNIP` in the log.
   `driverID=8` means the Qualcomm driver, and every GPU measurement on it is invalid.
5. **Always ship the latest Turnip (2026-08-03).** Run `python tools/update_turnip.py` at the start of
   GPU work. Source: `The412Banner/Banners-Turnip`, plain A6xx/A7xx asset. The driver ships inside the
   APK. A version bump needs a rebuild and a reinstall. Validate on device after each update.
6. **The BD native renderer is deleted (2026-08-07).** The BD EDRAM and D3D9-HLE era is archived in
   `docs/research/20260807-bd-edram-hle-era-archive.md`. Do not restart it.
7. **Test only in game (2026-08-08).** Menus, videos, title screens, and attract modes are not tests.
8. **Never benchmark a movie (2026-08-06).** A benchmark scene is real gameplay. Use a captured route:
   `--es hid nop --es hid_nop_button_sequence '<seq>'`. Skill: `xenia-blue-dragon-route-capture`.
9. **Device hygiene (2026-08-08).** Force-stop the emulator before and after every use. See section 5.
10. **Test acceleration theories in a bespoke harness (2026-08-13).** Build a native ARM64 binary and run
    it over adb. Building a benchmark binary needs no permission (2026-08-16). Put durable ones in `tools/`.
11. **Put device commands in a script (2026-08-14).** One script per experiment, in `tools/thor/` or
    `tools/edram_bench/`, with the abort conditions inside it. Keep inline commands to short reads.
12. **Frame generation is off in every measurement (2026-08-16).** `present_frame_extrapolation` is a user
    option, not a performance lever. Its factor cvar `present_frame_gen_factor` is read nowhere.
13. **Autonomous mode.** Pick the highest-value unit. Execute it end to end: implement, build, device
    test, commit, next. Do not ask which task to do. Surface only real external blockers.
14. **Current goals (2026-08-17).** Goal 1: ship the VRS quality choice for BD. VRS 2x1 on blended draws
    gives +33% fps (15.3 to 20.4). Goal 2: make a second title measurable. The Gears stall is a compat bug:
    five guest threads wait on an event the HLE never signals. MagnaCarta 2 is the cheaper path. Goal 3:
    make CPU work measurable on a CPU-bound title. CPU levers are capped at 6.8% on BD.
15. **Blue Dragon is low priority (user, 2026-09-20).** re:Blue (`zolaware/reblue`) is a native
    recompilation of it, so the fork does not chase BD speed. BD stays the regression check for the
    GPU path. Earlier: BD is about 93% GPU-bound; the frame is 89.6% guest fragment shading and
    10.4% EDRAM machinery; the in-pass GPU story is closed. Priority titles: Banjo-Kazooie, Gears,
    Lost Odyssey, Burnout, MagnaCarta 2.
16. **Do not spawn many subagents (user, 2026-09-18).** Subagents burn tokens too fast. Do the work
    directly with Bash, Read, and Edit. Use at most one subagent at a time, and only for a broad
    read-only search that would flood the main context. Never fan out several port, fix, or review
    agents in parallel. Ten agents ran in one session on 2026-09-18; that is the case to avoid.
17. **No cvars as the control surface (user, 2026-09-18).** Every behavior lever is a toggle in the
    app menu (`XeniaOptimizations`) with a default. The user flips it there; the MCP flips it with
    `xenia_toggle_set` and reads it with `xenia_toggles`. Do not add a lever without a menu toggle. Do
    not test with `--ez`, `--ei`, or `--es` extras that the play button does not pass. Launch through
    the app's own launch path (the launcher's VIEW intent, `xenia_launch`). A result from a launch the
    user cannot repeat from the menu is not a result. Validated levers also have Android code
    defaults (`XE_ANDROID_DEFAULT` in cvar.h) so a fresh install runs them; the toggle stays the
    control. Diagnostic cvars (traces, dumps, censuses) are not levers and stay as they are.
18. **The MCP is the way to debug, fix, measure, and profile (user, 2026-09-20).** Every device action
    goes through `tools/mcp/xenia_thor_mcp.py`. When a debugging step needs a tool the MCP lacks, add
    the tool first, then use it. A run must return the complete evidence packet in one pass. All work
    lands on `master`; no feature branches; push after each commit.
19. **No A/B campaigns (user, 2026-09-20).** Xbox 360 emulation on the Snapdragon 8 Gen 2 is new
    ground; nobody has the answers, and matched-pair comparisons cost more time than they return.
    Research online first (recompilers, other emulators, driver work), pick the best-known approach,
    implement it, verify it on the device with one run (screenshot, log, fps), record the number in
    the ledger, move on. Section 6 stays as the standard for a number that is quoted as a result; run a
    comparison only when the user asks for one.

## 5. Device safety and hygiene

- Device serial `c3ca0370`. Wifi adb `192.168.1.33:5555`. Package `jp.xenia.emulator.github.debug`.
- The device is shared with another session that runs rpcs3. Check `pidof` for both before you start.
- Pre-flight before every launch: `/sys/class/kgsl/kgsl-3d0/temp` below 50 to 55 C, `gpu_busy_percentage`
  idle, `dumpsys battery` level above 30% or charging, screen awake. Say the battery level when you launch.
- Force-stop past 70 C. Do not reboot the device. Do not blame the device for a result.
- Force-stop before and after every run. Verify with `pidof`. Disconnect wifi adb after a batch.
- Never use `adb shell input keyevent`. It targets the foreground app, which may be another session's
  game. This happened three times. Use `--es hid nop --es hid_nop_button_sequence` instead.
- A pre-flight is a point-in-time check. Re-check before every interaction that writes: input, install,
  force-stop.
- Verify a screenshot is yours: `dumpsys activity activities | grep topResumedActivity`.
- When the user says stop using the device, stop polling it too. Kill the background task.
- Killing a background task does not kill the emulator. Do both.
- A bare `adb shell am start` of EmulatorActivity runs the Qualcomm driver and none of the shipping
  behavior. Launch through the launcher's VIEW intent (`xenia_launch`), which is the play button's path.
- Run `--vulkan_validation=true` on device when a driver crash is suspected. The layer is bundled.

## 6. Measurement rules

These define what a quoted number must be. They do not require a comparison for every change
(directive 19).

1. Never fabricate. Every number comes from device output in this turn. Unmeasured means say so.
   Cross-run means say "confounded". Read the screenshot before you assert a visual.
2. Settle correctness on the PC trace loop (`tools/pc/bd_trace_ab.sh`, 1.2 s per arm). Spend device time
   only on a number the PC cannot give.
3. Matched pairs, never pooled runs. Match on scene complexity. Gate on `verts > 50000` for BD.
4. A picture that looks right is not a measurement. Use a pixel diff, a log line, or a frame capture.
5. Rank experiments by what they kill.
6. Run the discriminator before the write-up. A correlation from 2 or 3 runs is not a result.
7. `entry_delta` counts a64-compiled functions only. It is blind to LLVM. Use fps on an uncapped title.
   Burnout sits at a 60 fps cap and cannot show a CPU win.
8. `gpu_frame_us` is a timestamp span, not GPU busy time. Port edge `61810b48e` before you trust a
   GPU-side split.
9. Prove a lever can fire before you believe a flat result: compiled default, persisted config, GUI
   registry entry, and the launch path that sets it.
10. Every CPU semantics change needs a corpus run before and after. Run it with
    `--break_on_unimplemented_instructions=false`. The suite takes more than 10 minutes. Run it in the
    background.
11. Do not flip a default-off lever because the code looks equivalent. Wrong pixels are not a crash.

## 7. Sources

These are the external projects this fork reads, ports from, and compares against.

| source | what it is | URL | local | role |
|---|---|---|---|---|
| xenia-canary | Upstream base of this fork. | github.com/xenia-canary/xenia-canary | remote `canary`, branch `canary_experimental` | Base. Fork point `95a5c3ee2` (2026-02-18). Port fixes from it. |
| xenia-edge | has207's fork. Superset of canary plus compat and a64 fixes. | github.com/has207/xenia-edge | remote `edge`, branch `edge` (not `master`); clone `../reference/xenia-edge` | Compatibility reference. Our kernel layer is ported from it (`f3e780d2e`, 2026-08-04). Richest source of fixes. |
| XenDroid | rfandango's Android app. Kotlin shell plus vendored xenia-edge. | github.com/rfandango/XenDroid | remote `xendroid`, branch `main`; clone `../reference/XenDroid` | Stability and image quality bar. Runs on the same device. Filter with `--author=rfandango`. |
| xenia-project/xenia | Original upstream. | github.com/xenia-project/xenia | remote `upstream`, branch `master` | Nothing new since our base as of 2026-09-18. |
| Wunkolo/xenia | Author of the first ARM64 JIT backend (oaknut assembler). | github.com/Wunkolo/xenia | remote `wunkolo`, branch `canary-arm64-backend` | Not our lineage. Our a64 backend is the canary port by Herman S. (canary `883c2030d0`, xbyak_aarch64). Patches do not apply. Port by function name. |
| wmarti/xenia-mac | Apple Silicon ARM64 port. | github.com/wmarti/xenia-mac | remote `xeniamac`, branch `arm64-all` | ARM64 and POSIX fixes that may port. macOS and Metal parts are out of scope. |
| IhorHarayda/xenia-canary-arm64 | Small ARM64 fork. | github.com/IhorHarayda/xenia-canary-arm64 | remote `arm64fork`, branch `arm64` | 3 unique commits as of 2026-09-18. |
| AdrianCassar/xenia-canary | Netplay fork. | github.com/AdrianCassar/xenia-canary | remote `cassar` | Out of scope, except kernel lifetime fixes that reach canary as PRs. |
| XenonRecomp, XenosRecomp | hedge-dev static recompilers for PPC and Xenos shaders. | github.com/hedge-dev/XenonRecomp | `../reference/XenonRecomp`, `../reference/XenosRecomp` | Design reference for PPC to native and shader translation. |
| RexGlue SDK | Recompilation runtime, v0.9.0. | github.com/rexglue/rexglue-sdk | `../reference/RexGlue` | Residency flags reference. It ships every residency flag off. |
| FEX-Emu | x86 on ARM64 JIT. | github.com/FEX-Emu/FEX | `../reference/FEX` | ABI reference. `preserve_allcc` keeps the low 128 bits of v8 to v31. |
| Box64 | x86 on ARM64 JIT. | github.com/ptitSeb/box64 | `../reference/Box64` | JIT reference. "callret" is not register residency. |
| N64Recomp | N64 static recompiler. | github.com/N64Recomp/N64Recomp | `../reference/N64Recomp` | Recompilation design reference. |
| RPCS3, whatcookie | ARM64 optimization work, measured on the AYN Odin 2 with our SoC. | docs/research/20260805-rpcs3-arm64-optimizations-applicable.md, 20260809-whatcookie-video-full-mining.md | in-repo notes | Source of the ARM64 performance playbook. About half of its list is landed. |
| Mesa Turnip | Open-source Adreno Vulkan driver. | gitlab.freedesktop.org/mesa/mesa; builds from The412Banner/Banners-Turnip | `android/android_studio_project/app/src/main/assets/drivers/turnip.zip`, `tools/update_turnip.py` | Mandatory driver. Track the newest build. It ships the ir3 shader-debug facility. |
| GameNative | Android Steam client with a custom Turnip and Box64. | github.com/GameNative/GameNative | `../GameNative_inspect` | Reference for driver loading and packaging. RE2 at 30 fps on this Thor is the hardware proof point. |
| ARM manuals | Cortex-X3 and Cortex-A710 software optimization guides. AAPCS64 notes. | in-repo | `docs/reference/arm/` | Read section 4 first. Extract text with `pypdf`. |
| Adreno docs | Qualcomm mobile best practices. | in-repo | `docs/reference/adreno/` | VRS and tiler guidance. |
| Xenos R400 sequencer spec | Primary GPU source, IPR2015-00325. | public patent record | see archive `THE XENOS PRIMARY SOURCE IS PUBLIC AND LOCATED` | Occupancy numbers. |
| Codex gpt-5.6-sol | Second opinion for hard architecture questions. | Codex CLI | `codex exec -c model_reasoning_effort=max --sandbox read-only -` | Read-only. Give it the measured diagnosis and file:line context. |

Sweep marks. Start the next upstream sweep from these. The triage of 2026-09-18 is in
`docs/research/20260918-upstream-triage.md`.

| source | last mark | date | new commits at 2026-09-18 |
|---|---|---|---|
| edge/edge | `f6184518b4` | 2026-09-20 | 21 new since `1689b263d3`: the user-mode series, 3 fixes queued; see `docs/research/20260920-upstream-sweep-arm64-adreno.md` |
| canary/canary_experimental | `aee0871dd7` | 2026-09-16 | triaged 950 since the fork base; 52 kept |
| xendroid/main, author rfandango | `0b1120187` | 2026-08-16 | 0 new on main; `feat/vrs` 2 kept |
| canary open pull requests | 60 open on 2026-09-18 | | 16 kept |
| Mesa Turnip (Banners-Turnip) | bundled `26.3.0-20260807-r11`; newest `20260919` | | 167 driver commits between them; update and measure |
| forks: wunkolo, xeniamac, arm64fork | tips of 2026-03-19, 2025-08-25, 2026-06-29 | | 7 kept across all |

Sweep recipe:

```
git fetch canary edge xendroid upstream
git log --format='%h %ad %s' --date=short --no-merges <edge-mark>..edge/edge
git log --format='%h %ad %s' --date=short --no-merges --author=rfandango <xendroid-mark>..xendroid/main
git log --format='%h %ad %s' --date=short master..canary/canary_experimental
git fetch --no-tags canary +refs/pull/<N>/head:refs/remotes/canary/pr/<N>
git show <hash> | git apply --check
```

Port rules:

- Port the idea, not the patch, when the trees diverge. Expect true N/A results.
- Our cpu, gpu, base, apu, hid, and ui trees are mostly additive over upstream. Patches there usually apply.
- Our kernel tree has about 5,000 deleted upstream lines. Kernel patches often conflict.
- A wholesale subsystem merge must also take the caller-side changes from edge.
- Desktop oracles cannot validate an Android memory or threading port. Test on device.
- Record each ported hash, each N/A hash, and the reason, in the dated research note.

## 8. Current state, 2026-09-18 (Banjo lines added 2026-09-20)

- Banjo-Kazooie (4D5307ED) reaches the game world. Two a64 inline fast paths (spin lock,
  IRQL) used their own encodings and stalled it; both use the HLE's state now. The first
  visit to a scene runs at 2 fps while Turnip compiles pipelines on the command processor
  thread (170 ms each); the VkPipelineCache blob now persists during play, and a cached visit
  costs milliseconds. The lower half of the world frame is black with speckles (Xenos axis,
  open). One run in four ends in `XamShowDirtyDiscErrorUI` from the loader thread (open).
  Details: `docs/research/20260920-banjo-spinlock-protocol.md`.
- The device heats over consecutive runs: the case (`xo-therm`) at 44 C reaches the 70 C GPU
  abort within 45 s of play. `xenia_preflight` gates on case 41 C. Wait, do not lower the abort.

- PPC hardware corpus: 169,117 cases in `src/xenia/cpu/ppc/testing/`. a64 failures 7,907 to 3,038 on
  2026-08-18. x64 failures 17,851 to 14,591 on 2026-08-18. One class is 94% of what is left on a64.
- The edge kernel layer is landed and device-validated (`f3e780d2e`). Phase 4 IRQL unification is pending.
- ADPF is disabled on this device (2026-08-19). The hint session holds one thread. The target it feeds
  is a known false hint.
- The spin-park lever is flat. The sleep variant renders zero frames on MagnaCarta 2. Do not ship it.
- Android audio: native AAudio backend. The launcher opens `AndroidAudioDriver` on device.
- Turnip is bundled. The Vulkan validation layer is bundled.
- `vulkan_direct_host_resolve` is measured at about 1.3% of the frame, 0 faults. Turn it on.
- `vulkan_mid_frame_submission_draws=1300` is measured flat.
- Open port backlog from the 2026-08-16 sweep: inline `vexptefp` and `vlogefp` (edge `fb225d975`),
  `stvlx` and `stvrx` as overlapping stores (edge `e9582aca7`), ADPF for the audio pump (XenDroid
  `e0137c9a7`), XenDroid Vulkan descriptor set (`68e78ca92`, `036fedb3e`, `8e48dd4af`, `162c86ed1`),
  guest scheduler default on (edge `34357e257`), GPU busy time (edge `61810b48e`), FSI sample-mask fixes
  (`7c999ca76`, `3df64c029`). Check the git log before you start one. Some landed on 2026-08-18.
- AOT OOM fixed (2026-09-20): JITLink gave each function one r-x and one r-- page, 2 VMAs each,
  and `vm.max_map_count` (65,530) killed titles above about 32,000 functions (Banjo, Gears,
  MagnaCarta 2). `XeSlabMemoryMapper` (llvm_object_cache.cc) puts JIT code in 64 MB rwx slabs.
  Device: Banjo-Kazooie 38,104 functions compile in 693 s with 3,801 VMAs; Blue Dragon runs.
  The object cache is per app build (`__DATE__ __TIME__` of llvm_assembler.cc); a rebuild of that
  TU costs one cold compile per title. Note: `docs/research/20260920-compile-overlay-ui-thread.md`.
- Game patches work on Android since 2026-09-20. Before, `XexModule::LoadContinue` ran the AOT
  precompile before the patcher, so compiled code never saw a patch; every patch was inert. The
  July "dynamic-res cap 640" patch on the device targets the planar-reflection width, not the field.
- BD field, village start, play-button path (2026-09-20): 9.9 fps control; **15.8 fps with the
  bundled "No anti-aliasing, single pass" patch** (the game's own FSAA flag, also ends predicated
  tiling); 11.9 with the game's render rate at 0.75; shadow map 512 flat. Patches live in
  `assets/patches/4D5307DF-thor.patch.toml`, toggled in the Game Patches screen, all off by
  default. Note: `docs/research/20260920-reblue-render-levers.md`. Route and fps: `xenia_route`,
  `xenia_press`, `xenia_fps` (tag `xenia-fps`), `tools/thor/bd_patch_ab.py`.
- Largest unclaimed CPU item: LLVM functions are not in the a64 indirection table. Every a64 to LLVM
  call pays a full `ResolveFunction`. See `llvm_backend.cc:251`.
- Cvar rework of 2026-09-18: 18 validated levers have Android code defaults; 49 menu toggles are the
  control surface and an off toggle writes false, so the menu wins both ways. The MCP sets them.
  Details in the worklog. Not verified on device.
- XNA user mode (edge series of 2026-09-19, 18 commits): not ported, by decision on 2026-09-20. XNA
  titles do not run. Size and start point in `docs/research/20260920-upstream-sweep-arm64-adreno.md`.
- Upstream port pass of 2026-09-18: 90 commits landed (Tier 1 to 4). Windows and NDK builds pass.
  x64 corpus 14,333 failures, unchanged. Device tests are owed; the list is in
  `docs/research/20260918-upstream-triage.md`, section "Port status".

## 9. Device control: the debug server inside the emulator, and the MCP client

**The server is inside the emulator (user, 2026-09-20).** `DebugServer.java` runs in the emulator
process and serves HTTP on port 41337 (debug builds). Its endpoints read the running engine directly
through `src/xenia/ui/debug_api_android.cc`: `/status`, `/fps`, `/threads` (kernel threads with host
tid, CPU ticks, state, wait reason, lr, r1, r3), `/log` (the in-process ring of 8,192 lines; logcat
rotation does not touch it), `/memory`, `/disasm`, `/gpu` (swap count, pipelines created, creation
ms), `/stall` (the last spin-lock stall, the hottest threads over one second, the badge history),
`/screenshot` (PixelCopy of the game surface), `/toggles`, `/cvar`, and the actions `/press`,
`/route`, `/toggle`, `/pause`, `/stop`. Reach it with `adb forward tcp:41337 tcp:41337` or over
wifi (`XE_THOR_API_HOST=192.168.1.33`).

`tools/mcp/xenia_thor_mcp.py` is the PC-side MCP client, a stdio server registered in `.mcp.json`.
Its tools call the in-app server first (`_api()`), and use adb only for what the app cannot do to
itself: install, port forward, the launch intent, force-stop, and simpleperf. `xenia_api` reaches any
endpoint. When a tool still runs an adb command that the app could answer, move it into the app.

| tool | what it does |
|---|---|
| `xenia_device_status` | connection, xenia pid, other emulator pids, foreground app, temperatures, battery |
| `xenia_preflight` | the launch gate: busy, hot, or low battery returns ok=false with reasons |
| `xenia_launch` | the launcher's VIEW intent, the same path as the play button: profiles, optimizations, and Turnip come from the app. No cvar extras (directive 17) |
| `xenia_force_stop` | force-stop and verify with pidof; optional wifi adb disconnect |
| `xenia_logcat`, `xenia_logcat_clear` | filtered log dump saved under `scratch/mcp/`; clear before a run |
| `xenia_aot_progress` | precompile state from the log markers and the overlay log |
| `xenia_fps` | GPU pass timing lines and a median from `gpu_frame_us` |
| `xenia_screenshot` | screencap plus the foreground package, so a capture is never another session's app |
| `xenia_backtrace` | native backtrace of every thread (`debuggerd -b`) for a wedged main thread |
| `xenia_threads`, `xenia_memory` | per-thread CPU and nice; RSS, PSS, heap, and the files directory sizes |
| `xenia_toggles`, `xenia_toggle_set` | the app menu toggles: list with defaults and device values; set one exactly as tapping it. The control surface for every lever |
| `xenia_config_get`, `xenia_config_set` | the persisted `files/xenia.config.toml`, diagnosis only |
| `xenia_build`, `xenia_install` | `thor_build.ps1` modes; install refuses while any emulator runs, then verifies the APK hash |
| `xenia_git_head` | the commit to cite for a build |
| `xenia_press`, `xenia_route` | one gamepad button, or a sequence with waits, through the app's debug gamepad broadcast. Refused unless the emulator is in front. The way into gameplay on a play-button launch |
| `xenia_crash` | the crash picture: `A64 CRASH DIAG` (guest function, nearest function, guest lr and r3 to r6), unhandled host faults, storms, the crash buffer tombstone, the newest app crash report |
| `xenia_profile` | fps, GPU busy, temperatures, per-thread CPU over a window; simpleperf sample of the app with the top symbols by dso (no root needed, the app is debuggable) |
| `xenia_probe` | launch a title, wait for the load, screenshot timeline with fps per interval, optional route, crash picture |
| `xenia_patches`, `xenia_patch_set` | the game patch files on the device: list and toggle one `[[patch]]` by name, as the Game Patches screen does |
| `xenia_guest_dump`, `xenia_disasm` | dump guest memory of a title to `scratch/mcp/` (diagnostic cvars, restored after), and disassemble PowerPC from a dump |
| `xenia_stall` | the stall picture in one call from inside the app: the last spin-lock stall record, the hottest threads over one second with wait channel, the badge history, the GPU counters, the stall and crash lines of the log ring, and a verdict. `xenia_probe` calls it by itself after two intervals without a frame |
| `xenia_api`, `xenia_log`, `xenia_shader_cache` | any endpoint of the in-app server; the in-process log ring with a filter; the pipeline creation lines and the cache files on the device |

Rules that stay in force with the MCP: force-stop after every run, never use `adb shell input keyevent`,
say the battery level when you launch, and stop polling when the user says stop.

The MCP is the control surface for debugging, fixing, measuring, and profiling. Every device action
goes through it. If the server fails to connect, run `python -m py_compile tools/mcp/xenia_thor_mcp.py`,
fix the file, and call its functions from Python (`sys.path.insert(0, 'tools/mcp'); import
xenia_thor_mcp as m`) until the session reconnects with `/mcp`. `thor_build.ps1` runs that check
before every build.

**The MCP grows with every issue (user, 2026-09-20).** When a bug or a mistake costs manual steps,
the fix commit also adds the tool or the reflex that returns those steps in one call. The test: the
next time the same class of problem appears, one MCP call must give the picture. Record the tool in
the table above. Examples: the Banjo spin-lock stall of 2026-09-20 took a profile, a thread table, a
diagnostic build, and a decode by hand; it became `xenia_stall` and the probe's stall reflex. The
compile order bug became `xenia_patches`. The crash decode became `xenia_crash`.

**Speed of the loop (user, 2026-09-20).** The loop is: syntax check (seconds), native build (2 m 12 s,
ARM64 only), `xenia_install` (3 s, hash verified), `xenia_probe` (the load plus the probe length).
Keep every step at that size. Run the build in the background and write the note or the next tool
while it runs. Never wait on a step that a tool can do; never repeat a step by hand that a tool does.

### Speed rules (user, 2026-09-21: "too slow to fix bugs and keep moving on")

The dirty-disc dialog of 2026-09-20 cost 14 device runs and two hours because the order was
wrong: guess, rebuild (2 min), wait for the device to cool, run (3 min), read. The order is:

1. **Kernel, HLE, and file-system bugs reproduce on the PC.** The Windows build runs the same
   kernel. Banjo's ISO is at `scratch/banjo/banjo.iso` (never committed); the canary oracle is
   `scratch/oracle/xenia_canary.exe`. A PC run is 30 to 75 s with no thermal wait:
   `build/bin/Windows/Release/xenia.exe --storage_root=scratch/banjo/pc --log_file=... --mount_cache=true scratch/banjo/banjo.iso`.
   When the fork fails and the oracle passes, diff the file-I/O traces (the June 7 method).
2. **No rebuild for state at a kernel export.** The in-app MCP `trap` tool arms a breakpoint on
   an export by name, records every guest register and 256 stack words, and with `pause=true`
   holds the thread so `memory` and `disasm` read the live state. `trap_release` continues.
3. **Bypass first when a bypass exists, then understand.** A cvar or toggle that skips a
   failing check is a 10-minute test. (Here the bypass led to the game's own exit, which the
   git history already said: `cc45b9efc3`. Read `git log --grep` for the symptom first.)
4. **Get the decisive data before the first guess.** One diagnostic run with the file-I/O
   trace, the stall picture, or a trap beats four guesses.
5. **Do not wait for a cool-down before a CPU-side test.** The 70 C abort protects the GPU;
   a check at +21 s in the capped intro runs at 52 C.
6. **Do the next step while the device or the build runs.** A build is 2 min, a probe 3 min;
   write the next patch, the note, or the tool in that time.

### Debug loop rules (2026-09-20)

These come from one day in which nine native builds and several wrong conclusions cost more time
than the fixes.

1. One run must return the complete evidence packet: the full log saved to a file from launch
   (`xenia_logcat` after `xenia_logcat_clear`), the crash picture, screenshots, and the fps summary.
   Never conclude from a rotated logcat buffer. The warm load writes 40,000 lines in 15 s; the
   buffer is 256 KB unless raised with `logcat -G 8M` (resets at reboot).
2. Check the screen is awake before a launch. A sleeping panel gives a black screenshot at a
   different resolution, `mCurrentFocus=null`, and a slow load. The preflight reports it. The rules
   forbid the wake key; ask the user.
3. Syntax-check every native edit with NDK clang before a build (section 10). Four of the nine
   builds of 2026-09-20 failed or did nothing because this step was skipped.
4. The build is ARM64 only. The x86_64 ABI never shipped and doubled every native build.
5. The object cache is keyed on a content hash of `llvm_assembler.cc` and `llvm_assembler.h`
   (`build/llvm_lowering_stamp.h`, generated by `tools/build/gen_version_h.py`). GPU, kernel, UI,
   and tool work never invalidates it. Before 2026-09-20 the key was the compile time and every
   rebuild cost a cold compile per title. Do not edit the lowering during other work; when you
   must, say so, because every title recompiles once.
6. GPU work runs with the "LLVM backend" toggle off (user, 2026-09-20): the a64 backend compiles
   in milliseconds per function, so a launch takes seconds and no compile wait sits in the loop.
   The CPU backend does not change a GPU result. Turn it back on for CPU work and for numbers.
7. When the PC can answer, use the PC: `xenia.exe --storage_root=<scratch> --log_file=<file> <iso>`
   boots Blue Dragon in seconds and prints the same kernel log lines.
8. Name the title from the log ("Title name:") or the crash report before you attribute a crash.
9. Files in this tree mix CRLF and LF, and the Bash tool's heredoc halves backslashes. Patch files
   with a script written to the scratchpad, normalize line endings inside it, and keep the size gate.
10. The crash diagnostic names a64 functions by range and LLVM functions by the nearest function
   start (`nearest_fn`), and prints the guest lr and r3 to r6. Read it before any theory.

## 10. Build, tools, and workflow

- Build scripts: `tools\thor\thor_build.ps1` with `-Mode NativeCore`, `ApkShell`, or `FullDeploy
  -DeviceSerial c3ca0370`. Debug: `tools\thor\thor_xenia_debug.ps1 -Mode Capture`.
- Windows does not compile the a64 backend. Syntax-check ARM64 edits with NDK clang:
  `clang++ --target=aarch64-linux-android29 -fsyntax-only <file> -I src -I . -I third_party
  -I third_party/xbyak_aarch64/xbyak_aarch64 -I third_party/fmt/include -I build/version -DFMT_HEADER_ONLY`.
- PPC test build: `MSBuild build\xenia-cpu-ppc-tests.vcxproj /p:Configuration="Release Windows"
  /p:Platform=x64`. Build by project file, not by `/t:<name>`.
- `python xenia-build gentests` does not run here. `vswhere` finds no Visual Studio instance. Use a
  standalone assembler script with `third_party/binutils-ppc-cygwin`.
- `qemu-aarch64` is available through WSL. Use it as the oracle for emitted-sequence rewrites.
  Harnesses live in `tools/qemu/`.
- Game profiles ship in the APK. An update needs a rebuild and a reinstall.
- The device persists `files/xenia.config.toml`. Only `--ez`, `--ei`, and `--es` extras beat it.
- `XeniaOptimizations.applyTo()` and `GameProfiles.applyTo()` run only on the launcher path.
- Skills: `.agents/skills/`. Read `xenia-windows-powershell-command-hygiene` and
  `xenia-thor-remote-debug` before command-heavy Android work. Codex agent rules: `AGENTS.md`.
- Check `git log` before you start an optimization. A parallel session may have landed it.
- Subagents: at most one at a time, read-only search only. See directive 16.
- Use USB adb for installs, logcat, screenshots, and bugreports. Use `adb bugreport` only for
  reproducible crashes. Use Perfetto only for scheduler, input, SurfaceFlinger, or present questions.
- Before long build, deploy, capture, or git work, check for overlapping work by another session.
- Per-agent tool notes. Codex runs in Windows PowerShell 5.1: no `&&` or `||` chaining, `rg` for
  searches, `apply_patch` for edits, `-LiteralPath` for paths with spaces. Claude Code has its own
  file and search tools and a Bash tool (Git Bash) beside PowerShell.

## 11. Git and file rules

- Work on `master`. No feature branches (user, 2026-09-20): all work lands on `master` and is pushed to
  `origin` after each commit. The three old remote branches were deleted on 2026-09-20 after a check
  that `master` contained every commit. Forward-only: never `git revert`.
- Targeted `git add` only. Never `git add -A`.
- Never commit ISOs, keys, dumps, screenshots, or config backups. `scratch/lost-odyssey/lo_disc1.iso`
  stays local.
- End each commit message with: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
- Each commit gets a dated worklog entry in `docs/worklogs/YYYYMMDD.md`. Research-driven changes also
  get a dated note in `docs/research/YYYYMMDD-topic.md`.
- Do not revert user changes or unrelated dirty work.
- Never let a file-writing script and `git` share a command with a newline between them. Use `&&`.
- When a script writes a file: build the bytes first, then open the file for write. Refuse a result
  smaller than the original unless a deletion is intended. Gate `git add` on a size check. Read the
  diffstat before you believe a commit. A script truncated this file to 0 bytes on 2026-08-07.
- A stale `.git/index.lock` blocked every commit once. Check the lock timestamp before you blame a
  concurrent process.

## 12. Where things live

- Research notes: `docs/research/YYYYMMDD-topic.md`. The experiment ledger:
  `docs/research/experiments.db` (`tools/exp_ledger.py`).
- The game ledger (user, 2026-09-20): `docs/research/games.db` through `tools/game_ledger.py`, one
  row per title (status, fps, build, blockers) and one event per finding, fix, patch, or
  measurement, exported to `docs/games/STATUS.md`. Update it and re-export in every commit that
  changes a title's state.
- Worklogs: `docs/worklogs/YYYYMMDD.md`.
- Archive of the former CLAUDE.md to 2026-09-18: `docs/research/20260918-claude-md-archive.md` and its index.
- Route capture skill: `xenia-blue-dragon-route-capture` under `.agents/skills/`.
- Device scripts: `tools/thor/`. GPU harness: `tools/edram_bench/`. PC trace A/B: `tools/pc/`.
- Memory files: `MEMORY.md` index in the Claude memory directory.
