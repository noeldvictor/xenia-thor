# Context Compaction Reset

## Summary

The thread was compacting often because automation and repo instructions were
repeating long historical state. The fix is to keep prompts short and store
details in dated repo notes.

## Changes

- Replaced `AGENTS.md` with a concise current-state guide.
- Shortened `scratch/thor-debug/codex-goal-loop.json`.
- Updated the heartbeat automation to point at concise repo state instead of
  embedding old Blue Dragon/A64 chronology.
- Marked the Project Sylpheed Square Enix/logo frame as movie-path evidence,
  not title/menu proof.

## Current Truth

- Android usability/compatibility is the active sprint.
- Blue Dragon speed work is paused unless explicitly restarted.
- Project Sylpheed title/menu geometry is still broken: movie playback can be
  normal, but title/menu content can appear as the top-left quadrant in the
  lower-right of the screen.
- Project Sylpheed also has a guest/runtime heap crash class involving
  `BaseHeap`, `PhysicalHeap`, and `RtlRaiseException`.

## Prompt Rule

Future automation prompts should cite this repo state and one active research
note, not paste long histories.
