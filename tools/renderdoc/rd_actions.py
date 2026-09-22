# Headless RenderDoc replay: the non-draw actions of an event range.
# Input rd_in.txt: line 1 the .rdc path, line 2 first event id, line 3 last
# event id. Output rd_out.txt: every copy, dispatch, clear, and resolve in
# the range with its source and destination resources, plus the render pass
# begins - the frame's data flow between two draws (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_actions.py
import os

DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


try:
    with open(os.path.join(DIR, "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    RDC = rd_in[0]
    first = int(rd_in[1]) if len(rd_in) > 1 else 0
    last = int(rd_in[2]) if len(rd_in) > 2 else 1 << 30
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s range %d..%d" % (st, first, last))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {r.resourceId: r.name for r in controller.GetResources()}
    sf = controller.GetStructuredFile()
    acts = []

    def flatten(a_list):
        for a in a_list:
            acts.append(a)
            flatten(a.children)

    flatten(controller.GetRootActions())
    draw_flags = int(rd.ActionFlags.Drawcall)
    for a in acts:
        if a.eventId < first or a.eventId > last:
            continue
        flags = int(a.flags)
        if flags & draw_flags:
            continue
        name = ""
        try:
            name = str(sf.chunks[a.events[-1].chunkIndex].name)
        except Exception:
            pass
        if not name or name in ("vkCmdPipelineBarrier", "vkCmdSetViewport", "vkCmdSetScissor"):
            continue
        src = getattr(a, "copySource", rd.ResourceId.Null())
        dst = getattr(a, "copyDestination", rd.ResourceId.Null())
        extra = ""
        if src != rd.ResourceId.Null() or dst != rd.ResourceId.Null():
            extra = " %s -> %s" % (names.get(src, src), names.get(dst, dst))
        try:
            extra += " " + a.GetName(sf)
        except Exception:
            pass
        log("eid=%d %s%s" % (a.eventId, name, extra))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
