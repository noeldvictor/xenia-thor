# Guest C++ Exception Dispatch (guest-EH) — feature + Project Sylpheed root-cause

2026-06-08. Built/validated this session (commits `f6aa9d14e` → `742d6245b`).

## What it is

Xbox 360 titles that `throw` a C++ exception call `RtlRaiseException(code=0xE06D7363)`
(the MSVC C++ throw). xenia's historical `HandleCppException` stub just **logs and
returns**, so the guest resumes at the compiler-unreachable code after the throw and
crashes. `guest-EH` instead **unwinds the guest PPC stack to the registered catch
handler and resumes there** — the real behaviour a throwing-and-catching title needs.

It is **default-off** and gated behind two cvars:

- **`guest_cpp_exception_dispatch`** (default false) — enables the *diagnostic* path:
  parse the XEX exception directory, walk the guest stack from the throw site, decode
  the MSVC EH descriptors, type-match against the throw, and **log** the result. On any
  failure or no-match it falls back to the historical log+return stub. **Safe** — it
  never changes control flow by itself. Parsing the exception directory only happens
  when this is on at load.
- **`guest_cpp_exception_dispatch_transfer`** (default false) — additionally performs
  the **live control transfer** to the matched catch funclet. Kept separate so the
  diagnostic path can be validated before risking a transfer.

## Architecture (the pieces, all gated default-off)

| Unit | What | Where |
|------|------|-------|
| U2 | Parse the XEX PE exception directory (`DataDirectory[3]`, packed 8-byte big-endian `{FuncStart; bits}`) into a sorted `guest_runtime_functions_` table; `FindRuntimeFunction(pc)` | `src/xenia/cpu/xex_module.{h,cc}` |
| U3 | `WalkGuestStack` — guest PPC back-chain walk (`caller_sp=*(be32)[sp]`, `return_pc=*(be32)[caller_sp-8]`), fails closed | `src/xenia/kernel/xboxkrnl/xboxkrnl_cpp_eh.{h,cc}` (impl in `xboxkrnl_debug.cc`) |
| U4 | Decode the big-endian MSVC EH descriptors (`FuncInfo` at `FuncStart-4`, magic `0x19930522`; `TryBlockMapEntry`; `HandlerType`; `TypeDescriptor`) | same |
| U5 | Type-match personality — `catch(...)` + mangled-name match vs the throw's `CatchableTypeArray`; PMD this-pointer adjust | same |
| U6 | `FindGuestCatchForThrow` (pure, host-tested decision) + `TryDispatchGuestCppException` — fault-safe readers, build the establisher frame + funclet ABI (`r12`), seed `lr`, `Reenter()` at the funclet (mirrors `KeSetCurrentStackPointers`) | `xboxkrnl_debug.cc` |

The throw-side structs (`x_s__ThrowInfo` / `x_s__CatchableType` / `x_PMD`) are the
existing ones in `xboxkrnl_debug.cc`; per-throw magic is `0x19930520` (distinct from the
FuncInfo magic `0x19930522`).

## Validation status

- **Host-validated** (cpu-tests, `[guest-eh]` tag, 69 assertions): the PC→function
  lookup, the back-chain walk (fail-closed cases), the EH-descriptor decode, the
  type-match (exact / base-class / `catch(...)` / PMD), and the **catch-found dispatch
  decision** (`FindGuestCatchForThrow`, synthetic throw-and-catch).
- **On-device validated** (Project Sylpheed fires): the XEX parse (23073 entries,
  100% in-image), the stack walk (7 frames, monotonic SPs, terminates at the poisoned
  stack top), and the FuncInfo decode (`magic=0x19930522`).
- **Not yet exercised**: the *live* `Reenter` transfer + the post-catch continuation —
  no assessed title throws **and** catches (log-mining of all captured fires found C++
  throws only in Sylpheed, which is **uncaught**). The transfer *mechanism* is proven by
  the shipped `KeSetCurrentStackPointers`→`Reenter` precedent; `A64Backend::PrepareForReentry`
  resets the stackpoint depth on reentry (no manual reconciliation needed).

## When to use it

If a title currently crashes with "Guest attempted to throw a C++ exception!" **and** it
actually has a matching catch (enable `guest_cpp_exception_dispatch` and look for a
`guest-eh: MATCH` line in logcat): turn on **both** cvars, validate that it reaches the
catch, and refine the continuation seed (currently the establisher's return address — a
first cut) from the funclet structure. Only `catch`-by-reference is wired today;
`catch`-by-value declines back to the stub.

## Project Sylpheed (535107D4) — why guest-EH does NOT fix it

This instrumentation traced Sylpheed's crash end-to-end: it throws an **uncaught**
`std::out_of_range` "invalid map/set<T> iterator" at a `std::map` erase (`823070B0`).
Root cause (on-device file-I/O trace): **`game:\dat\files.tbl` is missing** — the only
missing `dat\` file (siblings `GP_TITLE.p00`/`sound.p04` open fine; `tables.pak` present).
The game opens `files.tbl` **read-only and never creates it**, so it *requires* a
pre-existing file → with it absent, the file-table `std::map` is empty → the
invalid-iterator erase throws. Other file reads succeed (`Base.xpr` reads fully), so it
is **not** read corruption and **not** a xenia VFS bug — it is a **missing, required data
file** (an incomplete ISO dump or an HDD-install file), i.e. **user-side**: verify/re-dump
the disc. guest-EH cannot help because the throw is uncaught (no catch to transfer to).
Full trace in the `guest-eh-build-plan` memory.
