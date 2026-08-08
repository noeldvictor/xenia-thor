#!/usr/bin/env bash
# Reclaim build disk. Everything removed here is REGENERABLE.
#
# Why this exists: a full Android build leaves ~26 GB behind (build/ objects +
# app/build gradle intermediates). On 2026-08-08 that filled the disk mid-link,
# and the failure does NOT say "disk full" - the linker dies with
#   clang++: error: linker command failed due to signal
# which reads like a toolchain bug. If you see that, check free space first.
#
# NOT touched, deliberately:
#   scratch/    - gitignored, holds ISOs and captures that exist nowhere else
#   reference/  - XenDroid / xenia-edge clones the port workflow depends on
set -eu
cd "$(dirname "$0")/.."
before=$(df -k . | awk 'NR==2{print $4}')
rm -rf build
rm -rf android/android_studio_project/app/build
after=$(df -k . | awk 'NR==2{print $4}')
echo "freed ~$(( (after-before)/1024 )) MB"
echo
# build/ also held two generated things that are NOT in git. Restore both here
# so a clean can never leave the tree unbuildable.
echo "regenerating build/version.h ..."
python3 tools/build/gen_version_h.py
echo "regenerating premake makefiles (--os=android is REQUIRED) ..."
./tools/build/bin/premake5.exe --file=premake5.lua --os=android androidndk >/dev/null
echo "done - tree is buildable again."
