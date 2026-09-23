#!/usr/bin/env bash
# Build Mesa Turnip (the Adreno Vulkan driver) for Android arm64 in WSL, and
# package it as an adrenotools zip (meta.json + libvulkan_freedreno.so) that
# the app's driver manager imports (GpuDriverManager, "Import driver").
#
#   wsl -d Ubuntu -- bash tools/turnip/build_turnip.sh [MESA_REF] [PATCH_DIR]
#
# MESA_REF: a branch, tag or commit of https://gitlab.freedesktop.org/mesa/mesa
# (default main). PATCH_DIR: *.patch files applied with git am/apply in name
# order - the place for xenia-specific driver changes. The bundled driver is
# Mesa main e40d93a (2026-08-07, KGSL). Output:
#   ~/turnip-build/out/turnip-<ref>-<short sha>.zip   (and the .so beside it)
# Everything lives under ~/turnip-build (NDK, source, build); nothing is
# installed system-wide.
set -euo pipefail
REF="${1:-main}"
PATCH_DIR="${2:-}"
# CPU tuning for the Thor (Snapdragon 8 Gen 2): ARMv8.2 features every core
# has (dotprod, fp16, rcpc, crypto; /proc/cpuinfo) and scheduling for the X3,
# where the emulator's command thread - and so Turnip's command recording -
# runs (thor_gpu_thread_affinity_cpu=7). No SVE: the kernel does not expose
# it, so -mcpu=cortex-x3 would emit instructions that fault. Empty = the NDK
# default (armv8-a). The zip name carries a -tuned suffix when set.
CPU_FLAGS="${TURNIP_CPU_FLAGS--march=armv8.2-a+dotprod+fp16+rcpc+crypto -mtune=cortex-x3}"
ROOT="$HOME/turnip-build"
NDK_VER=r27c
NDK="$ROOT/android-ndk-$NDK_VER"
API=33
mkdir -p "$ROOT/out"
cd "$ROOT"

if [ ! -d "$NDK" ]; then
  echo "== NDK $NDK_VER"
  curl -sL -o ndk.zip "https://dl.google.com/android/repository/android-ndk-$NDK_VER-linux.zip"
  unzip -q ndk.zip && rm ndk.zip
fi

if [ ! -d mesa ]; then
  echo "== mesa clone"
  git clone -q --filter=blob:none https://gitlab.freedesktop.org/mesa/mesa.git mesa
fi
cd mesa
# A commit that is already local (a SHA of an earlier build) checks out
# directly; a branch or tag is fetched. Fetching a short SHA fails and the
# old fallback took the latest main instead (2026-09-22) - never again.
if git cat-file -e "$REF^{commit}" 2>/dev/null && ! git show-ref -q --verify "refs/remotes/origin/$REF"; then
  git checkout -q --detach "$REF"
else
  git fetch -q origin "$REF"
  git checkout -q --detach FETCH_HEAD
fi
git reset -q --hard
git clean -qfdx
SHA=$(git rev-parse --short=10 HEAD)
echo "== mesa $REF at $SHA ($(git log -1 --format=%cs))"
if [ -n "$PATCH_DIR" ]; then
  for p in "$PATCH_DIR"/*.patch; do
    [ -e "$p" ] || continue
    echo "   patch $(basename "$p")"
    git apply "$p"
  done
fi

# Python modules the build needs, user level (no system install).
python3 -c 'import mako, yaml, packaging' 2>/dev/null || \
  python3 -m pip install --user --break-system-packages -q mako pyyaml packaging

TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
CPU_ARGS=""
SUFFIX=""
for f in $CPU_FLAGS; do CPU_ARGS="$CPU_ARGS, '$f'"; done
[ -n "$CPU_FLAGS" ] && SUFFIX="-tuned"
cat > "$ROOT/android-aarch64.txt" <<EOF
[binaries]
ar = '$TC/llvm-ar'
c = ['ccache', '$TC/aarch64-linux-android$API-clang'$CPU_ARGS]
cpp = ['ccache', '$TC/aarch64-linux-android$API-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-static-libstdc++', '-Wno-c++11-narrowing'$CPU_ARGS]
c_ld = 'lld'
cpp_ld = 'lld'
strip = '$TC/llvm-strip'
pkg-config = ['env', 'PKG_CONFIG_LIBDIR=$NDK/pkgconfig', '/usr/bin/pkg-config']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOF
command -v ccache >/dev/null || sed -i "s/'ccache', //" "$ROOT/android-aarch64.txt"

echo "== configure"
rm -rf build
meson setup build --cross-file "$ROOT/android-aarch64.txt" \
  -Dbuildtype=release -Dplatforms=android -Dplatform-sdk-version=$API \
  -Dandroid-stub=true -Dgallium-drivers= -Dvulkan-drivers=freedreno \
  -Dvulkan-beta=true -Dfreedreno-kmds=kgsl -Dstrip=true \
  -Degl=disabled -Dglx=disabled -Dgbm=disabled -Dllvm=disabled \
  > "$ROOT/out/configure-$SHA.log" 2>&1 || { tail -30 "$ROOT/out/configure-$SHA.log"; exit 1; }

echo "== build"
ninja -C build src/freedreno/vulkan/libvulkan_freedreno.so > "$ROOT/out/build-$SHA.log" 2>&1 || {
  grep -m20 -E 'error|Error' "$ROOT/out/build-$SHA.log"; exit 1; }

SO=build/src/freedreno/vulkan/libvulkan_freedreno.so
PKG="$ROOT/out/pkg-$SHA"
rm -rf "$PKG" && mkdir -p "$PKG"
cp "$SO" "$PKG/libvulkan_freedreno.so"
cat > "$PKG/meta.json" <<EOF
{
  "schemaVersion": 1,
  "name": "Mesa Turnip $REF $SHA$SUFFIX (xenia-thor)",
  "description": "Turnip from Mesa $REF ($SHA, $(git log -1 --format=%cs)), KGSL, CPU flags: ${CPU_FLAGS:-NDK default}; tools/turnip/build_turnip.sh.",
  "author": "xenia-thor",
  "packageVersion": "1",
  "vendor": "Mesa",
  "driverVersion": "Vulkan 1.4",
  "minApi": 28,
  "libraryName": "libvulkan_freedreno.so"
}
EOF
ZIP="$ROOT/out/turnip-${REF//\//_}-$SHA$SUFFIX.zip"
(cd "$PKG" && rm -f "$ZIP" && zip -q "$ZIP" meta.json libvulkan_freedreno.so)
ls -la "$SO" "$ZIP"
echo "done: $ZIP"
