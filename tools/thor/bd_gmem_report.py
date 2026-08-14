#!/usr/bin/env python3
"""Average gpu_frame_us over GAMEPLAY frames for each arm.

FILTER BY SCENE CONTENT, NEVER BY THE METRIC UNDER TEST. This file records a
result that inverted (+12% "faster" vs -18% slower) purely because frames were
filtered by pass_us+gap_us - selection on the dependent variable. total_vertices
is a property of the scene, so it is safe.
"""
import sys
import re

THRESHOLD = 150_000  # gameplay-tier, per the documented protocol


def load(path):
    """Fields arrive in file order: total_vertices then gpu_frame_us per frame."""
    verts = None
    frames = []
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if line.startswith("total_vertices="):
            verts = int(line.split("=", 1)[1])
        elif line.startswith("gpu_frame_us="):
            us = int(line.split("=", 1)[1])
            if verts is not None:
                frames.append((verts, us))
            verts = None
    return frames


def report(path):
    frames = load(path)
    play = [us for v, us in frames if v > THRESHOLD]
    name = re.sub(r".*gmem_|\.txt", "", path)
    if not play:
        print(f"{name:>10}: VOID - {len(frames)} frames, none above "
              f"{THRESHOLD:,} vertices (never reached gameplay)")
        return None
    play.sort()
    mean = sum(play) / len(play)
    med = play[len(play) // 2]
    print(f"{name:>10}: {len(play):>5} gameplay frames  mean={mean:>9,.0f}us  "
          f"median={med:>9,}us  fps={1e6/mean:>5.2f}  (of {len(frames)} total)")
    return mean


if __name__ == "__main__":
    a = report(sys.argv[1])
    b = report(sys.argv[2])
    if a and b:
        print(f"\nforcing binning: {100.0*(b-a)/a:+.1f}% frame time "
              f"({'SLOWER' if b > a else 'FASTER'})")
