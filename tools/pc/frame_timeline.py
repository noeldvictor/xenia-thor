"""Where does a run's time go, scene by scene? A timeline of the per-frame lines.

  python tools/pc/frame_timeline.py XENIA.LOG [--bucket 10] [--json OUT.json]

Reads the "GPU draw outcomes/frame" lines that vulkan_trace_draw_outcomes_per_frame
writes at each guest swap (pc_run.py --timeline turns it on and prints this at
the end) and groups them by guest time (guest_ms) into buckets: frames per
second, the median draws, vertices, render pass breaks (brk_open) and their
buffer barriers, the GPU frame time, the command processor's time
(cpu_real_us) and the time it waited on a GPU fence (fence_us). Flags:
- WAITS-GPU: the command processor waits on the GPU for more than 1 ms a frame
  - the recording and the GPU do not overlap (2026-09-25: the Thor upload
  path's whole-buffer smart-sync, 11.5 ms a frame on MagnaCarta 2's title);
- BREAKS: more than 16 render pass breaks a frame (each one stores and reloads
  the framebuffer on the Thor's tiler);
- CP-HEAVY: the command processor records for more than 5 ms a frame.
The PC runs at the guest's frame cap, so its fps hides these; the flags are
what the Thor pays.
"""
import argparse
import collections
import json
import re
import statistics
import sys

LINE = 'GPU draw outcomes/frame'
FIELDS = ('rendered', 'total_vertices', 'copy', 'brk_open', 'brk_buf', 'gpu_frame_us',
          'cpu_real_us', 'fence_us')
PAIR = re.compile(r'(\w+)=(-?\d+)')


def frames(log_path):
    out = []
    with open(log_path, encoding='utf-8', errors='replace') as f:
        for line in f:
            if LINE in line:
                d = {k: int(v) for k, v in PAIR.findall(line)}
                out.append(d)
    return out


def timeline(rows, bucket_s):
    buckets = collections.OrderedDict()
    for r in rows:
        buckets.setdefault(r.get('guest_ms', 0) // (bucket_s * 1000), []).append(r)
    result = []
    for b, sel in buckets.items():
        med = {k: int(statistics.median([r.get(k, 0) for r in sel])) for k in FIELDS}
        flags = []
        if med['fence_us'] > 1000:
            flags.append('WAITS-GPU')
        if med['brk_open'] > 16:
            flags.append('BREAKS')
        if med['cpu_real_us'] > 5000:
            flags.append('CP-HEAVY')
        result.append(dict(start_s=b * bucket_s, frames=len(sel),
                           fps=len(sel) / float(bucket_s), flags=flags, **med))
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--bucket', type=int, default=10, help='seconds of guest time per row')
    ap.add_argument('--json', default='')
    args = ap.parse_args()
    rows = frames(args.log)
    if not rows:
        print('no "%s" lines - run with vulkan_trace_draw_outcomes_per_frame=true '
              '(pc_run.py --timeline)' % LINE)
        return 1
    result = timeline(rows, args.bucket)
    print('%7s %6s %5s %6s %8s %6s %7s %8s %8s %8s  %s' % (
        'guest s', 'frames', 'fps', 'draws', 'vertices', 'breaks', 'brk_buf', 'gpu_us',
        'cp_us', 'fence_us', 'flags'))
    for t in result:
        print('%3d-%-3d %6d %5.1f %6d %8d %6d %7d %8d %8d %8d  %s' % (
            t['start_s'], t['start_s'] + args.bucket, t['frames'], t['fps'], t['rendered'],
            t['total_vertices'], t['brk_open'], t['brk_buf'], t['gpu_frame_us'],
            t['cpu_real_us'], t['fence_us'], ' '.join(t['flags'])))
    flagged = collections.Counter(f for t in result for f in t['flags'])
    print('%d frames; flagged buckets: %s' % (
        len(rows), ', '.join('%s %d' % kv for kv in flagged.items()) or 'none'))
    if args.json:
        json.dump(result, open(args.json, 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
