# Project Sylpheed Heap And Reenter Source Audit

## Summary

The Project Sylpheed New Game/default-difficulty failure is not safe to fix as
a simple physical-heap rounding change. The latest repro has two nearby but
separate signals:

- an Android native abort from an uncaught `xe::kernel::reenter_exception`
- a physical heap release failure for an interior physical address

The heap failure is real and needs diagnostics, but the source path only logs
and returns from `MmFreePhysicalMemory`. The process abort is directly explained
by `KeSetCurrentStackPointers` throwing `reenter_exception` through generated
code on Android/AArch64.

## Evidence

Primary repro:
`scratch/thor-debug/20260527-221200-sylpheed-library-debug-input-repro`

Status report:

```text
classification=guest_heap_native_abort
native_abort_message=terminating with uncaught exception of type xe::kernel::reenter_exception
crash_thread=XThread3E82DCB0
base_heap_release_address=1DA98C80
physical_heap_release_address=BDA98C80
physical_heap_parent_address=1DA98C80
```

Important timing split in `logcat-after-new-game-sequence.txt`:

- `22:11:51.034`, tid `20659`: libc++abi aborts on uncaught
  `xe::kernel::reenter_exception`.
- `22:11:51.066`, tid `20611`: Xenia logs
  `BaseHeap::Release failed because address 1DA98C80 is not a region start`.

So the heap failure aligns with the same New Game route, but it is not the
native abort throw site.

## Symbolized Abort

Command used:

```powershell
$tool='C:\Users\leanerdesigner\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-addr2line.exe'
$lib='android\android_studio_project\app\build\intermediates\ndkBuild\githubDebug\obj\local\arm64-v8a\libxenia-app.so'
& $tool -C -f -e $lib 0x0c5e4ec 0x0c1b8e8 0x0c2ac50 0x0c2a9a8
```

Relevant frames:

```text
xe::kernel::XThread::Reenter(unsigned int)
src/xenia/kernel/xthread.cc:567
xe::kernel::xboxkrnl::KeSetCurrentStackPointers_entry(...)
src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc:525
KernelTrampoline(...)
src/xenia/kernel/util/shim_utils.h:513
RegisterExport<...>::X::Trampoline(...)
src/xenia/kernel/util/shim_utils.h:546
```

`XThread::Reenter` is designed to throw, and `XThread::Execute` tries to catch
that exception around `processor()->Execute` / `ExecuteRaw`. On Android ARM64,
the exception is escaping through the generated-code trampoline path instead of
reaching that catch scope. The tombstone confirms this by ending in the
generated code cache after the export trampoline.

## Heap Release Path

`MmFreePhysicalMemory_entry`:

- treats the argument as a result of `MmAllocatePhysicalMemory`
- looks up the heap from the address
- calls `heap->Release(base_address)`
- logs if release fails
- returns `void`

`PhysicalHeap::Release`:

- translates the physical address through `GetPhysicalAddress`
- calls the parent virtual heap `Release(parent_base_address, out_region_size)`
- only then triggers callbacks and releases the physical heap row

`BaseHeap::Release`:

- requires the address to be the region start
- rejects the Sylpheed parent address because page `1DA98` belongs to a region
  whose base is `1C220000`
- returns `false`, leaving the physical release incomplete

Current failing physical address:

```text
physical_address=BDA98C80
parent_address=1DA98C80
heap_base=A0000000
page_size=10000
parent_region_base=1C220000
parent_region_pages=14976
```

This strongly suggests an interior physical free or a stale/suballocated
physical pointer. A blind round-down release would free the much larger parent
region and is too risky. A blind ignore would avoid the log but may leak or keep
stale physical callbacks alive. A split/free-subrange model would need allocator
semantics the current page table does not expose.

## Decision

Do not patch `PhysicalHeap::Release` behavior yet.

The next compatibility slice should target the reenter abort first because it
is the direct process-kill path:

1. Add a behavior-neutral Android/A64 reenter audit around
   `KeSetCurrentStackPointers_entry`, `XThread::Reenter`, and the export
   trampoline path. It should count throw attempts, LR targets, current guest
   PC, whether execution is in APC delivery, and whether the throw would cross
   generated code.
2. Source-design an Android/A64-safe reenter mechanism that does not depend on
   C++ exceptions unwinding through generated code. Candidate designs must keep
   guest stackpoint/debug/exception visibility explicit.
3. Separately add physical-free diagnostics that classify exact-region frees,
   interior frees, already-free frees, and parent/physical heap mismatches
   before any compatibility cvar.

Re-run Project Sylpheed New Game only after the reenter audit or guarded
compatibility path exists. A rerun of the same APK/log path would only reproduce
the same uncaught exception.
