# Blue Dragon Full-Speed Goal Loop

Date: 2026-05-21 19:23 EDT

## Request

Implement the continuation tooling so Codex keeps researching until Blue Dragon
works at full speed on AYN Thor.

## Decision

Add a dedicated bounded goal-loop mode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
```

The proof marker is:

```text
scratch/thor-debug/latest-blue-dragon-full-speed-proof.json
```

## Full-Speed Definition

Use a concrete stop condition rather than an unbounded "keep going forever":

- Blue Dragon Disc 1 launches from the known Thor SD-card path.
- Visible route evidence reaches title, opening, or gameplay.
- Quiet Thor speed capture records route/FPS/profiler data.
- Commit hash, APK hash, launch cvars, capture path, and screenshot path are
  recorded.
- Searched fatal markers are absent or explained.
- Sustained FPS is at least 30 over a representative 180-second route segment.
- 60 FPS remains a stretch target when the title and route allow it.

## Loop Instructions

Each continuation must:

1. Inspect latest `AGENTS.md`, worklog, research, and Thor captures first.
2. Use repo-local xenia-thor skills as relevant.
3. Avoid repeating rejected experiments without new evidence.
4. Run offline HIR/profiler/Ghidra analysis before risky code edits.
5. Make one-variable changes.
6. Build/deploy/capture on Thor when needed.
7. Update dated worklogs and research.
8. Commit and push validated progress on `master`.
9. Stop only after writing the proof marker or recording a concrete blocker and
   next experiment.

## Safety

The loop is still bounded by max attempts. This avoids runaway sessions, stale
ADB/device state, repeated failed hypotheses, and accidental background work
after the proof target is reached.
