# Headless RenderDoc replay: everything about one draw.
# Input rd_in.txt (next to this script): line 1 the .rdc path, line 2 the event id.
# Output rd_out.txt: the pipeline's pixel-stage samplers with every field, the
# bound images with their format and a value histogram of binding 0 (min, max,
# zero and one fractions: a shadow map that is all 0 or all 1 tells the story),
# the color target after the draw saved as rd_rt_<eid>.png next to the script,
# and the bound image 0 saved as rd_tex0_<eid>.png. Ends with "=== DONE ===".
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_draw_detail.py
import os
import struct
import sys

# __file__ is not defined under qrenderdoc --python: absolute paths.
DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


def fields(obj, skip=("this", "thisown")):
    out = []
    for k in dir(obj):
        if k.startswith("_") or k in skip:
            continue
        try:
            v = getattr(obj, k)
        except Exception:
            continue
        if callable(v):
            continue
        out.append("%s=%s" % (k, v))
    return " ".join(out)


try:
    with open(os.path.join(DIR, "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    RDC = rd_in[0]
    EID = int(rd_in[1])
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s eid=%d" % (st, EID))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    tex = {t.resourceId: t for t in controller.GetTextures()}
    names = {r.resourceId: r.name for r in controller.GetResources()}
    controller.SetFrameEvent(EID, True)
    ps = controller.GetPipelineState()
    log("pipeline: " + names.get(ps.GetGraphicsPipelineObject(), "?"))
    try:
        vk = controller.GetVulkanPipelineState()
        log("vk graphics pipeline layout / flags: " + fields(vk.graphics))
    except Exception as e:
        log("no vk state: " + repr(e))
    for u in ps.GetSamplers(rd.ShaderStage.Pixel):
        log("sampler access: " + fields(u.access))
        log("sampler: " + fields(u.sampler))
    images = []
    for u in ps.GetReadOnlyResources(rd.ShaderStage.Pixel):
        rid = u.descriptor.resource
        t = tex.get(rid)
        log("image access: " + fields(u.access))
        log("image descriptor: " + fields(u.descriptor))
        if t:
            log("image %s: %dx%d fmt=%s mips=%d arrays=%d" % (names.get(rid, rid), t.width, t.height, t.format.Name(), t.mips, t.arraysize))
            images.append(rid)
    # Binding 0's values.
    if images:
        rid = images[0]
        t = tex[rid]
        sub = rd.Subresource(0, 0, 0)
        data = controller.GetTextureData(rid, sub)
        log("image0 bytes=%d" % len(data))
        if t.format.Name() == "R32_FLOAT":
            n = len(data) // 4
            vals = struct.unpack("<%df" % n, data[:n * 4])
            zero = sum(1 for v in vals if v == 0.0) / float(n)
            one = sum(1 for v in vals if v >= 1.0) / float(n)
            log("image0 R32: min=%g max=%g zero=%.3f one=%.3f" % (min(vals), max(vals), zero, one))
        save = rd.TextureSave()
        save.resourceId = rid
        save.destType = rd.FileType.PNG
        save.mip = 0
        controller.SaveTexture(save, os.path.join(DIR, "rd_tex0_%d.png" % EID))
    # Vertex-stage textures (a vertex-textured shadow caster).
    for u in ps.GetReadOnlyResources(rd.ShaderStage.Vertex):
        rid = u.descriptor.resource
        t = tex.get(rid)
        log("vertex image b%d: %s %s" % (u.access.index, names.get(rid, rid), ("%dx%d %s" % (t.width, t.height, t.format.Name())) if t else ""))
    for u in ps.GetSamplers(rd.ShaderStage.Vertex):
        log("vertex sampler b%d: %s" % (u.access.index, names.get(u.sampler.object, "")))
    # The depth target after this draw: its value range says whether the
    # draws of the pass wrote anything.
    try:
        d = ps.GetDepthTarget()
        rid = d.resource
        if rid != rd.ResourceId.Null():
            t = tex.get(rid)
            mm = controller.GetMinMax(rid, rd.Subresource(0, 0, 0), rd.CompType.Depth)
            log("depth target %s %s min=%s max=%s" % (names.get(rid, rid), ("%dx%d %s" % (t.width, t.height, t.format.Name())) if t else "", mm[0].floatValue[0], mm[1].floatValue[0]))
    except Exception as e:
        log("depth target: " + repr(e))
    # The color target after this draw.
    for i, d in enumerate(ps.GetOutputTargets()):
        rid = d.resource
        if rid == rd.ResourceId.Null():
            continue
        t = tex.get(rid)
        log("rt%d %s %s" % (i, names.get(rid, rid), ("%dx%d %s" % (t.width, t.height, t.format.Name())) if t else ""))
        save = rd.TextureSave()
        save.resourceId = rid
        save.destType = rd.FileType.PNG
        save.mip = 0
        controller.SaveTexture(save, os.path.join(DIR, "rd_rt_%d.png" % EID))
        break
    log("--- debug messages up to this event")
    for m in controller.GetDebugMessages():
        if m.eventId <= EID:
            log("eid=%d %s %s: %s" % (m.eventId, m.severity, m.category, m.description))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
