# Project Sylpheed Library New Game Native Abort

## Summary

Project Sylpheed now has a fresh launcher-library repro for the New Game
failure using the verified debug gamepad helper. This is no longer a vague
"it crashed" report: the game launches from the scanned library row, reaches
the title menu, accepts controller input, and aborts after selecting New Game /
default difficulty.

This is not a title/menu geometry bug, audio bug, or Android picker bug. The
current failure is a guest heap / native abort path around
`MmFreePhysicalMemory`, `PhysicalHeap::Release`, `BaseHeap::Release`, and an
uncaught `xe::kernel::reenter_exception`.

## Proof

Primary packet:
`scratch/thor-debug/20260527-221200-sylpheed-library-debug-input-repro`

Pre-touch live-state packet:
`scratch/thor-debug/remote-debug-20260527-220734`

Post-title screenshot packet:
`scratch/thor-debug/remote-debug-20260527-221102`

Post-crash screenshot/status packet:
`scratch/thor-debug/remote-debug-20260527-221222`

Build under test:

- Branch: `blue-dragon-route-engine-reset`
- Commit: `1bacd1f38`

## Route

1. Captured the live Sylpheed screen/log before clearing logcat.
2. Force-stopped the package, cleared logcat, and started
   `LauncherActivity`.
3. Scrolled the launcher until the actual scanned library row was visible.
4. Tapped the Project Sylpheed library row at `960,355`.
5. Sent debug gamepad `Start`, `A`, `A`.
6. Captured the title screen showing the selector on `TUTORIAL`; this proved
   the library launch and debug input path reached the game title.
7. Cleared logcat again and sent `DpadUp`, `DpadUp`, `A`, `A` to select
   `NEW GAME` and the default difficulty.

The UI dump proving the library row is
`ui-scroll-4.xml`, with:

```text
Project Sylpheed - Arc of Deception (USA, Europe) (En,Ja)
ISO - /storage/2664-21DE/Roms/xbox360/Project Sylpheed - Arc of Deception (USA, Europe) (En,Ja).iso
bounds=[55,302][1865,408]
```

## Crash Status

`status-report-after-new-game-sequence.txt`:

```text
classification=guest_heap_native_abort
native_signal_count=6
native_abort_message=terminating with uncaught exception of type xe::kernel::reenter_exception
crash_thread=XThread3E82DCB0
base_heap_release_address=1DA98C80
physical_heap_release_address=BDA98C80
physical_heap_parent_address=1DA98C80
```

Focused crash markers:

```text
I XeniaInput: debug-gamepad KEYCODE_DPAD_UP down holdMs=120
I XeniaInput: debug-gamepad KEYCODE_BUTTON_A down holdMs=140
E libc++abi: terminating with uncaught exception of type xe::kernel::reenter_exception
F libc    : Fatal signal 6 (SIGABRT), code -1 (SI_QUEUE) in tid 20659 (XThread3E82DCB0)
E xenia   : !> F8000028 BaseHeap::Release failed because address 1DA98C80 is not a region start
E xenia   : !> F8000028 PhysicalHeap::Release failed due to parent heap failure (physical_address=BDA98C80, parent_address=1DA98C80, heap_base=A0000000, page_size=10000)
E xenia   : !> F8000028 MmFreePhysicalMemory failed: type=0 base_address=BDA98C80 heap_type=2 heap_base=A0000000 page_size=10000
```

The app returned to the launcher after the abort. The visible recent-game row
still says `May have crashed/closed`, so launcher-side crash status can be
improved later, but the external scoped status report now preserves the heap
native-abort class even when title metadata is absent from the isolated log.

## Tooling Change

`tools/thor/thor_android_game_status_report.ps1` now reports
`guest_heap_native_abort` for scoped Android/native aborts that also contain
`BaseHeap` / `PhysicalHeap` markers, even if the isolated log lacks title ID
lines because logcat was cleared after launch.

## Next

Audit the physical-memory free path before any compatibility fix:

- `MmFreePhysicalMemory`
- `PhysicalHeap::Release`
- parent `BaseHeap::Release`
- whether Xbox 360 titles can legally pass interior physical addresses to a
  free path that should round, split, ignore, or defer regions
- how `xe::kernel::reenter_exception` is supposed to leave this path without
  aborting the Android process
