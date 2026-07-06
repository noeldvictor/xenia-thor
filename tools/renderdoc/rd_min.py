import os
OUT = r"C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\tools\renderdoc\rd_out.txt"
def log(m):
    with open(OUT, "a") as f:
        f.write(str(m) + "\n")
log("=== SCRIPT RAN ===")
try:
    import renderdoc as rd
    log("renderdoc imported")
    try:
        log("version " + str(rd.GetVersionString()))
    except Exception as e:
        log("GetVersionString err " + repr(e))
except Exception as e:
    log("import err " + repr(e))
log("=== END ===")
