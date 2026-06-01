# UMA present-hang root-cause — verdict of record (2026-06-01)

From the design+adversarial-verify workflow (tools/thor/wf_uma_presenthang.mjs, run
wf_d34a3f80-e82, 10 agents: 5 readers → root-cause → 3 adversarial lenses → synthesis).
Question: WHY does `gpu_uma_direct_shared_memory=true` still PRESENT-HANG Blue Dragon
(B74: black, VdSwap=0, process alive, GPU busy 0%, SoC 59→72 °C ⇒ CPU spinning) EVEN WITH
the smart-sync RAW + WAW upload-race guards on — i.e. the hang is NOT the upload race.
Device disconnected ~38 h: code-read only, no perf numbers, no device.

## Verdict: NOT fixable blind — action is `device-diagnostic-only`
The proximate cause cannot be confidently localized from code alone. A blind behavior change
would be a guess. What IS established vs not:

### Solidly ruled OUT (code-verified, HIGH confidence)
- **Host-side `AwaitSubmissionCompletion` self-deadlock / busy-spin — NO.** It bottoms out in
  `AwaitSubmissionImpl` = blocking `vkWaitForFences(…, UINT64_MAX)` (kernel SLEEP, not a spin;
  vulkan_gpu_completion_timeline.cc:155-158), and force-advances the counter when there is no
  matching pending fence (:175-177). A kernel sleep would show GPU-busy + low CPU, NOT the
  72 °C spin. So suspect (a) is out.
- **Smart-sync self-wait on the open submission — IMPOSSIBLE.** `Use()` stamps
  `uma_last_*_submission_ = GetCurrentSubmission()` (the open submission; vulkan_shared_memory.cc:337-347);
  the guard only waits when `wait_submission < current_submission` (:647), excluding the open one;
  the `CheckSubmissionCompletionAndDeviceLoss` clamp + `EndSubmission` (vulkan_command_processor.cc:4462-4469)
  plus the `awaited < upcoming` assert make waiting on un-submitted work impossible.
- **Present-fence dependency — NO.** The present submit waits only on its OWN acquire/present
  semaphores (vulkan_presenter.cc:2376-2440), references no CP-owned fence and never binds the
  512 MB shared buffer. CP↔present is a **lock-free mailbox** (compare_exchange producer/consumer,
  presenter.cc:428-432 / 664-670) that never blocks on CP work. **Present is the VICTIM, not the cause.**
- **The upload race — NO (by elimination + signature).** Already guarded by `gpu_uma_smart_sync`
  (RAW) + `gpu_uma_smart_sync_writes` (WAW), both default-on; B74 hangs WITH them on. An unguarded
  race would fault the GPU (MMU/GMU TDR, GPU NOT idle) — the opposite of B74's GPU-idle/CPU-spin.

### Strengthened finding: the stall is UPSTREAM of present, in the guest/CP path
`VdSwap` is a **guest** kernel export (xboxkrnl_video.cc:628) that the GUEST CPU calls to post the
XE_SWAP PM4 packet. `VdSwap=0` therefore proves the **guest never reached the swap-posting code at
all** — the wedge is in the guest CPU's per-frame loop (or the CP thread before finishing a frame),
upstream of both the CP swap-parse and the host present.

### NOT established blind (MEDIUM-LOW confidence — do not build on it)
- The appealing story that the spin is specifically Blue Dragon's draw-wait fastpath at guest
  `0x8246B408` (EmitBlueDragonDrawWaitFastpathBody, a64_emitter.cc:4812-4991) starved of a
  GPU-progress token writeback is **partly contradicted by that fastpath's own code**: it
  deschedules itself (sched_yield + ~100 µs sleep every 16 hits, a64_backend.cc:134-147) so it
  can't peg a core at 72 °C forever, and it has a 5000 ms timeout that returns "stop waiting"
  unless the elapsed clock (KTHREAD+0x58) is ALSO stalled — unproven. Nothing in the repo ties
  the polled offset `0x2A10` to a host GPU writeback under UMA. So the constructive mechanism is
  a hypothesis to TEST on device, not a basis for a blind fix.

### Do NOT ship blind: the "Use()-parity" change is MISTARGETED
Adversarial verifiers (2 of 3) flagged it: making `UploadRangesDirect` call `Use(kRead/kTransferDestination)`
would NOT change the guard's `wait_submission` (the stamps are already updated per-draw via
`Use()` at vulkan_command_processor.cc:3924/3929 and equal the open submission), and would only
double-touch barrier bookkeeping. It is speculative and likely wrong — do not build it blind.

## Turnkey device plan (already runnable — all cvars in the EmulatorActivity --ez allowlist)
Verified present: `gpu_uma_direct_shared_memory` (EmulatorActivity.java:163),
`gpu_uma_serialize_before_write` (:164), `gpu_trace_swap` (:161),
`arm64_blue_dragon_draw_wait_probe` (:308). No allowlist edit needed.

**Step 1 (ZERO new code — run first):** on reconnect, after the safety gate (read
`/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage` + `/temp`; launch only if busy low and temp < 60 °C;
prefer tools/thor/thor_evidence.ps1), launch ONE capture with
`--ez gpu_uma_direct_shared_memory true --ez gpu_uma_serialize_before_write true`
(full GPU drain before every direct write) and count `VdSwap(` over ~6 s. This cleanly splits the
suspect space:
- **Outcome A — VdSwap > 0 (renders):** the upload/await-timing family IS the cause →
  proceed to a targeted, gated fix in `UploadRangesDirect`'s submission-boundary timing.
- **Outcome B — VdSwap = 0 (still hangs):** the entire upload/await/smart-sync family is
  **exonerated** → the cause is the present/swapchain + 512 MB persistently-mapped buffer
  interaction, or a guest stall unrelated to the upload await → stop chasing `UploadRangesDirect`.

Force-stop `jp.xenia.emulator.github.debug` immediately after the single capture. Do NOT loop launches.
Clear/inspect the device `files/xenia.config.toml` first (a stale `gpu_uma_*`/`arm64_*` cvar would
confound the A/B).

**Step 2 (only if Step 1 is inconclusive / still hangs and localization is needed):** add a SINGLE
default-off, `gpu_uma_*`-gated, throttled (one-shot or every-Nth-hit) `XELOGI` capturing in one line
the polled token `[x14+0]` vs stored `wait_state+0x8` and the elapsed delta inside the fastpath
return-one branch, plus whether `IssueSwap` is entered (reuse the existing `gpu_trace_swap`
begin/end logs at vulkan_command_processor.cc:2087/2642 to confirm whether the guest stops issuing
VdSwap before adding anything). Gate behind a NEW default-false cvar (e.g. `gpu_uma_present_hang_probe`).

## Honest uncertainties (carried to the device)
- Which loop actually spins is unknown blind (the cited fastpath self-deschedules + has a 5 s timeout).
- The causal bridge "UMA-direct shifts WHEN a guest-visible progress token is written back" is an inference.
- B74's "no GPU draw-outcomes line at all" equally supports a CP-thread host-side wedge before
  finishing a frame as it does a guest spin — the IssueSwap-entry log is needed to distinguish.

Source: wf_d34a3f80-e82 (full output in the task temp file). All file:line verified against the
actual xenia-thor repo (src/, not the upstream mirror) by the adversarial lenses.
Related: docs/worklog/2026-05-30-uma-and-cross-game-3d.md (B74 evidence, lines 1971-1992);
memory burnout-uma-present-hang-regression.
