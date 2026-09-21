#!/usr/bin/env bash
# git bisect run script for the Banjo dirty-disc dialog on the PC build.
# Run inside ../xenia-thor-bisect:
#   git bisect start HEAD db92dc6327
#   git bisect run ../xenia-thor/tools/pc/bisect_banjo.sh
# Exit 0 = good (no dialog in 70 s), 1 = bad (dialog), 125 = cannot build (skip).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ISO="$ROOT/scratch/banjo/banjo.iso"
MSBUILD="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe"
LOG="$ROOT/scratch/banjo/bisect.log"
here="$(git rev-parse --short HEAD)"
echo "== $(date +%H:%M:%S) bisect step $here" | tee -a "$LOG"

# The three premake fixes older trees need (no-ops when already present).
python - <<'EOF'
import re
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
git submodule update --init --depth 1 > /dev/null 2>&1
# Third-party trees that some commits reference without a submodule mapping
# (pugixml came with the Edge kernel port): copy them from the main tree.
for tp in pugixml; do
  [ -d "third_party/$tp/src" ] || cp -r "$ROOT/third_party/$tp" third_party/ 2>/dev/null
done
H=$(git rev-parse HEAD); S=$(git rev-parse --short=9 HEAD)
mkdir -p build
printf '#ifndef GENERATED_VERSION_H_\n#define GENERATED_VERSION_H_\n#define XE_BUILD_BRANCH "master"\n#define XE_BUILD_COMMIT "%s"\n#define XE_BUILD_COMMIT_SHORT "%s"\n#define XE_BUILD_DATE "bisect"\n#endif\n' "$H" "$S" > build/version.h
[ -f tools/build/gen_version_h.py ] && python tools/build/gen_version_h.py > /dev/null 2>&1
./tools/build/bin/premake5.exe --file=premake5.lua --os=windows vs2022 > /dev/null 2>&1 || { echo "premake failed" | tee -a "$LOG"; exit 125; }
rm -f build/bin/Windows/Release/xenia.exe
MSYS_NO_PATHCONV=1 "$MSBUILD" build/xenia-app.vcxproj '/p:Configuration=Release Windows' /p:Platform=x64 /m:6 /v:q /nologo > "$ROOT/scratch/banjo/bisect-build-$here.log" 2>&1
if [ ! -f build/bin/Windows/Release/xenia.exe ]; then
  echo "build failed: $here" | tee -a "$LOG"; grep -m3 "error" "$ROOT/scratch/banjo/bisect-build-$here.log" | cut -c1-200 | tee -a "$LOG"; exit 125
fi
rm -rf storage && mkdir -p storage
timeout 70 build/bin/Windows/Release/xenia.exe --storage_root=storage --log_file=storage/xenia.log --mount_cache=true "$ISO" > storage/stdout.txt 2>&1
if grep -q "XamShowDirtyDiscErrorUI: user" storage/xenia.log; then
  echo "BAD  $here (dialog)" | tee -a "$LOG"; exit 1
fi
if ! grep -q "Title name" storage/xenia.log; then
  echo "SKIP $here (no title loaded)" | tee -a "$LOG"; exit 125
fi
echo "GOOD $here" | tee -a "$LOG"; exit 0
