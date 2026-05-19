# Blue Dragon Scored Resolve Selector - 2026-05-18 21:36 EDT

Scope:

- Add an opt-in Vulkan present probe that scores resolve destinations by sparse
  shared-memory readback instead of manually forcing one address at a time.
- Keep the probe research-only. It is a Thor/Blue Dragon visibility tool, not a
  correctness or compatibility fix.

Implementation:

- Added `vulkan_present_scored_resolve_on_swap`,
  `vulkan_present_scored_resolve_min_width`,
  `vulkan_present_scored_resolve_min_height`, and
  `vulkan_present_scored_resolve_budget`.
- Extended Vulkan shared-memory readback with sample, nonzero, varying, checksum,
  and score statistics.
- Added a scored present candidate, a one-entry fallback candidate, and stale
  candidate clearing when a later readback of the same address becomes zero.
- Fixed scored/forced candidate metadata to distinguish surface width from
  memory pitch. Blue Dragon's 80x45 downsample chain uses width 80 with pitch
  96; treating pitch as width stretched padded memory into visible garbage.
- Added Java and ADB helper plumbing for the new cvars.

Validation:

- `git diff --check` passed with only normal LF/CRLF warnings.
- `NativeCore` passed.
- `FullDeploy` passed and installed on Thor serial `c3ca0370`.
- APK SHA-256 after the width/pitch fix:
  `FE58A1A7973940A19744B270EC477B8AC456A7FF6B3EABBFD03051944130CD81`.

Captures:

- `scratch/thor-debug/20260518-211415-*`: first scorer pass, stale all-time
  best selected.
- `scratch/thor-debug/20260518-211930-*`: stale clearing without fallback;
  no scored source survived to swap.
- `scratch/thor-debug/20260518-212346-*`: fallback enabled; 96x45/pitch-96
  candidate presented and produced a colorful stretched strip.
- `scratch/thor-debug/20260518-212551-*`: 320x180 minimum excluded the tiny
  chain and returned to black/full-size stale candidates.
- `scratch/thor-debug/20260518-213038-*`: width/pitch fix changed the tiny
  chain to 80x45 pitch 96; screenshot became clean dark-blue with no strip.
- `scratch/thor-debug/20260518-213253-*`: low-noise 180 second run at
  `time_scalar=0.75` forced `1C340000`; no watchdog or crash, still flat
  dark-blue.

Findings:

- The scorer proved the prior "recent/full-size" candidate strategy was wrong:
  the best-looking 1280x720 buffers become zero or black by swap time.
- The stable visible candidates are post-process/downsample surfaces such as
  `1C340000` 640x360 and `1F2E0000` 80x45 pitch 96, but they contain only a
  flat dark-blue field in the latest runs.
- The Thor path is alive: the latest long run had `XE_SWAP=1929`,
  `VdSwap=1932`, `XmaContext: kicking=14796`, and zero watchdog/crash/fallback
  lines.
- `time_scalar=0.75` remained stable for 180 host seconds, so `0.5` is not the
  only usable timing value.
- Blue Dragon is still not at title. The current wall is no longer just Android
  presentation; it is guest progress or a missing render-output handoff before
  the actual VdSwap frontbuffers.

Next:

- Stop expanding presenter guesses until we know why VdSwap frontbuffers remain
  empty.
- Add guest progress probes around post-pack-load code paths and cache pack
  handling, especially after `battle_start.ipk` / `item_rec.ipk`.
- Add render causality probes that link non-clear draws, resolves, and the
  final `VdSwap` frontbuffer selection in one short capture.
