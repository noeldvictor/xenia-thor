#!/usr/bin/env python3
"""Pair Turnip's u_trace fields into per-pass records and report the render mode.

The extractor in bd_render_mode.sh emits fields in file order. One pass is:
    width=W, height=H      (start_render_pass)
    numberOfBins=N         (start_render_pass)
    tiledRender=B          (end_render_pass)
    tilingDisableReason=R  (end_render_pass)
    drawCount=D            (end_render_pass)

THE QUESTION: which of BD's passes run in binning (GMEM) mode and which in
direct (sysmem) mode. The harness showed attachment height is FREE in sysmem
and costs ~+31us/pass in binning, so the answer decides whether the EDRAM-span
render targets cost us anything at all.
"""
import re
import sys
from collections import defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else "scratchpad/bd_mode_raw.txt"

cur = {}
passes = []
for line in open(path, encoding="utf-8", errors="replace"):
    line = line.strip()
    if not line:
        continue
    m = re.match(r"width=(\d+), height=(\d+)", line)
    if m:
        # A new start_render_pass: anything half-built is a torn record.
        cur = {"w": int(m.group(1)), "h": int(m.group(2))}
        continue
    if line.startswith("numberOfBins="):
        cur["bins"] = int(line.split("=", 1)[1])
    elif line.startswith("tiledRender="):
        cur["tiled"] = line.split("=", 1)[1] == "true"
    elif line.startswith("tilingDisableReason="):
        cur["why"] = line.split("=", 1)[1].strip() or "-"
    elif line.startswith("drawCount="):
        cur["draws"] = int(line.split("=", 1)[1])
        if {"w", "h", "tiled"} <= set(cur):
            passes.append(cur)
        cur = {}

if not passes:
    sys.exit("no complete pass records - check the extractor's field order")

tiled = sum(1 for p in passes if p["tiled"])
print(f"passes parsed        : {len(passes):,}")
print(f"  binning (GMEM)     : {tiled:,} ({100.0*tiled/len(passes):.1f}%)")
print(f"  direct  (sysmem)   : {len(passes)-tiled:,} ({100.0*(len(passes)-tiled)/len(passes):.1f}%)")

print("\n=== by geometry: how the EDRAM-span passes actually render ===")
agg = defaultdict(lambda: [0, 0, 0])  # (w,h,tiled) -> [count, draws, bins]
for p in passes:
    a = agg[(p["w"], p["h"], p["tiled"])]
    a[0] += 1
    a[1] += p["draws"]
    a[2] = p.get("bins", 0)
rows = sorted(agg.items(), key=lambda kv: -kv[1][0])
print(f"{'geometry':>14} {'mode':>8} {'passes':>9} {'bins':>5} {'draws':>10} {'draws/pass':>11}")
for (w, h, t), (n, d, b) in rows[:14]:
    print(f"{w}x{h:<9} {'GMEM' if t else 'sysmem':>8} {n:>9,} {b:>5} {d:>10,} {d/n:>11.1f}")

print("\n=== why direct mode was chosen ===")
why = defaultdict(int)
for p in passes:
    if not p["tiled"]:
        why[p.get("why", "-")] += 1
for k, v in sorted(why.items(), key=lambda kv: -kv[1]):
    print(f"  {v:>9,}  {k}")

# The cost question: EDRAM-span passes that actually bin are the only ones the
# harness's +31us/pass figure applies to.
span = [p for p in passes if p["h"] > 1024 and p["tiled"]]
print(f"\nEDRAM-span (h>1024) passes IN BINNING MODE: {len(span):,}"
      f"  ({100.0*len(span)/len(passes):.2f}% of all passes)")
