// qemu-a64 equivalence check for a64_vmx_nan_fixup_branchless.
//
// FixupVmxNan_V128 exists to impose PPC's NaN rules on an ARM FP result. The
// branchless rewrite must be OBSERVATIONALLY IDENTICAL to the scalar lane loop
// it replaces, or it is a correctness regression dressed as an optimisation.
//
// PPC rule (AltiVec PEM 3.2.5.1 "NaN Precedence"): result is vA's NaN if vA is
// a NaN, else vB's; a selected SNaN is quieted. If NEITHER source is NaN but
// the operation produced one (inf-inf, 0*inf), the result is the PPC default
// NaN 0xFFC00000 - ARM produces 0x7FC00000 there, which is the ONLY case the
// fixup actually has to correct.
//
// Build/run (no device):
//   aarch64-linux-gnu-g++ -x c++ -O2 -march=armv8.2-a+crypto+sha3 -static \
//       -o vnbe vmx_nan_branchless_equiv.c && qemu-aarch64 ./vnbe
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef unsigned int u32;

#define QN1  0x7FC00001u
#define QN2  0x7FD00002u
#define SN1  0x7F800001u
#define NUM3 0x40400000u
#define NUM2 0x40000000u
#define INF  0x7F800000u
#define NINF 0xFF800000u
#define PPC_DEFAULT_NAN 0xFFC00000u

static void set_vmx_fpcr(void) {           // DEFAULT_VMX_FPCR: FZ only, DN clear
  unsigned long v = 1UL << 24;
  __asm__ volatile("msr fpcr, %0" :: "r"(v));
}

// The op under fixup: v2 = fadd(s1, s2), with s1 in v4 and s2 in v5.
static void fp_op(const u32 *a, const u32 *b, u32 *res) {
  __asm__ volatile(
      "ld1 {v4.4s}, [%0]\n" "ld1 {v5.4s}, [%1]\n"
      "fadd v2.4s, v4.4s, v5.4s\n"
      "st1 {v2.4s}, [%2]\n"
      :: "r"(a), "r"(b), "r"(res) : "v2","v4","v5","memory");
}

// Reference: what the SCALAR lane loop computes, expressed in C.
// per lane: result NaN? -> s1 NaN ? quiet(s1) : s2 NaN ? quiet(s2) : DEFAULT
static u32 scalar_ref(u32 s1, u32 s2, u32 res) {
  const u32 exp = 0x7F800000u, man = 0x007FFFFFu;
  int res_nan = ((res & exp) == exp) && (res & man);
  if (!res_nan) return res;
  int s1_nan = ((s1 & exp) == exp) && (s1 & man);
  if (s1_nan) return s1 | 0x00400000u;
  int s2_nan = ((s2 & exp) == exp) && (s2 & man);
  if (s2_nan) return s2 | 0x00400000u;
  return PPC_DEFAULT_NAN;
}

// The BRANCHLESS form, instruction-for-instruction as emitted by
// FixupVmxNan_V128 under a64_vmx_nan_fixup_branchless.
static void branchless(const u32 *a, const u32 *b, u32 *out) {
  static const u32 defnan[4] = {PPC_DEFAULT_NAN, PPC_DEFAULT_NAN,
                                PPC_DEFAULT_NAN, PPC_DEFAULT_NAN};
  __asm__ volatile(
      "ld1 {v4.4s}, [%0]\n"                 // s1
      "ld1 {v5.4s}, [%1]\n"                 // s2
      "fadd  v2.4s, v4.4s, v5.4s\n"         // v2 = the FP result
      "fcmeq v3.4s, v2.4s, v2.4s\n"         // v3 = ~0 where result NOT NaN
      "fcmeq v0.4s, v4.4s, v4.4s\n"         // v0 = ~0 where s1 NOT NaN
      "fcmeq v1.4s, v5.4s, v5.4s\n"         // v1 = ~0 where s2 NOT NaN
      "and   v0.16b, v0.16b, v1.16b\n"      // neither source NaN
      "bic   v0.16b, v0.16b, v3.16b\n"      // ...AND result IS NaN
      "ld1   {v1.4s}, [%3]\n"               // v1 = PPC default NaN
      "bsl   v0.16b, v1.16b, v2.16b\n"
      "mov   v2.16b, v0.16b\n"
      "st1 {v2.4s}, [%2]\n"
      :: "r"(a), "r"(b), "r"(out), "r"(defnan)
       : "v0","v1","v2","v3","v4","v5","memory");
}

int main(void) {
  set_vmx_fpcr();
  struct { u32 a, b; const char *name; } cases[] = {
      {NUM3, NUM2, "num  + num "}, {QN1, NUM2, "QNaN + num "},
      {NUM3, QN1,  "num  + QNaN"}, {QN1, QN2,  "QNaN + QNaN"},
      {SN1,  NUM2, "SNaN + num "}, {NUM3, SN1, "num  + SNaN"},
      {SN1,  QN2,  "SNaN + QNaN"}, {SN1, SN1,  "SNaN + SNaN"},
      {INF,  NINF, "inf  + -inf"},           // GENERATED NaN - the real case
      {NINF, INF,  "-inf + inf "},
  };
  int bad = 0;
  printf("%-13s %-9s %-9s %-9s %-9s\n", "case", "result", "scalar", "branchless", "");
  for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
    u32 A[4], B[4], R[4], O[4];
    for (int l = 0; l < 4; ++l) { A[l] = cases[i].a; B[l] = cases[i].b; }
    fp_op(A, B, R);
    branchless(A, B, O);
    u32 want = scalar_ref(cases[i].a, cases[i].b, R[0]);
    int ok = (want == O[0]);
    if (!ok) ++bad;
    printf("%-13s %08X  %08X  %08X  %s\n", cases[i].name, R[0], want, O[0],
           ok ? "ok" : "MISMATCH");
  }
  printf("\n%s\n", bad ? "FAIL - branchless is NOT equivalent"
                       : "PASS - branchless matches the scalar reference on all cases");
  return bad != 0;
}
