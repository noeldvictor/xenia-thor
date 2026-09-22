# Headless RenderDoc replay: image content statistics at an event.
# Input rd_in.txt: line 1 the .rdc path, line 2 the event id, then one image
# id number per line. Output rd_out.txt: per image its size and format, the
# byte count RenderDoc returns for mip 0 (decoded to RGBA8 for compressed
# formats), the fraction of zero bytes, the mean of every byte, and a PNG
# saved next to this script as rd_img_<id>.png. A texture that reads as all
# zeros was never loaded (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_tex_stats.py
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
    numbers = [int(x) for x in rd_in[2:]]
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s eid=%d" % (st, EID))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    tex = {}
    for t in controller.GetTextures():
        tex[int(str(t.resourceId).split("::")[-1])] = t
    controller.SetFrameEvent(EID, True)
    for number in numbers:
        t = tex.get(number)
        if t is None:
            log("image %d: not a texture" % number)
            continue
        data = controller.GetTextureData(t.resourceId, rd.Subresource(0, 0, 0))
        n = len(data)
        zeros = sum(1 for b in data if b == 0)
        mean = sum(data) / float(max(n, 1))
        log("image %d: %dx%d %s mips=%d bytes=%d zero_bytes=%.3f mean_byte=%.1f" % (number, t.width, t.height, t.format.Name(), t.mips, n, zeros / float(max(n, 1)), mean))
        save = rd.TextureSave()
        save.resourceId = t.resourceId
        save.destType = rd.FileType.PNG
        save.mip = 0
        controller.SaveTexture(save, os.path.join(DIR, "rd_img_%d.png" % number))
    controller.Shutdown()
    cap.Shutdown()
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
