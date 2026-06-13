#!/bin/bash
# Build Turnip (vulkan.ad07xx.so) for Android/Adreno 740 (AYN Thor) with the
# KGSL non-blocking fence-status patch applied.
#
# Root cause + patch rationale:
#   docs/research/20260612-turnip-kgsl-blocking-fence-status.md
# (vkGetFenceStatus on Turnip-over-KGSL blocks until the queried submission
# retires; this patch makes timeout-0 status polls a true non-blocking
# retired-timestamp query.)
#
# Run inside WSL Ubuntu (the xenia-thor dev box's WSL has the setup):
#   ~/android-ndk-r27c          (Linux NDK)
#   ~/mesa                      (shallow clone of mesa main)
#   meson + ninja + python3-mako/yaml + bison + flex + glslang installed
# (Setup bootstrap: see ~/turnip_setup.log from the 2026-06-13 session.)
#
# Usage: bash build_turnip_kgsl.sh [mesa_dir] [ndk_dir]
# Output: <mesa_dir>/build-android/install/lib/vulkan.ad07xx.so
#   -> push to the app's files/turnip/ dir (same path the in-app driver
#      manager uses; see memory turnip-driver-integration) and launch with
#      the usual gpu_vulkan_driver=turnip extras to A/B against the shipped
#      v26.0.0_R7 binary.

set -euo pipefail

MESA="${1:-$HOME/mesa}"
NDK="${2:-$HOME/android-ndk-r27c}"
SDKVER=33   # Thor = Android 13
PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$MESA"

# Apply the patch (idempotent: skip if already applied). Strip CRLF in case
# the patch is read from a Windows checkout mount.
if ! grep -q "IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID, &read" \
     src/freedreno/vulkan/tu_knl_kgsl.cc; then
  sed 's/\r$//' "$PATCH_DIR/kgsl-nonblocking-fence-status.patch" > /tmp/kgsl_fs.patch
  git apply /tmp/kgsl_fs.patch || patch -p1 --fuzz=3 < /tmp/kgsl_fs.patch
fi

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"

cat > build-crossfile-android <<EOF
[binaries]
ar = '$TOOLCHAIN/bin/llvm-ar'
c = ['ccache', '$TOOLCHAIN/bin/aarch64-linux-android$SDKVER-clang', '-Wno-deprecated-declarations', '-Wno-gnu-alignof-expression']
cpp = ['ccache', '$TOOLCHAIN/bin/aarch64-linux-android$SDKVER-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-static-libstdc++', '-Wno-error=c++11-narrowing', '-Wno-deprecated-declarations', '-Wno-gnu-alignof-expression']
c_ld = '$TOOLCHAIN/bin/ld.lld'
cpp_ld = '$TOOLCHAIN/bin/ld.lld'
strip = '$TOOLCHAIN/bin/llvm-strip'
pkg-config = ['env', 'PKG_CONFIG_LIBDIR=NDKDIR/pkgconfig', '/usr/bin/pkg-config']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOF

# ccache optional
command -v ccache >/dev/null || sed -i "s/'ccache', //g" build-crossfile-android

meson setup build-android \
  --cross-file build-crossfile-android \
  -Dbuildtype=release \
  -Dplatforms=android \
  -Dplatform-sdk-version=$SDKVER \
  -Dandroid-stub=true \
  -Dgallium-drivers= \
  -Dvulkan-drivers=freedreno \
  -Dfreedreno-kmds=kgsl \
  -Degl=disabled \
  -Dgles1=disabled \
  -Dgles2=disabled \
  -Dopengl=false \
  -Dglx=disabled \
  -Dshared-glapi=disabled \
  --reconfigure 2>/dev/null ||
meson setup build-android \
  --cross-file build-crossfile-android \
  -Dbuildtype=release \
  -Dplatforms=android \
  -Dplatform-sdk-version=$SDKVER \
  -Dandroid-stub=true \
  -Dgallium-drivers= \
  -Dvulkan-drivers=freedreno \
  -Dfreedreno-kmds=kgsl \
  -Degl=disabled \
  -Dgles1=disabled \
  -Dgles2=disabled \
  -Dopengl=false \
  -Dglx=disabled \
  -Dshared-glapi=disabled

ninja -C build-android

OUT="build-android/src/freedreno/vulkan/libvulkan_freedreno.so"
ls -la "$OUT"
mkdir -p build-android/install/lib
cp "$OUT" build-android/install/lib/vulkan.ad07xx.so
echo "DONE: $MESA/build-android/install/lib/vulkan.ad07xx.so"
