# Headless RenderDoc replay: the inputs of draws as the GPU sees them, to files
# and hashes, so two captures can be compared where their outputs differ.
# Input rd_in.txt: line 1 the .rdc path, line 2 the output file prefix, then
# one event id per line. Output rd_out.txt: per event the index buffer
# (resource, offset, stride, count, hash of the bytes; the bytes go to
# <prefix>_<eid>_indices.bin), the vertex buffers, and the Vulkan viewport,
# rasterizer and depth-stencil state as attribute lines - diff two outputs.
# 2026-09-29: Gears' draw 270 - identical post-VS vertices, different depth.
# Run: tools/renderdoc/run.ps1 <abs>\rd_draw_inputs.py <capture.rdc> <prefix> <eid>...
import hashlib
import os

DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


def dump(name, obj, depth=0):
    # Simple attributes of a pipeline state object, one per line.
    if depth > 3:
        return
    for attr in sorted(dir(obj)):
        if attr.startswith("_") or attr in ("this", "thisown"):
            continue
        try:
            value = getattr(obj, attr)
        except Exception:
            continue
        if callable(value):
            continue
        if isinstance(value, (int, float, bool, str)) or type(value).__name__ in (
                "ResourceId",) or hasattr(value, "name") and type(value).__module__ == "renderdoc" and isinstance(getattr(value, "value", None), int):
            log("  %s.%s = %s" % (name, attr, value))
        elif isinstance(value, (list, tuple)) or type(value).__name__.endswith("List"):
            for i, item in enumerate(list(value)[:8]):
                if isinstance(item, (int, float, bool, str)):
                    log("  %s.%s[%d] = %s" % (name, attr, i, item))
                else:
                    dump("%s.%s[%d]" % (name, attr, i), item, depth + 1)
        else:
            dump("%s.%s" % (name, attr), value, depth + 1)


try:
    with open(os.path.join(DIR, "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    RDC = rd_in[0]
    prefix = rd_in[1]
    eids = [int(x) for x in rd_in[2:]]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    cap.OpenFile(RDC, "", None)
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    actions = {}

    def flatten(a_list):
        for a in a_list:
            actions[a.eventId] = a
            flatten(a.children)

    flatten(controller.GetRootActions())
    for eid in eids:
        a = actions.get(eid)
        controller.SetFrameEvent(eid, True)
        ps = controller.GetPipelineState()
        log("eid=%d indices=%d indexOffset=%d baseVertex=%d" % (
            eid, a.numIndices, a.indexOffset, a.baseVertex))
        ib = ps.GetIBuffer()
        stride = ib.byteStride or 2
        data = bytes(controller.GetBufferData(
            ib.resourceId, ib.byteOffset + a.indexOffset * stride,
            a.numIndices * stride))
        with open("%s_%d_indices.bin" % (prefix, eid), "wb") as f:
            f.write(data)
        log("  ib %s offset %X stride %d bytes %d sha1 %s" % (
            ib.resourceId, ib.byteOffset, stride, len(data),
            hashlib.sha1(data).hexdigest()[:16]))
        for i, vb in enumerate(ps.GetVBuffers()):
            log("  vb%d %s offset %X stride %d" % (i, vb.resourceId, vb.byteOffset, vb.byteStride))
        vk = controller.GetVulkanPipelineState()
        dump("viewport", vk.viewportScissor)
        dump("raster", vk.rasterizer)
        dump("depth", vk.depthStencil)
        dump("ia", vk.inputAssembly)
        dump("ms", vk.multisample)
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
