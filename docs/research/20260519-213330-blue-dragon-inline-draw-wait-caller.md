# Blue Dragon Inline Draw-Wait At Hot Caller

Date: 2026-05-19 21:33 EDT

Branch: `master`

Base commit: `f2a026c2e`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

The caller sampler proved that the hot caller into Blue Dragon's draw-wait
function is `8246E618`, with the call returning to `8246E6A4`. The next question
was whether we can remove repeated guest call/prolog/epilog overhead without
replacing the whole caller loop.

## Static Pass

Capture:
`scratch/thor-debug/20260519-211951-*`

Focused dump:
`8246B258`, `8246B288`, `8246E618`, `8246B408`

Findings:

- `8246B258` is tiny setup for the wait state:
  store draw object, store flag/count, read current KTHREAD time, read the
  producer token through `[r4+0x2A10]`, and seed wait-state offsets `0x8`,
  `0xC`, and `0x10`.
- `8246B288` is cleanup/stat accounting, not a trivial epilog. It uses the time
  base, updates counters under the draw object, and may call through a callback
  at `[object+0x330C]`.
- Because cleanup is not trivial, replacing the entire `8246E618` body is higher
  risk than inlining only the already-validated `8246B408` wait predicate at the
  hot call site.

## Patch

Added a default-off, title-specific cvar:

- `arm64_blue_dragon_draw_wait_inline_in_caller`

New Thor script switch:

- `-Arm64BlueDragonDrawWaitInlineInCaller true|false`

Behavior:

- Requires `arm64_blue_dragon_draw_wait_fastpath=true`.
- Only fires when the current guest function is `8246E618`.
- Only fires for the direct call target `8246B408`.
- Reuses the same hand-emitted `8246B408` predicate body, but emits it inline at
  the call site instead of calling the separate compiled guest function.

This keeps caller setup, caller cleanup, and the caller's producer-progress
predicate on the normal generated path.

## Speed Capture

Capture:
`scratch/thor-debug/20260519-212544-*`

APK SHA-256:
`4AFE43F197A22F9B290084CA60FE7E7DD73D463A7B932AC5EE25B5C66C018872`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 130 -PerfSampleSeconds "70,115" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -MountCache true
```

Result:

- Screenshot was alive at the loading spinner:
  `scratch/thor-debug/20260519-212544-screenshot.png`.
- No fatal signal, AndroidRuntime crash, `VK_ERROR_DEVICE_LOST`, GPU-hung, or
  ANR markers were found in the filtered log search.
- `8246B408` no longer appeared in top speed-profile entries, which means the
  hot separate-function entry storm was removed.
- Late top entries moved to other CPU/system functions, especially:
  `827294CC`, `8272A3A4`, `8272A8E8`, `826C5620`, `826BF770`, and kernel-style
  critical-section / IRQL helpers.

Caveat:

- `LaunchBlueDragonSpeedCapture` currently forces
  `vulkan_force_signed_2101010_unorm_fallback=true`, so use this as a CPU
  profile-shape capture, not the final visual-route comparison.

## Live Route

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 75 -LogLevel 1 -HideAndroidOsd true -HidNopConnected true -HidNopButtonSequence "start@45000:3000;a@68000:3000" -DisassembleFunctions false -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -MountCache true -GpuUnknownRegisterLogBudget 0 -XboxkrnlNtCreateFileFailLogBudget 0
```

Captures:

- `scratch/thor-debug/20260519-212846-*`: first screenshot was black during the
  route.
- `scratch/thor-debug/20260519-213117-*`: follow-up screenshot reached the
  loading spinner.
- `scratch/thor-debug/20260519-213255-*`: follow-up screenshot reached the
  Microsoft Game Studios opening scene.

Result:

- The inline route is alive and can reach rendered opening content.
- It is not yet proven faster than the prior host-counter route, so keep the
  new switch default-off and treat it as an experiment/profiling tool.

## Decision

Keep `arm64_blue_dragon_draw_wait_inline_in_caller` default-off.

Use it when the question is "what is behind the `8246B408` wall?" because it
removes that function from the profile and exposes the next CPU/HLE hot set.

Do not claim it as a speed win until a same-route A/B capture shows better
time-to-loading/title/opening and a comparable profiler interval.

## Next Targets

- Classify hot functions now exposed by the inline run:
  `827294CC`, `8272A3A4`, `8272A8E8`, `826C5620`, `826BF770`.
- Separate guest critical-section/kernel/HLE wait cost from actual JIT codegen
  cost.
- Consider adding a symbol/dump queue for those addresses, then only patch the
  subsystem the evidence points to.
