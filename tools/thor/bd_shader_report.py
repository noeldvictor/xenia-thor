#!/usr/bin/env python3
"""Aggregate ir3 FRAG variant stats into the numbers that decide occupancy.

ir3 prints several lines per variant, keyed by "FRAG prog A/B":
    N instr, N nops, N non-nops, N mov, N cov, N dwords
    N last-baryf, N last-helper, N half, N full, N constlen
    N max_waves, N double_threadsize

THE COMPARISON THAT MATTERS: ATI budgeted UNDER 8 GPRs for a typical Xenos
pixel shader (r400 shader-processor spec p5), and the sequencer will not start
the next wave until the registers are free. If our translated shaders are far
above that, occupancy collapses and the SP is busy-but-idle - which is exactly
what "fragment-ALU bound" looks like from outside.
"""
import re
import sys
from collections import defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else "scratchpad/bd_frag_stats.txt"

variants = defaultdict(dict)
for line in open(path, encoding="utf-8", errors="replace"):
    key = re.search(r"FRAG prog (\d+/\d+)", line)
    if not key:
        continue
    v = variants[key.group(1)]
    for val, name in re.findall(r"(\d+) ([a-z_-]+)", line.split(":", 2)[-1]):
        v[name] = int(val)

full = [v for v in variants.values() if "instr" in v and "full" in v and "max_waves" in v]
if not full:
    sys.exit(f"no complete variants in {path} "
             f"({len(variants)} partial) - was the shader cache disabled?")

print(f"complete FRAG variants: {len(full)}  (of {len(variants)} seen)")


def dist(name, key, fmt="{:>6}"):
    xs = sorted(v[key] for v in full)
    n = len(xs)
    print(f"{name:>14}  min={fmt.format(xs[0])}  med={fmt.format(xs[n//2])}  "
          f"max={fmt.format(xs[-1])}  mean={sum(xs)/n:>8.1f}")


dist("instructions", "instr")
dist("full regs", "full")
dist("half regs", "half")
dist("max_waves", "max_waves")

nops = sum(v.get("nops", 0) for v in full)
instr = sum(v["instr"] for v in full)
print(f"\nNOPs: {nops:,} of {instr:,} instructions = {100.0*nops/instr:.1f}%"
      "   (XenDroid measured 26% overall on their title)")

print("\n=== the heaviest variants (what the frame actually pays) ===")
print(f"{'variant':>10} {'instr':>7} {'nops':>6} {'full':>5} {'half':>5} {'waves':>6}")
for k, v in sorted(variants.items(), key=lambda kv: -kv[1].get("instr", 0))[:12]:
    if "instr" not in v:
        continue
    print(f"{k:>10} {v['instr']:>7} {v.get('nops',0):>6} {v.get('full',0):>5} "
          f"{v.get('half',0):>5} {v.get('max_waves',0):>6}")

# Occupancy is the whole question: on a7xx more registers means fewer resident
# waves, and the Xenos budget was under 8 GPRs.
low = [v for v in full if v["max_waves"] <= 4]
print(f"\nvariants at <=4 waves: {len(low)} of {len(full)} "
      f"({100.0*len(low)/len(full):.0f}%)")
