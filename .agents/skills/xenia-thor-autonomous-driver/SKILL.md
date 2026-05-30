---
name: xenia-thor-autonomous-driver
description: Use when the user wants hands-off, end-to-end work on the AYN Thor from a Windows/PowerShell agent - "take over", "do it autonomously", "port and upgrade", "get <game> working/full speed", "merge and validate", "control the device". Drives one bounded loop: preflight -> build -> deploy -> launch via real library -> capture proof packet -> classify -> worklog/research note -> commit/merge/push. Composes the existing thor_*.ps1 tools and the remote-debug, powershell-hygiene, and working-fast-autonomy skills. Also encodes recovery from flaky-mount torn reads, phantom git index.lock, and truncated-file corruption.
---

# Xenia Thor Autonomous Driver

Top-level driver for hands-off Thor work. One invocation = one durable result
(a proof-backed code/tooling change, a validated capture, a merge/push, or a
concrete blocker), never just orientation. Obeys the project rule: **no
behavioral claim without device proof.**

## Environment Truths

- Device: AYN Thor `c3ca0370`, native Android, `arm64-v8a`, Adreno 740 /
  Snapdragon 8 Gen 2 class, Vulkan first. PowerPC guest runs through AArch64.
- Shell is Windows PowerShell 5.1. No Bash `&&`, `||`, here-docs, `export`,
  `rm -rf`, or `/c/...` paths. One command per call; newline-separated
  statements with `$LASTEXITCODE` checks for dependent steps. Quote spaced paths
  with `-LiteralPath` or the call operator. See
  `.agents/skills/xenia-windows-powershell-command-hygiene/SKILL.md`.
- Only the agent running on the Windows host can reach `adb`, the device, the
  Android NDK/Gradle build, and the SSH push remote. A sandboxed Linux agent
  cannot - if you are not on Windows, you can only read/edit repo files and must
  hand build/deploy/capture/push back to the Windows side.

## Filesystem Reliability (read before trusting any read)

If the repo is reached through a network/virtual mount, treat mounted-shell I/O
as untrusted:

- Authoritative file reads/writes go through real file tools (Read/Edit/Write
  /apply_patch), never a mounted `cat`/`tail`/`wc`. The mount can serve **stale
  or torn views** - e.g. a complete file showing as truncated mid-function.
- Symptoms of mount trouble: a `git diff` that shows files truncated mid-token
  (`+DE`, `+DECLARE`, `}  // namespace `), brace counts that don't balance only
  in the mounted view, deletes failing with "Operation not permitted", a
  `.git/index.lock` that `cat` reports as "No such file" while `ls`/`stat`
  still show it (a **phantom lock**).
- Before declaring corruption, re-verify the exact byte range with a real file
  read. Most "corruption" through a flaky mount is a torn read, not real loss.

### Recovery playbook

- Phantom `index.lock` blocking commits: it is not on the real disk, so deleting
  it on the Windows host clears it. From a sandbox that cannot delete it, avoid
  index writes - prefer ref-only operations (see Merge).
- Genuinely truncated tracked file: restore the committed copy without the index
  by piping the object back over it: `git show HEAD:<path>` redirected to
  `<path>` (object-DB read, plain write - needs no index lock). Back up the
  suspect working copy first, then re-verify the restored file with a real read.
- Never act on a single mounted read for destructive recovery; confirm twice.

## Preflight (always)

```powershell
adb devices
git status --short --branch
git rev-parse --abbrev-ref HEAD
Get-Content -Raw AGENTS.md
Get-Content -Raw scratch\thor-debug\codex-goal-loop.json
```

Read the newest `docs/worklogs/YYYYMMDD.md` and the one research note closest to
the active bug. If a build/deploy/capture/git operation is already active, do
not start a parallel slice. Confirm `adb devices` shows `c3ca0370` as `device`
(authorized) before any device step.

## Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell -NoSubst
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShellDeploy -NoSubst -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

- Java/XML/resource-only launcher changes: `ApkShell` fast path first.
- Native (C++/Vulkan/CPU backend) changes: `NativeCore`, then a deploy mode.
- Record the deployed APK SHA256 in the packet `meta.txt`; an unrecorded APK
  hash invalidates the proof.

## Launch + Capture (the proof packet)

Use the real Android launcher/library path for any UX or compatibility claim;
reserve direct `EmulatorActivity` launches for cvar diagnostics only. Clear
logcat before a fresh route, but when the user says "check now" or reports a
fresh crash, capture the current screen + log **before** force-stopping or
clearing. See `.agents/skills/xenia-thor-remote-debug/SKILL.md`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenrecord -Seconds 30
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode UiDump
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode CrashBundle
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_game_status_report.ps1 -LogPath scratch\thor-debug\<packet>\logcat-focused.txt
```

One timestamped dir under `scratch/thor-debug/` per route, holding `meta.txt`
(branch, commit/dirty state, APK SHA256, serial, launch path, cvars, repro
steps), `screen.png`/`screen.mp4`, full + focused `logcat.txt`,
`status-report.txt`, and `ui.xml` when focus/reachability matters. Add a
`bugreport.zip` or `*.perfetto-trace` only when screenshots + logcat cannot
explain a crash/scheduler/present/frame-pacing issue.

## Classify, Don't Guess

Drive game triage to a named class with a tool, not a hand scan:

- No-present / black-frame stalls: count guest `VdSwap(` progress, scoped fatal/
  AndroidRuntime/guest-crash markers, heap/`RtlRaiseException` markers, AAudio
  open, near-black share, and input-after-last-swap. See
  `tools\thor\thor_burnout_no_present_stall_audit.ps1` as the pattern.
- Distinguish: guest execution wait/deadlock (A64 thread snapshots +
  `XboxkrnlThreadWaitTrace`), frontend/UI state, vs render-target/presentation.
  Audio playing + `VdSwap` advancing + black frame points at render-target/
  frontbuffer, not a process crash.

## Speed Work A/B Harness

For any "faster / full speed" change, prove it with a controlled before/after on
a title that is already presenting and progressing:

1. Fixed route, fixed settings, baseline capture (cvar off) - FPS/frame-time.
2. Same route with the change on, e.g. the unified-memory direct-write path:
   `--gpu_uma_direct_shared_memory=true` (default off; forces a non-sparse
   512 MB host-visible+device-local shared-memory buffer, skipping the staging
   copy). A coherency bug here shows as corrupted/black frames - watch for it.
3. Report the delta with both packets cited. Do not claim a speedup without the
   paired capture. Background context:
   `docs/research/20260529-202407-unified-memory-zero-copy-speed-plan.md`.

## Merge + Push

- Default: `git checkout master; git merge <branch>; git push origin master`,
  ideally only after a known-good build.
- Fast-forward when `master` is a strict ancestor of the branch tip
  (`git merge-base --is-ancestor master <branch>`): the merge is just moving the
  `master` ref. If the index is phantom-locked, do it ref-only without touching
  the working tree or index:
  `git update-ref refs/heads/master <branch-tip-sha> <old-master-sha>`
  (the old sha is the compare-and-swap guard; record it for rollback).
- Push needs the host SSH key; a sandbox without it must hand the push to the
  Windows side.

## Record + Stop

- Worklog `docs/worklogs/YYYYMMDD.md`: commands, proof paths, findings,
  blockers, next action - compact.
- Research-driven change also gets `docs/research/YYYYMMDD-HHMMSS-topic.md`.
- Commit validated progress with a dated worklog entry; do not revert unrelated
  dirty work. Do not claim a game is fixed until the actual failing screen/route
  is shown working.
- End on one of: proof marker written, validated patch committed/pushed, a dated
  capture/analysis report, or a concrete blocker naming the missing input,
  failed command, and next experiment.
