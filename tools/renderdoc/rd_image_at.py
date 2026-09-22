# Headless RenderDoc replay: an R32_FLOAT image's value statistics at several
# events. Input rd_in.txt: line 1 the .rdc path, line 2 the image id number,
# then one event id per line. Output rd_out.txt: per event min, max, and the
# fractions of 0 and >= 1 - when a loaded texture turns to a constant between
# its load and the draw that samples it, the event that did it is between
# two lines (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_image_at.py
import os
import struct

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
    number = int(rd_in[1])
    eids = [int(x) for x in rd_in[2:]]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s image %d" % (st, number))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    rid = None
    for r in controller.GetResources():
        if str(r.resourceId).endswith("::%d" % number):
            rid = r.resourceId
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        data = controller.GetTextureData(rid, rd.Subresource(0, 0, 0))
        n = len(data) // 4
        vals = struct.unpack("<%df" % n, data[:n * 4])
        zero = sum(1 for v in vals if v == 0.0) / float(n)
        one = sum(1 for v in vals if v >= 1.0) / float(n)
        log("eid=%d min=%g max=%g zero=%.3f one=%.4f" % (eid, min(vals), max(vals), zero, one))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
