import os
OUTP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rd_out.txt")
def log(m):
    with open(OUTP, "a") as f:
        f.write(str(m) + "\n")
log("=== script started ===")
import sys
log("argv=" + repr(sys.argv))
try:
    import renderdoc as rd
    log("renderdoc imported")
    try:
        log("version=" + str(rd.GetVersionString()))
    except Exception as e2:
        log("no GetVersionString: " + repr(e2))
    cap = rd.OpenCaptureFile()
    RDC = r"C:\Users\leanerdesigner\.claude\jobs\032c0116\tmp\bd_cap_frame3601.rdc"
    st = cap.OpenFile(RDC, "", None)
    log("OpenFile=" + str(st))
    res = cap.OpenCapture(rd.ReplayOptions(), None)
    log("OpenCapture=" + repr(type(res)))
    controller = res[1] if isinstance(res, (tuple, list)) else res
    if controller:
        acts = controller.GetRootActions()
        log("root actions=" + str(len(acts)))
        controller.Shutdown()
    cap.Shutdown()
    log("DONE")
except Exception as e:
    import traceback
    log("ERR " + repr(e) + "\n" + traceback.format_exc())
