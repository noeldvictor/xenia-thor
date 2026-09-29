# Headless RenderDoc replay: the post-VS positions of the draws with a given
# index count - how many are NaN or Inf, the w range, how many have w <= 0,
# and the largest |x/w| and |y/w| of the vertices in front of the eye. For a
# draw that renders differently from run to run (a vertex with a NaN or
# infinite position is undefined behavior for the rasterizer). Input
# rd_in.txt: line 1 the .rdc path, line 2 the index count to match (0 = all
# draws). Output rd_out.txt: one line per draw, then the first vertices with a
# non-finite component (2026-09-29, Gears' skinned mesh through the near plane).
# Run: tools/renderdoc/run.ps1 <abs>\rd_postvs_nonfinite.py <capture.rdc> <count>
import math
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
    want = int(rd_in[1]) if len(rd_in) > 1 else 0
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s index count %d" % (st, want))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {r.resourceId: r.name for r in controller.GetResources()}
    acts = []

    def flatten(a_list):
        for a in a_list:
            acts.append(a)
            flatten(a.children)

    flatten(controller.GetRootActions())
    for a in acts:
        if not (int(a.flags) & int(rd.ActionFlags.Drawcall)):
            continue
        if want and a.numIndices != want:
            continue
        controller.SetFrameEvent(a.eventId, True)
        ps = controller.GetPipelineState()
        nonfinite = 0
        behind = 0
        wmin, wmax = 1e30, -1e30
        big = 0.0
        examples = []
        count = 0
        try:
            pv = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            if pv.numIndices and pv.vertexResourceId != rd.ResourceId.Null():
                data = controller.GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0)
                stride = pv.vertexByteStride
                count = min(pv.numIndices, len(data) // stride)
                for i in range(count):
                    x, y, z, w = struct.unpack_from("<ffff", data, i * stride)
                    if not all(math.isfinite(c) for c in (x, y, z, w)):
                        nonfinite += 1
                        if len(examples) < 8:
                            examples.append((i, x, y, z, w))
                        continue
                    wmin = min(wmin, w)
                    wmax = max(wmax, w)
                    if w <= 0:
                        behind += 1
                    else:
                        big = max(big, abs(x / w), abs(y / w))
        except Exception as e:
            log("  post-VS error at %d: %r" % (a.eventId, e))
        log("eid=%d idx=%d vs=%s vertices=%d nonfinite=%d behind_eye=%d w %.4g..%.4g max|ndc xy|=%.4g" % (
            a.eventId, a.numIndices,
            names.get(ps.GetShader(rd.ShaderStage.Vertex), "?").replace("Shader Module ", "vs"),
            count, nonfinite, behind, wmin, wmax, big))
        for e in examples:
            log("   vertex %d: %r %r %r %r" % e)
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
