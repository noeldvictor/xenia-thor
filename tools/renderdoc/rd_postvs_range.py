# Headless RenderDoc replay: post-VS coverage of every draw in an event
# range whose viewport matches. Input rd_in.txt: line 1 the .rdc path, line 2
# first event id, line 3 last event id, line 4 the viewport width to match
# (0 = all). Output rd_out.txt: one line per draw - event id, index count,
# vertex shader, how many post-VS vertices land inside the clip volume, the
# NDC ranges, and the vertex float constants' buffer offset and first dwords.
# Sum the "inside" column of two captures of the same scene to see which
# draws of a pass stopped landing (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_postvs_range.py
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
    first = int(rd_in[1])
    last = int(rd_in[2])
    vp_width = int(rd_in[3]) if len(rd_in) > 3 else 0
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s range %d..%d viewport %d" % (st, first, last, vp_width))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {r.resourceId: r.name for r in controller.GetResources()}
    acts = []

    def flatten(a_list):
        for a in a_list:
            acts.append(a)
            flatten(a.children)

    flatten(controller.GetRootActions())
    total_inside = 0
    for a in acts:
        if a.eventId < first or a.eventId > last or not (int(a.flags) & int(rd.ActionFlags.Drawcall)):
            continue
        controller.SetFrameEvent(a.eventId, True)
        ps = controller.GetPipelineState()
        v = ps.GetViewport(0)
        if vp_width and int(v.width) != vp_width:
            continue
        inside = 0
        count = 0
        mn = [1e9, 1e9, 1e9]
        mx = [-1e9, -1e9, -1e9]
        try:
            pv = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            if pv.numIndices and pv.vertexResourceId != rd.ResourceId.Null():
                data = controller.GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0)
                stride = pv.vertexByteStride
                count = min(pv.numIndices, len(data) // stride)
                for i in range(count):
                    x, y, z, w = struct.unpack_from("<ffff", data, i * stride)
                    if w == 0:
                        continue
                    ndc = (x / w, y / w, z / w)
                    for k in range(3):
                        mn[k] = min(mn[k], ndc[k])
                        mx[k] = max(mx[k], ndc[k])
                    if -1 <= ndc[0] <= 1 and -1 <= ndc[1] <= 1 and 0 <= ndc[2] <= 1:
                        inside += 1
        except Exception as e:
            log("  post-VS error at %d: %r" % (a.eventId, e))
        total_inside += inside
        cb = ""
        for u in ps.GetConstantBlocks(rd.ShaderStage.Vertex):
            if u.access.index != 1:
                continue
            d = u.descriptor
            if d.resource != rd.ResourceId.Null() and d.byteSize:
                data = controller.GetBufferData(d.resource, d.byteOffset, 16)
                cb = "cb1 off=%X %s" % (d.byteOffset, " ".join("%08X" % x for x in struct.unpack("<4I", data[:16])))
        log("eid=%d idx=%d vs=%s inside=%d/%d x %.2f..%.2f y %.2f..%.2f z %.3f..%.3f %s" % (a.eventId, a.numIndices, names.get(ps.GetShader(rd.ShaderStage.Vertex), "?").replace("Shader Module ", "vs"), inside, count, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], cb))
    log("total inside=%d" % total_inside)
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
