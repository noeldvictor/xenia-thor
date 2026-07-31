# Lost Odyssey infinite "Loading" — root-cause RE: skipped fade-in (state race)

## Status
Desktop-reproducible (our `xenia.exe` stalls; official canary 02d2cb5 boots the
same ISO past it on the same PC — pure canary-lineage compat gap, not
environment). All earlier kernel fixes (f2ea321a6: trimmed NtCreateFile paths,
XamGetOverlappedResult bWait, overlapped event reset) are real bugs but NOT this
stall. This doc records the full guest-side decode from the 2026-07-31 cdb/
capstone session.

## The wait structure (all guest addresses, LO 4D5307FA disc 1)

Main thread loops in the loading-screen manager fn at **0x82321D88** (frame via
`KeDelayExecutionThread` wrapper, wait-trace lr 827CACFC, ctr 823221F8). Its
completion is gated per-iteration:

- Loading-screen object ptr at **\*(0x83262F0C)** (obj3). Fields:
  `+0xC0` active=1, `+0xC1` exit-request (0 forever = the stall), `+0xC2/+0xC3`
  fade phases, `+0xA8` job refcount (0), `+0xB0` timestamp (0.0), `+0xAC`
  min-display 1.0s, `+0xBC` fade restore value.
- Global flags **0x83262EEC/ED/EE** (phase-done latches, all 0),
  **0x83262F08** restart-request (0).
- The ONLY path that sets `+0xC1` (exit) is `bl 0x82811E78` from **0x823221A0**,
  reached only when `+0xB0 != 0`, which is only written on the pump-passed path
  (0x82321EC4..0x82321F08, timestamps via `bl 0x82290888` = time()).
- **Pump = 0x8229EB90**: returns 1 iff fade-state != 0/finished-check passes AND
  fade current-value >= **0.95f** (const at 0x82000A24). Live: returns 0 forever.

## The fade controller (the actual blocker)

Global fade object **0x832C0BCC** (found via `lis 0x832C/addi 0xBCC` scan; 17
ref sites). Layout: `+0` active byte, `+1` finished byte, `+2` direction?, `+4`
elapsed/current, `+8` duration, `+0xC` target, `+0x10` ?, `+0x14..1C` color,
`+0x20` current/held VALUE, `+0x24` cleared-on-start, `+0x28` mode.
- State getter **0x8229EBE8**: byte0==0 -> state 0 (idle); byte1!=0 -> state 5
  (finished, out=held +0x20); else interpolating states.
- Start **0x82967D68** (wrappers startA=0x828F79A0, startB=0x828F7A10),
  reset **0x82967E38** (wrapper 0x828F7AC8).
- LIVE at stall: byte0=1, byte1=1 (finished), held +0x20 = **0.0** -> the last
  completed fade was the fade-to-black INTO the loading screen; pump wants
  >= 0.95 (fade-in) forever.

## The smoking gun: Show() skips the fade-in

Loading-screen Show() epilogue at **0x8232111C**:
```
bl 0x82389F78        ; get fade state
cmpwi r3, 0
bne  0x82321154      ; state != 0 -> SKIP starting the fade-in  <-- taken!
...                  ; else startB(fade-in to *(0x82007784))
bl 0x828F7A10
```
Show() ran while the fade object was NOT reset (state 5 held from the previous
fade-out) -> the fade-in was never started -> pump never passes -> `+0xC1` never
set -> infinite loading. In canary the reset (0x82967E38 via 0x828F7AC8, callers
8231F6F8 / 82829320 / 8285E44C / 828D1B34 / 828F7F2C / 8290E740) lands before
Show() reads the state; in our emulator the ordering inverts. This is a guest
thread/timing ordering divergence, not a missing API.

## Verification instrumentation (in flight)

cdb hardware write-breakpoints on 0x1`832C0BCC/BCD (byte0/byte1) + 0x1`832C0BEC
(+0x20), armed at first VdSwap (script
C:\Users\LEANER~1\AppData\Local\Temp\lo_fade_bp.txt) -> logs the exact write
order of start/finish/reset in our run. Compare orderings; then find which
kernel timing behavior (thread priority, delay granularity, vblank/interrupt
cadence) lets the reset lose the race, and fix THAT (or port Edge's behavior).

## Tooling recipe (worked well, reuse)

- Guest memory base on desktop = **0x1_00000000** (guest addr + base = host).
- `cdb -p <pid> -c ".writemem <file> 1`<guest> L<len>; qd"` + capstone
  (CS_ARCH_PPC, CS_MODE_64|BIG_ENDIAN, skipdata) = instant targeted disasm, no
  Ghidra/XEX extraction needed.
- Guest stack walk: backchain at [r1], saved LR of each frame at
  **[backchain - 8]** (`stw r12,-8(r1)` before `stwu`).
- bl-target xref scan + `lis/addi` address-materialization scan (struct.unpack
  '>I', masks 0xFC000003==0x48000001, 0xFC1FFFFF==0x3C00xxxx) find callers and
  global refs in seconds on the 15MB dump.
- Wait/event trace cvars (budget 2M, after_ms 0) + begin-without-end analyzer
  pin parked threads and give their lr/ctr/r1 for the stack walk.
