# JIT Boot Cost: No Persistent Code Cache (autonomous iter 18)

## Finding

The A64 (PPC->AArch64) backend does **not** persist translated code across
launches. The guest is re-translated from scratch every boot, which is a large
part of the ~80s cold-boot cost that dominates every test iteration.

Evidence (src/xenia/cpu/backend/a64/):
- `a64_code_cache.cc` / `a64_code_cache_{win,posix}.cc`: an in-memory EXECUTABLE
  MAPPING manager, not a serializer. `FillCode` fills unused space with `BRK #0`;
  `FlushCodeRange` only `FlushInstructionCache`. Derives from `CodeCacheBase`.
- No `fopen`/`CreateFile`/`fwrite`/`fread`/`mmap`-from-file/`.bin`/`persist`/
  `load_from`/`save_to` anywhere in the a64 code-cache files (grep empty). The
  cache lives only for the process lifetime.
- Mirrors upstream Xenia's x64 backend (x64_code_cache.* same shape) — upstream
  also JITs per-run; this is not a Thor regression, it's inherent.

So: translated blocks are generated on demand (and/or via guest-module
precompile) into RAM each launch and thrown away on exit.

## Why a persistent cache is non-trivial (scoped, not implemented)

A correct on-disk translation cache needs:
1. A stable key per guest module: title ID + module name + a hash of the guest
   code image + a translator/version stamp (so a backend change invalidates).
2. Position-independence or relocation: cached host code embeds absolute host
   addresses (data refs, helper thunks, guest->host call targets). Either compile
   PIC, or serialize + apply a relocation table on load. This is the hard part.
3. Resolved guest->host address mappings (the function/table that maps guest PCs
   to host code) persisted alongside, re-pointed on load.
4. Invalidation on self-modifying guest code (already handled at runtime; the
   on-disk copy must be re-validated against the live guest image).

This is a real, multi-day backend feature with correctness risk — exactly the
kind to NOT guess-edit autonomously. Documented as a design lane.

## Cheaper boot wins to consider first (lower risk)

- Measure the boot breakdown: how much of the ~80s is JIT vs ISO mount vs asset
  load vs guest init. If JIT is a minority, a code cache won't help much.
- A save-state/snapshot (none exists in this fork) would skip boot+JIT+movies
  entirely and is arguably higher ROI than a code cache, but is also large.
- Per-iteration, the deterministic hid_nop input + (future) a steady warm-APK
  already cut wall-clock; the JIT cache is the only thing that attacks the
  recompile specifically.

## Baseline to capture next

Measure cold-boot time on device: timestamp from `am start` to the first
`VdSwap(` in logcat for a known title (Gears 3 -> menu). That number is the
target any future cache/snapshot work must beat, and isolates how much is JIT
(compare first-boot vs a hypothetical warm path). Not captured this iteration
(investigation-only); it's the concrete next step.

## Status

No code change. Durable result: confirmed no persistent JIT cache exists +
scoped what one requires + identified boot-breakdown measurement as the next
step. Recommend boot-breakdown profiling before committing to the cache feature.
