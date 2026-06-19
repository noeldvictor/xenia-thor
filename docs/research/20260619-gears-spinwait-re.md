# Gears of War (1) campaign-load spin-wait — RE + device-tested fix attempts

Date 2026-06-19. Gears boots + menus but the CAMPAIGN LOAD never reaches gameplay (black ~9.9fps). The #1
hot guest fn during the black load is the spin-wait `guest_8222F460`. RE'd via a 9-agent workflow +
device-tested. The diagnosis is SOLID; the easy fixes are RULED OUT with device data; the clean fix is a
deeper GPU-ring change (next effort).

## Diagnosis (disasm-confirmed)
`guest_8222F460` is the shared **Microsoft XDK Direct3D9 "wait for the GPU command-ring to drain to a fence"**
primitive — byte-identical (same offsets, same 0x1388=5000 timeout) to Blue Dragon's `0x8246B408` and Lost
Odyssey's spin class. The arg object `r29 = *(arg+0)` is the D3D9 device's ring manager = xenia's
`GraphicsSystem::interrupt_callback_data_` (proven by the timeout path printing "The GPU is hung!"
@ 0x820BDA98/0x820BE050).
- Returns **0 (done)** when **bit 0x2 of byte (r29+0x2A39)** is set (`8222F4B4 lbz; 8222F4B8 rlwinm.,30,30;
  8222F4BC bne -> li r3,0`). Bit 0x2 = "GPU read-pointer caught up to the write target = ring drained."
- The bit is set by the NORMAL drain writer `0x82221A40` (`lbz 0x2A39; ori ,2; stb 0x2A39`) inside drain
  routine `0x82221980`, GATED at `0x82221A28` on **obj+0x2A1C == 2** (the consume counter reaching the
  write target; the read-pointer writeback is `*(obj+0x2A10)`).
- The WATCHDOG: `8222F4CC lwz r30,0x58(r10)` (r10 = KPCR.current_thread = KTHREAD) reads **KTHREAD+0x58 =
  kernel-time**; timeout fires when `r30 - start(0xC(r31)) >= 0x1388 (5000)`. The timeout handler
  `0x8222FD78` force-sets +0x2A39 bits 3 (`ori ,3; stb`) THEN prints "GPU is hung" + ends in
  **`8222FE14 twui r0,0x16`** (unconditional trap) unless a global guard at 0x82BED124 is nonzero.
- (`0x827A7B08` = GetCurrentThreadId, used for lock ownership — not a fence source.)

## Two xenia gaps
- **Gap A (real cause):** xenia never advances the guest ring read-pointer writeback `*(obj+0x2A10)` to the
  write target for this D3D-layer object, so obj+0x2A1C never reaches 2 and the drain routine never sets bit
  0x2. (Lost-Odyssey class: a GPU completion the guest's own path expects, that xenia isn't producing.)
- **Gap B:** xenia never writes **KTHREAD+0x58** (grep-confirmed: xthread.cc writes 0x54/0x5C/0x60/0x14C,
  skips 0x58), so `r30 = 0`, elapsed = 0, the watchdog never fires either.

## Device-tested fix attempts (both RULED OUT for Gears)
xenia has two BD-built mechanisms (BD gained +27% fps from them): (a) `gpu_blue_dragon_kick_wait_token`
(graphics_system.cc:386 — on a source==1 GPU interrupt, `*(*(interrupt_callback_data_+0x2A10)) += 1`;
title-agnostic in code), and (b) the JIT KTHREAD+0x58 advance (a64_seq_memory.cc:228, gated to BD's load
site 0x8246B474 + cvar arm64_blue_dragon_draw_wait_probe).
- **(a)+(b) together → CRASH.** Watchdog (b) fired ("GPU is hung" x3) → Gears' GPU-hung handler hit the
  `twui` trap → emulator exited to the Android home screen. **(b) traps Gears** (BD recovers; Gears doesn't).
- **(a) alone (+ budget 256, no watchdog) → still BLACK, no crash.** Diagnostic (gpu_log_interrupt_counts):
  **src1 (CP) interrupts DO fire** during the stall (src1 4251→4337, ~30/sec), the token IS kicked — but
  bit 0x2 of +0x2A39 is **never set** → Gears stays black. So the +1-per-interrupt BD kick does NOT satisfy
  Gears' drain check (obj+0x2A1C==2 via 0x82221980). **The BD kick is BD-specific in effect.**

CONCLUSION: the easy BD mechanisms do not cleanly unblock Gears. The (b) Gears-site addition was REVERTED
(it crashes Gears = a footgun).

## The real fix (next effort)
1. **Clean (Gap A):** make xenia advance Gears' ring read-pointer (`*(obj+0x2A10)`) to the write target as
   it drains the command ring, so the drain routine `0x82221980` sets +0x2A39 bit 0x2 naturally. Needs RE of
   the exact RPTR/WPTR/obj+0x2A1C protocol + WHY 0x82221980 isn't reaching ==2 even with src1 interrupts
   firing (is 0x82221980 even being entered? is the kick advancing the wrong word?). NEXT PROBE: log
   obj+0x2A1C, obj+0x2A10, *(obj+0x2A10), and +0x2A39 each second during the black load (extend
   gpu_log_interrupt_counts) to watch the drain counters.
2. **Degraded fallback:** game-patch NOP the trap `0x8222FE14: twui r0,0x16 -> 0x60000000`, then enable (b)
   so the watchdog self-recovers without crashing (the game thinks the GPU hung but proceeds). Untested;
   may have visual/behavioral artifacts.

## Lost Odyssey
LO spins on the same primitive (0x827B6278, same 0x1388 watchdog) but its prior diagnosis pointed at a
+0x2ABD device-status bit set by its GPU driver layer — a DIFFERENT field than Gears' +0x2A39. Gap B
(KTHREAD+0x58 frozen) is shared, but LO's clean fix likely differs. The trap-NOP fallback would need LO's
own trap site. Validate LO separately.
