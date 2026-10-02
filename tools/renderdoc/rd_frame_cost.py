# Headless RenderDoc replay: per draw the vertex and fragment shader
# invocations (pipeline statistics) and the SPIR-V of both stages, for
# tools/pc/frame_cost.py, which weights the Adreno instruction counts of the
# shader lab by them.
# Input rd_in.txt: line 1 the .rdc path, line 2 the output directory.
# Output: <dir>/shader_<sha1 16>.vulkan.bin.vert|frag (the shader lab's input
# naming), <dir>/draws.tsv (eid, vertex module, fragment module, VS
# invocations, PS invocations, indices, instances, PC GPU us, and the
# non-zero specialization constants of each stage as "id=value;..." - the
# zero rule, bool and texture sign constants the pipeline was made with;
# RenderDoc stores each value in 8 bytes, little-endian), and rd_out.txt
# with the counter names and "=== DONE ===".
# Run: tools/renderdoc/run.ps1 <abs>\rd_frame_cost.py <capture.rdc> <dir>
import hashlib
import os

import renderdoc as rd

DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


def main():
    with open(os.path.join(DIR, "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    rdc, out_dir = rd_in[0], rd_in[1]
    os.makedirs(out_dir, exist_ok=True)
    cap = rd.OpenCaptureFile()
    cap.OpenFile(rdc, "", None)
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res

    draws = []

    def flatten(actions):
        for a in actions:
            if a.flags & rd.ActionFlags.Drawcall:
                draws.append(a)
            flatten(a.children)
    flatten(controller.GetRootActions())

    available = set(controller.EnumerateCounters())
    wanted = [c for c in (rd.GPUCounter.VSInvocations, rd.GPUCounter.PSInvocations,
                          rd.GPUCounter.EventGPUDuration) if c in available]
    log("counters: %s" % ", ".join(controller.DescribeCounter(c).name for c in wanted))
    values = {}
    for r in controller.FetchCounters(wanted):
        d = controller.DescribeCounter(r.counter)
        if d.resultByteWidth == 8 and d.resultType == rd.CompType.Float:
            v = r.value.d
        elif d.resultByteWidth == 8:
            v = r.value.u64
        else:
            v = r.value.u32
        values[(r.eventId, int(r.counter))] = v

    modules = {}  # ResourceId -> file name

    def module(pipe, stage, ext):
        rid = pipe.GetShader(stage)
        if rid == rd.ResourceId.Null():
            return "-"
        key = (str(rid), ext)
        if key not in modules:
            refl = pipe.GetShaderReflection(stage)
            raw = bytes(refl.rawBytes) if refl else b""
            if not raw:
                modules[key] = "-"
            else:
                name = "shader_%s.vulkan.bin.%s" % (hashlib.sha1(raw).hexdigest()[:16].upper(), ext)
                path = os.path.join(out_dir, name)
                if not os.path.exists(path):
                    with open(path, "wb") as f:
                        f.write(raw)
                modules[key] = name
        return modules[key]

    def specialization(stage):
        ids = list(stage.specializationIds or [])
        data = bytes(stage.specializationData or b"")
        values = [int.from_bytes(data[8 * i:8 * i + 8], "little") for i in range(len(ids))]
        return ";".join("%d=%d" % (i, v) for i, v in zip(ids, values) if v) or "-"

    rows = []
    for a in draws:
        controller.SetFrameEvent(a.eventId, False)
        pipe = controller.GetPipelineState()
        vs = module(pipe, rd.ShaderStage.Vertex, "vert")
        ps = module(pipe, rd.ShaderStage.Pixel, "frag")
        vk = controller.GetVulkanPipelineState()
        rows.append("%d\t%s\t%s\t%d\t%d\t%d\t%d\t%.1f\t%s\t%s" % (
            a.eventId, vs, ps,
            values.get((a.eventId, int(rd.GPUCounter.VSInvocations)), -1),
            values.get((a.eventId, int(rd.GPUCounter.PSInvocations)), -1),
            a.numIndices, a.numInstances,
            1e6 * values.get((a.eventId, int(rd.GPUCounter.EventGPUDuration)), 0.0),
            specialization(vk.vertexShader), specialization(vk.fragmentShader)))
    with open(os.path.join(out_dir, "draws.tsv"), "w") as f:
        f.write("eid\tvs\tps\tvs_inv\tps_inv\tindices\tinstances\tpc_gpu_us\tvs_spec\tps_spec\n")
        f.write("\n".join(rows) + "\n")
    log("draws %d, modules %d" % (len(rows), len([m for m in modules.values() if m != "-"])))
    controller.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception as e:
    import traceback
    log("ERROR " + repr(e))
    log(traceback.format_exc())
log("=== DONE ===")
