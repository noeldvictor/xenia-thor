"""The device A/Bs that are owed, as one plan - so that an approved session
runs them without preparation.

  python tools/thor/pending_ab.py              # the plan: items, arms, scenes, why
  python tools/thor/pending_ab.py --run ITEM   # one item (after the user's go)

Every item is one launch at a scoreboard scene (tools/thor/scoreboard.py
ENTRIES) through tools/thor/live_ab.py: the arms are switched live inside the
launch (the cvars are read at use time - checked for each lever, see LIVE),
two rounds in alternating order, presented fps and GPU busy per arm, a
screenshot per arm, and live_ab's stop at 44 C case. An item marked
"relaunch" changes shader translation and cannot switch live: it prints the
two launch-cvar sets for scoreboard runs instead.

The device rules (AGENTS.md section 5): the user approves every session, a
session is at most 5 minutes and stops at 44 C case. One item is about 3-4
minutes with the route; do not chain items without a cool-down.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# The levers and why each switches live (the place its cvar is read).
LIVE = {
    'gpu_uma_smart_sync_pages': 'VulkanSharedMemory upload wait (tracking always on '
                                'with smart-sync since 2026-09-29)',
    'gpu_uma_direct_upload_barrier': 'each direct upload',
    'gpu_fold_msaa_depth_clears': 'RenderTargetCache::SetMsaaDepthClearFold, each draw',
    'gpu_skip_dead_resolves': 'each resolve (the history starts when it is set)',
    'global_lock_spin': 'each global lock acquire (mutex.cc)',
    'rtl_critical_section_min_spin': 'each RtlEnterCriticalSection',
}

PLAN = [
    dict(id='upload', entry='gears1', why=(
        'The Thor upload path: per-page smart-sync (PC twin: the command processor '
        'no longer waits for the previous frame, Gears fence 26 ms -> 0) and the '
        'spec-redundant upload barrier off (Gears cold frame 363 -> 143 breaks).'),
        arms=['base:gpu_uma_smart_sync_pages=false,gpu_uma_direct_upload_barrier=true',
              'pages:gpu_uma_smart_sync_pages=true,gpu_uma_direct_upload_barrier=true',
              'pages_nobarrier:gpu_uma_smart_sync_pages=true,gpu_uma_direct_upload_barrier=false']),
    dict(id='tiler', entry='gears1', why=(
        'Render passes: the 4x MSAA shadow-clear fold (PC: 68 -> 54 passes a frame; '
        'with the dead-transfer cutout of 2026-10-02 22 -> 17 transfers, and '
        'xenia_frame_cost 4.38 -> 3.56 G Adreno instruction-invocations a Gears '
        'frame, -19%) and the dead-resolve skip (Gears 2 of 20 resolves a frame).'),
        arms=['base:gpu_fold_msaa_depth_clears=false,gpu_skip_dead_resolves=0',
              'fold:gpu_fold_msaa_depth_clears=true,gpu_skip_dead_resolves=0',
              'fold_dead:gpu_fold_msaa_depth_clears=true,gpu_skip_dead_resolves=8']),
    dict(id='locks', entry='gears1', why=(
        'Lock handoffs: spin before the futex sleep (2026-09-22 live A/B cut short by '
        'heat: 20.4 -> 25.9 fps with both).'),
        arms=['base:global_lock_spin=0,rtl_critical_section_min_spin=0',
              'both:global_lock_spin=128,rtl_critical_section_min_spin=256']),
    dict(id='spin', entry='gears1', relaunch=True, why=(
        'Gears heat: its worker XThread F800003C spins on the PowerPC priority '
        'hint (8222F460) and held a whole big core (heat_probe 2026-10-01: '
        '98-115% of a core, case +5.8 C/min at 15 fps). The Gears profile sets '
        'cpu_spin_hint_backoff_us=200 and lists the 18 functions with the hint in '
        'cpu_backend_llvm_skip_addrs (the LLVM object cache served the old code '
        'otherwise); with 8222F460 only, F800003C fell 93% -> 44-57%. First run '
        'heat_probe.py gears (no --cool) for the case slope and the thread table; '
        'then this item for fps. Both arms keep the skip list, so both translate '
        'those functions fresh.'),
        arms=['off:cpu_spin_hint_backoff_us=0',
              'on:cpu_spin_hint_backoff_us=200']),
    dict(id='bools', entry='banjo_story', relaunch=True, why=(
        'Bool constants as specialization constants (app toggle opt_bool_specialize, '
        'off): the driver drops the branch side not taken - shader lab -154k to '
        '-368k Adreno instructions over 4 titles, exact on 59 traces - against more '
        'pipelines (Blue Dragon +60%, Banjo a few, Gears none). fps and hitches '
        'decide (no Blue Dragon scoreboard scene yet: Banjo).'),
        arms=['off:gpu_specialize_bool_constants=false',
              'on:gpu_specialize_bool_constants=true']),
]


def show():
    print('Owed device A/Bs (each needs the user\'s go; 5 min, 44 C case):\n')
    for item in PLAN:
        mode = 'relaunch per arm' if item.get('relaunch') else 'live arms, one launch'
        print('%-10s scene %-12s %s' % (item['id'], item['entry'], mode))
        print('           ' + item['why'])
        for arm in item['arms']:
            print('           arm ' + arm)
        print()
    print('Live-switchable levers (where the cvar is read):')
    for name, where in LIVE.items():
        print('  %-32s %s' % (name, where))


def run(item_id, rounds, seconds):
    item = next((i for i in PLAN if i['id'] == item_id), None)
    if not item:
        print('no item %s' % item_id)
        return 2
    if item.get('relaunch'):
        print('"%s" changes shader translation: one scoreboard run per arm, with a '
              'cool-down between them:' % item_id)
        for arm in item['arms']:
            label, _, cvars = arm.partition(':')
            print('  %s: python tools/thor/scoreboard.py %s --cvars "%s"' % (
                label, item['entry'], cvars.replace(',', ' ')))
        return 0
    cmd = [sys.executable, os.path.join(ROOT, 'tools', 'thor', 'live_ab.py'), item['entry']]
    cmd += item['arms'] + ['--rounds', str(rounds), '--seconds', str(seconds)]
    print(' '.join(cmd), flush=True)
    return subprocess.call(cmd)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--run', default='', help='the item to run on the device')
    ap.add_argument('--rounds', type=int, default=2)
    ap.add_argument('--seconds', type=float, default=8.0)
    args = ap.parse_args()
    if not args.run:
        show()
        return 0
    return run(args.run, args.rounds, args.seconds)


if __name__ == '__main__':
    sys.exit(main())
