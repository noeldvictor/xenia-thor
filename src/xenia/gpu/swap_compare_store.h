/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SWAP_COMPARE_STORE_H_
#define XENIA_GPU_SWAP_COMPARE_STORE_H_

#include <cstdint>
#include <cstring>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif

namespace xe {
namespace gpu {

// Register writes that games repeat every draw (fetch constants) must not
// dirty anything when the value is the same. These store `count` (at most 64)
// big-endian 32-bit words from `src` byte-swapped into `dst` and return a mask
// with bit i set where dst[i] changed. Header-only so the qemu harness
// (tools/qemu/swap_compare_store_equiv.cc) tests exactly this code.

inline uint32_t SwapCompareStoreByteSwap32(uint32_t v) {
  return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) |
         (v << 24);
}

inline uint64_t SwapCompareStore32Scalar(uint32_t* dst, const uint32_t* src,
                                         uint32_t count) {
  uint64_t changed = 0;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t value;
    std::memcpy(&value, src + i, sizeof(value));
    value = SwapCompareStoreByteSwap32(value);
    if (dst[i] != value) {
      changed |= uint64_t(1) << i;
    }
    dst[i] = value;
  }
  return changed;
}

inline uint64_t SwapCompareStore32(uint32_t* dst, const uint32_t* src,
                                   uint32_t count) {
#if defined(__aarch64__) || defined(_M_ARM64)
  // NEON (2026-09-23): the scalar per-slot loop was 6 dwords per fetch slot,
  // below the auto-vectorizer's minimum, on every draw that re-sends fetch
  // constants. Four words per step: rev32 swap, compare with the register
  // file, store, and narrow the four lane results to four mask bits.
  uint64_t changed = 0;
  uint32_t i = 0;
  for (; i + 4 <= count; i += 4) {
    uint32x4_t value = vreinterpretq_u32_u8(
        vrev32q_u8(vld1q_u8(reinterpret_cast<const uint8_t*>(src + i))));
    uint32x4_t differs = vmvnq_u32(vceqq_u32(value, vld1q_u32(dst + i)));
    vst1q_u32(dst + i, value);
    // Each 16-bit lane is 0 or 0xFFFF after the narrowing move; take one bit
    // from each lane.
    uint64_t lanes =
        vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(differs)), 0);
    uint64_t bits = (lanes & 1) | ((lanes >> 15) & 2) | ((lanes >> 30) & 4) |
                    ((lanes >> 45) & 8);
    changed |= bits << i;
  }
  if (i < count) {
    changed |= SwapCompareStore32Scalar(dst + i, src + i, count - i) << i;
  }
  return changed;
#else
  return SwapCompareStore32Scalar(dst, src, count);
#endif
}

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SWAP_COMPARE_STORE_H_
