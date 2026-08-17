#!/usr/bin/env python3
"""Aggregate a BD census logcat: vertex distribution + per-pass composition.

Answers two questions the route script structurally cannot:

  1. DID THE RUN REACH THE SCENES BD IS SLOW IN? The route's scene gate reads
     the LAST frame, which says where the run ended. A run can crest at 260k
     vertices and end at 158k on the way out of a scene. This prints the whole
     distribution, so "never got there" and "got there and came back" stop
     looking identical.

  2. IS THERE ANYTHING FOR THE LRZ HACK TO RECOVER? Decision rule, fixed in
     advance so it cannot be rationalised afterwards:

         recoverable = zwrite_after_blend - zwrite_masked_after

     ZERO  -> dead for BD: every depth-write following a blend is already
              masked, so suppressing depth-write on blended draws restores
              nothing. Drop the hack.
     NON-0 -> that is the population the hack can act on.

TAKES THE LOG PATH AS ARGV - it is NOT hardcoded, deliberately. Git Bash's
/tmp and Windows Python's /tmp are DIFFERENT DIRECTORIES (the latter resolves
to C:\\tmp), and that mismatch has silently voided the reporting half of an A/B
here twice: 273 good frames captured, then FileNotFoundError reading them back.
Write into the repo and pass the path.

LOGCAT WRAPS LONG LINES, AND THE COMPOSITION FIELDS ARE AT THE END OF THE
BIGPASS RECORD - so they are the FIRST thing a wrap throws away. Only the first
physical line carries the BIGPASS token; the rest arrive with no prefix. This
parser therefore accumulates continuation lines into the open record instead of
reading one line and trusting it. A field-wise grep over the whole file would
recover the values and lose the per-pass correlation, which is the thing being
measured.
"""
import re
import sys
from collections import Counter

INT_FIELDS = (
    "draws blend_draws zwrite_draws first_blend_zwrite "
    "zwrite_after_blend zwrite_masked_after"
).split()
HEX_FIELDS = "blendctl0 colormask depthctl".split()
DIMS = re.compile(r"\b(\d+)x(\d+)\b")


def parse(path):
    passes, records = [], []
    cur = None
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "BIGPASS" in line or "SMALLPASS" in line:
                if cur:
                    records.append(cur)
                cur = {"kind": "BIG" if "BIGPASS" in line else "SMALL",
                       "text": line}
            elif cur is not None and any(f + "=" in line for f in INT_FIELDS):
                # A continuation of the record still open.
                cur["text"] += " " + line
            if "total_vertices=" in line:
                m = re.search(r"total_vertices=(\d+)", line)
                if m:
                    passes.append(int(m.group(1)))
    if cur:
        records.append(cur)

    out = []
    for r in records:
        rec = {"kind": r["kind"]}
        for f in INT_FIELDS:
            m = re.search(r"\b" + f + r"=(\d+)", r["text"])
            rec[f] = int(m.group(1)) if m else None
        for f in HEX_FIELDS:
            m = re.search(r"\b" + f + r"=([0-9A-Fa-f]+)", r["text"])
            rec[f] = m.group(1) if m else None
        m = DIMS.search(r["text"])
        rec["dims"] = "%sx%s" % m.groups() if m else "?"
        out.append(rec)
    return passes, out


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: bd_lrz_report.py <logcat.txt>")
    verts, recs = parse(sys.argv[1])

    print("=== SCENE: total_vertices per frame ===")
    if not verts:
        print("  NONE - no frame trace. Pass --ez vulkan_trace_draw_outcomes_per_frame true.")
    else:
        buckets = [(0, 50_000), (50_000, 120_000), (120_000, 180_000),
                   (180_000, 230_000), (230_000, 300_000), (300_000, 1 << 31)]
        for lo, hi in buckets:
            n = sum(1 for v in verts if lo <= v < hi)
            hi_s = "inf" if hi > 1 << 30 else f"{hi:,}"
            print(f"  {lo:>9,} - {hi_s:>9}  n={n:6d}")
        print(f"  frames={len(verts)}  peak={max(verts):,}  median={sorted(verts)[len(verts)//2]:,}")
        # The heavy buckets are the ones BD is slow in and the ones a warm start
        # samples worst. Say so explicitly rather than leaving it to be noticed.
        heavy = sum(1 for v in verts if v >= 180_000)
        if heavy < 50:
            print(f"  !! ONLY {heavy} HEAVY FRAMES (>=180k). Too thin to conclude from.")
            print("     Cause is almost always thermal: the run hit the 70C guard")
            print("     before the deep scene. Start colder (COLD=37000).")
        else:
            print(f"  OK: {heavy} heavy frames (>=180k)")

    big = [r for r in recs if r["kind"] == "BIG"]
    print(f"\n=== PASS COMPOSITION: {len(big)} BIGPASS records ===")
    if not big:
        print("  NONE - pass --ez gpu_trace_resolve_timing true.")
        return

    def s(field):
        return sum(r[field] or 0 for r in big)

    # ENGAGEMENT CHECK FIRST. A blended pass reporting blend_draws=0 means the
    # counters are being cleared before the log reads them (the ordering bug
    # fixed 2026-08-17) - the numbers below would be an artefact, not a result.
    blended_passes = [r for r in big
                      if r["blendctl0"] and r["blendctl0"] != "00000000"]
    if blended_passes and s("blend_draws") == 0:
        print("  !! blend_draws=0 across passes whose blendctl0 is non-trivial.")
        print("     The counters are not being read before they are reset.")
        print("     This APK predates the ordering fix - rebuild, do not interpret.")
        return

    print(f"  draws              {s('draws'):>9,}")
    print(f"  blend_draws        {s('blend_draws'):>9,}")
    print(f"  zwrite_draws       {s('zwrite_draws'):>9,}")
    print(f"  zwrite_after_blend {s('zwrite_after_blend'):>9,}")
    print(f"  zwrite_masked_after{s('zwrite_masked_after'):>9,}")

    recoverable = s("zwrite_after_blend") - s("zwrite_masked_after")
    print(f"\n  RECOVERABLE = zwrite_after_blend - zwrite_masked_after = {recoverable:,}")
    if recoverable <= 0:
        print("  ==> LRZ HACK IS DEAD FOR BD. Every depth-write following a blend is")
        print("      already masked, so suppressing depth-write on blended draws")
        print("      cannot restore LRZ write. Drop gpu_no_depth_write_on_blend.")
    else:
        print(f"  ==> {recoverable:,} draws are an UPPER BOUND on the addressable set.")
        print("      It is an upper bound and not a count, because zwrite_after_blend")
        print("      includes BLENDED depth-writing draws - and the hack strips those")
        print("      of their depth-write rather than preserving it, so they populate")
        print("      no LRZ. The real value is the NON-blended, fully-masked subset.")
        print("      Zero is therefore conclusive; non-zero needs one more split.")

    print("\n  heaviest passes:")
    for r in sorted(big, key=lambda x: -(x["draws"] or 0))[:6]:
        print(f"    {r['dims']:>10}  draws={r['draws']:>5}  blend={r['blend_draws']:>5}"
              f"  zwrite={r['zwrite_draws']:>5}  after_blend={r['zwrite_after_blend']:>5}"
              f"  masked={r['zwrite_masked_after']:>5}"
              f"  blendctl0={r['blendctl0']} depthctl={r['depthctl']}")

    top = Counter(r["dims"] for r in big).most_common(4)
    print("  framebuffer shapes: " + ", ".join(f"{d} x{n}" for d, n in top))


if __name__ == "__main__":
    main()
