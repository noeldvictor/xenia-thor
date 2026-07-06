# Headless RenderDoc replay analysis for xenia captures.
# Usage:  "C:\Program Files\RenderDoc\qrenderdoc.exe" --python rd_analyze.py [capture.rdc]
# Writes results to rd_out.txt (qrenderdoc stdout is not captured; poll rd_out.txt for
# "=== DONE ===" then kill qrenderdoc).
#
# RenderDoc 1.44 API notes (differ from old docs):
#   - GetPipelineState().GetOutputTargets() -> list of Descriptor; RT id is .resource
#   - No GetTexture(id): build {tex.resourceId: (w,h)} from controller.GetTextures()
#   - GetPostVSData(inst, view, rd.MeshDataStage.VSOut) + GetBufferData(id, off, 0)
#   - controller.SetFrameEvent(eventId, True) before per-draw queries (SLOW: replays)
import os, struct, sys
# NOTE: __file__ is NOT defined in qrenderdoc --python; hardcode absolute paths.
OUT = r"C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\tools\renderdoc\rd_out.txt"
RDC = r"C:\Users\leanerdesigner\.claude\jobs\032c0116\tmp\bd_cap_frame3601.rdc"
for _a in sys.argv:
    if str(_a).endswith(".rdc"):
        RDC = _a
open(OUT, "w").close()
def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")

log("start; importing renderdoc")
import renderdoc as rd
log("opening " + RDC)
cap = rd.OpenCaptureFile()
cap.OpenFile(RDC, "", None)
log("OpenCapture (replay init, slow)...")
res = cap.OpenCapture(rd.ReplayOptions(), None)
controller = res[1] if isinstance(res, (tuple, list)) else res
log("controller ready=" + str(controller is not None))

tex_dims = {}
for t in controller.GetTextures():
    tid = getattr(t, "resourceId", None) or getattr(t, "resource", None)
    tex_dims[tid] = (t.width, t.height)

def flatten(acts, out):
    for a in acts:
        out.append(a)
        flatten(a.children, out)
all_acts = []
flatten(controller.GetRootActions(), all_acts)
draws = [a for a in all_acts
         if (int(a.flags) & int(rd.ActionFlags.Drawcall)) and a.numIndices > 0]
log("draws=%d" % len(draws))

def rt_id(desc):
    v = getattr(desc, "resource", None)
    return v if isinstance(v, rd.ResourceId) else None

def xrange_ndc(a):
    # post-VS NDC x bounding box (min,max). NDC outside [-1,1] = frustum-clipped.
    try:
        pv = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
        if pv.numIndices == 0 or pv.vertexResourceId == rd.ResourceId.Null():
            return None
        data = controller.GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0)
        stride = pv.vertexByteStride
        mn, mx = 1e9, -1e9
        for i in range(min(pv.numIndices, 500)):
            off = i * stride
            if off + 16 > len(data): break
            x, y, z, w = struct.unpack_from("<ffff", data, off)
            if w != 0:
                nx = x / w
                mn = min(mn, nx); mx = max(mx, nx)
        return (round(mn, 2), round(mx, 2)) if mx > -1e9 else None
    except Exception as e:
        return ("err", repr(e)[:80])

# Sample draws that render into a 1280-wide RT (the BD native field RT).
step = max(1, len(draws) // 40)
log("1280-RT field draws (eventId, numIdx, RT, vp_w, post-VS ndc_x min..max):")
for idx in range(0, len(draws), step):
    a = draws[idx]
    try:
        controller.SetFrameEvent(a.eventId, True)
        ps = controller.GetPipelineState()
        vp = ps.GetViewport(0)
        tg = ps.GetOutputTargets()
        rid = rt_id(tg[0]) if tg else None
        rt = "%dx%d" % tex_dims[rid] if (rid and rid in tex_dims) else "none"
        if rt.startswith("1280x720"):
            log("  ev=%d n=%d rt=%s vp=(x=%d w=%d) ndcx=%s" %
                (a.eventId, a.numIndices, rt, round(vp.x), round(vp.width),
                 xrange_ndc(a)))
    except Exception as e:
        log("  ev=%d ERR %s" % (a.eventId, repr(e)[:80]))

controller.Shutdown()
cap.Shutdown()
log("=== DONE ===")
os._exit(0)
