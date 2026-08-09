// Differential for the new SCALAR f32/f64 MUL_ADD / MUL_SUB lowering in the LLVM
// backend (llvm_assembler.cc, OPCODE_MUL_ADD/MUL_SUB scalar path).
//
// WHY: ~830 of 1,022 LLVM fallbacks on Blue Dragon are scalar FMA, which had no
// lowering at all - every guest function containing one dropped to the a64
// backend and lost register residency with it. Adding the lowering is only safe
// if it is BYTE-IDENTICAL to the a64 reference, because the failure mode is
// silently wrong float math, not a crash.
//
// REFERENCE = a64 EmitFmaWithPpcNan_F32 / _F64 (a64_sequences.cc:1686), which is
// what ships today:
//   if any source is NaN -> propagate the FIRST NaN in order s1,s2,s3, QUIETED
//                           (f32 |= 1<<22, f64 |= 1<<51)
//   else                 -> fused multiply-add (single rounding)
//   if the RESULT is NaN -> PPC default NaN, which is NEGATIVE
//                           (f32 0xFFC00000, f64 0xFFF8000000000000)
// ARM FNMSUB Sd,Sn,Sm,Sa = Sn*Sm - Sa, so MUL_SUB == fma(s1,s2,-s3).
//
// CANDIDATE = the branchless select chain the LLVM lowering emits.
//
// Build/run (no device):
//   aarch64-linux-gnu-g++ -x c++ -O2 -march=armv8.2-a -static \
//       -o sfma scalar_fma_ppc_nan_equiv.c && qemu-aarch64 ./sfma
#include <stdio.h>
#include <stdint.h>
#include <math.h>

typedef unsigned int u32;
typedef unsigned long long u64;

static inline u32 f2b(float f)  { u32 u; __builtin_memcpy(&u,&f,4); return u; }
static inline float b2f(u32 u)  { float f; __builtin_memcpy(&f,&u,4); return f; }
static inline u64 d2b(double d) { u64 u; __builtin_memcpy(&u,&d,8); return u; }
static inline double b2d(u64 u) { double d; __builtin_memcpy(&d,&u,8); return d; }

// ---------- REFERENCE: the a64 sequence, transcribed branch-for-branch --------
static u32 ref_f32(u32 a, u32 c, u32 d, int is_sub) {
  float s1 = b2f(a), s2 = b2f(c), s3 = b2f(d);
  if (isnan(s1)) return a | (1u << 22);          // first NaN wins, quieted
  if (isnan(s2)) return c | (1u << 22);
  if (isnan(s3)) return d | (1u << 22);
  float r = fmaf(s1, s2, is_sub ? -s3 : s3);     // single-rounded
  if (isnan(r)) return 0xFFC00000u;              // GENERATED NaN -> PPC default
  return f2b(r);
}
static u64 ref_f64(u64 a, u64 c, u64 d, int is_sub) {
  double s1 = b2d(a), s2 = b2d(c), s3 = b2d(d);
  if (isnan(s1)) return a | (1ull << 51);
  if (isnan(s2)) return c | (1ull << 51);
  if (isnan(s3)) return d | (1ull << 51);
  double r = fma(s1, s2, is_sub ? -s3 : s3);
  if (isnan(r)) return 0xFFF8000000000000ull;
  return d2b(r);
}

// ---------- CANDIDATE: the branchless select chain the LLVM lowering emits ----
static u32 cand_f32(u32 a, u32 c, u32 d, int is_sub) {
  float s1 = b2f(a), s2 = b2f(c), s3 = b2f(d);
  float addend = is_sub ? -s3 : s3;
  float res = fmaf(s1, s2, addend);
  u32 gen = isnan(res) ? 0xFFC00000u : f2b(res);
  u32 out = gen;
  out = isnan(s3) ? (d | (1u << 22)) : out;      // selects applied in REVERSE
  out = isnan(s2) ? (c | (1u << 22)) : out;      // so s1 wins over s2 over s3
  out = isnan(s1) ? (a | (1u << 22)) : out;
  return out;
}
static u64 cand_f64(u64 a, u64 c, u64 d, int is_sub) {
  double s1 = b2d(a), s2 = b2d(c), s3 = b2d(d);
  double addend = is_sub ? -s3 : s3;
  double res = fma(s1, s2, addend);
  u64 gen = isnan(res) ? 0xFFF8000000000000ull : d2b(res);
  u64 out = gen;
  out = isnan(s3) ? (d | (1ull << 51)) : out;
  out = isnan(s2) ? (c | (1ull << 51)) : out;
  out = isnan(s1) ? (a | (1ull << 51)) : out;
  return out;
}

int main(void) {
  const u32 QN = 0x7FC00001u, QN2 = 0x7FD00002u, SN = 0x7F800001u;
  const u32 INF = 0x7F800000u, NINF = 0xFF800000u, ZERO = 0u;
  const u32 ONE = 0x3F800000u, TWO = 0x40000000u, THREE = 0x40400000u;
  struct { u32 a, c, d; const char* name; } cases[] = {
      {ONE, TWO, THREE, "1*2+3 normal        "},
      {QN,  TWO, THREE, "s1 QNaN             "},
      {ONE, QN,  THREE, "s2 QNaN             "},
      {ONE, TWO, QN,    "s3 QNaN             "},
      {QN,  QN2, THREE, "s1+s2 NaN (s1 wins) "},
      {ONE, QN,  QN2,   "s2+s3 NaN (s2 wins) "},
      {SN,  TWO, THREE, "s1 SNaN -> quieted  "},
      {ONE, TWO, SN,    "s3 SNaN -> quieted  "},
      {INF, ZERO, THREE,"inf*0 GENERATED NaN "},
      {INF, ONE, NINF,  "inf-inf GENERATED   "},
      {NINF, ONE, INF,  "-inf+inf GENERATED  "},
      {ZERO, ZERO, ZERO,"0*0+0               "},
  };
  int bad = 0;
  for (int sub = 0; sub < 2; ++sub) {
    printf("=== %s (f32) ===\n", sub ? "MUL_SUB" : "MUL_ADD");
    for (unsigned k = 0; k < sizeof(cases)/sizeof(cases[0]); ++k) {
      u32 r = ref_f32(cases[k].a, cases[k].c, cases[k].d, sub);
      u32 g = cand_f32(cases[k].a, cases[k].c, cases[k].d, sub);
      int ok = (r == g);
      if (!ok) ++bad;
      printf("  %s ref=%08X cand=%08X %s\n", cases[k].name, r, g,
             ok ? "ok" : "MISMATCH");
    }
  }
  // f64: same shapes, promoted.
  printf("=== f64 spot checks ===\n");
  struct { u64 a, c, d; const char* name; } d64[] = {
      {0x3FF0000000000000ull, 0x4000000000000000ull, 0x4008000000000000ull, "1*2+3        "},
      {0x7FF8000000000001ull, 0x4000000000000000ull, 0x4008000000000000ull, "s1 QNaN      "},
      {0x7FF0000000000001ull, 0x4000000000000000ull, 0x4008000000000000ull, "s1 SNaN      "},
      {0x7FF0000000000000ull, 0x0000000000000000ull, 0x4008000000000000ull, "inf*0 -> gen "},
  };
  for (int sub = 0; sub < 2; ++sub) {
    for (unsigned k = 0; k < sizeof(d64)/sizeof(d64[0]); ++k) {
      u64 r = ref_f64(d64[k].a, d64[k].c, d64[k].d, sub);
      u64 g = cand_f64(d64[k].a, d64[k].c, d64[k].d, sub);
      int ok = (r == g);
      if (!ok) ++bad;
      printf("  %-13s %s ref=%016llx cand=%016llx %s\n", d64[k].name,
             sub ? "sub" : "add", (unsigned long long)r, (unsigned long long)g,
             ok ? "ok" : "MISMATCH");
    }
  }
  printf("\n%s\n", bad
      ? "FAIL - the scalar FMA lowering is NOT equivalent to the a64 reference"
      : "PASS - scalar FMA lowering matches the a64 reference on every case");
  return bad != 0;
}
