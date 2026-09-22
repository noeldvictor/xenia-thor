# Headless RenderDoc replay: one shader stage's complete binding state for a
# draw. Input rd_in.txt: line 1 the .rdc path, line 2 the event id, line 3
# the stage (vertex|pixel). Output rd_out.txt: the reflection's texture and
# sampler declarations, then every bound read-only resource and sampler with
# all descriptor fields, and the Vulkan descriptor set objects behind them.
# The vertex stage of a draw whose vertex texture fetches return zero shows
# its missing or wrong sampler here (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_stage_bindings.py
import os

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
    stage_name = rd_in[2] if len(rd_in) > 2 else "vertex"
    import renderdoc as rd
    stage = rd.ShaderStage.Vertex if stage_name == "vertex" else rd.ShaderStage.Pixel
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s eid=%d stage=%s" % (st, EID, stage_name))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {r.resourceId: r.name for r in controller.GetResources()}
    controller.SetFrameEvent(EID, True)
    ps = controller.GetPipelineState()
    refl = ps.GetShaderReflection(stage)
    log("shader %s" % names.get(ps.GetShader(stage), "?"))
    for r in refl.readOnlyResources:
        log("  decl texture set=%d bind=%d %s %s" % (r.fixedBindSetOrSpace, r.fixedBindNumber, r.name, r.textureType))
    for r in refl.samplers:
        log("  decl sampler set=%d bind=%d %s" % (r.fixedBindSetOrSpace, r.fixedBindNumber, r.name))
    for u in ps.GetReadOnlyResources(stage):
        log("bound texture: " + fields(u.access))
        log("   descriptor: " + fields(u.descriptor))
        try:
            sw = u.descriptor.swizzle
            log("   swizzle=%s,%s,%s,%s format=%s" % (sw.red, sw.green, sw.blue, sw.alpha, u.descriptor.format.Name()))
        except Exception as e:
            log("   swizzle: " + repr(e))
        log("   -> %s" % names.get(u.descriptor.resource, u.descriptor.resource))
    for u in ps.GetSamplers(stage):
        log("bound sampler: " + fields(u.access))
        log("   sampler: " + fields(u.sampler))
    try:
        vk = controller.GetVulkanPipelineState()
        log("vk pipeline=%s preRastLayout=%s fragmentLayout=%s" % (vk.graphics.pipelineResourceId, vk.graphics.pipelinePreRastLayoutResourceId, vk.graphics.pipelineFragmentLayoutResourceId))
        for i, ds in enumerate(vk.graphics.descriptorSets):
            log("vk set %d: %s" % (i, fields(ds)))
    except Exception as e:
        log("vk: " + repr(e))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
