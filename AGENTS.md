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
  open). The `XamShowDirtyDiscErrorUI` dialog on the device is closed (2026-09-21): it was the
  profile cvar `xam_redirect_xui_font_cache`, removed from the Banjo profile. The PC dialog
  was the x64 NaN helper (`943c14d15f`). START then stopped at the sign-in prompt: the login
  persists through `logged_profile_slot_0_xuid` in the config file, which Android never reads,
  so `ProfileManager` now signs the existing profile into slot 0 at start. START reaches the
  main menu and three A presses reach the opening story in the world (2026-09-21).
  The black and gray grass on the Spiral Mountain title screen (PC Vulkan, and the device's
  "black glitches around trees") is closed (2026-09-21, commit of this line): the tree pushed
  BOTH texture sets as push descriptors, and a pipeline layout may hold one push descriptor set
  (VUID-VkPipelineLayoutCreateInfo-pSetLayouts-00293). NVIDIA and Turnip dropped the vertex set,
  every vertex texture fetch read zero, and the grass vertex shader (color = vertex color *
  terrain texture) output black. Found with RenderDoc: identical shader, bindings, constants
  and textures in the good and bad captures; the post-VS color output was 0. Only the pixel set
  is pushed now. The PC title screen scores GOOD (lower_black 0.000) in every run since.
  Open on the device (2026-09-21 night): the shadow atlas (1024x1024 k_24_8 at 156AD000,
  resolved every frame) ACCUMULATES casters frame after frame (`not_far24` in the resolve trace
  climbs 599 -> 86033 -> 600274, then resets), where the PC stays under 7000. The lower half
  of the title frame is dark and shadows flicker over objects. Not the launcher toggles
  (gate_rt_update, ownership memo, whole-draw cull, draw merging, fp16, UMA direct, the xendroid
  parity set, TU_DEBUG=nolrz each A/B'd with `tools/thor/atlas_ab.py`): it is in the device
  render-target path or the driver. Next: RenderDoc on the device (the layer from the
  renderdoccmd APK, the in-app trigger cvar), or the device .xtr replayed on the PC with
  `--vulkan_trace_resolve_checksum`. The puzzle-transition stall hit 5 of 14 launches during
  the A/B; it blocks measurement and needs the r3 trap next.
  The stall's fault site is named (2026-09-21 night): `821E3080` is `RtlpDeCommitFreeBlock` in
  the game's own statically linked heap (reNut's function table), the heap whose critical
  section pointer is null under the emulator. The recomp replaces that heap and the memcpy/memset
  family with native code; xenia now does the same at the same guest addresses
  (`src/xenia/cpu/guest_crt_hooks.cc`, cvar `cpu_guest_crt_hooks`, launcher toggle "Run the
  game's C runtime as host code"): a thread-safe small-block heap inside guest memory, host
  memmove/memset. Two lessons from the first device build (2026-09-22): (a) the hooks planted
  nowhere because the lazy code hash was computed after the game patches changed code bytes -
  the kernel's pristine hash (UserModule::CalculateHash) now feeds XexModule::code_hash, and the
  plant happens in XexModule::DeclareFunction; (b) with the hooks planted, only a64-compiled
  callers reached the host heap: the LLVM guest-call helper and GuestFunction::Call entered the
  translated body of a kExtern function. Both now dispatch to the handler. With every call
  routed the device booted to the puzzle on the host heap (48 traced calls, no fault), but the
  PC deadlocked once at boot with the hooks on (every thread waiting on events), so
  `cpu_guest_crt_hooks` defaults OFF with the A/B levers `cpu_guest_crt_hooks_heap`,
  `cpu_guest_crt_hooks_mem`, `cpu_guest_crt_heap_zero_all`, `cpu_guest_crt_heap_no_recycle`;
  a PC series (four rounds of on / on-without-mem / off) and a device series are the next data.
  The offline tools that made this a 40-minute diagnosis instead of a night: the PC code dump
  (`--dump_guest_mem_*` on the Windows build, 15 s) + `guest_disasm_offline.py` with reNut's
  names (meInternalAlloc, the trailer 0x9876+index at block end, RtlSizeHeap as the heap-of-
  pointer probe), and the heap's own boot trace.

  Performance, corrected (2026-09-22 night, `emit_ab.py` and `title_probe.py` at the title
  after the puzzle, device, cool): 6.7 fps. The GPU takes 1 to 13 ms per frame and IssueDraw
  14 to 23 ms, so the emit phase is NOT the bound; the earlier "CPU-bound in emit" reading came
  from six no-op levers (both merge cvars were already off; the attract camera moved between
  the windows). The hottest host threads are two of the game's own worker XThreads (62% and
  59% of a core) and a third at 29%; the main thread and the command processor sit below them.
  simpleperf over 15 s: 48% in JIT'd guest code spread over many functions (the top one,
  `sub_82942B90`, 3.2%), 22% in libxenia (4% `xe_llvm_resolve_cached` and
  `xe_llvm_resolve_function`, the indirect-call resolution; 3% bionic mutex slow paths,
  `NonPI::MutexLockWithTimeout` and the 16-bit CAS), 21% in the kernel in one unnamed symbol
  (the mutex and futex signature), 6% libc. Next: the callgraph run (`title_probe.py
  --callgraph`) names the mutex and the syscall callers; the levers are the lock it names, an
  inline cache for the indirect calls, and `__emutls_get_address` (0.5%, emulated TLS).
  The host-heap stall study ended at 3 of 4 clean boots on the PC with freed blocks kept intact;
  the residual stall is the XCTD read queue (the wait chains name it); default off.

  The puzzle-transition stall, named (2026-09-22 night): a guest call through a garbage
  function pointer. Instance 1: the main thread in `sub_82CE6A38+B0` (a per-tick virtual-call
  loop under `appMainTickPreDraw`) with r3 = 0x361580, an unmapped address; the vtable read
  returns 0 from the readable zero page, and the call to guest 0 loads the indirection table's
  own base (host 0x200000000, code 2). Instance 2: worker thread B in `sub_82951B78+2EC` with
  ctr = 0x3F92C642, a float. The fault handler parks the thread, every other guest thread waits
  forever, the swaps stop. `/trap` now holds the parked thread's full record (the fault hook,
  129ff78e14). The "Null critical section" is `RtlFreeHeap(heap = 0)`: the game's
  `sub_82240178` matches a block's trailer tag against its three "me" heaps (40100000, 40300000,
  40500000; all serialized, locks intact) and returned -1. It did not precede instance 2, so it
  is a second symptom, not the cause. The discriminator ran (`stall_study.py --runs 4 ""
  "cpu_backend_llvm=false"`, 02:26 to 02:45): with the LLVM backend, 3 of 4 launches froze at
  swap 836 to 838 (the same frame every time; the fourth stayed alive); with a64 only, 0 of 4
  froze and all four ran past swap 1045 (the title-colour check missed on the slower backend, so
  the study now counts only frozen swaps as a stall and reports "alive" separately). The stall
  is an LLVM-backend code-generation defect until shown otherwise. Arms so far: the residency
  options off froze 1 of 2 valid runs; every run without the object cache showed swaps 0 (LLVM
  compiles the title at boot for longer than the harness window), so the "fresh LLVM freezes
  3 of 3" and the first barrier arm were VOID, not stalls (the study now says NO_BOOT for
  swaps 0). `llvm_bisect.py` kept [82000000,82400000) (one freeze) and then found neither
  quarter freezing in 2 runs each, which points at a pair (an LLVM caller and an a64 callee)
  rather than one function; stopped. The two candidate mechanisms are in the tree's own
  comments (llvm_assembler.cc ~805-870): an a64 guest entry reached raw by a musttail jump
  reads its backend context through x19, which LLVM did not reserve (fix: `+reserve-x19`,
  object cache version 4), and LLVM callers keeping values in x22-x28/q8-q15 across a call
  into a64 code (`cpu_llvm_guest_call_clobber_barrier`, a cache-key letter, so a warm-up
  launch rebuilds the cache first). Results (04:00 to 04:15): the barrier arm 0 of 4 classic
  freezes (one run froze at swap 307 instead); the x19 arm (`+reserve-x19`, cache v4) 0 of 4
  classic freezes (one run froze at swap 308). Baseline the same night: 3 of 4. Each arm ran on
  a FRESHLY BUILT object cache, the baseline on the old one, and the cache stamp hashes only
  `llvm_assembler.cc/.h` (`tools/build/gen_version_h.py`), not `ppc_context.h`, the helpers'
  signatures or the a64 thunks - so a stale cache was the third candidate. The control (no
  x19, cache v5, 04:25) froze 2 of 2 at the classic point (swaps 843 and 858, one with
  ctr = 0): a fresh cache is not the fix. RETRACTED at 05:00: the shipped x19 build (rebuilt,
  cache v4 rebuilt) froze 2 of 2 at swaps 838 and 839 with the instance-1 signature
  (`sub_82CE6A38+B0`, ctr = 0). So neither the x19 reservation nor (by the same doubt) the
  barrier is shown to fix it; the freeze rate moves between 0 and 4 of 4 across batches of
  four, and four launches cannot separate a fix from a lucky batch. The x19 reservation stays
  (it costs one register and the a64 contract does need it) but is not a verdict. Rule 7
  applied to myself: a 0-of-4 is a claim about the batch first. The skip arm
  (`cpu_backend_llvm_skip_addrs=82CE6A38 82951B78`, 05:02) froze 1 of 4, at swap 858 with NO
  fault record (a freeze without a parked thread: a third shape, or a missed /trap read).
  The asm dump launch then showed `LLVMfallback fn=0x82CE6A38 opcode=is_false -> a64`:
  the instance-1 crash site is an a64 function in every build, so the garbage object pointer
  in its array is DATA corrupted by other code, not that function's code generation. State
  at 05:15: the freeze needs the LLVM backend (a64-only 4 of 4 alive, one batch), it is a
  call through a garbage pointer read from game data, and no single lever tested tonight
  (residency off, x19, barrier, the two callers on a64) is shown to remove it. Next, in this
  order: an 8-launch a64-only arm and an 8-launch LLVM arm on the same evening (the rate
  swings 0 to 4 of 4 between batches of four); then `llvm_bisect.py --runs 4` (the window
  bisection, one frozen run keeps a half); then a write trap on the container's array
  (`sub_82CE6A38`'s r28+0x18) to name the writer. Tools for all three exist.
  The a64-only arm ran (05:25 to 05:55): 0 of 8 froze, all eight past the freeze point
  (12 of 12 tonight), against ten-plus freezes across the LLVM batches. The freeze needs the
  LLVM backend. Interim for the player: `cpu_backend_llvm=false` (the launcher toggle
  "opt_llvm_backend" off) removes it; the title-scene cost of a64-only is the next A/B
  (`emit_ab.py --launch "cpu_backend_llvm=false"`), and in the stall records the a64 runs
  advanced the swap counter about twice as fast during the watch window.
  THE NUMBER (06:05, `emit_ab.py --launch`, the title after the puzzle, case 40.8 C): LLVM on
  6.3 fps (2,303 draws, IssueDraw 23.0 ms, GPU 3.8 ms; the game's workers at 64% and 59%);
  **a64 only 26.7 fps** (2,233 draws, IssueDraw 14.6 ms, GPU 9.4 ms; the command processor
  thread at 63%, the main thread 55%). Same scene, same draw count, four times the frame
  rate, and no freeze in 12 of 12 launches. The LLVM backend is the title's bottleneck AND the
  freeze's precondition. Ship `cpu_backend_llvm=false` for Banjo; measure Blue Dragon both
  ways before touching its default (the backend was built for it).
  SHIPPED (06:20): `XeniaOptimizations.SHIPPED_TITLE_OVERRIDES` gives 4D5307ED the
  "opt_llvm_backend" toggle OFF unless the user chose otherwise on the per-game screen (the
  menu shows it, directive 17). Verified through the play path: the launcher resolved
  4D5307ED, no "LLVMBackend" line in the log (LLVM never initialized), 26.5 fps at the title
  with the a64 thread signature (command processor 65%, main thread 57%). The title is
  30fps-native and the profile caps it at 30, so the title scene now runs near its ceiling.
  Two harness notes: `/cvar?name=` returns the config value, not a launch-Bundle override
  (it said `cpu_backend_llvm=true` while LLVM was off; read the "LLVMBackend" line instead
  until the getter is fixed); and one `emit_ab.py` baseline printed 30.3 fps with 0 draws
  (no trace lines: not a scene measurement, ignore rows with draws=0).
  Confirmed (06:30 to 07:00): eight play-path launches with the shipped default, 0 frozen,
  all eight past the freeze point (20 of 20 tonight on the a64 path). The cvar getter now
  returns the live value (6d07a9db4f). Still open: the null-heap free (`meInternalFree` ->
  `RtlFreeHeap(0)`, about 1 in 4 once the classic freeze is gone - none seen in these
  eight), Blue Dragon's own LLVM-on versus a64 measurement on `bd_gameplay_route.sh`, and
  gameplay-scene fps for Banjo beyond the title (the title now sits near its 30 cap).
  Blue Dragon, a64 only (07:10, `bd_gameplay_route.sh`, EXTRA='--ez cpu_backend_llvm
  false'): the gameplay scene at 11.7 fps stable for 90 s (gate OK, 1,190 draws, 263,199
  vertices per frame, 0 faults) against the documented ~17.5 fps with LLVM on the 2026-08-09
  build. The opposite of Banjo: the LLVM backend earns its place on Blue Dragon, so the
  global default stays ON and the per-title override is the right shape. The same-build LLVM
  arm follows after a BD cache warm-up.
  `cpu_global_lock_mutex=false` (04:45, one run): the transition passed, then no frames with
  the main thread and one worker at 100% each - the livelock the original mtmsr comment
  predicted. The per-thread depth alone is not a substitute for the mutex; the lever stays
  off and the kernel-time attack needs another shape (a cheaper lock, fewer enter/leave
  pairs, or the per-section analysis of what the guest does under mtmsr). Harness lesson of
  the same hour: `xenia_install` refuses while the app runs and the chain read its answer as
  a dict without `exit` - force-stop first, and check `match` before trusting a device run.
  The swap-307/308 freeze is the SECOND bug, now with its chain: the main thread faults in
  `RtlpInsertFreeBlock+8` <- `RtlFreeHeap+1CC` <- `meInternalFree+BC` <- `sub_82364BB8+54`
  with ctr = `DoWork_CStreamingWaveBank_XACT`: `sub_82240178` matched no heap's trailer tag,
  returned -1, and the free ran on heap 0 (the "Null critical section" lines). About 1 launch
  in 4 once the classic freeze is gone. Next for it: the trap dump of the block at the
  null-CS hit (`title_probe.py` arms it) and `RtlSizeHeap`'s answer for that block.

  Retro 2026-09-21 (the stop ritual): slow = one device launch per cvar, 50 launches for four
  facts, 40% of them lost to the stall and the rest to heat; the tool that would have made it
  fast = live `cvar_set` A/B inside one launch with a per-frame metric, RenderDoc headless on the
  PC, the offline code dump with the recomp's names; exists now = yes for all three
  (`atlas_ab.py`, `pc_goto.py --renderdoc` + `tools/renderdoc/*`, `guest_disasm_offline.py`),
  and the missing one, a device-side RenderDoc capture, is the next tool to build.

  Retro 2026-09-22 (the stop ritual): slow = a per-frame metric read across live cvar windows
  without the lever's current value (six no-ops read as six wins), and a fault whose one diag
  line the log ring cut and the harness cleared; the tool that would have made it fast = the
  harness refusing a no-op lever, and the fault filling the trap record; exists now = yes for
  both (`emit_ab.py`, `KernelTrapRecordFault`), plus `title_probe.py` (profile and trap in one
  launch) and the offline disassembler decoding past VMX128 words.
  Open (2026-09-21 evening): the puzzle transition crashes a guest thread in two runs of three,
  and the game then stalls with a half-lit puzzle on the panel (every thread waits on the crashed
  thread's semaphore). Two fault shapes so far: a halfword load at guest 0xFFFFFFF8 (null minus 8)
  inside the allocator 821E2F60 on the bundle thread, and a jump to host 0x200000000 (a guest
  target of 0x100000000, a 33-bit value) with the host lr in JIT code. The extended
  `A64 CRASH DIAG` (every fault code, the lr function, r1, the guest back chain) now names it:
  the main thread faults in guest 82716418 with r3 = 0 right after "Null critical section in
  RtlEnterCriticalSection", after the title enumerated the two save games the fork wrote at
  13:57 (XamContentCreateEnumeratorInternal lists them twice) and opened one. With the saves
  moved aside the transition did not crash but stalled early instead: the bundle thread and a
  second thread wait on the async read event F8000058 (NtCreateEvent) while the main thread
  runs inside the allocator 821E2F60 - the async-completion race class of June
  (`xboxkrnl_ntreadfile_force_complete`). Next: a run with `xboxkrnl_file_io_trace=true` through
  `xenia_launch_cvars`, and a look at the save the fork writes (1,411 bytes + a 328-byte
  header under `content/B13EBABEBABEBABE/4D5307ED/00000001`). Banjo's water draws fail in the Vulkan backend on the PC and the
  device alike (tessellation is unimplemented upstream; `PM4_DRAW_INDX(99, 17, 0): Failed in
  backend` = kTrianglePatch). The title screen shows speckle noise on textures (open).
  Details: `docs/research/20260920-banjo-spinlock-protocol.md`,
  `docs/research/20260921-banjo-dirty-disc-font-cache.md`.
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
`/route`, `/toggle`, `/pause`, `/stop`. Added 2026-09-21: `/trap` carries the guest chain, the memory
at r24 to r31 with one dereference, and the stack text; `/threads` rows carry their chain and stack
text; `/frame_stats` and `/goto` (screen-driven routing, `ScreenRoutes.java`); `/backtrace`
(in-process native frames); `/trace_frame` and `/trace_stream` (.xtr into `files/traces`);
`/launch_cvars` (the next launch's diagnostic cvars, `files/debug_launch_cvars.properties`).
The trace viewer activity takes `am start` extras (`target_trace_file`, `trace_viewer_dump_png`,
`trace_viewer_dump_frame`) and renders one frame to a PNG, then quits. Reach it with `adb forward tcp:41337 tcp:41337` or over
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
| `xenia_threads`, `xenia_memory` | per-thread CPU and nice; RSS, PSS, heap, and the files directory sizes |
| `xenia_toggles`, `xenia_toggle_set` | the app menu toggles: list with defaults and device values; set one exactly as tapping it. The control surface for every lever |
| `xenia_config_get`, `xenia_config_set` | the persisted `files/xenia.config.toml`, diagnosis only |
| `xenia_build`, `xenia_install` | `thor_build.ps1` modes; install refuses while any emulator runs, then verifies the APK hash |
| `xenia_git_head` | the commit to cite for a build |
| `xenia_press`, `xenia_route` | one gamepad button, or a sequence with waits, through the app's debug gamepad broadcast. Refused unless the emulator is in front. The way into gameplay on a play-button launch |
| `xenia_crash` | the crash picture: `A64 CRASH DIAG` (guest function, nearest function, guest lr and r3 to r6), unhandled host faults, storms, the crash buffer tombstone, the newest app crash report |
| `xenia_profile` | fps, GPU busy, temperatures, per-thread CPU over a window; simpleperf sample of the app with the top symbols by dso (no root needed, the app is debuggable). The `libxenia-app.so[+offset]` rows are symbolized against the unstripped .so (2026-09-21). A profile on a hot device is not a measurement: at junction 89 C the title screen ran at 7 fps with the main thread waiting and the GPU 35 % busy |
| `xenia_probe` | launch a title, wait for the load, screenshot timeline with fps per interval, optional route, crash picture |
| `xenia_trap_context` | hold a guest thread at a kernel export: registers, request objects, guest chain with disassembly, every other thread's chain and stack text |
| `xenia_guest_disasm` | PowerPC disassembly of guest ranges through the in-app server |
| `xenia_dialog_check` | one Banjo run: dialog time or none, the cause lines, the saved log ring |
| `xenia_goto` | drive a title to a screen by what is on the panel (the app's `goto`): presets per title (Banjo: title, menu, world) or raw steps `until:gold>0.35;press:START;settle:1500|until:lower_black>0.4`; launches when needed; returns the frame stats at each step and a screenshot |
| `xenia_trace_frame` | record the next GPU frame on the device as an .xtr, pull it, render it on the PC with `xenia-gpu-vulkan-trace-dump` (the offscreen presenter fix); the PC render of the device's command stream |
| `xenia_trace_replay` | replay a device trace ON THE DEVICE through the trace viewer's dump mode and pull the PNG: the GPU fix loop with no game boot |
| `xenia_launch_cvars` | diagnostic cvars for the next launch (debug builds), typed by syntax; after the profile and the toggles; clear when done |
| `renderdoc_trigger_capture` (cvar) | in-app RenderDoc capture request, polled at every guest swap; set it live through the nop HID trigger file (`cvar:renderdoc_trigger_capture=1`) or the launch cvars. `pc_goto.py --renderdoc <path>` runs the Windows build under `renderdoccmd` and captures the frame at the screen; `tools/renderdoc/run.ps1 <script> <rdc> [args]` replays headless (`rd_bindings`, `rd_draw_detail`, `rd_draw_io`, `rd_events`, `rd_usage`, `rd_buffer_range`, `rd_image_at`, `rd_postvs_range`, `rd_shader`, `rd_stage_bindings`, `rd_tex_stats`, `rd_dispatch`, `rd_actions`). A .rdc positional argument to qrenderdoc opens the UI and runs no script: the path goes through `rd_in.txt` |
| `vulkan_trace_resolve_checksum_length` (cvar) | with `vulkan_trace_resolve_checksum`: read back only resolves of exactly this many bytes; the line carries `not_far24` (dwords whose low 24 bits are not 0xFFFFFF) and `distinct`. `tools/thor/atlas_ab.py "<cvars>" ...` runs the device A/B on it |
| `xenia_gpu_driver` | the Vulkan drivers installed in the app (Turnip builds, a custom fork) and the selected one; select, install from a URL (`latest://owner/repo`) or push a zip from the PC, delete. The app's `drivers` tool. A driver is a paradigm axis: name the one a measurement used |
| `xenia_backtrace` | now from inside the app: every thread's native frames (a realtime signal, `_Unwind_Backtrace`), symbolized on the PC with `llvm-symbolizer` against the unstripped .so. The hang picture: which host wait each guest thread sits in |
| `xenia_patches`, `xenia_patch_set` | the game patch files on the device: list and toggle one `[[patch]]` by name, as the Game Patches screen does |
| `xenia_guest_dump`, `xenia_disasm` | dump guest memory of a title to `scratch/mcp/` (diagnostic cvars, restored after), and disassemble PowerPC from a dump |
| `xenia_stall` | the stall picture in one call from inside the app: the last spin-lock stall record, the hottest threads over one second with wait channel, the badge history, the GPU counters, the stall and crash lines of the log ring, and a verdict. `xenia_probe` calls it by itself after two intervals without a frame |
| `xenia_api`, `xenia_log`, `xenia_shader_cache` | any endpoint of the in-app server; the in-process log ring with a filter; the pipeline creation lines and the cache files on the device |

Rules that stay in force with the MCP: force-stop after every run, never use `adb shell input keyevent`,
say the battery level when you launch, and stop polling when the user says stop.

The MCP is the control surface for debugging, fixing, measuring, and profiling. Every device action
goes through it. `.mcp.json` starts it through `cmd /c python ...` (2026-09-21): `python` on this PC is the
Microsoft Store app-execution alias, which breaks stdio pipes when a host spawns it directly and the
server then reports CONNECTION_CLOSED although it answers `initialize` from a shell. If the server
still fails to connect, run `python -m py_compile tools/mcp/xenia_thor_mcp.py`,
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

### The faster loop (user, 2026-09-21 night: "plan a better faster strategy")

Fifty device launches in one evening gave four facts; one RenderDoc capture on the PC gave the
descriptor bug in an hour. The order of tools, fastest first:

1. **PC first when the bug shows on the PC.** `pc_goto.py` reaches the title in 45 s, never
   stalls, has no thermals, and the window stays off-screen. RenderDoc headless answers "what did
   the GPU get" in one capture. The device is for device-only questions.
2. **One device launch, many answers.** The device MCP has `cvar_set` (live). A cvar that is
   read live (most `vulkan_*` and `gpu_*` levers) is A/B'd inside one run with a per-frame
   metric (the resolve checksum trace, `frame_stats`), not with one launch per value. A launch
   costs 90 s plus 3 to 5 min of cooling; a live toggle costs 5 s.
3. **The recomp tables are the map.** `reference/reNut/config/renut_funcs.toml` names 4,894 of
   Banjo's functions, `renut_crt.toml` the C runtime, `renut_gpu_funcs.toml` the D3D layer,
   `renut_hooks.toml` the render levers. A fault address is named in seconds; a game-side lever
   is a patch, not an emulator hack; the C runtime runs as host code (`guest_crt_hooks.cc`).
4. **A stall run collects everything once.** The harness keeps the logcat, `xenia_stall`,
   `xenia_crash` and the crash diag in `scratch/mcp/stall-*.json`; nothing is relaunched "to see
   it again". `stall_study.py` counts the stall per cvar set; `atlas_ab.py` measures the atlas.
5. **The Thor needs air.** At the title screen (8 fps, full load) the case passes 41 C in 60 s;
   the harness gates on it and waits. A fan under the device is the cheapest 3x of the whole loop.
6. **Build then install, and check.** `-Mode NativeCore` builds the .so only; `ApkShellDeploy`
   assembles and installs. Read `lastUpdateTime` and the new log line before trusting a device
   result (two verifications today ran the old library).
7. **A harness verdict is a claim about the harness first.** Four PC "TIMEOUT waiting for
   puzzle" verdicts on 2026-09-22 were the parked window: a window entirely off the desktop is
   not composed, its swapchain stops presenting, and PrintWindow returns a blank frame. The
   `live.png` mean (196, white) said so; the game was fine. `pc_goto` now parks a 24-pixel
   sliver at the screen edge. Before reading a TIMEOUT as a stall, look at the capture and at
   the log's wait lines with their chains.
8. **A lever already at its target is a no-op, not a result.** Read the cvar first; `emit_ab.py`
   prints `SKIPPED: already false`. A parked thread's picture is one `/trap` call (the fault
   record), not a log line; the harness keeps the CRASH DIAG lines before it clears the log.
9. **Every retro leads to a change in the MCP or the process (user, 2026-09-22).** The three
   retro lines name a tool or a reflex, and the same day's commits carry it: a new or changed
   file under `tools/mcp`, `tools/thor`, `tools/pc`, the in-app debug server, the trap record,
   or the hooks. The stop hook (`.claude/hooks/thor_retro.py`) refuses a retro day whose commits
   touch none of these. A paragraph that only describes the slowness is not a retro.

### Device-only rules (user, 2026-09-21: "take a step back, figure out why we are stuck in a loop")

The Banjo dirty-disc dialog on the device was declared "the a64 recompiler" six times from
2026-06-07 to 2026-09-21 with no direct evidence. The cause was the Banjo profile cvar
`xam_redirect_xui_font_cache`, a device-only compatibility hack from June. One hour with the
trap tool closed it: `docs/research/20260921-banjo-dirty-disc-font-cache.md`.

1. **When the PC passes and the device fails, list what the PC does not do, first.** The list
   is short and complete: the title's profile in `GameProfiles.java`, the `XE_ANDROID_DEFAULT`
   code defaults in `cvar.h` and where they are used, the launcher extras in
   `EmulatorActivity.java`, and the Android platform files. Test the profile cvars off before
   any backend theory. A backend miscompile is the last hypothesis, not the first, and it is
   tested with the PPC corpus in `src/xenia/cpu/ppc/testing/`, never with a game run.
2. **Capture the failing operation, not the symptom.** The in-app `trap` report carries the
   guest call chain, the memory at r24 to r31 with one dereference (the request objects), and
   the text on the stack; every `threads` row carries its chain and stack text. The PC tool
   `xenia_trap_context` (`tools/thor/guest_trap_context.py`) presents them with disassembly at
   each return address. A dialog is one bit; the request that failed names the cause.
3. **Compare sets before sequences.** Multi-threaded loaders reorder. The device's lookup ids
   were a subset of the PC's except the one wrong id; the per-thread sequences "diverged"
   at index 18 and meant nothing.
4. **Crack the data offline.** `tools/pc/gdfx_read.py` reads the disc; the Banjo bundle table
   is 12-byte entries `{id, offset, size}` after a 0x14-byte header; the id is
   `(type << 24) | (~crc32(name) & 0xFFFFFF)`. Ten minutes of Python beat a device cycle.
5. **A compatibility hack has a date and a reason; re-test it when the reason is gone.** The
   font-cache redirect fixed a white screen on 2026-06-26 that later kernel fixes also fixed.
   The hack stayed in the profile and became the bug.
6. **Reach screens by the panel, not by the clock (2026-09-21).** A timed START landed on the
   static puzzle one run in three; `xenia_goto` waits for the screen's own statistics. It also
   found the real title screen (Spiral Mountain, "A START") that the timed route had never
   reached: START during the puzzle animation goes there, START on the stalled puzzle does not.
7. **A stalled frame is not a rendering bug until the swap count says the game still swaps.**
   The Banjo "lower half unlit/black" frames were the last frame of a game whose bundle thread
   had crashed (an unhandled fault storm parks the thread; every other thread then waits on its
   semaphore). `frame_stats` reports swaps; `xenia_backtrace` and `xenia_crash` name the thread
   and the fault. The PC replay of the same frame's trace renders it whole.
8. **`--skip-preflight` is for one short run, not a series.** Ten runs in an hour with the gate
   off took the junction to 92 C: 8 fps at the title screen and buzzing audio (the pump starves).
   Between runs let the case fall under 41 C; measure nothing while it is above.
9. **A PC-reproducible GPU glitch goes to RenderDoc before any code theory (2026-09-21).** Three
   push-descriptor "fixes" were written from reading the code; none changed the picture. One
   capture of a bad and a good frame (`pc_goto.py --renderdoc`, `tools/renderdoc/*.py` headless)
   showed the draw, its bindings, its constants, its textures and its vertex-shader outputs, and
   the zero color output named the vertex texture set. Then two cvar A/Bs (push the pixel set
   only, push the vertex set only) named the rule: one push descriptor set per pipeline layout.
   No validation layers are installed on the PC; a spec rule with no validation shows up as a
   silent driver difference. The Vulkan SDK's validation layer would have said it in one line.
10. **A rendering trace metric must not depend on the camera.** The title camera pans; a
    screenshot score at "the title" compares different moments. The resolve checksum trace
    (`vulkan_trace_resolve_checksum` with `_length=4194304`, `not_far24` per resolve) measures
    the shadow atlas itself, every frame, on both machines.

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
  -DeviceSerial c3ca0370`. The parameter is `-Mode`; `-Step` is silently ignored under `-File`
  and the script runs the default `FullApk` without installing (2026-09-21: the device ran the
  old library for a whole verification). After `Install`, check `dumpsys package ... lastUpdateTime`.
  Run it from Bash (`powershell -File ...`), not through the PowerShell tool: `2>&1` there turns
  premake's warning into a terminating error. Debug: `tools\thor\thor_xenia_debug.ps1 -Mode Capture`.
- **The native loop is 10 to 20 s (2026-09-21).** `NativeCore` runs ndk-build directly into
  Gradle's object tree and copies the .so to the tree the APK shell reads; Gradle's
  `externalNativeBuild` task took 2 m 43 s for a build with no change (its metadata step dry-runs
  every module). The Android Release configuration links with ThinLTO and a cache
  (`build/thinlto-cache`, premake5.lua); full LTO re-optimized the whole program on every link,
  110 s of a 127 s one-file rebuild. A no-op build is 4 s, a one-file change 10 to 20 s, then
  `ApkShellDeploy` 4 s. `NativeGradle` is the old path. The direct build uses the NDK named by
  `ndkVersion` in `app/build.gradle` (25); a newer NDK's clang fails the tree with new `-Werror`
  warnings. The first ThinLTO build after the switch recompiles everything (about 4 min).
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
