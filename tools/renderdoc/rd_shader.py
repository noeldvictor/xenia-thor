# Headless RenderDoc replay: a draw's shader disassembly and reflection.
# Input rd_in.txt: line 1 the .rdc path, line 2 the event id, line 3 the
# stage (vertex|pixel), line 4 the output file for the disassembly. Output
# rd_out.txt: the shader's resource reflection (bindings, sets), the
# disassembly line count, and the pipeline's blend/depth state; the SPIR-V
# disassembly goes to the output file. Diff two captures' disassemblies to
# see whether the emulator translated the same guest shader differently
# (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_shader.py
import os

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
    EID = int(rd_in[1])
    stage_name = rd_in[2] if len(rd_in) > 2 else "pixel"
    out_file = rd_in[3] if len(rd_in) > 3 else os.path.join(DIR, "rd_shader_%d.txt" % EID)
    import renderdoc as rd
    stage = rd.ShaderStage.Pixel if stage_name == "pixel" else rd.ShaderStage.Vertex
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s eid=%d stage=%s" % (st, EID, stage_name))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {r.resourceId: r.name for r in controller.GetResources()}
    controller.SetFrameEvent(EID, True)
    ps = controller.GetPipelineState()
    refl = ps.GetShaderReflection(stage)
    pipe = ps.GetGraphicsPipelineObject()
    log("shader=%s pipeline=%s entry=%s" % (names.get(ps.GetShader(stage), "?"), names.get(pipe, "?"), ps.GetShaderEntryPoint(stage)))
    for r in refl.readOnlyResources:
        log("  ro set=%d bind=%d %s type=%s" % (r.fixedBindSetOrSpace, r.fixedBindNumber, r.name, r.textureType))
    for r in refl.samplers:
        log("  sampler set=%d bind=%d %s" % (r.fixedBindSetOrSpace, r.fixedBindNumber, r.name))
    for r in refl.constantBlocks:
        log("  cb set=%d bind=%d %s bytes=%d" % (r.fixedBindSetOrSpace, r.fixedBindNumber, r.name, r.byteSize))
    try:
        vk = controller.GetVulkanPipelineState()
        ds = vk.depthStencil
        log("depth test=%s write=%s func=%s stencil=%s" % (ds.depthTestEnable, ds.depthWriteEnable, ds.depthFunction, ds.stencilTestEnable))
        cb = vk.colorBlend
        for i, b in enumerate(cb.blends):
            log("blend%d enabled=%s write=%X src=%s dst=%s op=%s" % (i, b.enabled, b.writeMask, b.colorBlend.source, b.colorBlend.destination, b.colorBlend.operation))
        log("alpha to coverage=%s" % cb.alphaToCoverageEnable)
    except Exception as e:
        log("vk state: " + repr(e))
    targets = controller.GetDisassemblyTargets(True)
    log("disassembly targets: %s" % list(targets))
    text = controller.DisassembleShader(pipe, refl, targets[0] if targets else "")
    with open(out_file, "w") as f:
        f.write(text)
    log("disassembly %d lines -> %s" % (text.count(chr(10)), out_file))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
