# Headless RenderDoc replay: every API call in an event range, with the set
# numbers, layouts, and pipelines of the binding calls. Input rd_in.txt: line
# 1 the .rdc path, line 2 first event id, line 3 last event id. Output
# rd_out.txt: one line per event - the chunk name and, for
# vkCmdPushDescriptorSetKHR / vkCmdBindDescriptorSets / vkCmdBindPipeline,
# the parameters that decide descriptor state (set, firstSet, count, layout,
# pipeline). Shows what a draw's descriptor state was disturbed by (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_events.py
import os

DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


def child(obj, name):
    try:
        for i in range(obj.NumChildren()):
            c = obj.GetChild(i)
            if c.name == name:
                return c
    except Exception:
        pass
    return None


def val(c):
    if c is None:
        return "?"
    try:
        if "ResourceId" in str(c.type.basetype):
            return str(c.data.basic.id).split("::")[-1]
        if "Enum" in str(c.type.basetype) or "UnsignedInteger" in str(c.type.basetype):
            return str(c.data.basic.u)
        return str(c.data.basic.u)
    except Exception as e:
        return "?(%s)" % e


try:
    with open(os.path.join(DIR, "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    RDC = rd_in[0]
    first = int(rd_in[1])
    last = int(rd_in[2])
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s range %d..%d" % (st, first, last))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    sf = controller.GetStructuredFile()
    acts = []

    def flatten(a_list):
        for a in a_list:
            acts.append(a)
            flatten(a.children)

    flatten(controller.GetRootActions())
    for a in acts:
        for ev in a.events:
            if ev.eventId < first or ev.eventId > last:
                continue
            chunk = sf.chunks[ev.chunkIndex]
            name = str(chunk.name)
            extra = ""
            if name.startswith("vkCmdPushDescriptorSet"):
                writes = child(chunk, "pDescriptorWrites")
                n = writes.NumChildren() if writes is not None else 0
                binds = []
                for i in range(n):
                    w = writes.GetChild(i)
                    binds.append("b%s:%s" % (val(child(w, "dstBinding")), val(child(w, "descriptorType"))))
                imgs = []
                for i in range(n):
                    w = writes.GetChild(i)
                    info = child(w, "pImageInfo")
                    if info is not None and info.NumChildren():
                        e = info.GetChild(0)
                        imgs.append("%s/%s/%s" % (val(child(e, "sampler")), val(child(e, "imageView")), val(child(e, "imageLayout"))))
                extra = " bindPoint=%s set=%s layout=%s writes=%d [%s] infos(smp/view/layout)=[%s]" % (val(child(chunk, "pipelineBindPoint")), val(child(chunk, "set")), val(child(chunk, "layout")), n, " ".join(binds), " ".join(imgs))
            elif name == "vkCmdBindDescriptorSets":
                extra = " firstSet=%s count=%s layout=%s dyn=%s" % (val(child(chunk, "firstSet")), val(child(chunk, "setCount")), val(child(chunk, "layout")), val(child(chunk, "dynamicOffsetCount")))
            elif name == "vkCmdBindPipeline":
                extra = " pipeline=%s bindPoint=%s" % (val(child(chunk, "pipeline")), val(child(chunk, "pipelineBindPoint")))
            if extra.endswith("?") or "=?" in extra:
                # Unknown structure: dump the chunk's children once.
                try:
                    tree = []
                    for i in range(chunk.NumChildren()):
                        c = chunk.GetChild(i)
                        sub = []
                        for j in range(min(c.NumChildren(), 12)):
                            cc = c.GetChild(j)
                            sub.append(cc.name)
                        tree.append("%s(%s)" % (c.name, ",".join(sub)))
                    extra += " children: " + " ".join(tree)
                except Exception as e2:
                    extra += " children? " + repr(e2)
            log("eid=%d %s%s" % (ev.eventId, name, extra))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
