# Headless RenderDoc replay: the depth attachment's content in a region at
# several events. Input rd_in.txt: line 1 the .rdc path, line 2 the image id
# number, line 3 region "x,y,w,h", then one event id per line. Output
# rd_out.txt: per event the byte size RenderDoc returns for the image, and for
# the region the fraction of pixels whose depth is not the far value (1.0),
# with the depth read as float32 when the data is 4 bytes per pixel and the
# values look like floats, else as the low 24 bits of a D24S8 dword. Tells
# whether a depth-only pass wrote anything before it was resolved (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_depth_region.py
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
    number = int(rd_in[1])
    rx, ry, rw, rh = [int(x) for x in rd_in[2].split(",")]
    eids = [int(x) for x in rd_in[3:]]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s image %d region %d,%d %dx%d" % (st, number, rx, ry, rw, rh))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    rid = None
    tex = None
    for t in controller.GetTextures():
        if str(t.resourceId).endswith("::%d" % number):
            rid = t.resourceId
            tex = t
    log("image %dx%d %s" % (tex.width, tex.height, tex.format.Name()))
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        data = controller.GetTextureData(rid, rd.Subresource(0, 0, 0))
        bpp = len(data) // (tex.width * tex.height)
        not_far = 0
        total = 0
        mn = 1e9
        mx = -1e9
        as_float = bpp == 4 and tex.format.Name().startswith("D32")
        for y in range(ry, min(ry + rh, tex.height)):
            row = (y * tex.width + rx) * bpp
            n = min(rw, tex.width - rx)
            if bpp == 4:
                vals = struct.unpack_from("<%dI" % n, data, row)
                for v in vals:
                    if as_float:
                        d = struct.unpack("<f", struct.pack("<I", v))[0]
                    else:
                        d = (v & 0xFFFFFF) / float(0xFFFFFF)
                    total += 1
                    if d < 0.99999:
                        not_far += 1
                    mn = min(mn, d)
                    mx = max(mx, d)
            elif bpp == 8:
                vals = struct.unpack_from("<%dI" % (2 * n), data, row)
                for i in range(n):
                    d = struct.unpack("<f", struct.pack("<I", vals[2 * i]))[0]
                    total += 1
                    if d < 0.99999:
                        not_far += 1
                    mn = min(mn, d)
                    mx = max(mx, d)
        log("eid=%d bytes=%d bpp=%d region not_far=%.4f min=%g max=%g" % (eid, len(data), bpp, not_far / float(max(total, 1)), mn, mx))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
