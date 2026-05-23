# Continual Harness Adaptation For xenia-thor

Date: 2026-05-23 12:59 EDT

## Sources Checked

- GitHub repo: https://github.com/sethkarten/continual-harness
- README accessed 2026-05-23. It describes Continual Harness as a reset-free
  framework that starts from a minimal environment interface and lets an LLM
  refiner rewrite harness state in place: prompt, sub-agents, skills, and
  memory.
- `agents/utils/harness_evolver.py` accessed 2026-05-23. It implements an
  adaptive refiner schedule, gathers recent trajectories, then runs independent
  prompt, subagent, skill, and memory evolution passes.
- `agents/tools/registry.py` accessed 2026-05-23. It uses scaffold-specific
  tool availability so different agent harnesses can expose different tool
  surfaces.
- License accessed 2026-05-23: MIT.

## What Transfers

The Pokemon-specific emulator, map, battle, and button code does not transfer
to Xenia. The useful portable pattern is the harness-control loop:

1. Keep a trajectory window.
2. Periodically run a refiner over that window.
3. Make CRUD edits to the harness: prompt, skills, subagents, memory.
4. Persist the evolved harness so the next continuation bootstraps from it.
5. Keep the runtime episode continuous instead of resetting after every prompt
   update.

For xenia-thor, the trajectory window is not game buttons and frames. It is:

- recent `docs/worklogs/YYYYMMDD.md` entries;
- recent `docs/research/YYYYMMDD-HHMMSS-*.md` notes;
- `scratch/thor-debug/codex-goal-loop.json`;
- recent `scratch/thor-debug/*-meta.txt` capture metadata;
- current git state and proof-marker state.

The CRUD targets are:

- prompt/contract: `AGENTS.md`, heartbeat prompt, and
  `tools/thor/thor_codex_goal_loop.ps1`;
- skills: repo-local `.agents/skills/*/SKILL.md`;
- memory: dated research notes and worklogs;
- tools: deterministic parsers under `tools/thor/*.ps1`;
- subagents: represented locally as role skills plus the war-room protocol,
  not separate spawned agents unless the user explicitly asks for subagents.

## Implemented First Step

- Added `.agents/skills/xenia-continual-harness-refiner/SKILL.md`.
- Added `tools/thor/thor_continual_harness_review.ps1`.

The helper writes:

```text
scratch/thor-debug/continual-harness-window.md
```

It intentionally writes to scratch so the window does not become repository
noise. The durable result of a refiner pass should be a specific committed
docs/skills/tooling/code change.

Run it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_continual_harness_review.ps1 -Mode Window
```

## How To Use In The Current Blue Dragon Sprint

Use the refiner pass after repeated route nondeterminism or when a new capture
changes the next target. It should answer:

- Is the current Stop-hook/heartbeat prompt stale?
- Which local skill still points at rejected work?
- Which manual analysis command should become a tool?
- Which research note should become durable memory in `AGENTS.md`?
- Should the next slice be capture, parser, Ghidra/HIR analysis, or codegen?

For the current state, the refiner should keep the next target at filtered
`8227F1D8` HIR plus delayed body/block-time, because the latest route-clean
callee split shows `822809F4 -> 8227F1D8` as the measured child wall under
`8227FEE8`.

## Guardrails

- Do not run an unbounded self-editing loop.
- Do not let the refiner change emulator behavior without the experiment gate.
- Do not auto-enable default-off speed cvars from a prompt edit.
- Do not import Pokemon-specific implementation code.
- If any MIT-licensed source code is copied later, preserve attribution and
  license text. The current change imports only the process pattern.
