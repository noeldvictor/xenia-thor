#!/usr/bin/env bash
# BD at SAME VISUAL QUALITY: does the shipping GameProfile GPU set actually help?
#
# ⚠ WHY THIS RUN EXISTS: GameProfiles is applied ONLY from the launcher
# (XeniaAndroidSettings.java:290). Every headless route measurement this project
# has ever taken therefore ran WITHOUT BD's profile - including the clamps, each
# device-validated at ~10% and both LOSSLESS. So the 15.4 fps "baseline" was
# never the shipping config, and the heavy passes profiled at 720x1824 because
# the 768 clamp was not active.
#
# All three levers here are QUALITY-NEUTRAL: the clamps trim tile-store rows the
# guest never renders into (BD max render height 720 < 768, aliasing transfers
# 0), and direct_host_resolve changes the resolve PATH, not the pixels.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
R=/c/Users/leanerdesigner/Documents/xenia-thor-workspace/xenia-thor/tools/thor
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"; export ADB DEV
OUT=/c/Users/leanerdesigner/Documents/xenia-thor-workspace/xenia-thor/scratchpad
temp(){ t=$("$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r'); case "$t" in ''|*[!0-9]*) echo 99999;; *) echo "$t";; esac; }
cool(){ for i in $(seq 1 160); do t=$(temp); [ "$t" -le 38000 ] && { echo "  at $((t/1000))C"; return 0; }; sleep 10; done; echo "ABORT: no cooldown"; exit 1; }
arm(){ label="$1"; shift
  echo; echo "########## $label ##########"; cool
  EXTRA="--ez vulkan_trace_pass_timestamps true $*" bash "$R/bd_gameplay_route.sh" || { echo "$label VOID"; return 1; }
  for i in 1 2 3 4 5; do
    "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | tr -d '\r' | grep "GPU pass timing" > "$OUT/ship_$label.txt"
    [ -s "$OUT/ship_$label.txt" ] && break; echo "  empty pull, retry $i"; sleep 3
  done
  python - "$OUT/ship_$label.txt" "$label" <<'PY'
import re,sys
rows=[]
for l in open(sys.argv[1],encoding='utf-8',errors='replace'):
    m=re.search(r'gpu_frame_us=(\d+).*?verts=(\d+)',l)
    if m: rows.append((int(m.group(1)),int(m.group(2))))
def med(v): v=sorted(v); return v[len(v)//2]
print("%-10s frames=%d"%(sys.argv[2],len(rows)))
for lo,hi in [(50000,120000),(120000,180000),(180000,230000),(230000,300000)]:
    s=[r[0] for r in rows if lo<=r[1]<hi]
    if len(s)>=8: print("   verts %6d-%6d n=%3d  %7d us  %.2f fps"%(lo,hi,len(s),med(s),1e6/med(s)))
PY
}
SHIP="--ei gpu_clamp_rt_framebuffer_height 768 --ei gpu_clamp_rt_image_height 768 --ez vulkan_direct_host_resolve true"
# ARMS is space-separated arm names: noprofile | shipgpu | roaa
#
# ROAA keeps EDRAM RESIDENT IN TILE MEMORY and eliminates the ownership-transfer
# COPIES outright - device-measured ~9ms / ~22% of the GPU frame on BTTF, and
# NEVER ONCE TESTED ON BD. It aims at the only bucket addressable without
# spending image quality: BD does 45 EDRAM ownership transfers a frame where
# Burnout does 9, and the frame is 89.6% shading / 10.4% EDRAM machinery.
# ⚠ Marked EXPERIMENTAL upstream, so CHECK THE PIXELS - "different path, same
# output" is a claim, not an observation.
for a in ${ARMS:-noprofile shipgpu}; do
  case "$a" in
    noprofile) arm noprofile ;;
    shipgpu)   arm shipgpu $SHIP ;;
    roaa)      arm roaa $SHIP --ez gpu_vulkan_edram_roaa true ;;
    clamps)    arm clamps --ei gpu_clamp_rt_framebuffer_height 768                           --ei gpu_clamp_rt_image_height 768 ;;
    dhr)       arm dhr --ez vulkan_direct_host_resolve true ;;
    *) echo "unknown arm: $a"; exit 1 ;;
  esac
done
