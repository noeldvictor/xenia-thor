# Banjo-Kazooie: Nuts & Bolts stalls on the loading screen

Date: 2026-09-20. Build `3847a3c5a1` (slab JIT memory, patches before the precompile). Title
4D5307ED, hash `C03916823ADAC91B`, no patch file. Launched by the user from the app at 19:23:11.

## Timeline (from the log and the `xenia-fps` line)

| time | event |
|---|---|
| 19:23:11 | module launched; the precompile loads 38,104 cached functions in 37.9 s |
| 19:23:49 | "Title name: Banjo Kazooie: N&B"; guest starts |
| 19:23:49 to 19:24:17 | guest thread 0x7C logs 100,201 IRQL lines in 28 s: `KeRaiseIrqlToDpcLevel - old_irql > 2` and `KfLowerIrql : new_irql > kpcr->current_irql!`, alternating |
| 19:23:50 to 19:24:20 | 29.7 fps presented (the intro; the profile caps at 30) |
| 19:24:20 onward | 0.0 fps. The golden jigsaw loading screen stays. One guest host thread at 72 % CPU; seven guest threads wait 30 s, then 60 s, on events |
| 19:28 | stopped by me. GPU 58 C, hottest CPU zone 93 C |

Waiting threads: 0x4 (WaitMultiple on two guest-native events at 82FA7C18 and 82FA7C5C), 0x6,
0x8, 0x9 (type 8 objects, guest_lr 821E0528), 0xA, 0xE (events from NtCreateEvent, guest_lr
821E0528 and 8271CC88), 0xD (a guest-native event at 40E6E74C, guest_lr 82662EE0). Thread 0x7C
is not among the waiters; it is the spinner.

## Reading

The IRQL pair means the KPCR `current_irql` of thread 0x7C is above 2 when the game raises to
DPC level, and the game then lowers back to that value. The two HLE functions are identical in
xenia-edge, so the divergence is elsewhere: what wrote the value above 2 (the a64 inline
`KfLowerIrql` fast path, a direct guest write to the KPCR, or a wrong r13 for that thread), and
why the spin condition never clears. Same shape as the Gears Act-1 stall of directive 14: one
thread spins, the others wait on an event the HLE never signals. CLAUDE.md section 8 lists
"Phase 4 IRQL unification" as pending; this is its first title-level symptom on record.

Not done: the guest PC of the spinning thread (no root for `debuggerd`), and the edge kernel
diff of the spin lock and event paths. Both are the next step.
