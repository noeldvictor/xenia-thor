# Headless RenderDoc replay: a compute dispatch's full state.
# Input rd_in.txt: line 1 the .rdc path, then one event id per line.
# Output rd_out.txt: per dispatch the group counts, the compute pipeline and
# shader, every bound buffer and image with offset and size, and the push
# constants as dwords - the emulator's texture load and resolve dispatches
# are driven by these (guest offset, pitch, format) (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_dispatch.py
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
        dim = getattr(a, "dispatchDimension", None) if a else None
        log("== eid=%d groups=%s pipeline=%s shader=%s" % (eid, list(dim) if dim else "?", names.get(ps.GetComputePipelineObject(), "?"), names.get(ps.GetShader(rd.ShaderStage.Compute), "?")))
        for u in ps.GetReadOnlyResources(rd.ShaderStage.Compute):
            d = u.descriptor
            log("  ro b%d %s off=%X size=%X" % (u.access.index, names.get(d.resource, d.resource), d.byteOffset, d.byteSize))
        for u in ps.GetReadWriteResources(rd.ShaderStage.Compute):
            d = u.descriptor
            log("  rw b%d %s off=%X size=%X" % (u.access.index, names.get(d.resource, d.resource), d.byteOffset, d.byteSize))
        for u in ps.GetConstantBlocks(rd.ShaderStage.Compute):
            d = u.descriptor
            log("  cb b%d %s off=%X size=%X" % (u.access.index, names.get(d.resource, d.resource), d.byteOffset, d.byteSize))
            if d.resource != rd.ResourceId.Null() and d.byteSize and d.byteSize <= 256:
                data = controller.GetBufferData(d.resource, d.byteOffset, d.byteSize)
                n = len(data) // 4
                log("     " + " ".join("%08X" % x for x in struct.unpack("<%dI" % n, data[:n * 4])))
        try:
            vk = controller.GetVulkanPipelineState()
            pc = bytes(vk.pushconsts)
            if pc:
                n = len(pc) // 4
                log("  push constants: " + " ".join("%08X" % x for x in struct.unpack("<%dI" % n, pc[:n * 4])))
        except Exception as e:
            log("  push constants: " + repr(e))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
