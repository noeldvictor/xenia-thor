# Gears harvest fire — results (CRT-port NO-GO, + 3 real findings)

Date 2026-06-18. The device "harvest batch" (per the Codex consult) to decide GO/NO-GO on populating the
shared-function CRT fast-path. Title: Gears of War (1), profiled during a campaign LOAD screen (the
memcpy-heavy phase Codex wanted). simpleperf `--app`, 46,181 samples, perf-map via `cpu_perf_map_path`.

## Method note: manual symbolization (simpleperf's vendor build wouldn't auto-apply the map)
- `cpu_perf_map_path` must point at an APP-WRITABLE path: the app uid CANNOT write `/data/local/tmp`
  (`drwxrwx--x shell shell` → Permission denied; the silent `fopen` failure is why fire #1 produced no
  map). Use `/data/user/0/jp.xenia.emulator.github.debug/files/xenia_perf.map`, then extract to the
  `perf-<pid>.map` name via `run-as <pkg> cat files/xenia_perf.map > /data/local/tmp/perf-<pid>.map`
  (the shell does the redirect; run-as reads as the app). Map verified: 13,704 host→guest entries.
- The Thor's vendor simpleperf (`1.build.eng.Thor.20260206`) does NOT auto-apply `/data/local/tmp/perf-<pid>.map`
  to the anonymous JIT region (shows `unknown[+<hostVA>]`). WORKAROUND: resolve host VA → guest fn by hand
  from the pulled map (`grep` the entry whose `start <= hostVA < start+size`), then look the guest addr up in
  the compile-time harvest log for size/hash.

## Findings
### 1. CRT-port hot-leaf hypothesis = REFUTED for this scene -> table stays EMPTY
Per-thread (hot XThread, 11,433 samples) the CPU concentrates in TWO guest functions:
- **`guest_8222F460` (212B, ~25% of the thread, ~8% total): a SPIN-POLL/WAIT routine, NOT a CRT kernel.**
  Disasm (tools/xex on the pulled Gears XEX): spin-delay (8x `mr r31,r31` NOPs looped 4x via a manual
  counter at `0x50(r1)`) -> poll flag bit `lbz 0x2a39(r29)`+`rlwinm.` -> read PCR `0x100(r13)` + object
  fields `0x2a08/0x2a10/0x2a70(r29)` -> timestamp compare vs `0x1388` (=5000 timeout) -> `bl 0x827a7b08`,
  `bl 0x8222fd78` -> return 0/1. A classic busy-wait/timeout-poll. NOT a leaf (it calls others), NOT memcpy.
- `guest_8221A070` (~1.5KB host, ~21%): medium, game-logic-sized (not a CRT leaf).
- Per Codex's falsification ("hot samples in title-specific code, not shared CRT ranges -> ship nothing"),
  the shared-function CRT fast-path does NOT capture the hot code. `cpu_shared_function_fastpath` stays
  default-off with an EMPTY table = correct. (The compile-time harvest confirmed 6,955 leaf-sized fns exist,
  but the HOT one is sync-polling, not CRT.)

### 2. Lock/atomic contention ~13.5% -> validates the lock-free lookup
Top host symbols: `__aarch64_cas2_acq` 5.56%, `__aarch64_swp2_rel` 2.99%, `pthread_mutex_unlock` 2.68%,
`pthread_mutex_lock` 2.25% (~13.5% combined) = LSE CAS/SWP + mutex contention (the `global_critical_region`).
Direct evidence for the `cpu_lockfree_entry_lookup` lever (shipped, default-off) -> A/B it next.

### 3. XMA decoder thread = 21.5% (host audio, already native)
Thread split (pid 21308): XThreadBE459CB0 31%, Thread-3 24.8%, **XMA Decoder 21.5%**, XThreadCABFCCB0 9.7%,
Main XThread 2.9%. The XMA audio decode is a major LOAD-phase cost. Host-side (already "ported") -> the
lever is optimizing the host XMA decoder (NEON), and it's cross-title.

### 4. NEW lever: a hot guest spin-wait during loading
`guest_8222F460` busy-polls (spin-delay + flag + 5000-timeout). The guest burns CPU waiting for something
(asset-load/event/GPU) during loading. Fix class = spin-detect-and-yield, or speed the awaited operation -
NOT codegen (a spinwait doesn't benefit from a trace-specializer). Relates to the LO spin at 0x827B6278 and
the general spin-yield lever. (Steady-gameplay hot-fn may differ - this is the LOAD phase.)

## Net verdict
The harvest did its job: it KILLED the CRT-port-is-a-win hypothesis for this scene (the hot leaf is
sync-polling, not memcpy), keeping `cpu_shared_function_fastpath` correctly inert, AND redirected to three
better-evidenced levers: (a) lock-free lookup A/B (contention is real, ~13.5%), (b) host XMA decoder
optimization (21.5%, cross-title), (c) guest spin-wait detection/yield. Device fires: thermal-gated, ended
force-stopped + idle. Artifacts: scratch/gears_perf.map, scratch/gears_harvest_clean.txt, scratch/gears_default.xex.
