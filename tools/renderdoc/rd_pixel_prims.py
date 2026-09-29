# Headless RenderDoc replay: which triangles of a draw wrote a pixel, and
# their vertices. Input rd_in.txt: line 1 the .rdc path, line 2 the event id
# of the draw, then one "x,y" (or "x,y,sample") per line - pixels of the
# depth target. Output rd_out.txt: per pixel the pixel history entries of
# that event (primitive id, shader depth, passed or not), and per primitive
# its three indices and their post-VS positions (clip x y z w and NDC) - a
# spike across the screen names the vertex that is off (2026-09-29, Gears'
# black light-shaft wedges).
# Run: tools/renderdoc/run.ps1 <abs>\rd_pixel_prims.py <capture.rdc> <eid> <x,y>...
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
    eid = int(rd_in[1])
    pixels = []
    for line in rd_in[2:]:
        parts = [int(v) for v in line.split(',')]
        pixels.append((parts[0], parts[1], parts[2] if len(parts) > 2 else 0))
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    cap.OpenFile(RDC, "", None)
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    controller.SetFrameEvent(eid, True)
    ps = controller.GetPipelineState()
    depth = ps.GetDepthTarget()
    target = depth.resource
    log("event %d depth target %s" % (eid, target))
    # Post-VS data and its index buffer.
    pv = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
    vdata = controller.GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0)
    stride = pv.vertexByteStride
    idata = b""
    istride = pv.indexByteStride
    if pv.indexResourceId != rd.ResourceId.Null():
        idata = controller.GetBufferData(pv.indexResourceId, pv.indexByteOffset, 0)
    log("post-VS: %d indices, index stride %d, vertex stride %d, %d vertex bytes" % (
        pv.numIndices, istride, stride, len(vdata)))

    def index_at(i):
        if not idata:
            return i
        fmt = "<I" if istride == 4 else "<H"
        return struct.unpack_from(fmt, idata, i * istride)[0]

    def vertex(i):
        x, y, z, w = struct.unpack_from("<ffff", vdata, i * stride)
        return x, y, z, w

    prims = set()
    for (x, y, sample) in pixels:
        sub = rd.Subresource(0, 0, sample)
        history = controller.PixelHistory(target, x, y, sub, rd.CompType.Typeless)
        for mod in history:
            if mod.eventId != eid:
                continue
            log("pixel %d,%d s%d: prim %d depth %r passed %s" % (
                x, y, sample, mod.primitiveID, mod.shaderOut.depth, mod.Passed()))
            prims.add(mod.primitiveID)
    for p in sorted(prims):
        idx = [index_at(3 * p + k) for k in range(3)]
        verts = [vertex(i) for i in idx]
        log("prim %d: indices %s" % (p, idx))
        for i, (x, y, z, w) in zip(idx, verts):
            ndc = (x / w, y / w, z / w) if w else (0, 0, 0)
            log("   vertex %d: clip %.4g %.4g %.4g %.4g  ndc %.3f %.3f %.3f" % (
                i, x, y, z, w, ndc[0], ndc[1], ndc[2]))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
