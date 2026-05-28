# Project Sylpheed library wait evidence and APK native guard

## Context

This slice started from the real game-library path with focused Project
Sylpheed diagnostics:

- filtered guest dumps for `821507BC` plus the known `821511D0`-`821517D0`
  free-site CTR cluster,
- `xboxkrnl_physical_memory_audit`,
- `xboxkrnl_thread_wait_trace`,
- A64 thread snapshots.

The first attempt exposed a packaging regression before any game compatibility
evidence: packet `scratch/thor-debug/remote-debug-20260528-175642` crashed at
activity startup with `java.lang.UnsatisfiedLinkError: dlopen failed: library
"libxenia-app.so" not found`. The APK installed by a resource-only build did
not contain `lib/arm64-v8a/libxenia-app.so`.

## APK guard

The device was repaired by packaging an APK with the existing native build
output staged into `jniLibs`, installing it, and then removing the temporary
staging directory. The permanently validated build-script fix makes
`tools/thor/thor_build.ps1 -Mode ApkShell` and `-Mode ApkShellDeploy` stage
existing native outputs while skipping native rebuild, then refuses installs
when the APK lacks `lib/arm64-v8a/libxenia-app.so`.

Validation:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell -NoSubst`
- APK zip inspection shows `lib/arm64-v8a/libxenia-app.so` and
  `lib/x86_64/libxenia-app.so`.
- temporary `android/android_studio_project/app/src/main/jniLibs` staging was
  removed after packaging.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode Install -DeviceSerial c3ca0370 -NoSubst`

## Sylpheed packet

The repaired APK launched Project Sylpheed from the real game-library row.
Packet:

`scratch/thor-debug/remote-debug-20260528-180311`

Important joined outputs:

- visual report:
  `scratch/thor-debug/20260528-180311-sylpheed-visual-status.txt`
- loading-loop report:
  `scratch/thor-debug/20260528-180311-sylpheed-loading-loop-audit-joined.txt`
- physical free-site report:
  `scratch/thor-debug/20260528-180311-sylpheed-physical-free-site-audit.txt`
- live top snapshot:
  `scratch/thor-debug/20260528-180311-sylpheed-top-threads.txt`

Findings:

- Visual classification is `black_frame_likely`, with `near_black_share=1.000000`.
- Status classification remains
  `project_sylpheed_heap_release_failures_no_crash_marker`.
- Presentation is still live: `vdswap_count=2443`,
  `vdswap_span_seconds=48.303`, `vdswap_rate_per_second=50.577`.
- Guest execution is active: `guest_cpu_sum=130.3`, two guest XThreads running,
  top guest thread `XThread24E43CB0=34.6%`.
- GPU and audio are also active in the live top sample:
  `GPU Commands=42.3%`, `XMA Decoder=38.4%`.
- Wait trace exists but is only the bounded first burst:
  `wait_trace_count=256`, `wait_trace_span_seconds=2.329`.
- A64 thread snapshots exist: `a64_thread_snapshot_count=350`,
  top `last_fn=8284E08C`, top `last_ret=824AB214`.
- Physical frees still look like one guest suballocator owner, not a safe
  round-down target: 16 failed interior frees, owner `BC220000:03A80000`,
  LR `821507BC`, zero failed frees matching exact allocation results.
- Filtered PPC dump landed for the free-site cluster, including
  `Filtered function dump 821511D0-82151914`.

## Decision

Do not hide or round down physical frees. This packet shows a live black-frame
state with active VdSwap, active guest CPU, active GPU/XMA threads, wait-trace
coverage, and the same single-owner interior-free cluster. The remaining useful
work is to interpret the wait trace and map the filtered free-site guest code,
not to patch heap ownership blindly.

Next: extract the filtered PPC/HIR dump around `821511D0-82151914` and map how
the CTR cases call the free helpers, then decide whether the black frame is
driven by a guest free-site contract, guest wait/progress loop, or a separate
presentation/audio dependency.
