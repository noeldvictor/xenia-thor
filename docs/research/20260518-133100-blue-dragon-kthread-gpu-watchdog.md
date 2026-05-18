# Blue Dragon KTHREAD Timer And GPU Watchdog

Date: 2026-05-18 13:31 EDT

This note records the Blue Dragon Disc 1 status after the ARM64 scalar and
vec128 mini-JIT fallback walls were cleared.

## Sources Checked

- Local Thor captures in `scratch/thor-debug/20260518-*`.
- Upstream Xenia `xboxkrnl_video.cc`, checked 2026-05-18:
  https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_video.cc
- Xenia Canary source clone under `scratch/upstream/xenia-canary`, checked
  2026-05-18:
  https://github.com/xenia-canary/xenia-canary

## Findings

- Blue Dragon is past the earlier ARM64 mini-JIT fallback wall. Recent captures
  did not show ARM64 fallback, unsupported opcode/type, Android tombstone, or
  native fatal-signal lines in the launch window.
- The draw wait loop at guest function `8246B408` was polling the current
  guest KTHREAD uptime at `r13 + 0x100 -> KTHREAD + 0x58`. Before the fix,
  that value stayed zero, so the draw thread waited forever.
- `arm64_update_kthread_time` now writes `Clock::QueryGuestUptimeMillis()` into
  the current guest KTHREAD before ARM64 function dispatch. Capture
  `20260518-125759-*` proved the draw wait `global_tick` now advances and the
  loop exits after the 5000 ms threshold.
- After the timer fix, Blue Dragon enters its D3D GPU watchdog path and prints:
  `The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0`.
- The first GPU dump showed `CP_RB_RPTR = 0` while `CP_RB_WPTR` advanced. The
  command processor now mirrors live ring read/write pointers into MMIO
  registers `0x01C4` and `0x01C5`; capture `20260518-130625-*` proved the dump
  changed to matching read/write pointers such as `0x1f/0x1f` and `0x25/0x25`.
- The watchdog still fires after ring pointer mirroring, so the current wall is
  not the stale ring read pointer alone.
- Canary has a useful `clear_memory_page_state` path and zeroes the ring buffer
  on initialization. Those were added as research-grade Android bring-up
  support. They did not get Blue Dragon to title in capture `20260518-131640-*`.
- Focused guest PPC dumps show `VdSetGraphicsInterruptCallback` installs
  callback `8246DBB0` with user data `4000E880`. The draw wait loop polls the
  object token pointer at `object + 0x2A10`, the saved token at wait-state
  `+0x8`, and the KTHREAD tick at `+0x58`.
- The `gpu_blue_dragon_kick_wait_token` experiment increments the observed
  token pointer `FFCA3000`, but capture `20260518-132520-*` still hit the same
  D3D watchdog. The wait state simply captured the new token value and then
  timed out again. Token movement alone is not the missing completion.

## Current Interpretation

The port is no longer stuck at the first CPU bring-up wall. It now runs enough
guest code to load later Blue Dragon pack files, create the draw thread, run the
KTHREAD-timed draw wait, and execute the game's D3D watchdog code.

The title-screen blocker appears to be a graphics completion or presentation
contract problem: command ring pointers drain, but Blue Dragon still decides
frame 0 never completed. The lack of `XE_SWAP` / real `VdSwap` activity in the
current captures supports this.

## Strategy Decision

Do not keep chasing every Blue Dragon symptom one at a time while the AArch64
backend is still a partial mini-JIT. The faster lane is:

1. Build broader ARM64 JIT/HIR coverage in batches from opcode coverage and
   known fork audits.
2. Keep Thor Blue Dragon runs as milestone checkpoints after each batch.
3. Use focused Thor debugging only when the log shows a stable wall, as with
   the KTHREAD timer and D3D watchdog.
4. Keep all title-specific hacks off by default and clearly marked as research
   experiments.

This avoids two bad loops: blindly porting everything with no device proof, or
debugging one title forever on top of incomplete CPU/GPU emulation.

## Next Work

- Keep defaulting experimental hacks off:
  `gpu_interrupt_on_ring_idle` and `gpu_blue_dragon_kick_wait_token`.
- Improve ARM64 coverage tooling so Blue Dragon, homebrew, and microtests can
  produce a ranked HIR/opcode/function backlog.
- Study the `has207/xenia-edge` AArch64 backend shape for broader direct
  emitter coverage, but keep license/provenance notes before copying code.
- Add GPU ring/PM4/Vd instrumentation for:
  `VdGetSystemCommandBuffer`, `VdSwap`, PM4 `PM4_XE_SWAP`, interrupt callback
  dispatch, ring write/read pointer changes, and frame-close state.
- Use RenderDoc only once normal logs show a real guest frame/swap submission.
