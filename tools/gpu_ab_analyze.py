#!/usr/bin/env python3
"""Matched-draw-bucket gpu_frame_us comparison for the BD GPU A/B.

WHY: cross-run fps (and even raw mean gpu_frame_us) is confounded - BD's scene
complexity swings ~4x/second, so two runs sample different scenes. This buckets
frames by their DRAW COUNT (`rendered=`) and compares the MEDIAN gpu_frame_us
within each bucket, so only like-for-like frames are compared across configs.
Reports a draw-weighted overall ratio computed ONLY over buckets both configs hit.
"""
import re
import sys
import statistics
from pathlib import Path

HERE = Path(__file__).resolve().parent
RE_FRAME = re.compile(r"gpu_frame_us=(\d+)")
RE_RENDERED = re.compile(r"\brendered=(\d+)")
RE_PASS = re.compile(r"gpu_pass_us=(\d+)")
# The MECHANISM: how many EDRAM ownership transfers ran vs were dropped. On a TBDR
# each dropped transfer is a deleted render pass = a deleted GMEM tile store, which
# is both the speed win and the energy win (less memory traffic).
RE_XFER = re.compile(r"rt_transfers=(\d+)")
RE_XFER_DROP = re.compile(r"rt_xfers_dropped=(\d+)")
BUCKET = 25  # draw-count bucket width


def load(path):
    """-> {bucket: [gpu_frame_us,...]}, plus flat lists."""
    buckets, frames, passes, xfers, drops = {}, [], [], [], []
    if not path.exists():
        return buckets, frames, passes, xfers, drops
    # PowerShell's `>` redirect writes UTF-16LE with a BOM; plain utf-8 decoding
    # yields NUL-separated bytes and every regex silently misses. Sniff the BOM.
    raw = path.read_bytes()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = raw.decode("utf-16", errors="ignore")
    else:
        text = raw.decode("utf-8", errors="ignore")
    for line in text.splitlines():
        mf, mr = RE_FRAME.search(line), RE_RENDERED.search(line)
        if not (mf and mr):
            continue
        us, rendered = int(mf.group(1)), int(mr.group(1))
        if us <= 0 or rendered <= 0:
            continue  # idle / non-rendering frame
        buckets.setdefault((rendered // BUCKET) * BUCKET, []).append(us)
        frames.append(us)
        for rx, acc in ((RE_PASS, passes), (RE_XFER, xfers), (RE_XFER_DROP, drops)):
            m = rx.search(line)
            if m:
                acc.append(int(m.group(1)))
    return buckets, frames, passes, xfers, drops


def med(xs):
    return statistics.median(xs) if xs else 0


DEFAULT_DIR = HERE.parent / "scratch" / "thor-debug"


def main():
    # usage: gpu_ab_analyze.py [A_trace.txt B_trace.txt]
    if len(sys.argv) >= 3:
        a_path, b_path = Path(sys.argv[1]), Path(sys.argv[2])
    else:
        a_path = DEFAULT_DIR / "gpuab_A_lle.txt"
        b_path = DEFAULT_DIR / "gpuab_B_colordrop.txt"
    print(f"A: {a_path}\nB: {b_path}\n")
    a_b, a_f, a_p, a_x, a_d = load(a_path)
    b_b, b_f, b_p, b_x, b_d = load(b_path)

    if not a_f or not b_f:
        print(f"MISSING DATA: A frames={len(a_f)} B frames={len(b_f)}")
        print("A run or B run produced no 'GPU draw outcomes' lines - discard.")
        return 1

    print(f"A_lle       : {len(a_f)} rendering frames, median gpu_frame_us={med(a_f):.0f}"
          f"  (median gpu_pass_us={med(a_p):.0f})")
    print(f"B_colordrop : {len(b_f)} rendering frames, median gpu_frame_us={med(b_f):.0f}"
          f"  (median gpu_pass_us={med(b_p):.0f})")
    print()
    print("MECHANISM (median per frame) - EDRAM ownership transfers, i.e. render")
    print("passes / GMEM tile stores. Each dropped one is both a speed and an")
    print("energy saving (less memory traffic):")
    print(f"  A_lle       rt_transfers={med(a_x):.0f}  rt_xfers_dropped={med(a_d):.0f}")
    print(f"  B_colordrop rt_transfers={med(b_x):.0f}  rt_xfers_dropped={med(b_d):.0f}")
    print()
    print("MATCHED DRAW BUCKETS (only buckets BOTH configs hit; medians):")
    print(f"{'draws':>8} {'nA':>5} {'nB':>5} {'A us':>9} {'B us':>9} {'B/A':>7}")

    shared = sorted(set(a_b) & set(b_b))
    tot_a = tot_b = 0.0
    weight = 0
    for k in shared:
        na, nb = len(a_b[k]), len(b_b[k])
        if na < 3 or nb < 3:
            continue  # too few samples to trust this bucket
        ma, mb = med(a_b[k]), med(b_b[k])
        w = min(na, nb)
        tot_a += ma * w
        tot_b += mb * w
        weight += w
        print(f"{k:>8} {na:>5} {nb:>5} {ma:>9.0f} {mb:>9.0f} {mb/ma:>7.3f}")

    print()
    if weight:
        ratio = tot_b / tot_a
        print(f"WEIGHTED over matched buckets: A={tot_a/weight:.0f}us  B={tot_b/weight:.0f}us")
        print(f"  B/A = {ratio:.3f}  =>  {'FASTER' if ratio < 1 else 'SLOWER'} by "
              f"{abs(1-ratio)*100:.1f}%   (speedup {1/ratio:.3f}x)")
        print(f"  frame-time budget: 33333us = 30fps, 16667us = 60fps")
    else:
        print("NO MATCHED BUCKETS with >=3 samples each - the two runs never rendered")
        print("comparable scenes. Do NOT compare their raw medians; re-run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
