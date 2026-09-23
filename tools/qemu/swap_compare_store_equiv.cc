// Differential check for xe::gpu::SwapCompareStore32 (NEON on ARM64) against
// SwapCompareStore32Scalar: the same stored words and the same changed-mask for
// every count 0..64, random data, all-equal, all-different, single changes and
// unaligned sources. It includes the real header, so it tests the shipped code.
//
// Build and run (no device needed):
//   NDK=<sdk>/ndk/25.0.8775105/toolchains/llvm/prebuilt/<host>/bin
//   $NDK/clang++ --target=aarch64-linux-android29 -O2 -static -I src \
//       -o swap_compare_store_equiv tools/qemu/swap_compare_store_equiv.cc
//   qemu-aarch64 ./swap_compare_store_equiv
// Expected output: "ALL PASS".

#include <cstdio>
#include <cstring>

#include "xenia/gpu/swap_compare_store.h"

static uint32_t rng_state = 0x9E3779B9u;
static uint32_t rnd() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

int main() {
  using xe::gpu::SwapCompareStore32;
  using xe::gpu::SwapCompareStore32Scalar;
  uint32_t failures = 0;
  uint64_t cases = 0;
  // One extra word in front so src + 1 is a 4-byte but not 16-byte aligned
  // pointer, like guest packet data.
  alignas(16) uint32_t src_storage[66];
  alignas(16) uint32_t dst_a[64];
  alignas(16) uint32_t dst_b[64];
  for (uint32_t round = 0; round < 20000; ++round) {
    uint32_t count = round % 65;
    uint32_t offset = (round / 65) & 1;
    uint32_t* src = src_storage + offset;
    for (uint32_t i = 0; i < 66; ++i) {
      src_storage[i] = rnd();
    }
    // The destination starts as the swapped source (all equal), then some
    // words are changed according to the pattern of this round.
    for (uint32_t i = 0; i < 64; ++i) {
      uint32_t v = i < count ? xe::gpu::SwapCompareStoreByteSwap32(src[i])
                             : rnd();
      dst_a[i] = v;
    }
    switch ((round / 130) % 4) {
      case 0:  // all equal
        break;
      case 1:  // all different
        for (uint32_t i = 0; i < 64; ++i) dst_a[i] ^= 0x80000001u;
        break;
      case 2:  // one change
        if (count) dst_a[rnd() % count] ^= 1u << (rnd() & 31);
        break;
      default:  // random changes
        for (uint32_t i = 0; i < 64; ++i)
          if (rnd() & 1) dst_a[i] = rnd();
        break;
    }
    std::memcpy(dst_b, dst_a, sizeof(dst_a));
    uint64_t mask_neon = SwapCompareStore32(dst_a, src, count);
    uint64_t mask_ref = SwapCompareStore32Scalar(dst_b, src, count);
    ++cases;
    if (mask_neon != mask_ref || std::memcmp(dst_a, dst_b, sizeof(dst_a))) {
      if (++failures <= 10) {
        std::printf("FAIL round %u count %u offset %u: mask %016llX ref %016llX\n",
                    round, count, offset, (unsigned long long)mask_neon,
                    (unsigned long long)mask_ref);
      }
    }
  }
  std::printf("%llu cases, %u failures\n", (unsigned long long)cases, failures);
  std::printf(failures ? "FAIL\n" : "ALL PASS\n");
  return failures ? 1 : 0;
}
