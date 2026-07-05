---
name: xenia-experiment-ledger
description: Record and query xenia-thor performance/correctness experiments in a SQLite RAG DB to avoid re-running dead levers. Use BEFORE running any device experiment (check if it was already tried) and AFTER every result (record the verdict). This is the anti-repetition system - the project has 50+ experiments and repeatedly wasted device runs re-deriving dead ends.
---

# xenia-thor experiment ledger (SQLite RAG)

The DB (`docs/research/experiments.db`) is the source of truth for every perf/correctness experiment + its
verdict. It replaces grepping the prose markdown (which failed - we re-ran native-vertex-fetch, EDRAM-black,
draw-merge, the interlock, all already-dead). Tool: `tools/exp_ledger.py`.

## THE RULE (non-negotiable)
1. **BEFORE running ANY device experiment or building any lever:** `python tools/exp_ledger.py check "<keyword>"`.
   If it returns DEAD / FLAT / GFX-LOSS, DO NOT re-run it - read the result and build on it.
2. **AFTER every result (the moment you get it):** `python tools/exp_ledger.py add ...` with the verdict.
   De-dupes by lever name (re-running `add` updates the row), so record freely.

## Commands
```
python tools/exp_ledger.py check "<keyword>"     # search lever/hypothesis/result/notes - DO THIS FIRST
python tools/exp_ledger.py add "<lever>" "<category>" "<verdict>" "<hypothesis>" "<result>" "<notes>"
python tools/exp_ledger.py dead [category]        # all DEAD/FLAT/GFX-LOSS (the do-not-retry list)
python tools/exp_ledger.py wins                   # all WIN levers (the shipped stack)
python tools/exp_ledger.py list [category]         # everything or by category
```

- **verdicts:** `DEAD` (tried, no win, do not re-run) · `FLAT` (no measurable effect) · `WIN` (shipped/validated)
  · `GFX-LOSS` (works but corrupts pixels) · `CONFOUNDED` (untrustworthy cross-run number) · `OPEN` (unresolved).
- **categories:** `cpu gpu edram interlock shader rearch draw vertex fill measurement`.

## Measurement discipline (record it honestly)
Cross-run fps/gpu_frame_us is CONFOUNDED (BD scene complexity swings ~4x/second). Only trust: single-run
in-place alternating A/B on a GPU-busy frame, screenshot correctness, qemu byte-identical, code facts. If a
number is cross-run, verdict it `CONFOUNDED`, not `WIN`/`DEAD`. Post-temp change confirms the field was reached
(no heating = idle/menu scene = invalid run).

## The standing conclusion (query `check "HLE"`)
BD's gap is HLE-vs-LLE, proven by RE2 Remake running on the same Thor via GameNative/DXVK. xenia EMULATES the
360 GPU (slow); the fix is TRANSLATING D3D9->Vulkan like DXVK. Every incremental GPU lever is DEAD/FLAT because
it patches the emulator instead of replacing it. Do not propose more incremental GPU levers - `check` first.

## Note
The markdown `docs/research/experiment-ledger.md` remains the human-readable narrative; the DB is the queryable
index. Keep both in sync for anything significant; the DB is what you `check` before acting.
