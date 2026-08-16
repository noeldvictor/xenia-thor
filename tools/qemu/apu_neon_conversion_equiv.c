// Differential check for the ARM64 NEON audio conversion paths.
//
// src/xenia/apu/conversion.h and src/xenia/apu/xma_context.cc each carry an
// SSE2 path for x64 and a NEON path for ARM64. The two must give identical
// samples, or an ARM64 build sounds different from an x64 build.
//
// This harness re-implements the SSE2 semantics as scalar reference code and
// compares the NEON code against it, over random inputs plus NaN, inf,
// denormal and out-of-range cases.
//
// Build and run (no device needed):
//   NDK=$HOME/Android/Sdk/ndk/25.0.8775105/toolchains/llvm/prebuilt/<host>/bin
//   $NDK/clang --target=aarch64-linux-android29 -O2 -static \
//       -o apu_neon_conversion_equiv tools/qemu/apu_neon_conversion_equiv.c
//   qemu-aarch64 ./apu_neon_conversion_equiv
//
// Expected output: "ALL PASS".

#include <arm_neon.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SAMPLES_PER_FRAME 512

static uint32_t rng_state = 0x12345678u;
static uint32_t rnd(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static float bits_to_float(uint32_t b) {
  float f;
  memcpy(&f, &b, sizeof(f));
  return f;
}
static uint32_t float_to_bits(float f) {
  uint32_t b;
  memcpy(&b, &f, sizeof(b));
  return b;
}
static float byte_swap_f(float f) {
  return bits_to_float(__builtin_bswap32(float_to_bits(f)));
}

// ---------------------------------------------------------------------------
// Reference: the SSE2 downmix, written out scalar.
// ---------------------------------------------------------------------------
static void ref_downmix(float* output, const float* input, size_t n) {
  for (size_t s = 0; s < n; s++) {
    float fl = byte_swap_f(input[0 * n + s]);
    float fr = byte_swap_f(input[1 * n + s]);
    float fc = byte_swap_f(input[2 * n + s]);
    float bl = byte_swap_f(input[4 * n + s]);
    float br = byte_swap_f(input[5 * n + s]);
    float center_halved = fc * 0.5f;
    // Association must match the SSE2 path: (fl + bl) + center_halved.
    output[s * 2] = (fl + bl + center_halved) * (1.0f / 2.5f);
    output[s * 2 + 1] = (fr + br + center_halved) * (1.0f / 2.5f);
  }
}

// The NEON downmix, copied from src/xenia/apu/conversion.h.
static void neon_downmix(float* output, const float* input, size_t n) {
  const float32x4_t half = vdupq_n_f32(0.5f);
  const float32x4_t two_fifths = vdupq_n_f32(1.0f / 2.5f);
  const uint8_t* in_bytes = (const uint8_t*)input;
  for (size_t sample = 0; sample < n; sample += 4) {
#define LOAD_SWAP(ch)                                                    \
  vreinterpretq_f32_u8(vrev32q_u8(                                       \
      vld1q_u8(&in_bytes[((ch) * n + sample) * sizeof(float)])))
    const float32x4_t fl = LOAD_SWAP(0);
    const float32x4_t fr = LOAD_SWAP(1);
    const float32x4_t fc = LOAD_SWAP(2);
    const float32x4_t bl = LOAD_SWAP(4);
    const float32x4_t br = LOAD_SWAP(5);
#undef LOAD_SWAP
    const float32x4_t center_halved = vmulq_f32(fc, half);
    float32x4_t left = vaddq_f32(vaddq_f32(fl, bl), center_halved);
    float32x4_t right = vaddq_f32(vaddq_f32(fr, br), center_halved);
    left = vmulq_f32(left, two_fifths);
    right = vmulq_f32(right, two_fifths);
    vst1q_f32(&output[sample * 2], vzip1q_f32(left, right));
    vst1q_f32(&output[(sample + 2) * 2], vzip2q_f32(left, right));
  }
}

// ---------------------------------------------------------------------------
// Reference: the SSE2 ConvertFrame, written out scalar.
// ---------------------------------------------------------------------------
static int32_t ref_cvtps_epi32(float v) {
  // CVTPS2DQ rounds to nearest even and gives the integer indefinite value
  // (INT32_MIN) for NaN, the infinities and anything out of range.
  if (!(fabsf(v) < 2147483648.0f)) return INT32_MIN;
  float r = nearbyintf(v);
  return (int32_t)r;
}
static int16_t ref_packs_epi32(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}
static void ref_convert_frame(const float* ch0, const float* ch1,
                              int is_two_channel, int16_t* out) {
  const float scale = (float)((1 << 15) - 1);
  if (is_two_channel) {
    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
      int16_t l = ref_packs_epi32(ref_cvtps_epi32(ch0[i] * scale));
      int16_t r = ref_packs_epi32(ref_cvtps_epi32(ch1[i] * scale));
      out[i * 2] = (int16_t)__builtin_bswap16((uint16_t)l);
      out[i * 2 + 1] = (int16_t)__builtin_bswap16((uint16_t)r);
    }
  } else {
    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
      int16_t s = ref_packs_epi32(ref_cvtps_epi32(ch0[i] * scale));
      out[i] = (int16_t)__builtin_bswap16((uint16_t)s);
    }
  }
}

// The NEON ConvertFrame, copied from src/xenia/apu/xma_context.cc.
static inline int32x4_t ConvertToInt32X64(float32x4_t v) {
  const uint32x4_t representable = vcaltq_f32(v, vdupq_n_f32(2147483648.0f));
  return vbslq_s32(representable, vcvtnq_s32_f32(v), vdupq_n_s32(INT32_MIN));
}
static void neon_convert_frame(const float* ch0, const float* ch1,
                               int is_two_channel, int16_t* out) {
  const float scale = (float)((1 << 15) - 1);
  const float32x4_t scale_v = vdupq_n_f32(scale);
  if (is_two_channel) {
    for (int i = 0; i < SAMPLES_PER_FRAME; i += 4) {
      const float32x4_t in_l = vld1q_f32(&ch0[i]);
      const float32x4_t in_r = vld1q_f32(&ch1[i]);
      const int32x4_t l32 = ConvertToInt32X64(vmulq_f32(in_l, scale_v));
      const int32x4_t r32 = ConvertToInt32X64(vmulq_f32(in_r, scale_v));
      const int16x4_t l16 = vqmovn_s32(l32);
      const int16x4_t r16 = vqmovn_s32(r32);
      const int16x4x2_t zipped = vzip_s16(l16, r16);
      const int16x8_t interleaved = vcombine_s16(zipped.val[0], zipped.val[1]);
      const uint8x16_t swapped = vrev16q_u8(vreinterpretq_u8_s16(interleaved));
      vst1q_u8((uint8_t*)&out[i * 2], swapped);
    }
  } else {
    for (int i = 0; i < SAMPLES_PER_FRAME; i += 8) {
      const float32x4_t in0 = vld1q_f32(&ch0[i]);
      const float32x4_t in1 = vld1q_f32(&ch0[i + 4]);
      const int32x4_t s0 = ConvertToInt32X64(vmulq_f32(in0, scale_v));
      const int32x4_t s1 = ConvertToInt32X64(vmulq_f32(in1, scale_v));
      const int16x8_t packed = vcombine_s16(vqmovn_s32(s0), vqmovn_s32(s1));
      const uint8x16_t swapped = vrev16q_u8(vreinterpretq_u8_s16(packed));
      vst1q_u8((uint8_t*)&out[i], swapped);
    }
  }
}

// ---------------------------------------------------------------------------

static const uint32_t kInterestingBits[] = {
    0x00000000u, 0x80000000u,  // +/- 0
    0x00000001u, 0x80000001u,  // smallest denormals
    0x007FFFFFu, 0x807FFFFFu,  // largest denormals
    0x00800000u, 0x80800000u,  // smallest normals
    0x3F800000u, 0xBF800000u,  // +/- 1
    0x7F7FFFFFu, 0xFF7FFFFFu,  // +/- FLT_MAX
    0x7F800000u, 0xFF800000u,  // +/- inf
    0x7FC00000u, 0xFFC00000u,  // QNaN
    0x7F800001u, 0xFF800001u,  // SNaN
    0x461C4000u, 0xC61C4000u,  // +/- 10000, past int16 after scaling
    0x4F000000u, 0xCF000000u,  // 2^31, the conversion boundary
};
#define NUM_INTERESTING \
  (int)(sizeof(kInterestingBits) / sizeof(kInterestingBits[0]))

static uint32_t random_sample_bits(void) {
  uint32_t r = rnd();
  if ((r & 7) == 0) return kInterestingBits[rnd() % NUM_INTERESTING];
  if (r & 1) {
    // A plausible audio sample in [-2, 2].
    float f = ((float)(int32_t)rnd() / 2147483648.0f) * 2.0f;
    return float_to_bits(f);
  }
  return r;  // fully random bit pattern
}

int main(void) {
  int failures = 0;
  const int kRounds = 4000;

  // --- downmix ---
  static float in6[6 * 64];
  static float out_ref[64 * 2], out_neon[64 * 2];
  for (int round = 0; round < kRounds; round++) {
    const size_t n = 64;
    for (size_t i = 0; i < 6 * n; i++) {
      in6[i] = bits_to_float(random_sample_bits());
    }
    memset(out_ref, 0, sizeof(out_ref));
    memset(out_neon, 0, sizeof(out_neon));
    ref_downmix(out_ref, in6, n);
    neon_downmix(out_neon, in6, n);
    for (size_t i = 0; i < n * 2; i++) {
      if (float_to_bits(out_ref[i]) != float_to_bits(out_neon[i])) {
        if (failures < 10) {
          printf("DOWNMIX MISMATCH round %d lane %zu: ref %08X neon %08X\n",
                 round, i, float_to_bits(out_ref[i]),
                 float_to_bits(out_neon[i]));
        }
        failures++;
      }
    }
  }

  // --- ConvertFrame ---
  static float ch0[SAMPLES_PER_FRAME], ch1[SAMPLES_PER_FRAME];
  static int16_t cf_ref[SAMPLES_PER_FRAME * 2], cf_neon[SAMPLES_PER_FRAME * 2];
  for (int round = 0; round < kRounds; round++) {
    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
      ch0[i] = bits_to_float(random_sample_bits());
      ch1[i] = bits_to_float(random_sample_bits());
    }
    for (int two = 0; two <= 1; two++) {
      memset(cf_ref, 0, sizeof(cf_ref));
      memset(cf_neon, 0, sizeof(cf_neon));
      ref_convert_frame(ch0, ch1, two, cf_ref);
      neon_convert_frame(ch0, ch1, two, cf_neon);
      const int count = two ? SAMPLES_PER_FRAME * 2 : SAMPLES_PER_FRAME;
      for (int i = 0; i < count; i++) {
        if (cf_ref[i] != cf_neon[i]) {
          if (failures < 20) {
            printf("CONVERT MISMATCH round %d two=%d i=%d: ref %04X neon %04X "
                   "(in %08X)\n",
                   round, two, i, (uint16_t)cf_ref[i], (uint16_t)cf_neon[i],
                   float_to_bits(two ? ch0[i / 2] : ch0[i]));
          }
          failures++;
        }
      }
    }
  }

  if (failures) {
    printf("FAILED: %d mismatches\n", failures);
    return 1;
  }
  printf("ALL PASS (%d rounds downmix + %d rounds ConvertFrame x2)\n", kRounds,
         kRounds);
  return 0;
}
