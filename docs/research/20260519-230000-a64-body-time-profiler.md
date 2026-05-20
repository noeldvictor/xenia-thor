# A64 Body-Time Profiler

Date: 2026-05-19 23:00 EDT

Branch: `master`

Base commit: `900c6bb6a`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

The A64 speed profile reports guest function entry deltas. That is useful for
finding high-frequency helpers, but it can overstate tiny functions after they
receive body fastpaths. The question for the Blue Dragon loading-spinner wall
was:

Which filtered guest functions are actually spending generated-code body time on
Thor, rather than merely being called often?

## Implementation

Added an opt-in CNTVCT body-time profiler for the current A64 backend:

- `arm64_speed_profile_body_time_filter`
- Android launch extra: `arm64_speed_profile_body_time_filter`
- Thor script switch: `-Arm64SpeedProfileBodyTimeFilter "..."`

The profiler requires `arm64_speed_profile_interval_ms`. For each guest
function selected by the filter, the A64 emitter:

- allocates an 8-byte stack local;
- reads `CNTVCT_EL0` at function prolog;
- reads `CNTVCT_EL0` again at normal epilog and A64 tail-call exits;
- atomically adds the elapsed body ticks to `A64Function::profile_body_ticks`;
- logs `A64 speed profile body top NN` rows sorted by body tick delta.

Tail-call exits are important for Blue Dragon because helpers like `827294CC`
tail-dispatch through the A64 indirection table. The profiler now ends timing
before those tail exits while preserving the already-resolved tail target in
host `x9`. The atomic add helper uses caller-scratch temporaries `x12`, `x13`,
and `w14` so it does not corrupt the branch target.

Filter tokens are comma, semicolon, or whitespace separated. A token can be a
single address or an inclusive range. A single address matches any compiled
function whose guest range contains that address; this is useful for interior
addresses but can include neighboring/containing functions when using broad
filters.

## Validation

Build and deploy:

```powershell
git diff --check
$null = [scriptblock]::Create((Get-Content -Raw 'tools\thor\thor_xenia_debug.ps1'))
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -PerfSampleSeconds "70" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4,8272A8E8,826C5620,827294CC,826BF770" -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpFastpath true -Arm64BlueDragonJumpTableFastpath true -MountCache true
```

Capture:
`scratch/thor-debug/20260519-225321-*`

APK SHA-256:
`3951852CB716C00DB006ED96216BFBA056070BF679FF8733286E43DFED17F6FC`

Result:

- Screenshot reached the Blue Dragon loading spinner at 95 seconds.
- No searched fatal signal, AndroidRuntime crash, `VK_ERROR_DEVICE_LOST`,
  GPU-hung, or ANR markers were found.
- The app process stayed foreground as `jp.xenia.emulator.EmulatorActivity`.

## Findings

The 22:54:38 interval is the clearest filtered spinner interval:

```text
8272A3A4 body_ticks_delta=10527445 entries_delta=903854 ticks_per_entry=11
827294CC body_ticks_delta=1066299  entries_delta=1366763 ticks_per_entry=0
8272A8E8 body_ticks_delta=613020   entries_delta=565235  ticks_per_entry=1
82729368 body_ticks_delta=281856   entries_delta=120594  ticks_per_entry=2
82729EB8 body_ticks_delta=254948   entries_delta=994     ticks_per_entry=256
826BF770 body_ticks_delta=140921   entries_delta=141469  ticks_per_entry=0
826C5620 body_ticks_delta=1271     entries_delta=1609    ticks_per_entry=0
```

The 22:54:53 interval shows the same shape after route phase changes:

```text
8272A3A4 body_ticks_delta=4123298 entries_delta=78724   ticks_per_entry=52
826C5620 body_ticks_delta=1611795 entries_delta=2148278 ticks_per_entry=0
826BF770 body_ticks_delta=704752  entries_delta=838630  ticks_per_entry=0
827294CC body_ticks_delta=82825   entries_delta=110292  ticks_per_entry=0
8272A8E8 body_ticks_delta=36020   entries_delta=37865   ticks_per_entry=0
```

Entry-count top functions and body-time top functions now disagree in the way
we needed to see. `827294CC` and `826C5620` can dominate entry counts while
remaining relatively cheap per call after their fastpaths. `8272A3A4` is the
largest measured guest-body cost in the filtered set, by roughly an order of
magnitude over the jump-table helper in the clearest interval.

## Decision

Keep the body-time profiler default-off and use it as the next canonical
low-noise speed discriminator before adding more title-specific fastpaths.

Next experiments:

- inspect `8272A3A4` / `8272A8E8` as the likely Blue Dragon decompression or
  bitstream state-machine wall;
- use Ghidra or focused PPC dumps to identify the high-cost inner loops before
  replacing any body;
- add an exact-start-only filter mode if future body-time captures are polluted
  by range containment;
- continue treating `827294CC`, `826C5620`, and `826BF770` as default-off
  speed probes rather than compatibility fixes.
