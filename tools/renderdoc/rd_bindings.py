# Headless RenderDoc replay: the texture and sampler bindings of every draw.
# Usage:  "C:\Program Files\RenderDoc\qrenderdoc.exe" --python rd_bindings.py capture.rdc [min_indices]
# Or:     tools\renderdoc\run.ps1 rd_bindings.py capture.rdc
# Writes rd_out.txt next to this script: one line per draw with the event id,
# index count, viewport, and the pixel stage's read-only resources (set,
# binding, resource, size, format) and samplers; then the capture's debug
# messages. Ends with "=== DONE ===". A draw whose bound image is not the one
# the emulator meant to bind shows here as the wrong size or format
# (Banjo-Kazooie's foliage shadow map under push descriptors, 2026-09-21).
import os
import sys

# __file__ is not defined under qrenderdoc --python: an absolute path.
OUT = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc\rd_out.txt"
# The capture path and the index-count floor come from rd_in.txt (two
# lines) next to the output: qrenderdoc's argv is not reliable, and a .rdc
# positional argument opens the capture in the UI instead.
RDC = ""
MIN_INDICES = 0
try:
    with open(OUT.replace("rd_out.txt", "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    RDC = rd_in[0]
    if len(rd_in) > 1:
        MIN_INDICES = int(rd_in[1])
except Exception:
    pass
open(OUT, "w").close()
try:
    with open(OUT, "a") as f:
        f.write("argv=" + repr([str(a) for a in sys.argv]) + chr(10))
except Exception:
    pass


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


try:
    import renderdoc as rd
    log("opening " + RDC)
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=" + str(st))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    if not controller:
        log("no controller")
        raise SystemExit
    tex = {}
    for t in controller.GetTextures():
        tex[t.resourceId] = (t.width, t.height, str(t.format.Name()) if hasattr(t.format, "Name") else str(t.format))
    names = {}
    for r in controller.GetResources():
        names[r.resourceId] = r.name

    def flatten(acts, out):
        for a in acts:
            out.append(a)
            flatten(a.children, out)

    acts = []
    flatten(controller.GetRootActions(), acts)
    draws = [a for a in acts if (int(a.flags) & int(rd.ActionFlags.Drawcall)) and a.numIndices >= MIN_INDICES]
    log("draws=%d (min indices %d)" % (len(draws), MIN_INDICES))

    def describe(desc):
        rid = getattr(desc, "resource", None)
        if rid is None or rid == rd.ResourceId.Null():
            return "null"
        d = tex.get(rid)
        return "%s%s" % (names.get(rid, str(rid)), (" %dx%d %s" % d) if d else "")

    for a in draws:
        controller.SetFrameEvent(a.eventId, True)
        ps = controller.GetPipelineState()
        vp = ""
        try:
            v = ps.GetViewport(0)
            vp = "vp=%gx%g+%g,%g" % (v.width, v.height, v.x, v.y)
        except Exception:
            pass
        parts = []
        try:
            for u in ps.GetReadOnlyResources(rd.ShaderStage.Pixel):
                acc = u.access
                parts.append("ro[set%d b%d] %s" % (getattr(acc, "descriptorStore", 0) and 0 or getattr(acc, "index", -1), getattr(acc, "index", -1), describe(u.descriptor)))
        except Exception as e:
            parts.append("ro? " + repr(e))
        try:
            for u in ps.GetSamplers(rd.ShaderStage.Pixel):
                acc = u.access
                smp = u.descriptor
                parts.append("smp[b%d] %s filt=%s/%s cmp=%s" % (getattr(acc, "index", -1), names.get(getattr(smp, "object", None), ""), getattr(smp, "filter", ""), getattr(smp, "mipFilter", ""), getattr(smp, "compareFunction", "")))
        except Exception as e:
            parts.append("smp? " + repr(e))
        shader = ""
        try:
            shader = names.get(ps.GetShader(rd.ShaderStage.Pixel), "")
        except Exception:
            pass
        log("eid=%d idx=%d %s ps=%s %s" % (a.eventId, a.numIndices, vp, shader, " | ".join(parts)))
    log("--- debug messages")
    try:
        for m in controller.GetDebugMessages():
            log("eid=%d %s %s: %s" % (m.eventId, m.severity, m.category, m.description))
    except Exception as e:
        log("no debug messages: " + repr(e))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
