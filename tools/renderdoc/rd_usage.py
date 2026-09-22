# Headless RenderDoc replay: every event that touches a resource, in order.
# Input rd_in.txt: line 1 the .rdc path, then one resource id number per line
# (the number in "2D Image 5221", "Buffer 12"). Output rd_out.txt: for each
# resource the (event id, usage) list from RenderDoc's usage tracking - the
# clears, copies, dispatches that write it and the draws that sample it. This
# follows a texture's data flow: which dispatch loaded it, whether the
# resolve that fed the load ran before or after (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_usage.py
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
    wanted = [int(x) for x in rd_in[1:]]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s" % st)
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {}
    by_number = {}
    for r in controller.GetResources():
        names[r.resourceId] = r.name
        try:
            by_number[int(str(r.resourceId).split("::")[-1])] = r.resourceId
        except Exception:
            pass
    acts = {}

    def flatten(a_list):
        for a in a_list:
            acts[a.eventId] = a
            flatten(a.children)

    flatten(controller.GetRootActions())
    for num in wanted:
        rid = by_number.get(num)
        if rid is None:
            log("resource %d: not found" % num)
            continue
        log("== %s (%d)" % (names.get(rid, rid), num))
        for u in controller.GetUsage(rid):
            a = acts.get(u.eventId)
            what = ""
            if a is not None:
                what = " " + a.GetName(controller.GetStructuredFile()) if hasattr(a, "GetName") else ""
                try:
                    what = " " + str(controller.GetStructuredFile().chunks[a.events[-1].chunkIndex].name)
                except Exception:
                    pass
            extra = ""
            if a is not None:
                src = getattr(a, "copySource", rd.ResourceId.Null())
                dst = getattr(a, "copyDestination", rd.ResourceId.Null())
                if src != rd.ResourceId.Null() or dst != rd.ResourceId.Null():
                    extra = " copy %s -> %s" % (names.get(src, src), names.get(dst, dst))
            log("  eid=%d %s%s%s" % (u.eventId, u.usage, what, extra))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
