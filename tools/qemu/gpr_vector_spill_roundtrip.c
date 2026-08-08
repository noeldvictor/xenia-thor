// qemu-a64 correctness check for a64_spill_gprs_to_vector.
//
// That lever redirects integer spill slots from the stack into vector
// registers, per Cortex-A710 SWOG 4.3. The emitted pair is:
//   spill  FMOV Dn, Xs        (STORE_LOCAL_I64)
//   fill   UMOV Xd, Vn.D[0]   (LOAD_LOCAL_I64)
// If that pair does not round-trip a full 64-bit value BIT-EXACTLY, the lever
// silently corrupts spilled guest registers - a far worse outcome than the
// cache-latency win it is chasing. The 32-bit forms matter too, because the
// allocator spills I32 values through the same slots.
//
// Also checks the thing that would be easy to get wrong: writing lane 0 via
// FMOV must not disturb a value already held in lane 1, since a Q register
// holds TWO 64-bit spill slots and the whole point is to pack them.
//
// Build/run (no device):
//   aarch64-linux-gnu-g++ -x c++ -O2 -march=armv8.2-a -static \
//       -o gvs gpr_vector_spill_roundtrip.c && qemu-aarch64 ./gvs
#include <stdio.h>
#include <stdint.h>

static uint64_t roundtrip64(uint64_t v) {
  uint64_t out;
  __asm__ volatile(
      "fmov d31, %1\n"          // spill:  GPR -> vector lane 0
      "umov %0, v31.d[0]\n"     // fill:   vector lane 0 -> GPR
      : "=r"(out) : "r"(v) : "v31");
  return out;
}

static uint32_t roundtrip32(uint32_t v) {
  uint32_t out;
  __asm__ volatile(
      "fmov s31, %w1\n"
      "umov %w0, v31.s[0]\n"
      : "=r"(out) : "r"(v) : "v31");
  return out;
}

// Pack two 64-bit values into one Q register and read both back. Writing lane 0
// with FMOV must leave lane 1 intact, or two spill slots per register is unsafe.
static void pack_two(uint64_t a, uint64_t b, uint64_t *oa, uint64_t *ob) {
  __asm__ volatile(
      "fmov d30, %2\n"          // lane 0 = a
      "ins  v30.d[1], %3\n"     // lane 1 = b
      "umov %0, v30.d[0]\n"
      "umov %1, v30.d[1]\n"
      : "=&r"(*oa), "=&r"(*ob) : "r"(a), "r"(b) : "v30");
}

// Does writing lane 0 clobber lane 1?
static uint64_t lane1_survives(uint64_t lane1, uint64_t new_lane0) {
  uint64_t out;
  __asm__ volatile(
      "movi v29.2d, #0\n"
      "ins  v29.d[1], %1\n"     // seed lane 1
      "fmov d29, %2\n"          // overwrite lane 0 the way the spill path does
      "umov %0, v29.d[1]\n"     // lane 1 must be unchanged
      : "=r"(out) : "r"(lane1), "r"(new_lane0) : "v29");
  return out;
}

int main(void) {
  static const uint64_t vals64[] = {
      0ull, 1ull, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull,
      0x7FFFFFFFFFFFFFFFull, 0xDEADBEEFCAFEBABEull, 0x0000000100000000ull,
      0xFFFFFFFF00000000ull, 0x00000000FFFFFFFFull,
      0x7FF0000000000000ull,  // an FP NaN/inf bit pattern - must NOT be canonicalised
      0xFFF8000000000001ull,  // signalling-NaN pattern in the double domain
  };
  int bad = 0;
  puts("=== 64-bit spill/fill round-trip (FMOV Dn,Xs -> UMOV Xd,Vn.D[0]) ===");
  for (unsigned i = 0; i < sizeof(vals64)/sizeof(vals64[0]); ++i) {
    uint64_t got = roundtrip64(vals64[i]);
    int ok = got == vals64[i];
    if (!ok) ++bad;
    printf("  %016llx -> %016llx  %s\n",
           (unsigned long long)vals64[i], (unsigned long long)got,
           ok ? "ok" : "CORRUPTED");
  }
  static const uint32_t vals32[] = {0u, 1u, 0xFFFFFFFFu, 0x80000000u,
                                    0x7F800001u, 0xDEADBEEFu};
  puts("=== 32-bit round-trip ===");
  for (unsigned i = 0; i < sizeof(vals32)/sizeof(vals32[0]); ++i) {
    uint32_t got = roundtrip32(vals32[i]);
    int ok = got == vals32[i];
    if (!ok) ++bad;
    printf("  %08x -> %08x  %s\n", vals32[i], got, ok ? "ok" : "CORRUPTED");
  }
  puts("=== two slots per Q register ===");
  uint64_t a, b;
  pack_two(0x1111222233334444ull, 0xAAAABBBBCCCCDDDDull, &a, &b);
  int ok2 = a == 0x1111222233334444ull && b == 0xAAAABBBBCCCCDDDDull;
  if (!ok2) ++bad;
  printf("  lane0=%016llx lane1=%016llx  %s\n",
         (unsigned long long)a, (unsigned long long)b, ok2 ? "ok" : "CORRUPTED");
  puts("=== does FMOV to lane 0 preserve lane 1? ===");
  uint64_t l1 = lane1_survives(0xFEEDFACEDEADC0DEull, 0x0123456789ABCDEFull);
  // NOT a failure: AArch64 scalar FP writes ZERO the upper lanes, so FMOV Dn,Xs
  // clobbering lane 1 is architectural. It is recorded because it CAPS the
  // scheme at ONE 64-bit slot per vector register - using lane 1 as well would
  // need INS (latency 5) and lane 0 could then never be refilled with FMOV.
  int ok3 = l1 == 0xFEEDFACEDEADC0DEull;
  printf("  lane1 after writing lane0 = %016llx  %s\n",
         (unsigned long long)l1,
         ok3 ? "preserved - two slots per register would be safe"
             : "ZEROED (architectural) - ONE slot per register only");
  printf("\n%s\n", bad ? "FAIL" : "PASS - GPR<->vector spill path is bit-exact");
  return bad != 0;
}
