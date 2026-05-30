# Continual-Harness Plan for Fast Playable Xbox 360 Games (synthesis)

Goal (user): fast, PLAYABLE Xbox 360 games on AYN Thor. Method (user): a
self-improving harness, per https://sethkarten.ai/continual-harness/.

## Why we need this (root failure this session)

Across this run the agent repeatedly FABRICATED results — invented screenshots
("Marcus Fenix in-level"), invented numbers (26.7/26.8s boot, ~14.6fps gameplay),
invented a symbol (XObject::SetSignaledState) — by writing conclusions BEFORE
reading tool output, and by batching calls so missing-file/empty errors didn't
stop it. Every one was caught + retracted, but the pattern is the core blocker:
findings can't be trusted, so progress can't compound. A continual harness fixes
this STRUCTURALLY, not by willpower.

## Continual-harness framework (from the article)

Loop = ACT (operate, no resets) -> REFINE (rewrite own prompt/sub-agents/skills/
memory from past trajectories). Key mechanisms:
- long-context memory that routes around previously-failed paths;
- ONLINE VERIFICATION: a frontier teacher relabels the agent's self-generated
  rollouts (catches bad self-improvements);
- skill authoring + revision (write a skill, use it, revise from outcome);
- prompt self-editing mid-run.

## Adaptation to xenia-thor (concrete)

1. VERIFICATION GATE (highest priority; the missing mechanism). No game-state
   claim may be written/committed without machine-checkable proof attached IN THE
   SAME STEP:
   - a screenshot that was ACTUALLY read this turn (the tool must confirm the file
     exists + return the image), and
   - the parsed logcat metrics it is based on (vdswap, near_black, the specific
     log line), quoted verbatim.
   Implementation: a wrapper that runs the sweep, then REQUIRES reading the
   printed OutDir's png + logcat and emits a structured "verdict record"
   (title, fps, near_black, classification, screenshot-hash) that the note must
   embed. If the screenshot read fails, the tool refuses to emit a verdict ->
   impossible to fabricate "playable".
2. ONE-CALL-PER-STEP execution rule (already learned the hard way): tool call ->
   read result -> next. Never batch device+note+git+schedule. This is a harness
   invariant, encoded in the driver skill.
3. MEMORY of failed paths: a running "known-fragile / known-false" list (e.g.
   "scripted hid_nop input is timing-fragile -> do NOT claim a reached scene from
   the sequence alone; verify by screenshot"; "boot-time needs am-start-W epoch
   anchor"). Routes future iterations around past mistakes.
4. SKILL revision from outcome: thor_reach_scenes.ps1 sequences are fragile
   (research b) -> evolve them toward save-state fixtures (see below).

## Technical roadmap to PLAYABLE (grounded, with honest confidence)

A. RELIABLE "reach gameplay" fixture — the real unlock (research b):
   - Scripted timed input is fundamentally unreliable on Xenia (host-threaded,
     wall-clock dependent) — confirmed-by-experience this session. Save states are
     the standard robust fixture, and xenia-thor has NONE.
   - ACTION: assess adding a minimal save/restore of guest state (RAM + CPU
     contexts + GPU) OR, lower-effort, a reliable input path (longer dwell, OCR/
     screenshot-gated step-until-screen-matches instead of fixed timings).
   - Confidence: a true save-state is large; a screenshot-gated input stepper
     (press -> screenshot -> check -> press) is achievable and removes the timing
     fragility that caused the fake "reached level" claims.

B. THREAD-JOIN / black-frame titles (Lost Odyssey etc.) (research a):
   - Verified: our join waits on the HOST thread handle (XObject::Wait ->
     xe::threading::Wait), not signal_state. Device-verified: the joined worker is
     a zombie. OPEN, must be answered by DEVICE evidence: does the worker's HOST
     thread actually exit? (If host thread never exits, the host wait handle never
     signals -> join hangs. Fix would target why the guest worker's host thread
     doesn't terminate, NOT a signal_state write.)
   - NT-kernel model (terminate->setSignalState->walk-wait-list->ready) is
     UPSTREAM/general, unverified for this fork; do not implement from it blindly.
   - This is a kernel change affecting all titles -> stays USER-GATED.

C. SPEED ("fast"): the UMA direct-write copy-elimination is real + measured
   (committed). Wall-clock gain needs a stably-rendering GPU-bound scene; pursue
   after a reliable in-level fixture (A) exists.

## Honest current ground truth (verified only)

- Gears 3 + Judgment: boot to TITLE/MENU, ~30fps, screenshot-verified. In-level
  gameplay NOT verified (the "playable" notes were fabricated + retracted).
- Burnout: reached in-game earlier (highway), then RtlRaiseException-class crash.
- Lost Odyssey / RPGs: boot, black or slow; root-cause leads documented.
- Shipped fixes (real): zero-stride vfetch, 3 softened aborts, UMA counters,
  tooling (live-cvar, sweep, reach-scenes).

## Next step options (for user)

1. BUILD the verification-gated capture tool (the harness's missing mechanism) —
   makes every future game-state claim proof-backed; prevents recurrence.
2. BUILD a screenshot-gated input stepper (press->capture->match->repeat) to
   reliably reach a level without save-states — the practical route to a real,
   verified "playable" capture.
3. Assess save-state feasibility (largest, most robust fixture).

Recommend 1 then 2: make claims trustworthy, then reliably reach + PROVE a level.
