#!/usr/bin/env python3
"""Compare speed-hack arms on BD, bucketed by scene complexity.

    python tools/thor/bd_thin_report.py baseline=scratchpad/thin_baseline.txt \\
                                        b2=scratchpad/thin_b2.txt

WHY BUCKETING IS NOT OPTIONAL, and why a raw median is a trap here: each arm
aborts at the 70C guard at a different route DEPTH, so the arms do not sample
the same scenes. A straight median comparison once read +75.9% between two arms
that differed only in scene mix - that is the route, not the lever. Compare
only WITHIN a vertex band, where both arms are drawing the same thing.

AND MIND THE SAMPLE COUNT. The 2026-08-17 clamp A/B first read +7.4%/-1.2% with
the signs DISAGREEING, on n=9-12 in the heavy buckets, because a warm start let
the guard end the run before the deep scenes. Bands under ~30 frames are
printed but flagged; they are not evidence.

ENGAGEMENT IS CHECKED, NOT ASSUMED. A thinning arm that collapsed nothing is
indistinguishable from a lever that did nothing, and this project has recorded
a "measured flat" verdict that turned out to be a cvar that never applied. The
frame line carries rt_transfers and the VRS counters; a thin arm should differ
from baseline somewhere observable, and if it does not, say so.
"""
import re
import sys

BANDS = [(50_000, 120_000), (120_000, 180_000),
         (180_000, 230_000), (230_000, 300_000)]
ROW = re.compile(r"gpu_frame_us=(\d+).*?verts=(\d+)")


def load(path):
    rows = []
    extra = {"rt_transfers": [], "vrs_esc": [], "vrs_base": []}
    for line in open(path, encoding="utf-8", errors="replace"):
        m = ROW.search(line)
        if not m:
            continue
        us, v = int(m.group(1)), int(m.group(2))
        if us <= 0:
            continue          # frame 0 / not yet timed
        rows.append((us, v))
        for k in extra:
            mm = re.search(r"\b" + k + r"=(\d+)", line)
            if mm:
                extra[k].append(int(mm.group(1)))
    return rows, extra


def med(v):
    v = sorted(v)
    return v[len(v) // 2]


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    arms = []
    for a in args:
        label, _, path = a.partition("=")
        rows, extra = load(path)
        arms.append((label, rows, extra))
        print("%-10s timed frames=%d  peak_verts=%s" %
              (label, len(rows), max((r[1] for r in rows), default=0)))
        for k, v in extra.items():
            if v:
                print("             %-14s median=%d" % (k, med(v)))
    if not arms:
        return

    base_label, base_rows, _ = arms[0]
    print("\n%-10s %-16s %8s %10s %9s" %
          ("arm", "verts band", "n", "gpu_frame", "vs base"))
    for lo, hi in BANDS:
        b = [r[0] for r in base_rows if lo <= r[1] < hi]
        if len(b) < 8:
            continue
        bm = med(b)
        flag = "" if len(b) >= 30 else "  <-- THIN, not evidence"
        print("%-10s %6d-%-9d %8d %10d %9s%s" %
              (base_label, lo, hi, len(b), bm, "-", flag))
        for label, rows, _ in arms[1:]:
            s = [r[0] for r in rows if lo <= r[1] < hi]
            if len(s) < 8:
                print("%-10s %6d-%-9d %8d %10s %9s" %
                      (label, lo, hi, len(s), "-", "too few"))
                continue
            m = med(s)
            flag = "" if len(s) >= 30 else "  <-- THIN, not evidence"
            print("%-10s %6d-%-9d %8d %10d %8.1f%%%s" %
                  (label, lo, hi, len(s), m, 100.0 * (m - bm) / bm, flag))
        print()


if __name__ == "__main__":
    main()
