# Headless RenderDoc replay: where a draw's vertices land, and what it read.
# Input rd_in.txt: line 1 the .rdc path, then one event id per line.
# Output rd_out.txt: per draw the post-vertex-shader NDC ranges (x/w, y/w,
# z/w min..max over all vertices; outside -1..1 = clipped away), the
# viewport and scissor, the vertex buffers, and the vertex-stage constant
# buffers with their offsets and first dwords. Two captures of the same
# scene disagree here when a draw's constants or vertex data differ, not its
# textures (Banjo-Kazooie's shadow pass, 2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_postvs.py
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
    eids = [int(x) for x in rd_in[1:]]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s" % st)
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {r.resourceId: r.name for r in controller.GetResources()}
    acts = {}

    def flatten(a_list):
        for a in a_list:
            acts[a.eventId] = a
            flatten(a.children)

    flatten(controller.GetRootActions())
    for eid in eids:
        a = acts.get(eid)
        controller.SetFrameEvent(eid, True)
        ps = controller.GetPipelineState()
        log("== eid=%d indices=%d vs=%s ps=%s pipeline=%s" % (eid, a.numIndices if a else -1, names.get(ps.GetShader(rd.ShaderStage.Vertex), "?"), names.get(ps.GetShader(rd.ShaderStage.Pixel), "-"), names.get(ps.GetGraphicsPipelineObject(), "?")))
        try:
            v = ps.GetViewport(0)
            sc = ps.GetScissor(0)
            log("  viewport %gx%g+%g,%g z %g..%g scissor %dx%d+%d,%d" % (v.width, v.height, v.x, v.y, v.minDepth, v.maxDepth, sc.width, sc.height, sc.x, sc.y))
        except Exception as e:
            log("  viewport: " + repr(e))
        try:
            pv = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            if pv.numIndices and pv.vertexResourceId != rd.ResourceId.Null():
                data = controller.GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0)
                stride = pv.vertexByteStride
                count = min(pv.numIndices, len(data) // stride)
                mn = [1e9, 1e9, 1e9]
                mx = [-1e9, -1e9, -1e9]
                inside = 0
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
                log("  post-VS %d verts: x %.3f..%.3f y %.3f..%.3f z %.4f..%.4f inside=%d" % (count, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], inside))
            else:
                log("  post-VS: none")
        except Exception as e:
            log("  post-VS: " + repr(e))
        try:
            for vb in ps.GetVBuffers():
                log("  vb %s off=%X stride=%d" % (names.get(vb.resourceId, vb.resourceId), vb.byteOffset, vb.byteStride))
        except Exception as e:
            log("  vb: " + repr(e))
        for u in ps.GetConstantBlocks(rd.ShaderStage.Vertex):
            d = u.descriptor
            head = ""
            if d.resource != rd.ResourceId.Null() and d.byteSize:
                data = controller.GetBufferData(d.resource, d.byteOffset, min(d.byteSize, 64))
                n = len(data) // 4
                head = " ".join("%08X" % x for x in struct.unpack("<%dI" % n, data[:n * 4]))
            log("  cb b%d %s off=%X size=%X: %s" % (u.access.index, names.get(d.resource, d.resource), d.byteOffset, d.byteSize, head))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
