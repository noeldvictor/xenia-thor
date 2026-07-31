# Full custom-codebase adversarial review (2026-07-31) — findings ledger

Six parallel reviewers over: today's kernel/memory changes, Android Java, a64
backend, guest scheduler/fibers, GPU levers, LLVM backend. FIXED-NOW items
landed same day; BACKLOG items are verified-real but deferred.

## FIXED same day (commits f321d231d + this one)
- Java: AOT watcher leaked thread+logcat child+Activity on every exit
  (Process now field-held, destroyed in onDestroy); watcher went blind after
  first module (now hide-and-rearm); stale -T 1 seed; profiles defeating
  per-game OVERRIDE_OFF (duplicate entries removed).
- memory.cc F1: the ported any_watched==false 'return true' turned genuine
  AVs on guest-RO/decommitted pages into an infinite fault-retry livelock -
  now retries only when page_table_ says kReadWrite.
- memory.cc F2: stale assert_true(is_write) before the read-fault fallback.
- emulator.cc F3: Pause() null-derefed thread() for fiber-backed threads
  (killed the process inside the crash-halt path). Now skipped.
- xiocompletion F5: fiber wait timeout counted WAKES not ms - now wall-clock
  deadline via Clock::QueryHostUptimeMillis.
- processor F6: aot_runtime_phase_ never reset - second title in a session
  silently lost LLVM entirely. ResetAotPhaseForNewTitle() at title launch.

## BACKLOG - a64 backend (ranked by reviewer)
1. Stackpoint overflow = hard abort (a64_emitter.cc:6301-6351): grow/degrade
   instead; make overflow tail fail-closed (brk). Kills longjmp-heavy titles.
2. Longjmp sync helper (a64_backend.cc:1394-1442) restores host SP by
   guest_stack_ alone - canary does a second guest_return_address_ match; the
   field is captured but DEAD in our port. Wrong-frame restore on recursive
   setjmp. Port the disambiguation.
3. RtlCriticalSection::LockCount inc/dec is raw host-endian while guest+HLE
   slow path are BE (a64_emitter.cc:4714-4729, 4896-4908) - latent deadlock
   under a64_inline_kernel_high_frequency_exports.
4. a64_max_stackpoints=0 + stack-sync on = null store every prolog.
5. Indirection table: no lower-bound guard for targets < 0x80000000.

## BACKLOG - LLVM backend (ranked by reviewer)
1. P1: LLVM fns never installed in the a64 indirection table - every a64->LLVM
   call pays full ResolveFunction forever (largest unclaimed perf win; needs
   low-memory trampolines, raw JITLink addrs don't fit u32).
2. C5: object-cache key omits lower_vmaddfp/skip_opcodes - warm cache can
   resurrect the known vmaddfp miscompile. Must add before cache ships.
3. C3/C4: residency mirror stale on mixed-type/overlapping ctx offsets -
   audit before residency_writeback ships.
4. T3: fault-storm latch kills ALL LLVM on ANY single unhandled fault for the
   whole run - too coarse for a default-on backend.
5. C1: float unsigned-compare NaN divergence (FCmpO vs a64 unordered-true) -
   one-line fixes; C2 musttail null-deref on unresolvable target; C8 silent
   no-op guest_call on resolve failure (a64 faults loudly).
6. T4: small load-window gate race (kernel-thread compiles at launch edge).
7. P2: full a64 RA pass runs wasted before LLVM lowering (load-time cost).

## BACKLOG - today's native (remaining)
- F4: crash-halt Suspend(nullptr) while owning the global critical region =
  process-wide deadlock; Kernel Dispatch parking = silent IO death; no
  Resume pairing for Pause on that path. Needs self-ownership check or scoped
  abort fallback.
- UnlockApc fiber enqueue: no prompt WakeAll (APC delivery waits for repoll).

## Scheduler + GPU reviewer findings: see their reports in the session task
outputs (arrived after this doc; append when triaged).
