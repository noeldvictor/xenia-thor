---
name: xenia-thor-evidence-discipline
description: "MANDATORY before stating any xenia-thor performance number (fps, gpu_frame_us, GPU busy%, draw counts). Capture device evidence to a file with tools/thor/thor_evidence.ps1, read the SUMMARY it derives, verify the screenshot, and cite the evidence filename. Use this every time a perf claim, A/B, or worklog/commit number is involved - it exists because fabricated numbers were committed twice (see memory no-fabrication-autonomous-failure)."
---

# xenia-thor evidence discipline (anti-fabrication)

**Why this exists:** on 2026-05-31, fabricated fps/GPU numbers were committed to the worklog twice
(B62 and B64-B67, both retracted) by typing expected results instead of measured ones, especially
when running unsupervised. This skill makes that mechanically impossible: numbers come out of a saved
file, not out of my head.

## The hard rule
**No performance number (fps, gpu_frame_us, GPU busy%, rendered/draw counts, speedup ratio) may appear
in a reply, worklog entry, commit message, or memory unless it was produced by
`tools/thor/thor_evidence.ps1` into a `docs/evidence/<timestamp>-<label>.txt` file THIS turn, and you
cite that filename.** If you don't have the file, you don't have the number — say "not measured".

A build that didn't link, a task whose output you didn't read, or a scene you didn't screenshot = NO
number. Twice the fabrication was exactly this (a non-linking build "measured", a task output unread).

## Procedure
1. Build/install whatever you're testing. Confirm `BUILD SUCCESSFUL` AND that your cvar/symbol actually
   compiled (a 2-second "build" recompiled nothing; an undefined-symbol link error means it never ran).
2. Capture evidence:
   - Fresh launch to the heavy scene:
     `tools\thor\thor_evidence.ps1 -Label baseline -BootWaitSec 130 -WindowSec 15`
   - Live A/B on an already-running session (no scene drift):
     `tools\thor\thor_evidence.ps1 -Label off  -Attach -WindowSec 12`
     `tools\thor\thor_evidence.ps1 -Label skip16 -Attach -SetCvar gpu_skip_draws_below_verts=16 -WindowSec 12`
3. **Read the `.png` screenshot** the script saved and state what scene it is. fps is meaningless
   without knowing the scene (an intro/menu reads differently than the heavy field). If two A/B
   captures are different scenes, the comparison is INVALID (scene-confound — this caused the fake
   "9x" result). For same-scene comparison either use `-Attach` live-toggle, or match `guest_ms`
   across captures (guest content is a function of guest uptime).
4. Quote ONLY the `===== SUMMARY =====` numbers from the evidence file, and include `EVIDENCE_FILE:`
   in the worklog/commit so the claim is auditable.

## What the evidence file contains (all device-sourced)
- `vdswap_count` over the window -> `fps` (raw count shown alongside, so it's checkable)
- `kgsl_busy_samples` + `kgsl_clk_samples` -> `gpu_busy_median`, `gpu_clk_max`
- the last `GPU draw outcomes/frame` line -> `gpu_frame_us`, `rendered`, `guest_ms`
- a pulled screenshot (`.png`) of the scene
- the exact launch config / cvar applied

## Notes / gotchas (verified)
- Device serial `c3ca0370`; if Snapdragon Profiler is open it steals adb -> `adb kill-server; adb start-server` to reclaim, or close SP.
- ISO path has spaces+parens: the script passes `--es target '...'` single-quoted (the only form that
  survives the device shell). Don't hand-build `am start` with the raw path.
- `docs/evidence/` is gitignored (raw captures, screenshots). Worklog cites the filename + SUMMARY lines.
- fps proxy = guest VdSwap rate (what the guest actually presented), not the on-screen OSD counter.
- Heavy scene ~120-135s after launch; confirm via screenshot, never assume.

## Self-check before writing any number
Ask: "Which docs/evidence file and which SUMMARY line is this number from, and did I read the
screenshot?" If you can't answer with a real filename, do not write the number.
