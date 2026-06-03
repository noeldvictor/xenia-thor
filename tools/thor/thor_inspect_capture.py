#!/usr/bin/env python3
"""Parse xenia-thor GPU "Lane A" capture logcat(s) and classify the frame.

Reads the in-tree attachment-readback + draw-outcome trace lines emitted by:
  --ez vulkan_trace_dump_rt_image true
  --ez vulkan_trace_dump_depth_image true
  --ez vulkan_trace_draw_outcomes_per_frame true
and prints, per logcat file, the decisive numbers + a failure-mode verdict.
Pass two files (e.g. turnip then system) to diff them.

Usage:
  python tools/thor/thor_inspect_capture.py <logcat.txt> [<logcat2.txt>]
"""
import re
import sys


def _last(pattern, text, group):
    m = None
    for m in re.finditer(pattern, text):
        pass
    return m.group(group) if m else None


def parse(path):
    with open(path, "r", errors="replace") as f:
        text = f.read()
    out = {"path": path}
    # GPU draw outcomes/frame: rendered=N ... total_vertices=N ... guest_ms=N
    out["rendered"] = _last(r"GPU draw outcomes/frame: rendered=(\d+)", text, 1)
    out["total_vertices"] = _last(r"total_vertices=(\d+)", text, 1)
    out["guest_ms"] = _last(r"\bguest_ms=(\d+)", text, 1)
    # dump RT IMAGE checksum ... size=WxH color_format=F samples=N nonzero=N varying=N
    # Pick the WIDEST color RT (the main present target; several aliased base-0
    # RTs exist at smaller pitches), not just the last line.
    best_w = -1
    out["rt_size"] = out["rt_nonzero"] = out["rt_varying"] = None
    for m in re.finditer(
            r"dump RT IMAGE checksum .*?size=(\d+)x(\d+).*?nonzero=(\d+) "
            r"varying=(\d+)", text):
        w = int(m.group(1))
        if w >= best_w:  # last occurrence of the max width = the heavy-scene frame
            best_w = w
            out["rt_size"] = "%sx%s" % (m.group(1), m.group(2))
            out["rt_nonzero"] = m.group(3)
            out["rt_varying"] = m.group(4)
    # dump DEPTH IMAGE checksum ... samples=N nonzero=N varying=N
    out["d_nonzero"] = _last(r"dump DEPTH IMAGE checksum .*?nonzero=(\d+)", text, 1)
    out["d_varying"] = _last(r"dump DEPTH IMAGE checksum .*?varying=(\d+)", text, 1)
    out["turnip"] = "Loaded Turnip Vulkan driver" in text
    out["no_devices"] = "No Vulkan physical devices available" in text
    return out


def verdict(o):
    rendered = int(o["rendered"]) if o["rendered"] else None
    dvar = int(o["d_varying"]) if o["d_varying"] else None
    rtnz = int(o["rt_nonzero"]) if o["rt_nonzero"] else None
    gms = int(o["guest_ms"]) if o["guest_ms"] else None
    if o["no_devices"]:
        return "DRIVER FAIL: no Vulkan devices (stale hooks_path? re-derive via pm path)."
    if rendered is None:
        return "NO DATA: no draw-outcomes line (run did not render / cvars not set)."
    if gms is not None and gms < 60000:
        return ("EARLY HANG: guest_ms=%d (<60000) - did not reach the heavy "
                "scene; retry." % gms)
    if rendered == 0:
        return "draws NOT issued (rendered=0): CPU/command-processor, not GPU."
    if rtnz and rtnz > 0:
        return ("COLOR target IS written (rt_nonzero=%d): bug is DOWNSTREAM "
                "(dump/resolve/present)." % rtnz)
    # Depth heuristic: a real rasterized scene varies across most of the image
    # (hundreds+ of the ~5000 sampled words). varying in the low tens is just
    # clear-boundary noise on a mostly-uninitialized RT - and the 1xMSAA readback
    # only reaches a 1xMSAA depth; the active depth may be MSAA (not copyable),
    # so a near-empty result here is INCONCLUSIVE, not proof of no-raster.
    if dvar is not None and dvar >= 200:
        return ("GEOMETRY RASTERIZED (depth varying=%s) but COLOR empty: "
                "color-write / resolve bug." % o["d_varying"])
    return ("COLOR empty + depth near-empty (varying=%s) - INCONCLUSIVE M1-vs-M2: "
            "either no rasterization (vertex/clip/cull) OR the 1xMSAA depth "
            "readback missed the active MSAA depth. Read the active depth or "
            "capture a Qualcomm reference to diff." % o["d_varying"])


def show(o):
    drv = "turnip" if o["turnip"] else ("no-devices" if o["no_devices"] else "system/unknown")
    print("== %s  [%s] ==" % (o["path"], drv))
    print("  draws : rendered=%s total_vertices=%s guest_ms=%s"
          % (o["rendered"], o["total_vertices"], o["guest_ms"]))
    print("  color : size=%s nonzero=%s varying=%s"
          % (o["rt_size"], o["rt_nonzero"], o["rt_varying"]))
    print("  depth : nonzero=%s varying=%s" % (o["d_nonzero"], o["d_varying"]))
    print("  --> %s" % verdict(o))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    parsed = [parse(p) for p in argv[1:]]
    for o in parsed:
        show(o)
    if len(parsed) == 2:
        a, b = parsed
        print("\n== DIFF (%s  vs  %s) ==" % (a["path"], b["path"]))
        print("  color nonzero: %s vs %s | depth varying: %s vs %s"
              % (a["rt_nonzero"], b["rt_nonzero"], a["d_varying"], b["d_varying"]))
        print("  (compare at matched guest_ms %s vs %s / rendered %s vs %s)"
              % (a["guest_ms"], b["guest_ms"], a["rendered"], b["rendered"]))

        def _i(x):
            return int(x) if x and x.isdigit() else 0
        # If the reference (second file, e.g. system/Qualcomm) rasterizes the SAME
        # RTs that the first (e.g. turnip) leaves empty, the first driver is not
        # rasterizing - the empty-depth reading is NOT an inactive-RT artifact.
        if _i(b["d_varying"]) >= 200 and _i(a["d_varying"]) < 64 and _i(a["rt_nonzero"]) == 0:
            print("  --> %s is NOT rasterizing the geometry the reference renders "
                  "(same RTs) -> M1 confirmed: vertex-position/clip/viewport bug."
                  % a["path"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
