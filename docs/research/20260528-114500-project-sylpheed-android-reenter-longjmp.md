# Project Sylpheed Android Reenter Longjmp

## Summary

Project Sylpheed no longer kills the Android process when New Game / Normal
hits `KeSetCurrentStackPointers`. Android now uses a guarded `setjmp` /
`longjmp` path for guest reenter instead of throwing `reenter_exception` through
A64 generated code. Non-Android keeps the existing C++ exception path.

This fixes the native abort class, not all Project Sylpheed compatibility.
After the fix, the game stays alive but presents a black frame while continuing
to log the known interior physical heap release failures.

## What Changed

- Added `xboxkrnl_android_reenter_longjmp`, default-on only on Android.
- Wrapped `XThread::Execute` / `ExecuteRaw` in an Android reenter jump state.
- `XThread::Reenter` now longjmps back to the active guest execution loop when
  the reenter target is the current thread and the Android cvar is enabled.
- The old exception path remains as fallback when the Android jump state is not
  active.
- Reenter audit rows now report `longjmp_reenter`.
- Launcher debug overrides can now pass reenter audit cvars through the real app
  launcher path, so library launches can be diagnosed without direct ISO launch.

## Proof

Build/deploy:

```text
tools/thor/thor_build.ps1 -Mode NativeCore
tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Launcher route:

```text
tools/thor/thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher -XboxkrnlReenterAudit true -XboxkrnlReenterAuditBudget 128
```

Proof packets:

```text
scratch/thor-debug/remote-debug-20260528-113951
scratch/thor-debug/remote-debug-20260528-114109
scratch/thor-debug/remote-debug-20260528-114147
scratch/thor-debug/remote-debug-20260528-114243
scratch/thor-debug/remote-debug-20260528-114341
scratch/thor-debug/remote-debug-20260528-114503
```

Important screenshots:

```text
scratch/thor-debug/remote-debug-20260528-114109/screen.png
scratch/thor-debug/remote-debug-20260528-114147/screen.png
scratch/thor-debug/remote-debug-20260528-114243/screen.png
scratch/thor-debug/remote-debug-20260528-114503/screen.png
```

The final status report:

```text
classification=project_sylpheed_heap_release_failures_no_crash_marker
android_runtime_count=0
native_signal_count=0
guest_crash_count=0
base_heap_release_address=1DB18CC0
physical_heap_release_address=BDB18CC0
physical_heap_parent_address=1DB18CC0
```

Key audit rows after pressing New Game / Normal:

```text
Xboxkrnl reenter audit KeSetCurrentStackPointers ... throws_exception 0 generated_code_unwind_required 0 longjmp_reenter 1 behavior_changed 1
Xboxkrnl reenter audit XThread::Reenter ... throws_exception 0 longjmp_reenter 1 behavior_changed 1
```

The earlier failing class was:

```text
native_abort_message=terminating with uncaught exception of type xe::kernel::reenter_exception
```

That abort was not present in the fixed repro.

## Remaining Blocker

After the reenter fix, Project Sylpheed reaches a live black screen with the FPS
OSD still updating. Logcat continues to show interior physical heap release
failures, for example:

```text
BaseHeap::Release failed because address 1DB18CC0 is not a region start
PhysicalHeap::Release failed due to parent heap failure (physical_address=BDB18CC0, parent_address=1DB18CC0)
```

Do not treat that as solved by the reenter fix. The next compatibility slice
should classify the black-frame state and the physical heap release behavior.
Avoid blind physical-free round-down; previous source audit showed that could
free a much larger parent region.

Audio is also still not solved by the earlier `apu=any` migration. The fixed
repro still logs:

```text
NopAudioSystem created silent audio driver 0
```

That means Android currently falls back to the silent APU backend because no
real Android audio backend is registered.

## Decision

Keep the Android reenter longjmp path. It removes the process-killing native
abort and preserves the existing exception behavior as fallback. Continue
Project Sylpheed with black-frame / physical-heap diagnostics and a separate
Android audio backend plan.
