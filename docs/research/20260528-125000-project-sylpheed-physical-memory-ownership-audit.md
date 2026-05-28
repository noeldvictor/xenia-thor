# Project Sylpheed Physical Memory Ownership Audit

## Summary

Added default-off runtime audit plumbing for Project Sylpheed's physical memory
ownership question:

```text
xboxkrnl_physical_memory_audit=false
xboxkrnl_physical_memory_audit_budget=256
```

This is behavior-neutral instrumentation. It does not round, free, rescue, or
reinterpret any physical memory address.

## What Changed

- `MmAllocatePhysicalMemoryEx` logs physical allocation ownership rows when the
  audit is enabled.
- `MmFreePhysicalMemory` logs request/result rows with:
  - current guest thread, LR, CTR, and r1;
  - physical heap base/page size and parent address;
  - physical allocation base/size and parent allocation base/size;
  - region-start and page-alignment flags;
  - release result and returned region size;
  - `behavior_changed 0`.
- Android direct-emulator launches and the real launcher/library path can pass
  the audit cvars.
- `tools/thor/thor_xenia_debug.ps1` exposes:

```text
-XboxkrnlPhysicalMemoryAudit true
-XboxkrnlPhysicalMemoryAuditBudget 512
```

- `tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1` now counts
  the new audit rows when present, while staying compatible with old packets.

## Validation

Commands:

```text
PowerShell parser check: tools/thor/thor_xenia_debug.ps1
PowerShell parser check: tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1
tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1 -PacketDir scratch/thor-debug/remote-debug-20260528-114503 -Top 4
tools/thor/thor_build.ps1 -Mode NativeCore
tools/thor/thor_build.ps1 -Mode ApkShell
tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
git diff --check
```

Result:

```text
NativeCore: passed
ApkShell: passed
FullDeploy: passed
APK SHA256: 7B10B60782D1D420BE39E5F4340E7E79B118598175498B54FC87D86638958E89
Device: c3ca0370 / AYN Thor
lastUpdateTime: 2026-05-28 12:57:28
```

The old black-frame packet correctly reports no new audit rows because it was
captured before this patch:

```text
physical_memory_audit_allocate_count=0
physical_memory_audit_free_request_count=0
physical_memory_audit_free_result_count=0
physical_memory_audit_interior_free_request_count=0
```

## Next Repro

Use the real launcher/game library path:

```text
tools/thor/thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher -XboxkrnlPhysicalMemoryAudit true -XboxkrnlPhysicalMemoryAuditBudget 512 -XboxkrnlReenterAudit true -XboxkrnlReenterAuditBudget 128
```

Then launch Project Sylpheed from the game library, advance through New Game /
Normal using physical input or the verified debug gamepad helper, capture a
remote-debug packet, and run:

```text
tools/thor/thor_project_sylpheed_physical_heap_release_audit.ps1 -PacketDir <packet>
tools/thor/thor_android_packet_visual_status.ps1 -PacketDir <packet>
```

## Decision

The next behavior decision should wait for a fresh packet with ownership rows.
If the failing free corresponds to an allocation sub-handle or an expected
interior physical object, implement that contract explicitly. If ownership rows
show a normal region-base allocation followed by guest interior frees, keep the
free strict and investigate post-reenter execution/presentation instead.

Android audio remains a separate lane because `apu=any` still reaches
`NopAudioSystem`.
