---
name: xenia-thor-experiment-gate
description: Use before risky Xenia Thor ARM64, Vulkan, Blue Dragon, audio, input, timing, debug-prop, or title-specific experiments to prevent repeated guesses, keep one-variable hypotheses, and record dated research/worklog evidence. AYN Thor ARM64 is the primary gate.
---

# Xenia Thor Experiment Gate

Use this before changing emulator behavior or launching a noisy experiment.
Its job is to keep the loop fast: fewer repeated guesses, cleaner baselines,
and enough notes that the next agent does not rediscover the same wall.

## Gate

1. State the experiment in one sentence:
   `If we change X, then Y should move because Z`.
2. Search prior evidence before editing:

```powershell
rg -n "<guest-pc|cvar|shader-hash|function|symptom|hypothesis>" AGENTS.md docs\research docs\worklogs .agents\skills
rg -n "<guest-pc|cvar|shader-hash|function|symptom>" scratch\thor-debug
```

3. If the same hypothesis already failed or was inconclusive, change the
   evidence plan before repeating it.
4. Pick one primary variable. Do not mix CPU, GPU, audio, timing, and input
   toggles in the same proof run unless the run is explicitly labeled
   contaminated.
5. Decide whether this is:
   - `arm64-speed`
   - `arm64-correctness`
   - `gpu-render`
   - `guest-ppc`
   - `audio-xma`
   - `android-ui-input`
   - `adb-tooling`

## Experiment Packet

For risky changes, create or update a dated research note with these fields:

- hypothesis;
- primary variable;
- rollback switch or file list;
- baseline capture path;
- planned command;
- expected log, profiler, screenshot, or route change;
- failure criteria;
- final status: `succeeded`, `failed`, `inconclusive`, or `superseded`.

Use `docs/worklogs/YYYYMMDD.md` for the session ledger and reserve
`AGENTS.md` for standing rules or current-state facts.

## Thor First

- The AYN Thor ARM64 run is the truth gate for this fork.
- Windows is useful only for fast semantic checks, not as the default priority.
- Do not call a change faster based on Windows alone.
- Do not call a Vulkan/Adreno issue fixed without a Thor screenshot or log.
- Do not call an ARM64 backend change correct without a Thor run that reaches at
  least the same Blue Dragon route milestone as the baseline.

## Stop Rules

- After two failed or inconclusive guesses in the same subsystem, stop and add
  instrumentation, a profiler counter, a focused trace, RenderDoc, AGI, or
  Ghidra evidence.
- If a run uses trace-heavy logging, do not compare its speed against a quiet
  speed lane.
- If a diagnostic hack reaches farther, keep it named as a bring-up probe until
  it becomes emulator-semantics code and survives the baseline route.

## Acceptance

An experiment is closed only when the worklog names:

- capture directory and command;
- changed files or settings;
- result compared to baseline;
- whether the result was clean or contaminated;
- next action or rollback decision.
