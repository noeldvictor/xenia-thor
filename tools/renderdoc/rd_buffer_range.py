# Headless RenderDoc replay: a buffer range's content at several events, and
# the buffer copies that write into the range.
# Input rd_in.txt: line 1 the .rdc path, line 2 the buffer id number, line 3
# the byte offset (hex ok), line 4 the byte size, then one event id per line.
# Output rd_out.txt: per event the dword statistics of the range (fraction
# 0, fraction 0xFFFFFFFF, distinct sample), then every vkCmdCopyBuffer whose
# destination region overlaps the range. Shows when a GPU-written guest
# range (a resolve) is overwritten by a CPU upload (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_buffer_range.py
import os
import struct

DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


def find(obj, name):
    # Depth-first search of a structured-data object for a child by name.
    try:
        for i in range(obj.NumChildren()):
            c = obj.GetChild(i)
            if c.name == name:
                return c
            r = find(c, name)
            if r is not None:
                return r
    except Exception:
        pass
    return None


try:
    with open(os.path.join(DIR, "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    RDC = rd_in[0]
    buffer_number = int(rd_in[1])
    offset = int(rd_in[2], 0)
    size = int(rd_in[3], 0)
    eids = [int(x) for x in rd_in[4:]]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s buffer %d range %X+%X" % (st, buffer_number, offset, size))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    names = {}
    by_number = {}
    for r in controller.GetResources():
        names[r.resourceId] = r.name
        try:
            by_number[int(str(r.resourceId).split("::")[-1])] = r.resourceId
        except Exception:
            pass
    rid = by_number[buffer_number]
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        data = controller.GetBufferData(rid, offset, size)
        n = len(data) // 4
        dwords = struct.unpack("<%dI" % n, data[:n * 4])
        zero = sum(1 for d in dwords if d == 0) / float(max(n, 1))
        ones = sum(1 for d in dwords if d == 0xFFFFFFFF) / float(max(n, 1))
        distinct = len(set(dwords[::97]))
        # k_24_8 depth: the low 24 bits; far is 0xFFFFFF. A shadow map with
        # no casters has depth_not_far == 0.
        depth_not_far = sum(1 for d in dwords if (d & 0xFFFFFF) != 0xFFFFFF) / float(max(n, 1))
        log("eid=%d bytes=%d zero=%.3f ones=%.3f depth_not_far=%.4f distinct(sampled)=%d first=%s" % (eid, len(data), zero, ones, depth_not_far, distinct, " ".join("%08X" % d for d in dwords[:6])))
    # Copies into the range.
    sf = controller.GetStructuredFile()
    acts = []

    def flatten(a_list):
        for a in a_list:
            acts.append(a)
            flatten(a.children)

    flatten(controller.GetRootActions())
    log("--- copies into the range")
    for a in acts:
        if getattr(a, "copyDestination", rd.ResourceId.Null()) != rid:
            continue
        try:
            chunk = sf.chunks[a.events[-1].chunkIndex]
        except Exception:
            continue
        if str(chunk.name) != "vkCmdCopyBuffer":
            continue
        regions = find(chunk, "pRegions")
        if regions is None:
            continue
        for i in range(regions.NumChildren()):
            region = regions.GetChild(i)
            dst = find(region, "dstOffset")
            sz = find(region, "size")
            src = find(region, "srcOffset")
            if dst is None or sz is None:
                continue
            d0 = int(dst.data.basic.u)
            s = int(sz.data.basic.u)
            if d0 < offset + size and d0 + s > offset:
                log("eid=%d copy %s -> %s dst=%X size=%X src=%X" % (a.eventId, names.get(a.copySource, ""), names.get(rid, ""), d0, s, int(src.data.basic.u) if src is not None else 0))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
