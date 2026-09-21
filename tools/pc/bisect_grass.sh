#!/usr/bin/env bash
# git bisect run script for the Banjo title-screen foliage glitch on the PC
# Vulkan build (black grass or gray foliage; canary Vulkan and the fork's
# D3D12 are clean). Run inside ../xenia-thor-bisect:
#   git bisect start HEAD <good>
#   git bisect run ../xenia-thor/tools/pc/bisect_grass.sh
# Exit 0 = good (two runs reach the title screen and score clean), 1 = bad
# (any title capture scores black grass or gray foliage), 125 = cannot build
# or no run reached the title screen (skip).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ISO="$ROOT/scratch/banjo/banjo.iso"
MSBUILD="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe"
LOG="$ROOT/scratch/banjo/bisect-grass.log"
here="$(git rev-parse --short HEAD)"
echo "== $(date +%H:%M:%S) bisect step $here" | tee -a "$LOG"

python - <<'EOF'
def ensure(path, marker, old, new):
    try:
        s = open(path, encoding='utf-8').read()
    except FileNotFoundError:
        return
    if marker in s:
        return
    if old in s:
        s = s.replace(old, new, 1)
        open(path, 'w', encoding='utf-8', newline='\n').write(s)
        print('premake fix:', path)
ensure('src/xenia/ui/vulkan/premake5.lua', 'removefiles({"*_demo.cc"})',
       '  local_platform_files()\n', '  local_platform_files()\n  removefiles({"*_demo.cc"})\n')
ensure('src/xenia/ui/d3d12/premake5.lua', 'removefiles({"*_demo.cc"})',
       '  local_platform_files()\n', '  local_platform_files()\n  removefiles({"*_demo.cc"})\n')
ensure('src/xenia/app/premake5.lua', 'filter({})\n  files({\n    "xenia_main.cc"',
       '  files({\n    "xenia_main.cc",\n  })\n', '  filter({})\n  files({\n    "xenia_main.cc",\n  })\n')
EOF
# The x64 NaN-helper fix (943c14d15f, 2026-09-21): without it every commit
# from 2edd685258 (2026-08-18) on shows the dirty-disc dialog on the PC and
# never reaches the title screen. Applied to the tree, never committed.
if git merge-base --is-ancestor 2edd685258 HEAD 2>/dev/null && ! git merge-base --is-ancestor 943c14d15f HEAD 2>/dev/null; then
  git cherry-pick -n 943c14d15f > /dev/null 2>&1 || { git reset -q --hard; echo "x64 fix does not apply at $here" | tee -a "$LOG"; }
fi
git submodule update --init --depth 1 > /dev/null 2>&1
for tp in pugixml; do
  [ -d "third_party/$tp/src" ] || cp -r "$ROOT/third_party/$tp" third_party/ 2>/dev/null
done
H=$(git rev-parse HEAD); S=$(git rev-parse --short=9 HEAD)
mkdir -p build
printf '#ifndef GENERATED_VERSION_H_\n#define GENERATED_VERSION_H_\n#define XE_BUILD_BRANCH "master"\n#define XE_BUILD_COMMIT "%s"\n#define XE_BUILD_COMMIT_SHORT "%s"\n#define XE_BUILD_DATE "bisect"\n#endif\n' "$H" "$S" > build/version.h
[ -f tools/build/gen_version_h.py ] && python tools/build/gen_version_h.py > /dev/null 2>&1
./tools/build/bin/premake5.exe --file=premake5.lua --os=windows vs2022 > /dev/null 2>&1 || { echo "premake failed" | tee -a "$LOG"; exit 125; }
rm -f build/bin/Windows/Release/xenia.exe
MSYS_NO_PATHCONV=1 "$MSBUILD" build/xenia-app.vcxproj '/p:Configuration=Release Windows' /p:Platform=x64 /m:8 /v:q /nologo > "$ROOT/scratch/banjo/bisect-grass-build-$here.log" 2>&1
if [ ! -f build/bin/Windows/Release/xenia.exe ]; then
  echo "build failed: $here" | tee -a "$LOG"; grep -m3 "error" "$ROOT/scratch/banjo/bisect-grass-build-$here.log" | cut -c1-200 | tee -a "$LOG"
  git reset -q --hard > /dev/null 2>&1; exit 125
fi
rm -rf storage && mkdir -p storage
# The saves and the profile from the main tree, so the route is the same.
mkdir -p storage/content && cp -r "$ROOT/scratch/banjo/pcshots/content/B13EBABEBABEBABE" storage/content/ 2>/dev/null
# Sign the profile in (the PC reads the config): without it the sign-in
# dialog covers the logo and the title screen is not recognized.
printf '[Profiles]
logged_profile_slot_0_xuid = "B13EBABEBABEBABE"
' > storage/xenia.config.toml
verdict=125
for run in 1 2; do
  rm -f storage/pc-vulkan-*.png
  python "$ROOT/tools/pc/pc_screens.py" "$ISO" --gpu vulkan --seq "start@30000:300;b@42000:300" --shots 54,60 \
    --storage "$(pwd)/storage" --exe "$(pwd)/build/bin/Windows/Release/xenia.exe" > "storage/run$run.txt" 2>&1
  if ! grep -q "Title name" storage/xenia.log; then
    echo "SKIP $here (no title loaded)" | tee -a "$LOG"; cp storage/xenia.log "$ROOT/scratch/banjo/bisect-grass-skip-$here.log" 2>/dev/null
    git reset -q --hard > /dev/null 2>&1; exit 125
  fi
  python "$ROOT/tools/pc/grass_score.py" storage/pc-vulkan-54s.png storage/pc-vulkan-60s.png | tee -a "$LOG"
  code=${PIPESTATUS[0]}
  if [ "$code" = "1" ]; then verdict=1; break; fi
  if [ "$code" = "0" ]; then verdict=0; fi
done
# Revert the premake fixes so the next checkout is clean.
git reset -q --hard > /dev/null 2>&1
case $verdict in
  0) echo "GOOD $here" | tee -a "$LOG"; exit 0 ;;
  1) echo "BAD  $here (foliage)" | tee -a "$LOG"; exit 1 ;;
  *) echo "SKIP $here (never reached the title)" | tee -a "$LOG"; exit 125 ;;
esac
