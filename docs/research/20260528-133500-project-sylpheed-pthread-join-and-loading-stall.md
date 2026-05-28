# Project Sylpheed Pthread Join Abort And Loading Stall

## Summary

Reproduced Project Sylpheed through the real Android game library with:

```text
-XboxkrnlPhysicalMemoryAudit true
-XboxkrnlPhysicalMemoryAuditBudget 512
-XboxkrnlReenterAudit true
-XboxkrnlReenterAuditBudget 128
```

The next native abort was not the older uncaught Android reenter exception.
After selecting New Game, Normal, and a save slot, Android bionic aborted on:

```text
invalid pthread_t 0x751f219cb0 passed to pthread_join
```

The packet is:

```text
scratch/thor-debug/remote-debug-20260528-132027
```

Symbolized stack:

```text
pthread_join
xe::threading::PosixCondition<xe::threading::Thread>::post_execution()
xe::threading::PosixConditionBase::Wait()
xe::threading::Wait()
xe::kernel::XObject::Wait()
xe::kernel::xboxkrnl::NtWaitForSingleObjectEx_entry()
```

## Fix

`PosixCondition<Thread>::post_execution()` now consumes the pthread handle once
before joining it. A completed Xenia thread wait handle may be waited more than
once, but POSIX `pthread_join` is single-use and Android aborts hard on stale
pthread handles.

Added a regression test that waits a completed thread twice.

Also updated `tools/thor/thor_android_game_status_report.ps1` so old packets
with this abort classify as:

```text
classification=android_pthread_join_invalid_thread_abort
invalid_pthread_join_count=2
```

instead of being misread as a heap-native abort just because heap diagnostics
were nearby in the same log.

## Post-Fix Proof

Validation:

```text
NativeCore: passed
FullDeploy -DeviceSerial c3ca0370: passed
PowerShell parser: passed
git diff --check: passed with CRLF warnings only
```

Deployed APK:

```text
SHA256=6FB80504BD99231F8A38BDC4F05DB5C00B4BAD9246C4474C773C041D48E3A5DF
```

Post-fix packet:

```text
scratch/thor-debug/remote-debug-20260528-133426
```

Status:

```text
classification=unknown
native_signal_count=0
invalid_pthread_join_count=0
base_heap_release_count=0
physical_heap_count=0
```

Visual classifier:

```text
classification=black_frame_likely
near_black_share=0.998962
vd_swap_count=19473
native_abort_marker_count=0
heap_release_count=0
```

The pthread abort is fixed, but this is not gameplay proof. The remaining state
is a live loading or near-black stall with VdSwap continuing and no scoped crash
marker in this packet.

## Next

Keep the physical heap ownership lane separate from this pthread fix. For the
next Project Sylpheed slice, capture a fresh game-library repro that either
reaches the New Game path again with audit rows or classifies the current
loading stall with guest execution, kernel wait, and presentation evidence.
Do not round down physical frees without an ownership contract.
