#!/bin/bash
# A native (x86_64 Linux, WSL) build of Turnip and the freedreno drm-shim, for
# compiling shaders with the Thor's Adreno compiler on the PC
# (tools/turnip/shader_lab.py): LD_PRELOAD the shim with FD_GPU_ID=740 and
# Turnip runs its full ir3 pipeline against a fake Adreno 740.
#
#   wsl -d Ubuntu -- bash tools/turnip/build_host_shim.sh [MESA_REF]
#
# MESA_REF defaults to the ref of the Thor's driver (tools/turnip/build.py).
# The worktree is /root/mesa-host, from the Android build's clone
# (/root/turnip-build/mesa, which holds the ref).
set -euo pipefail
REF="${1:-885dd3a17a}"
cd /root/turnip-build/mesa
if [ ! -d /root/mesa-host ]; then
  git worktree add --detach /root/mesa-host "$REF"
fi
cd /root/mesa-host
git checkout -q --detach "$REF"
if [ ! -d build ]; then
  meson setup build -Dbuildtype=release -Dplatforms= \
    -Dvulkan-drivers=freedreno -Dgallium-drivers= -Dfreedreno-kmds=msm \
    -Dtools=drm-shim -Dglx=disabled -Degl=disabled -Dgles1=disabled \
    -Dgles2=disabled -Dopengl=false -Dllvm=disabled -Dxmlconfig=disabled \
    -Dvalgrind=disabled -Dlibunwind=disabled
fi
ninja -C build
ls -la build/src/freedreno/vulkan/libvulkan_freedreno.so \
  build/src/freedreno/drm-shim/libfreedreno_noop_drm_shim.so
