# Blue Dragon Thor Speed Pack Default

Date: 2026-05-20 11:10:34 -04:00

## Question

The current speed capture lane was still measuring the old slow path unless all
validated Blue Dragon A64 speed flags were passed manually. Should the Thor
speed lane enable the known-good pack by default?

## Evidence

Baseline capture:

- `scratch/thor-debug/20260520-110440-*`
- APK SHA-256: `9570772CCAF8A97642DB15D783F329FB8B6DFC496BD8DF8EC1FD74F8EA6A7B2A`
- All Blue Dragon A64 fastpaths were off.
- The old draw-wait wall returned:
  - `8246B408` delta `181968031`, then `261817095`, then `240044989`,
    then `168416144`.
- Simpleperf also showed useful secondary costs:
  - main guest thread/generic generated A64 around 40% children;
  - XMA decoder around 17% children;
  - A64 cold translation around 14% children during the sampled window.

Speed-pack capture:

- `scratch/thor-debug/20260520-110714-*`
- Same APK SHA-256.
- Enabled:
  - `a64_inline_ppc_thread_field_leaf_helpers`
  - `arm64_blue_dragon_draw_wait_probe`
  - `arm64_blue_dragon_draw_wait_fastpath`
  - `arm64_blue_dragon_draw_wait_fastpath_host_counter_time`
  - `arm64_blue_dragon_draw_wait_inline_in_caller`
  - `arm64_blue_dragon_memcpy_fastpath`
  - `arm64_blue_dragon_stricmp_fastpath`
  - `arm64_blue_dragon_jump_table_fastpath`
- Reached the visible Blue Dragon loading spinner at 95 seconds with no searched
  fatal markers.
- `8246B408` disappeared from the top speed-profile rows.
- The next body-time target is again `8272A3A4`:
  - `body_ticks_delta=11000831`, `entries_delta=893616`, `ticks_per_entry=12`
  - later interval: `body_ticks_delta=3708205`, `entries_delta=23703`,
    `ticks_per_entry=156`

## Decision

Enable the validated A64 speed pack automatically in
`Use-BlueDragonSpeedDefaults`. Preserve explicit command-line overrides, so a
capture can still force any speed flag off for A/B testing.

Keep `xma_fast_silence` opt-in only. It is useful for CPU experiments, but it is
not an audio-correct default.

## Validation

Post-change short speed capture:

- `scratch/thor-debug/20260520-111324-*`
- Command did not pass the individual speed flags manually.
- Metadata confirmed the validated speed pack defaulted on.
- `xma_fast_silence=false` stayed preserved.
- Screenshot at 20 seconds reached the Blue Dragon `press START` title screen.
- Filtered log had no new fatal/crash/tombstone marker for the launched process.

## Next

The speed loop should stop re-measuring `8246B408` by accident. The next real
optimization target is the `8272A3A4` decompression/state-machine region, not a
partial `8272A8B4` block splice.
