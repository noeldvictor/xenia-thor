# Verification Gate v1: Works, But Has a Stale-Logcat Flaw (found by using it)

## What I built

`tools/thor/thor_verify_capture.ps1` — captures the current device screen +
logcat, computes near_black + VdSwap-FPS + fatal markers, and emits verdict.json
with a screenshot SHA256 + evidence block. Refuses to classify (UNVERIFIED,
exit 2) if it can't read the PNG. Goal: make fabricated "playable" claims
structurally impossible by tying every verdict to a screenshot read this run.

## Self-test outcome (honest)

Ran it twice. Verdict: `rendering, vdswap=4040, fps~30.4, near_black=0.69,
png_read=True, sha=F8D03A8B...`. I then READ the actual screenshot
(verify-selftest2-20260530-115435/screen.png, the real bytes): it is the **AYN
Thor Android HOME SCREEN** (emulator launcher icons), NOT a game.

So v1 produced a FALSE "rendering" verdict. The near_black=0.69 was right (the
launcher is colorful), but vdswap=4040/fps~30.4 came from STALE logcat — a prior
Gears session still in the `-t 60000` tail — while the foreground was the
launcher. The classifier trusted stale guest-progress as if current.

## Two concrete flaws to fix (gate v2)

1. FOREGROUND CHECK: before grading, confirm the resumed activity is the emulator
   (`dumpsys activity activities | topResumedActivity` contains
   jp.xenia.emulator...). If not, classification = NOT_IN_EMULATOR, no rendered
   claim.
2. RECENCY: clear logcat at/near capture OR only count VdSwap rows within the last
   N seconds of the capture timestamp (not the whole stale tail). Stale swaps must
   not count as live present.

## Why this is the right kind of progress

This is the continual-harness "write a skill, use it, revise it after observing
the outcome" loop. The gate already did its core job — it forced me to read the
real screenshot, which exposed BOTH a false self-claim AND a real tool bug,
instead of me asserting "rendering" blind. v2 will be trustworthy enough to gate
all future game-state notes.

## Status

Gate v1 committed-worthy as the mechanism, but MUST get the foreground+recency fix
before any verdict it emits is trusted. No game-state claim should cite v1
verdicts. Next: implement v2, re-self-test on the launcher (must say
NOT_IN_EMULATOR) and on a launched title (must match the real screen).
