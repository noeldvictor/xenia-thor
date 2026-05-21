# Blue Dragon Nop Keystroke Route Fix

Date: 2026-05-21 16:40 EDT

## Goal

Make the automated Blue Dragon speed lane drive the same menu path a person
drives on Thor, then separate real A64/backend speed data from captures that
were only idling at the title screen.

## Problem

The speed lane passed a nop controller button schedule:

```text
start@45000:3000;a@68000:3000;a@86000:3000;a@106000:3000
```

but the nop HID driver only applied the schedule through `GetState`. Blue
Dragon's menus also poll `XamInputGetKeystroke`, so the scripted START/A
presses could be invisible to the title/menu route.

That made some "speed" captures misleading: they had healthy A64 counters, but
the game could still be stuck at the visible `press START` screen.

## Change

`NopInputDriver::GetKeystroke` now emits scheduled button transitions for the
research controller:

- Validates controller 0 / any-user requests and rejects disconnected state.
- Maps dpad, START, BACK, shoulder, and face buttons to XInput virtual keys.
- Emits key-up transitions before key-down transitions.
- Tracks `previous_keystroke_buttons_` separately from the existing state
  polling path.

The existing scheduled `GetState` path remains unchanged.

## Captures

APK SHA-256 after the nop keystroke fix:

```text
FB4877DF6BEA31D86B8354632668A36BDAD134D48738132E26813FD7C5F631B6
```

Relevant runs:

| Capture | Mode | Key cvars | Result |
| --- | --- | --- | --- |
| `scratch/thor-debug/20260521-161210-*` | 120s speed | `arm64_blue_dragon_stricmp_deferred_cr_fastpath=true` | Guest crash at PC `826A2498`; leave this experiment default-off. |
| `scratch/thor-debug/20260521-161938-*` | 75s speed | `arm64_blue_dragon_jump_table_inline_in_caller=true` | Short run stayed active and removed `827294CC` from early top rows, but route was before the input fix. Retest before enabling. |
| `scratch/thor-debug/20260521-162805-*` | 90s title/no auto input | deferred CR false, jump inline false | Reached visible `press START` and stayed there, proving the title route itself was healthy. |
| `scratch/thor-debug/20260521-163237-*` | 95s speed | deferred CR false, jump inline false | Scheduled input advanced to the loading spinner. Final interval `entry_delta=22492432`. |
| `scratch/thor-debug/20260521-163453-*` | 180s speed | deferred CR false, jump inline false | Scheduled input reached the opening rendered sky/dragon-wing scene. No searched fatal markers. |

The 180s capture metadata confirms:

```text
arm64_blue_dragon_stricmp_deferred_cr_fastpath=false
arm64_blue_dragon_jump_table_inline_in_caller=false
hid_driver=nop
hid_nop_connected=true
hid_nop_button_sequence=start@45000:3000;a@68000:3000;a@86000:3000;a@106000:3000
```

## Performance Read

At 175 seconds in `20260521-163453`, the Thor was CPU/JIT dominated:

- Main Thread: about 88.4% CPU.
- GPU Commands: about 11.5% CPU.
- Draw Thread: about 3.8% CPU.
- XMA Decoder: about 3.8% CPU.

The final screenshot showed an in-game/opening scene rather than a black idle,
title prompt, or loading spinner:

```text
scratch/thor-debug/20260521-163453-screenshot.png
```

Late hot functions in the opening scene shifted away from only the old
pre-title set:

```text
82274DB0 delta=263516
82287788 delta=259154
826BF770 delta=183923
82274E38 delta=155188
821CE028 delta=128210
82189CD0 delta=124740
82277100 delta=124696
82168610 delta=119609
820DFA50 delta=85853
82282490 delta=65296
```

## Decision

Keep the nop `GetKeystroke` path. It is a test automation fix, not a gameplay
shortcut, and it makes the speed lane actually reach the intended Blue Dragon
route.

Keep `arm64_blue_dragon_stricmp_deferred_cr_fastpath=false` in all Blue Dragon
presets. It is currently a negative result because it crashes the guest at
`826A2498`.

Keep `arm64_blue_dragon_jump_table_inline_in_caller=false` until a same-route
A/B run after this input fix proves it helps without breaking the longer route.

The next speed wall is now the real post-title/opening hot set. Start with
body-time and block-profile captures for `82274DB0`, `82287788`, `826BF770`,
`82274E38`, and `82282490`, then decide whether the fix belongs in generic A64
lowering, PPC helper inlining, branch/link dispatch, or HLE timing.
