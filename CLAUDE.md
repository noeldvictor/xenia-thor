# CLAUDE.md — xenia-thor (Xbox 360 emulator on AYN Thor)

Guidance for Claude working in this repo. Read this first.

## Goal
Make Xbox 360 games run **fast and playable** on the **AYN Thor** Android handheld via this xenia fork.
Priority title: **Blue Dragon at full speed**. Thor is ~10-20× more powerful than the Xbox 360, so
poor framerates are pathological/fixable, not hardware limits. Other priority titles: Lost Odyssey,
Banjo, Burnout.

## Device (the ONE target — optimize for it specifically)
- AYN Thor, ADB serial **`c3ca0370`**. SoC: Snapdragon 8 Gen 2 (QCS8550 "kalama").
- GPU: **Adreno 740v2** — a **tile-based deferred renderer (TBDR)**. Freq table MHz: 680 615 550 475
  401 348 295 220 124.8 (**max 680**). No `VK_EXT_fragment_shader_interlock`.
- CPU: Cortex-X3 + A715/A710 + A510; NEON, i8mm, bf16, dotprod.
- ADB path: `C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`
- Package: `jp.xenia.emulator.github.debug`

## Build / deploy / run (Windows)
- Path has spaces → use a subst drive for builds: `subst X: "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor"`.
- Build (foreground; **background tasks do NOT see the X: subst** — use real paths or run in foreground):
  `cmd /c "X:\android\android_studio_project\gradlew.bat -p X:\android\android_studio_project :app:assembleGithubDebug"`
  Native change ≈ 1-2.5 min; java-only ≈ 20s. APK: `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`.
- Install: `adb -s c3ca0370 install -r <APK>`
- Launch Blue Dragon (heavy 3D field scene ≈ 120-135s after launch; intros are slow at 1-2fps):
  ISO `/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
  `am start -W -n <comp> --es gpu vulkan --es cpu arm64 --es apu android --es hid nop --es hid_nop_button_sequence 'start@20000:300;a@26000:300;start@32000:300;a@38000:300;start@45000:300;a@52000:300;start@60000:300;a@70000:300;start@82000:300;a@92000:300;start@102000:300;a@112000:300' --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true --es target '<ISO>'`
- fps proxy: count `VdSwap(` logcat lines per second. Leave the device idle (force-stop) when done.

## Config gotcha (cost real days — remember it)
The device persists `files/xenia.config.toml` which **OVERRIDES compiled cvar defaults** (only
`--ez/--ei` intent extras beat it). A stale cvar left there caused a months-long phantom "black-3D"
bug (it was `arm64_vmx_dot_f32_fastpath=true`, a known BD black-idler). When defaults seem ignored,
check/clear the device TOML. New cvars must be added to the **EmulatorActivity allowlist**
(`android/.../EmulatorActivity.java`, the `copyBooleanExtra/copyIntExtra/copyStringExtra` lists) to be
settable via `--ez/--ei/--es`.

## Measurement harness (built this effort — USE IT, all default-off / read-only)
Enable with `--ez vulkan_trace_draw_outcomes_per_frame true`; reads the `GPU draw outcomes/frame`
logcat line. Fields: `rendered`, `avg_vertices`, `gpu_frame_us` (Vulkan timestamp = real GPU time/frame),
`guest_ms` (scene key), per-phase CPU buckets (`cpu_issuedraw/process/tex/rt/pipe/bind`), barrier-break
attribution (`brk_open/buf/img_sr/img_oth`), draw composition (`prim[...]`, `vtx[...]`).
Diagnostic cvars: `gpu_edram_passes_dont_care`, `gpu_skip_edram_transfers`, `gpu_freeze_at_guest_ms`.
GPU-vendor profiling over ADB (no GUI): skill **`.agents/skills/xenia-thor-gpu-profile`** — KGSL
`gpu_busy_percentage`/`clock_mhz` triage + perfetto `gpu_work_period` capture.

### MEASUREMENT RULE (the #1 reliability lesson)
Blue Dragon's content is a function of **guest uptime**. Relaunch A/Bs are **scene-confounded** — a
faster config races into a different cinematic frame. **Always compare configs at matched `guest_ms`
AND equal `rendered` count.** An unmatched A/B produced a fake "9× load/store" result that was
retracted. Trust profile/counter deltas only on content-matched frames + read the screenshot.

## Current root-cause verdict (as of B60, 2026-05-31)
Blue Dragon heavy 3D scene is **GPU-BOUND on the geometry / per-draw / binning / state front-end**,
PROVEN by Adreno GPU busy% = **77-79% @ 615MHz** on the live heavy scene. Content-matched harness
ruled out: tile load/store ≈3%, render-pass breaks ≈12%, **fill/resolution 0%** (2× res = 4× pixels,
no change). `gpu_frame_us` is pixel-independent and **super-linear in geometry/draw count**
(~2000 tiny draws/frame, avg ~136 verts, ~2000 descriptor binds). So the lever is **reducing per-draw/
geometry/binning GPU work**, NOT the tiler pass/load-store rewrite (that targets only ~15%) and NOT
the CPU (CPU is ~84% idle during these frames). Full chronological detail + retracted hypotheses:
`docs/worklog/2026-05-30-uma-and-cross-game-3d.md` (entries B40-B60).

Shipped wins (default-on, device-verified): bulk PM4 type-0 parse; Blue Dragon draw-wait fastpath.
Black-3D resolved (stale-config artifact). Next: instrument what the ~2000 draws are (prim types +
vertex sizes), then cut draw count / state churn / binning pressure. For the binning-vs-rendering GPU
split, a user-run Snapdragon Profiler / AGI capture is the one external step worth requesting.

## Working rules (do not violate)
- **Never fabricate.** Every fps/number/scene claim must come from device output you read THIS turn;
  read the screenshot before asserting a visual state. If unmeasured, say "not measured".
- **Targeted `git add` only — NEVER `git add -A`.** Commit only the specific files you changed.
  Do not commit game ISOs/keys/dumps, screenshots, or the device config backup (`_xenia.config.backup.toml`).
- Work on the current branch (`master`); commit + push each durable, proof-backed result.
- End commit messages with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Recover ADB drops with `adb reconnect`. Prefer one device action per step; read the result before the next.
- Skills live in `.agents/skills/` (repo-local). Worklog of record: `docs/worklog/2026-05-30-uma-and-cross-game-3d.md`.
