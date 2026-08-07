// qemu-a64 differential for FixupVmxNan_V128 (xenia-thor) - the ARITHMETIC-op
// NaN fixup, NOT the max/min one.
//
// WHY THIS EXISTS: on 2026-08-07 FixupVmxMaxMinNan was deleted after a
// differential showed ARM fmax/fmin already match PPC vmaxfp/vminfp exactly.
// The tempting generalisation - "ARM matches PPC on NaN, so delete the NaN
// fixups" - is WRONG for the arithmetic ops, and this test is the proof.
//
//   PROPAGATED NaN (an input is NaN):  ARM == PPC   -> that half IS redundant
//   GENERATED  NaN (inf + -inf, 0*inf): ARM 7FC00000
//                                       PPC FFC00000  -> DIFFERS IN THE SIGN BIT
//
// ARM's default NaN is positive; PPC's is negative. FixupVmxNan_V128 is
// therefore load-bearing for the generated-NaN case and must not be removed.
//
// Build/run (no device):
//   aarch64-linux-gnu-g++ -x c++ -O2 -march=armv8.2-a -static \
//       -o vmx_nan_arith vmx_nan_arith_differential.c && qemu-aarch64 ./vmx_nan_arith
#include <stdio.h>
#include <stdint.h>

typedef unsigned int u32;

#define INF  0x7F800000u
#define NINF 0xFF800000u
#define QN1  0x7FC00001u
#define SN1  0x7F800001u
#define NUM  0x40400000u   // 3.0f
#define PPC_DEFAULT_NAN 0xFFC00000u

static void vfadd(const u32 *a, const u32 *b, u32 *o) {
  __asm__ volatile("ld1 {v0.4s},[%0]\nld1 {v1.4s},[%1]\n"
                   "fadd v2.4s,v0.4s,v1.4s\nst1 {v2.4s},[%2]\n"
                   :: "r"(a), "r"(b), "r"(o) : "v0","v1","v2","memory");
}
static void vfsub(const u32 *a, const u32 *b, u32 *o) {
  __asm__ volatile("ld1 {v0.4s},[%0]\nld1 {v1.4s},[%1]\n"
                   "fsub v2.4s,v0.4s,v1.4s\nst1 {v2.4s},[%2]\n"
                   :: "r"(a), "r"(b), "r"(o) : "v0","v1","v2","memory");
}
static void vfmul(const u32 *a, const u32 *b, u32 *o) {
  __asm__ volatile("ld1 {v0.4s},[%0]\nld1 {v1.4s},[%1]\n"
                   "fmul v2.4s,v0.4s,v1.4s\nst1 {v2.4s},[%2]\n"
                   :: "r"(a), "r"(b), "r"(o) : "v0","v1","v2","memory");
}

// Match the emitter: EmitWithVmxFpcr installs DEFAULT_VMX_FPCR = (1<<24),
// i.e. FZ only, DN clear (a64_backend.h:110).
static void set_vmx_fpcr(void) {
  unsigned long v = 1UL << 24;
  __asm__ volatile("msr fpcr, %0" :: "r"(v));
}

static void run(const char *name, void (*fn)(const u32 *, const u32 *, u32 *),
                u32 x, u32 y, int generated) {
  u32 A[4], B[4], O[4];
  for (int i = 0; i < 4; ++i) { A[i] = x; B[i] = y; }
  fn(A, B, O);
  const char *verdict;
  if (generated) {
    verdict = (O[0] == PPC_DEFAULT_NAN) ? "matches PPC"
                                        : "DIFFERS FROM PPC (fixup required)";
  } else {
    verdict = "propagated (ARM matches PPC)";
  }
  printf("  %-20s %08X op %08X -> %08X   %s\n", name, x, y, O[0], verdict);
}

int main(void) {
  set_vmx_fpcr();
  unsigned long rb;
  __asm__ volatile("mrs %0, fpcr" : "=r"(rb));
  printf("FPCR=%08lX (FZ=%lu DN=%lu)  PPC default NaN = %08X\n\n",
         rb, (rb >> 24) & 1UL, (rb >> 25) & 1UL, PPC_DEFAULT_NAN);

  printf("--- GENERATED NaN (no NaN input) : the half the fixup MUST keep ---\n");
  run("fadd inf + -inf", vfadd, INF, NINF, 1);
  run("fsub inf - inf",  vfsub, INF, INF,  1);
  run("fmul 0 * inf",    vfmul, 0x00000000u, INF, 1);

  printf("\n--- PROPAGATED NaN (a NaN input) : the half ARM already does ---\n");
  run("fadd QNaN + num", vfadd, QN1, NUM, 0);
  run("fadd num + QNaN", vfadd, NUM, QN1, 0);
  run("fadd SNaN + num", vfadd, SN1, NUM, 0);
  run("fmul QNaN * num", vfmul, QN1, NUM, 0);
  return 0;
}
