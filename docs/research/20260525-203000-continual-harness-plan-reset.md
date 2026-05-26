# Continual Harness Plan Reset

Date: 2026-05-25

## Why This Exists

The Blue Dragon sprint was making real artifacts, but the harness had started
to steer itself in circles. The active prompt and several skills had become
chronological ledgers. That made every continuation inherit old "latest"
paragraphs, stale next steps, and narrow micro-audit momentum.

This note resets the active plan without throwing away the evidence. Detailed
history remains in dated research and worklog files; active skills and prompts
should stay short, current, and decision-oriented.

## External Source Reviewed

- `https://github.com/sethkarten/continual-harness` accessed 2026-05-25.
- `https://raw.githubusercontent.com/sethkarten/continual-harness/main/agents/utils/harness_evolver.py`
  accessed 2026-05-25.
- `https://raw.githubusercontent.com/sethkarten/continual-harness/main/agents/utils/prompt_optimizer.py`
  accessed 2026-05-25.
- `https://raw.githubusercontent.com/sethkarten/continual-harness/main/utils/stores/skills.py`
  accessed 2026-05-25.

The portable Continual Harness lesson is not Pokemon-specific code. It is the
control loop: read a recent trajectory window, identify failure signatures, and
run separate CRUD passes over prompt, subagents/skills, memory, and tooling.
Its implementation also uses adaptive cadence, a warmup window, explicit
tool-failure extraction, and persisted store entries with effectiveness and
importance fields.

## Clean-Room Adaptation For Xenia-Thor

Map the Continual Harness stores this way:

- Prompt: `AGENTS.md`, `tools/thor/thor_codex_goal_loop.ps1`, and any heartbeat
  automation prompt.
- Skills: repo-local `.agents/skills/*/SKILL.md`.
- Memory: dated `docs/research/*` notes and `docs/worklogs/*`.
- Tools: deterministic `tools/thor/*.ps1` analyzers and launchers.
- Trajectory window: `tools/thor/thor_continual_harness_review.ps1 -Mode Window`.

Run a refiner pass after two repeated or inconclusive slices, after three
counter-only no-speed slices, after stale prompt/skill detection, or after a
user complaint that the work is circular.

## Current Blue Dragon Decision

No sustained 30 FPS proof exists on AYN Thor. The current route remains
Main-Thread/A64 generated-code bound, not broad Vulkan bound.

The immediate edge-payload lane is closed for behavior work. The route-clean
lifetime audit in
`docs/research/20260525-195600-edge-payload-lifetime-audit.md` found:

- `segments_survived_no_kill=0`;
- `f1_reads_before_kill=0`;
- every observed segment first-killed by `CONTEXT_BARRIER`.

Do not materialize edge payload state and do not run a quiet speed A/B from
that lane. A one-off first-barrier audit is also too narrow unless it produces
a reusable backend rule or closes a broader class of barriers.

## New Active Lanes

Prefer structural work with plausible route-wide effect:

- A64 backend maturity: register allocation, guest-state caching, helper ABI,
  block linking, fastmem/addressing, and direct/indirect call dispatch.
- VMX128/NEON lowering: broad opcode families with tests and hit-volume
  evidence, not single-PC shortcuts.
- Hybrid/static hot-function design: only with explicit guest-visible state,
  normal entry fallback, invalidation, and exception rules.
- Measured GPU bulk-work offload: only for Xenos-like work that can stay
  GPU-resident, and only after counters show material CPU-side graphics work.
- Android product UX: controller/settings/profile work is valid user value but
  separate from FPS proof.

## Skill Deletion Decision

No skill directory was deleted in this pass. The repo-local skill inventory is
still differentiated by trigger:

- Thor/device war room;
- route capture;
- experiment gate;
- A64 hotpath;
- AArch64 Snapdragon JIT;
- Xenon PPC;
- Ghidra OODA;
- Vulkan/Adreno;
- Blue Dragon autopilot;
- fast autonomy;
- goal loop;
- continual harness refiner.

Instead, the pass deleted stale chronology from the overgrown skills and turned
them back into procedures. Future refiner passes should delete or merge any
skill that loses its unique trigger, duplicates another skill, stores mostly
dated history, or cannot name a durable output.

## Files Updated

- `AGENTS.md`: replaced the current Blue Dragon lane with a plan reset.
- `.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md`: compressed
  the autopilot and closed stale lanes.
- `.agents/skills/xenia-working-fast-autonomy/SKILL.md`: compressed the worker
  loop and anti-loop rules.
- `.agents/skills/xenia-a64-speed-hotpath/SKILL.md`: compressed the A64 speed
  lane into backend maturity gates and current closures.
- `.agents/skills/xenia-continual-harness-refiner/SKILL.md`: hardened the CRUD
  process and deletion criteria.
- `.agents/skills/xenia-codex-goal-loop/SKILL.md`: added prompt budget and
  loop breakers.
- `tools/thor/thor_codex_goal_loop.ps1`: replaced the giant chronological
  Blue Dragon full-speed prompt with a concise current decision prompt.

## Next Practical Slice

Start from the reset plan. Pick one structural lane and force it to state:

- expected FPS mechanism;
- evidence needed before runtime behavior changes;
- validation/capture plan;
- lane closure condition.

If a slice cannot answer those, it is not ready for a Thor run.
