# Headless RenderDoc replay: debug one vertex of a draw through the vertex
# shader and log every variable change (RenderDoc's SPIR-V debugger).
# Input rd_in.txt: line 1 the .rdc path, line 2 the event id, line 3 the
# vertex index (default 0). Output rd_out.txt: one line per changed
# variable: step, name, values. With the emulator's translated shaders the
# names are the SPIR-V ids (_670) and the register file entries, so the
# fetched texture values and the vertex attributes appear in the order the
# shader computed them - a zero where a color should be names the fetch
# that failed (2026-09-21).
# Run: "C:\Program Files\RenderDoc\qrenderdoc.exe" --python <abs>\rd_debug_vertex.py
import os

DIR = r"F:\Projects\xenia-thor-workspace\xenia-thor\tools\renderdoc"
OUT = os.path.join(DIR, "rd_out.txt")
open(OUT, "w").close()


def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")


def value_text(var):
    try:
        n = max(1, var.rows * var.columns)
        vals = list(var.value.f32v)[:n]
        return " ".join("%.5g" % v for v in vals)
    except Exception:
        return "?"


try:
    with open(os.path.join(DIR, "rd_in.txt")) as f:
        rd_in = [x.strip() for x in f.read().split(chr(10)) if x.strip()]
    RDC = rd_in[0]
    EID = int(rd_in[1])
    VERT = int(rd_in[2]) if len(rd_in) > 2 else 0
    import renderdoc as rd
    cap = rd.OpenCaptureFile()
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=%s eid=%d vertex=%d" % (st, EID, VERT))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    controller = res[1] if isinstance(res, (tuple, list)) else res
    controller.SetFrameEvent(EID, True)
    trace = controller.DebugVertex(VERT, 0, VERT, 0)
    if trace is None or trace.debugger is None:
        log("no debug trace (debugging unsupported for this shader)")
        raise SystemExit
    step = 0
    while True:
        states = controller.ContinueDebug(trace.debugger)
        if not states:
            break
        for s in states:
            for c in s.changes:
                v = c.after
                log("%5d %-28s = %s" % (step, v.name, value_text(v)))
            step += 1
        if step > 20000:
            log("stopped after 20000 steps")
            break
    controller.FreeTrace(trace)
    controller.Shutdown()
    cap.Shutdown()
except SystemExit:
    pass
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
log("=== DONE ===")
os._exit(0)
