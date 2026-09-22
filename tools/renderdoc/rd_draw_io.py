# Headless RenderDoc replay: a draw's inputs - the pixel and vertex stage
# constant buffers in full, and the post-vertex-shader outputs (position and
# every varying: min, max, mean per component over the vertices).
# Input rd_in.txt: line 1 the .rdc path, then one event id per line.
# Output rd_out.txt. Two captures of the same draw with identical shaders
# and bindings but different output differ here - in the constants or in
# what the vertex shader produced (a black vertex color, a zero light)
# (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_draw_io.py
import os
import struct

DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


def as_floats(data):
    n = len(data) // 4
    return struct.unpack("<%df" % n, data[:n * 4])


def as_dwords(data):
    n = len(data) // 4
    return struct.unpack("<%dI" % n, data[:n * 4])


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
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        ps = controller.GetPipelineState()
        log("== eid=%d" % eid)
        for stage, label in ((rd.ShaderStage.Vertex, "vs"), (rd.ShaderStage.Pixel, "ps")):
            refl = ps.GetShaderReflection(stage)
            if refl is None:
                continue
            for u in ps.GetConstantBlocks(stage):
                d = u.descriptor
                if d.resource == rd.ResourceId.Null() or not d.byteSize:
                    continue
                cb_name = ""
                for r in refl.constantBlocks:
                    if r.fixedBindNumber == u.access.index:
                        cb_name = r.name
                size = min(d.byteSize, 1024)
                data = controller.GetBufferData(d.resource, d.byteOffset, size)
                log("  %s cb b%d %s off=%X size=%X" % (label, u.access.index, cb_name, d.byteOffset, d.byteSize))
                floats = as_floats(data)
                dwords = as_dwords(data)
                for i in range(0, len(floats), 4):
                    row = floats[i:i + 4]
                    raw = dwords[i:i + 4]
                    log("    [%3d] %s  |%s" % (i // 4, " ".join("%12.6g" % v for v in row), " ".join("%08X" % v for v in raw)))
        # Post-VS outputs.
        try:
            pv = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            refl = ps.GetShaderReflection(rd.ShaderStage.Vertex)
            if pv.numIndices and pv.vertexResourceId != rd.ResourceId.Null():
                data = controller.GetBufferData(pv.vertexResourceId, pv.vertexByteOffset, 0)
                stride = pv.vertexByteStride
                count = min(pv.numIndices, len(data) // stride)
                outs = list(refl.outputSignature)
                log("  post-VS %d verts stride=%d outputs=%d" % (count, stride, len(outs)))
                offset = 0
                for o in outs:
                    comps = o.compCount
                    sums = [0.0] * comps
                    mins = [1e30] * comps
                    maxs = [-1e30] * comps
                    for i in range(count):
                        vals = struct.unpack_from("<%df" % comps, data, i * stride + offset)
                        for k in range(comps):
                            sums[k] += vals[k]
                            mins[k] = min(mins[k], vals[k])
                            maxs[k] = max(maxs[k], vals[k])
                    log("    %-14s reg=%d comps=%d mean=%s min=%s max=%s" % (o.varName, o.regIndex, comps, " ".join("%.4g" % (s / max(count, 1)) for s in sums), " ".join("%.4g" % v for v in mins), " ".join("%.4g" % v for v in maxs)))
                    offset += comps * 4
        except Exception as e:
            log("  post-VS: " + repr(e))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
