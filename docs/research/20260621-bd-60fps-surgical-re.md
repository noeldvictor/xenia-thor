# Blue Dragon 60fps — surgical reverse-engineering result (2026-06-21, workflow werh19xb7)

User thesis: "we CAN do clever semi-RE to get Blue Dragon to 60fps on the AYN Thor." **Substantially
VINDICATED.** A 5-agent workflow (web/community + 3 device-free guest-RE digs + a verifying synthesis,
186 tool uses, every load-bearing address re-decoded against the decrypted XEX) cracked BD's full pacing
mechanism and proved the existing upstream patch is genuinely broken — so "it's been tried and it fails"
was never a fair test. Cross-confirmed against the independent **re:Blue** static-RE project
(github.com/zolaware/reblue), which names every one of these functions.

Title: Blue Dragon (4D5307DF), dump hash 3C19B6F951F93D49. XEX base 0x82000000, entry 0x824669E0, comp=1.

## 1. BD's pacing model (verified + re:Blue-confirmed)
**BD is FIXED-TIMESTEP, anchored to 30fps. No delta-time feeds game logic.** Logic advances a constant
"1.0 frame-unit" per rendered frame. So doubling the frame rate doubles game speed unless the per-frame
advance is halved to 0.5.
- The timestep constant is the literal **1.0f** at pool 0x820551AC (0x3F800000); the canonical rate
  **30.0f** is at 0x820551A8 (0x41F00000). All 11 `mftb` sites are GPU-submit profiling; all 4
  `KeQuerySystemTime` callers are calendar/FILETIME utilities — none feed a frame delta.
- re:Blue independently confirms: no per-frame logic-delta scaling exists; its 60fps story is "native CPU
  is faster," NOT a pacing-correctness fix. It documents no battle-softlock fix either.

## 2. The REAL 30fps limiter (the key new discovery)
The actual logic-rate cap is the **per-frame gate inside `bdRenderStep` (0x82132BE8)**, NOT the present
interval. Verified the render-loop tail:
```
82132F08: lwz r3, 0x1a88(r25)     ; r25 = this; r3 = gate param (object field +0x1a88)
82132F0C: stb r28, 0x1a8c(r25)
82132F10: bl  0x8248cf88          ; <-- the per-frame pacing gate (re:Blue: "sole 30fps timing authority")
82132F14: lwz r31, 0x5c(r1)
82132F18: b   0x82132c78          ; loop back = the render loop
```
`0x8248cf88` calls kernel import thunk `0x8273f5e4`(r3=+0x1a88, r4=0) and branches on its status (>=0 →
return 1; else fallback `bl 0x82467470` → return 0). This is the frame pacer. The present interval
(0x8246ab68) is a SEPARATE mechanism (PM4 swap-packet vsync divisor) — which is exactly why patching
0x8246ab68 to interval-1 earlier this session "took but did not raise fps." There is also a third pacer:
the present-worker 30ms watchdog `KeWaitForSingleObject` at 0x824881E0 (timeout built at 0x82488160 =
0xFFFB6C20 = -300000 x100ns = 30.0ms).

So three pacing points exist: (a) render-loop gate 0x82132F10 [the logic limiter], (b) present interval
0x8246ab68 [GPU vsync divisor], (c) present-worker 30ms watchdog 0x82488160.

## 3. Why the upstream "60 FPS (WIP)" patch is BROKEN (verified, not opinion)
Upstream block (author "illusion", never revised; his own comment: "Causes softlocks in battles" +
"Will need to find a way to write 3f00 rather than nop"):
| upstream edit | reality (verified) |
|---|---|
| 0x82dec57c be16 0x3f00 ("Game speed") | **DEAD slot** — zero readers/writers/pointers anywhere. No-op. The prior "reader at 0x8276C598" was a capstone false-positive on a pointer word (any 0x82xxxxxx data word disassembles as a bogus `lwz`). |
| 0x82dda880 be16 0x3f00 ("Battle speed") | **overwritten every frame** — it's element[2] of a float table at 0x82DDA878 rebuilt each frame by **6 writers** (4 const-seeded: 0x8219BC18/0x821D0480/0x82389358/0x821A0640; 1 init loop 0x8273664C; 2 runtime-seeded: 0x821E1DB8/0x82389EA4). The static .bss write never survives. |
| 0x820ce6bc be32 nop | **mis-targeted** — it's `stfs f0,0x30c(r31)`, a controller-constructor object-field init, NOT a slotB writer. NOP'ing it removes a one-time init for no benefit (potential init breakage). |
| 0x821a0640 / 0x8273664c be32 nop | only 2 of slotB's 6 writers → the other 4 still re-assert 1.0. |
| 0x8246ab68 be32 0x39400001 (li r10,1) | the ONLY sound edit (present interval ONE) — but it's GPU present pacing, not the logic limiter. |

So the upstream patch "takes but does nothing" at the logic level, and its one live effect (forcing slotB
toward 0.5 in the frames it isn't yet overwritten) desyncs battle timers → softlock.

## 4. The battle softlock — root cause (verified idiom)
Animations are delta-correct w.r.t. the float timestep slotB (accumulator at 0x821F3F78 / consumer
0x821442A4: `acc += rate * slotB`), BUT **battle/effect logic uses raw INTEGER per-frame counters** that
ignore slotB and tick once per rendered frame (verified idiom at 0x82380504: `lwz r11,0xdb4(r31);
addi r11,r11,1; stw`). At 60fps with slotB=0.5, float animations play at correct wall-speed but the
integer counters run 2x → the "wait N frames then resolve damage / accept next command" handshake never
lines up → softlock. (At 60fps with slotB=1.0, both run 2x, stay in sync, no lock — but double speed.
That is the author's "disabling battle speed avoids softlocks but doubles speed.")

The community-proven fix (RPCS3/PCSX2/Dolphin): do NOT halve the increment — **double the `cmpwi N`
thresholds to 2N** on the integer counters. Candidate battle/effect/anim sites (UNVERIFIED list, each must
be individually decoded/classified — a spot-check found 0x82380504 is a clean counter but 0x82382A28's
neighborhood was a 0xDEAD bitmask not a counter): battle 0x82380504/0x82381818/0x82382A28/0x82384740/
0x823897D8/0x82392280; effects 0x821D4A60/0x821D67DC/0x821D6A5C/0x821D78C0/0x821DDC7C; anim 0x821E6324.
re:Blue function names for the cluster: bdBattleSystemUpdate(0x8219D708), bdCutsceneComputeFrameCount
(0x821F3418), bdEventSceneActionTimerUpdate(0x821E53B0).

## 5. Verdict — is correct-speed 60fps achievable by surgical patching?
- **Non-battle gameplay (field/town/dungeon): likely YES** with high confidence IF those areas are
  float-timestep-driven (slotB) — the fragile part is specifically the scattered battle integer counters.
- **Full battle-safe 60fps: achievable but NOT a clean 6-line patch** — it needs (a) the render-loop gate
  retuned to run the loop at 60, (b) slotB made to stick at 0.5 across all 6 writers (4 const-seed loads
  retargeted to an injected 0.5f word + 2 runtime writers needing an inserted `fmul 0.5`), and (c) an
  unbounded, individually-verified set of battle/effect integer counters doubled. Confidence for a fully
  battle-safe pure-guest patch: **~25%** (the counter grind is the risk). NOT recommended as the first
  deliverable; it's a real but substantial RE grind.
- **GPU reality (orthogonal):** logic-60 helps light/medium scenes only. The heavy foliage field is
  GPU-bound ~126ms (~8fps) regardless — that's the separate VRS/FDM/foliage-thinning/FP10 track. Most BD
  gameplay (towns/dungeons/menus, non-dense field) has GPU headroom (~25.9ms = ~38fps-capable).

So: 60fps is REACHABLE for the bulk of non-battle BD gameplay via surgical patching; battles need the
counter-doubling grind; the heavy field is a GPU-efficiency problem, not a pacing one.

## 6. The decisive DEVICE experiment (the one remaining unknown)
The biggest unverified question: does retuning the render-loop gate (0x82132F10) actually raise the logic
rate? Cheap single A/B (light/medium gameplay scene, NOT a menu/movie; matched guest_ms):
1. **Baseline OFF.**
2. **Gate-bypass ON:** patch `0x82132F10 = 0x60000000` (NOP the `bl 0x8248cf88`) — crude "unthrottle the
   loop" test. Measure VdSwap cadence AND actual game speed (stopwatch a scripted in-game animation / a
   known walk distance).
   - If the loop speeds up AND game speed rises proportionally → the gate IS the logic limiter and logic
     is fixed-timestep-coupled → proceed to slotB=0.5 + battle-counter doubling (Route B).
   - If nothing changes → the gate is a swap-sync poll, not the throttle; re-examine (the 30ms watchdog
     0x82488160 / a different wait).
3. If the loop accelerates, A/B add slotB=0.5 (via the 4 const-seed retargets) and check field-scene speed
   correctness; then enter a battle to confirm/measure the softlock and validate counter-doubling fixes.
   Caveat: NOPing 0x82132F10 removes the swap sync entirely (tearing/over-submit) — it's a diagnostic, not
   a ship; the shippable version retunes the gate's wait target, not removes it.

## 7. The honest alternative (if the battle-counter grind proves intractable)
True smooth 60fps from 30Hz logic = frame interpolation (present-frame extrapolation / reprojection warp),
which is robust by construction (one logic clock, synthetic in-between frames). Note: naive frame-DOUBLING
(present each 30Hz frame twice) does NOT add motion smoothness — only interpolation does. This is the
parallel frame-gen track; it targets the same 60fps-visual goal without touching game logic.
See [[fdm-overdraw-lever-next-major-build]] frame-gen integration map.

## Reusable RE tooling produced
tools/xex/xex_disasm.py + scan_stores.py + scan_bl.py (committed). CAUTION: scan_stores' raw disp match
yields capstone false-positives on 0x82xxxxxx pointer-table words (they decode as bogus `lwz`) — always
back-resolve the base register before trusting a "0x82.."-class hit (this is what made the upstream patch's
"slotA reader" a phantom). The workflow's base-resolving scanners live in scratch/ (find_slot_writer.py,
find_readers.py, emu_init.py, trace_writer.py) if they need promoting to tools/.

Full workflow output: tasks/werh19xb7.output.
