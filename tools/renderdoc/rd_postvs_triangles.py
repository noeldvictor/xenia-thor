# Headless RenderDoc replay: the post-VS positions and the index list of one
# draw, as files for a CPU rasterizer (which triangles cover a pixel, and how
# far each spreads on the screen). Input rd_in.txt: line 1 the .rdc path, line
# 2 the output file prefix, line 3 the event id. Output: <prefix>_pos.bin
# (float32 x y z w per post-VS vertex), <prefix>_idx.bin (uint32 per index,
# into the positions), and in rd_out.txt the counts and the viewport.
# 2026-09-29: Gears' draw 270 - do the replay-varying spike triangles exist in
# RenderDoc's own vertex-shader run?
# Run: tools/renderdoc/run.ps1 <abs>\rd_postvs_triangles.py <capture.rdc> <prefix> <eid>
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
    prefix = rd_in[1]
    eid = int(rd_in[2])
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    cap.OpenFile(RDC, "", None)
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    controller.SetFrameEvent(eid, True)
    pv = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
    vdata = bytes(controller.GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0))
    stride = pv.vertexByteStride
    count = len(vdata) // stride
    with open(prefix + "_pos.bin", "wb") as f:
        for i in range(count):
            f.write(vdata[i * stride:i * stride + 16])
    indices = []
    if pv.indexResourceId != rd.ResourceId.Null():
        idata = bytes(controller.GetBufferData(pv.indexResourceId, pv.indexByteOffset, 0))
        fmt = "<I" if pv.indexByteStride == 4 else "<H"
        n = min(pv.numIndices, len(idata) // pv.indexByteStride)
        indices = [struct.unpack_from(fmt, idata, i * pv.indexByteStride)[0] for i in range(n)]
    else:
        indices = list(range(pv.numIndices))
    with open(prefix + "_idx.bin", "wb") as f:
        f.write(struct.pack("<%dI" % len(indices), *indices))
    vp = controller.GetPipelineState().GetViewport(0)
    log("eid=%d vertices=%d stride=%d indices=%d topology=%s viewport %g %g %g %g %g %g" % (
        eid, count, stride, len(indices), pv.topology, vp.x, vp.y, vp.width, vp.height,
        vp.minDepth, vp.maxDepth))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
