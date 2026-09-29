# Headless RenderDoc replay: the raw contents of the bound depth target (every
# sample) after given events, written to files, so two captures of one trace
# can be compared byte by byte where their images differ. Input rd_in.txt:
# line 1 the .rdc path, line 2 the output file prefix, then one event id per
# line; "prev:<eid>" adds the draw before that event too. Output rd_out.txt:
# per event the target, size, format, samples, and the file names
# (<prefix>_<eid>_s<sample>.bin). 2026-09-29: Gears' nondeterministic draw 270
# - do two captured runs that differ in the image differ after the draw too?
# Run: tools/renderdoc/run.ps1 <abs>\rd_target_dump.py <capture.rdc> <prefix> <eid>...
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
    prefix = rd_in[1]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    cap.OpenFile(RDC, "", None)
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    draws = []

    def flatten(a_list):
        for a in a_list:
            if int(a.flags) & int(rd.ActionFlags.Drawcall):
                draws.append(a.eventId)
            flatten(a.children)

    flatten(controller.GetRootActions())
    eids = []
    for item in rd_in[2:]:
        if item.startswith("prev:"):
            eid = int(item[5:])
            before = [d for d in draws if d < eid]
            if before:
                eids.append(before[-1])
            eids.append(eid)
        else:
            eids.append(int(item))
    textures = {t.resourceId: t for t in controller.GetTextures()}
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        target = controller.GetPipelineState().GetDepthTarget().resource
        tex = textures.get(target)
        if tex is None:
            log("eid=%d no depth target" % eid)
            continue
        names = []
        for sample in range(max(1, tex.msSamp)):
            data = controller.GetTextureData(target, rd.Subresource(0, 0, sample))
            name = "%s_%d_s%d.bin" % (prefix, eid, sample)
            with open(name, "wb") as f:
                f.write(bytes(data))
            names.append(os.path.basename(name))
        log("eid=%d target=%s %dx%d %s samples=%d bytes=%d files=%s" % (
            eid, target, tex.width, tex.height, tex.format.Name(), tex.msSamp,
            len(data), ",".join(names)))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
